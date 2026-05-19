// MapleNight Updater — anonymous-HTTPS S3 client matching launcher.cpp CLI contract.
//
// Commands (invoked by launcher.cpp):
//   updater update         --data-dir DIR --work-dir DIR --workers N --maple-version GMS
//     Reads MN_UPDATE_BASE_URL from env. Compares Data\version vs <base>/data/latest.
//
//   updater runtime-update --install-dir DIR --work-dir DIR --base-url URL --local-version V
//                          [--check-only] [--repair] [--parent-pid PID]
//     Exit 11 if newer version available (with --check-only). Otherwise downloads + applies.
//
//   updater runtime-verify --install-dir DIR --work-dir DIR --base-url URL --local-version V
//     Exit 0 if every file in manifest matches local sha256. Nonzero if any drift.
//
// Stdout protocol parsed by launcher.cpp:824-895:
//   PROGRESS|<phase>|<cur>|<tot>|<item>|<bps>|||<overallBytes>|<overallTotal>
//
// Bucket layout (content-addressed; blobs dedupe across versions + channels):
//   <base>/<channel>/latest                       plain text: version string
//   <base>/<channel>/manifests/<version>.json     { version, files:[{path,size,sha256}] }
//   <base>/blobs/<sha[0:2]>/<sha>                 file blob, addressed by sha256

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")

namespace {

constexpr DWORD kHttpBufferSize = 64 * 1024;
constexpr DWORD kFileReadChunk = 64 * 1024;

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

void Logf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(stderr);
}

void EmitProgress(const char* phase, uint64_t cur, uint64_t tot, const std::string& item,
                  uint64_t bps, uint64_t overallBytes, uint64_t overallTotal) {
    std::printf("PROGRESS|%s|%llu|%llu|%s|%llu|||%llu|%llu\n",
                phase,
                (unsigned long long)cur,
                (unsigned long long)tot,
                item.c_str(),
                (unsigned long long)bps,
                (unsigned long long)overallBytes,
                (unsigned long long)overallTotal);
    std::fflush(stdout);
}

// --- URL ---
struct Url {
    std::wstring host;
    INTERNET_PORT port = 0;
    std::wstring path;
    bool https = false;
};

bool ParseUrl(const std::wstring& url, Url& out) {
    URL_COMPONENTSW comps{};
    comps.dwStructSize = sizeof(comps);
    comps.dwSchemeLength = (DWORD)-1;
    comps.dwHostNameLength = (DWORD)-1;
    comps.dwUrlPathLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &comps)) {
        Logf("WinHttpCrackUrl failed err=%lu url=%ls", GetLastError(), url.c_str());
        return false;
    }
    out.https = comps.nScheme == INTERNET_SCHEME_HTTPS;
    out.host.assign(comps.lpszHostName, comps.dwHostNameLength);
    out.path.assign(comps.lpszUrlPath, comps.dwUrlPathLength);
    out.port = comps.nPort;
    if (out.path.empty()) out.path = L"/";
    return true;
}

std::wstring StripTrailingSlash(std::wstring s) {
    while (!s.empty() && (s.back() == L'/' || s.back() == L'\\')) s.pop_back();
    return s;
}

std::wstring JoinUrl(std::wstring base, const std::wstring& tail) {
    base = StripTrailingSlash(std::move(base));
    if (tail.empty()) return base;
    return base + (tail[0] == L'/' ? std::wstring{} : std::wstring(L"/")) + tail;
}

// --- HTTP ---
class WinHttpSession {
public:
    WinHttpSession() {
        session_ = WinHttpOpen(L"MapleNightUpdater/1.0",
                               WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session_) {
            DWORD timeout = 30000;
            WinHttpSetTimeouts(session_, timeout, timeout, timeout, timeout * 4);
        }
    }
    ~WinHttpSession() { if (session_) WinHttpCloseHandle(session_); }
    HINTERNET handle() const { return session_; }
private:
    HINTERNET session_ = nullptr;
};

struct HttpResponse {
    DWORD status = 0;
    std::vector<uint8_t> body;
};

// Per-host connection wrapper. Reusing one HINTERNET conn across many GETs
// gives us TCP+TLS keep-alive (no handshake per file) — major speedup for
// many-small-files workloads like the data manifest.
class Connection {
public:
    Connection(HINTERNET session, const std::wstring& host, INTERNET_PORT port, bool https)
        : https_(https) {
        conn_ = WinHttpConnect(session, host.c_str(), port, 0);
        if (!conn_) Logf("WinHttpConnect err=%lu host=%ls", GetLastError(), host.c_str());
    }
    ~Connection() { if (conn_) WinHttpCloseHandle(conn_); }
    bool ok() const { return conn_ != nullptr; }

    // Send a GET for `path` (URL path including leading /). On success, fills
    // `status` and leaves the response body available via the returned request
    // handle. Caller must WinHttpCloseHandle it.
    HINTERNET Send(const std::wstring& path, DWORD& status) {
        DWORD flags = https_ ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET req = WinHttpOpenRequest(conn_, L"GET", path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!req) { Logf("WinHttpOpenRequest err=%lu", GetLastError()); return nullptr; }
        if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            Logf("WinHttpSendRequest err=%lu", GetLastError());
            WinHttpCloseHandle(req); return nullptr;
        }
        if (!WinHttpReceiveResponse(req, nullptr)) {
            Logf("WinHttpReceiveResponse err=%lu", GetLastError());
            WinHttpCloseHandle(req); return nullptr;
        }
        DWORD sz = sizeof(status);
        if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                                 WINHTTP_NO_HEADER_INDEX)) {
            Logf("WinHttpQueryHeaders err=%lu", GetLastError());
            WinHttpCloseHandle(req); return nullptr;
        }
        return req;
    }

    bool Get(const std::wstring& path, std::vector<uint8_t>& body, DWORD& status) {
        body.clear();
        HINTERNET req = Send(path, status);
        if (!req) return false;
        bool ok = true;
        if (status == 200) {
            uint8_t buf[kHttpBufferSize];
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                DWORD toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
                DWORD read = 0;
                if (!WinHttpReadData(req, buf, toRead, &read)) { ok = false; break; }
                if (read == 0) break;
                body.insert(body.end(), buf, buf + read);
            }
        }
        WinHttpCloseHandle(req);
        return ok;
    }

    bool GetToFile(const std::wstring& path, const std::wstring& filePath, uint64_t& written) {
        written = 0;
        DWORD status = 0;
        HINTERNET req = Send(path, status);
        if (!req) return false;
        bool ok = status == 200;
        if (!ok) Logf("HTTP %lu for %ls", status, path.c_str());

        HANDLE f = INVALID_HANDLE_VALUE;
        if (ok) {
            f = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE) {
                Logf("CreateFile %ls err=%lu", filePath.c_str(), GetLastError());
                ok = false;
            }
        }
        if (ok) {
            uint8_t buf[kHttpBufferSize];
            DWORD avail = 0;
            while (ok && WinHttpQueryDataAvailable(req, &avail)) {
                if (avail == 0) break;
                DWORD toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
                DWORD read = 0;
                if (!WinHttpReadData(req, buf, toRead, &read)) { ok = false; break; }
                if (read == 0) break;
                DWORD wrote = 0;
                if (!WriteFile(f, buf, read, &wrote, nullptr) || wrote != read) {
                    Logf("WriteFile err=%lu", GetLastError()); ok = false; break;
                }
                written += read;
            }
        }
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        WinHttpCloseHandle(req);
        return ok;
    }

private:
    HINTERNET conn_ = nullptr;
    bool https_ = false;
};

// One-shot fetch (creates session + connection for a single request). Used for
// latest/manifest where keep-alive provides no benefit.
bool HttpGet(const std::wstring& urlW, HttpResponse& resp) {
    Url url;
    if (!ParseUrl(urlW, url)) return false;
    WinHttpSession sess;
    if (!sess.handle()) { Logf("WinHttpOpen err=%lu", GetLastError()); return false; }
    Connection conn(sess.handle(), url.host, url.port, url.https);
    if (!conn.ok()) return false;
    return conn.Get(url.path, resp.body, resp.status);
}

// --- SHA256 (Windows CNG) ---
bool Sha256File(const std::wstring& path, std::string& hexOut) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = true;
    DWORD hashLen = 0, cb = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) ok = false;
    if (ok && BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PBYTE)&hashLen, sizeof(hashLen), &cb, 0) != 0) ok = false;
    if (ok && BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) != 0) ok = false;

    if (ok) {
        std::vector<uint8_t> buf(kFileReadChunk);
        DWORD read = 0;
        while (ReadFile(f, buf.data(), (DWORD)buf.size(), &read, nullptr) && read > 0) {
            if (BCryptHashData(hash, buf.data(), read, 0) != 0) { ok = false; break; }
        }
    }

    std::vector<uint8_t> digest(hashLen);
    if (ok && BCryptFinishHash(hash, digest.data(), hashLen, 0) != 0) ok = false;

    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    if (!ok) return false;

    static const char hex[] = "0123456789abcdef";
    hexOut.resize((size_t)hashLen * 2);
    for (DWORD i = 0; i < hashLen; ++i) {
        hexOut[2 * i] = hex[digest[i] >> 4];
        hexOut[2 * i + 1] = hex[digest[i] & 0xF];
    }
    return true;
}

// --- Tiny JSON parser (manifest schema only, no unicode escapes) ---
struct JsonParser {
    const char* p;
    const char* end;
    void Skip() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
    bool Peek(char c) { Skip(); return p < end && *p == c; }
    bool Expect(char c) { if (Peek(c)) { ++p; return true; } return false; }
    bool ReadString(std::string& out) {
        Skip();
        if (p >= end || *p != '"') return false;
        ++p;
        out.clear();
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                switch (p[1]) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'n':  out.push_back('\n'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    default:   out.push_back(p[1]); break;
                }
                p += 2;
            } else {
                out.push_back(*p++);
            }
        }
        if (p >= end) return false;
        ++p;
        return true;
    }
    bool ReadUInt64(uint64_t& out) {
        Skip();
        if (p >= end || *p < '0' || *p > '9') return false;
        out = 0;
        while (p < end && *p >= '0' && *p <= '9') { out = out * 10 + (uint64_t)(*p - '0'); ++p; }
        return true;
    }
    bool SkipValue() {
        Skip();
        if (p >= end) return false;
        if (*p == '"') { std::string s; return ReadString(s); }
        if (*p == '{') {
            ++p;
            if (Expect('}')) return true;
            for (;;) {
                std::string k;
                if (!ReadString(k) || !Expect(':') || !SkipValue()) return false;
                if (Expect(',')) continue;
                if (Expect('}')) return true;
                return false;
            }
        }
        if (*p == '[') {
            ++p;
            if (Expect(']')) return true;
            for (;;) {
                if (!SkipValue()) return false;
                if (Expect(',')) continue;
                if (Expect(']')) return true;
                return false;
            }
        }
        while (p < end && *p != ',' && *p != '}' && *p != ']'
               && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') ++p;
        return true;
    }
};

struct ManifestEntry {
    std::string path;
    uint64_t size = 0;
    std::string sha256;
};

struct Manifest {
    std::string version;
    std::vector<ManifestEntry> files;
};

bool ParseManifest(const std::vector<uint8_t>& body, Manifest& m) {
    const uint8_t* start = body.data();
    size_t len = body.size();
    if (len >= 3 && start[0] == 0xEF && start[1] == 0xBB && start[2] == 0xBF) {
        start += 3; len -= 3;
    }
    JsonParser j{(const char*)start, (const char*)start + len};
    if (!j.Expect('{')) return false;
    if (j.Expect('}')) return true;
    for (;;) {
        std::string key;
        if (!j.ReadString(key) || !j.Expect(':')) return false;
        if (key == "version") {
            if (!j.ReadString(m.version)) return false;
        } else if (key == "files") {
            if (!j.Expect('[')) return false;
            if (!j.Expect(']')) {
                for (;;) {
                    if (!j.Expect('{')) return false;
                    ManifestEntry e{};
                    if (!j.Expect('}')) {
                        for (;;) {
                            std::string fk;
                            if (!j.ReadString(fk) || !j.Expect(':')) return false;
                            if (fk == "path") { if (!j.ReadString(e.path)) return false; }
                            else if (fk == "size") { if (!j.ReadUInt64(e.size)) return false; }
                            else if (fk == "sha256") { if (!j.ReadString(e.sha256)) return false; }
                            else { if (!j.SkipValue()) return false; }
                            if (j.Expect(',')) continue;
                            if (j.Expect('}')) break;
                            return false;
                        }
                    }
                    m.files.push_back(std::move(e));
                    if (j.Expect(',')) continue;
                    if (j.Expect(']')) break;
                    return false;
                }
            }
        } else {
            if (!j.SkipValue()) return false;
        }
        if (j.Expect(',')) continue;
        if (j.Expect('}')) break;
        return false;
    }
    return true;
}

// --- Misc helpers ---
std::wstring EnvW(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return {};
    std::wstring out(n, L'\0');
    DWORD m = GetEnvironmentVariableW(name, out.data(), n);
    out.resize(m);
    return out;
}

std::string TrimAscii(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n' || s[a] == '\0')) ++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r' || s[b-1] == '\n' || s[b-1] == '\0')) --b;
    return s.substr(a, b - a);
}

bool ReadAllBytes(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    out.resize((size_t)sz.QuadPart);
    DWORD read = 0;
    BOOL ok = sz.QuadPart == 0 || ReadFile(f, out.data(), (DWORD)sz.QuadPart, &read, nullptr);
    CloseHandle(f);
    return ok && (uint64_t)read == (uint64_t)sz.QuadPart;
}

bool WriteAllBytes(const std::wstring& path, const std::string& data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    BOOL ok = data.empty() || WriteFile(f, data.data(), (DWORD)data.size(), &w, nullptr);
    CloseHandle(f);
    return ok && (size_t)w == data.size();
}

void EnsureParentDir(const std::wstring& path) {
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == L'\\' || path[i] == L'/') {
            std::wstring sub = path.substr(0, i);
            if (!sub.empty() && sub.back() != L':') {
                CreateDirectoryW(sub.c_str(), nullptr);
            }
        }
    }
}

void WaitForParentExit(DWORD pid) {
    if (pid == 0) return;
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h) return;
    WaitForSingleObject(h, 30000);
    CloseHandle(h);
}

bool ReplaceFileAtomic(const std::wstring& src, const std::wstring& dst) {
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (MoveFileExW(src.c_str(), dst.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
        DWORD e = GetLastError();
        Logf("MoveFileEx %ls -> %ls err=%lu attempt=%d", src.c_str(), dst.c_str(), e, attempt);
        Sleep(100);
    }
    return false;
}

bool FetchText(const std::wstring& url, std::string& out) {
    HttpResponse r;
    if (!HttpGet(url, r)) return false;
    if (r.status != 200) { Logf("HTTP %lu for %ls", r.status, url.c_str()); return false; }
    out.assign((const char*)r.body.data(), r.body.size());
    out = TrimAscii(out);
    return true;
}

bool FetchManifest(const std::wstring& url, Manifest& m) {
    HttpResponse r;
    if (!HttpGet(url, r)) return false;
    if (r.status != 200) { Logf("HTTP %lu for %ls", r.status, url.c_str()); return false; }
    if (!ParseManifest(r.body, m)) { Logf("manifest parse failed"); return false; }
    return true;
}

// --- Args ---
struct Args {
    std::wstring cmd;
    std::wstring dataDir;
    std::wstring installDir;
    std::wstring workDir;
    std::wstring baseUrl;
    std::wstring localVersion;
    std::wstring mapleVersion;
    DWORD parentPid = 0;
    int workers = 1;
    bool checkOnly = false;
    bool repair = false;
};

bool ParseArgs(int argc, wchar_t** argv, Args& a) {
    if (argc < 2) return false;
    a.cmd = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::wstring k = argv[i];
        auto next = [&]() -> std::wstring {
            if (i + 1 >= argc) return L"";
            return argv[++i];
        };
        if      (k == L"--data-dir")       a.dataDir = next();
        else if (k == L"--install-dir")    a.installDir = next();
        else if (k == L"--work-dir")       a.workDir = next();
        else if (k == L"--base-url")       a.baseUrl = next();
        else if (k == L"--local-version")  a.localVersion = next();
        else if (k == L"--maple-version")  a.mapleVersion = next();
        else if (k == L"--parent-pid")     a.parentPid = (DWORD)_wtoi(next().c_str());
        else if (k == L"--workers")        a.workers = _wtoi(next().c_str());
        else if (k == L"--check-only")     a.checkOnly = true;
        else if (k == L"--repair")         a.repair = true;
        else Logf("unknown arg: %ls", k.c_str());
    }
    return true;
}

// --- Sync engine ---
struct SyncOptions {
    std::wstring baseUrl;
    std::wstring channel;          // "data" or "runtime"
    std::wstring destDir;          // local install root
    std::wstring localVersion;     // optional (overrides on-disk version file)
    std::wstring localVersionFile; // path to write/read version stamp
    DWORD parentPid = 0;
    int workers = 4;               // download worker count
    bool checkOnly = false;
    bool repair = false;
    bool isRuntime = false;
};

int RunSync(const SyncOptions& opt) {
    EmitProgress("latest", 0, 1, "fetch", 0, 0, 0);
    std::string latest;
    std::wstring latestUrl = JoinUrl(opt.baseUrl, opt.channel + L"/latest");
    if (!FetchText(latestUrl, latest)) return 2;
    EmitProgress("latest", 1, 1, latest, 0, 0, 0);
    const std::wstring latestW = Utf8ToWide(latest);

    std::wstring localVersion = opt.localVersion;
    if (localVersion.empty()) {
        std::vector<uint8_t> vb;
        if (ReadAllBytes(opt.localVersionFile, vb)) {
            std::string s(vb.begin(), vb.end());
            localVersion = Utf8ToWide(TrimAscii(s));
        }
    }

    if (!opt.repair && !latestW.empty() && localVersion == latestW) {
        return 0;
    }
    if (opt.checkOnly) return 11;

    EmitProgress("manifest", 0, 1, "fetch", 0, 0, 0);
    std::wstring manifestUrl = JoinUrl(opt.baseUrl, opt.channel + L"/manifests/" + latestW + L".json");
    Manifest manifest;
    if (!FetchManifest(manifestUrl, manifest)) return 3;
    EmitProgress("manifest", 1, 1, "loaded", 0, 0, 0);

    struct Item {
        std::wstring localPath;
        std::wstring urlPath;     // path component of URL (e.g. "/blobs/ab/...")
        uint64_t size;
        std::string sha;
        std::string relpath;
    };

    // Parse base URL once; workers reuse host:port over keep-alive.
    Url base;
    if (!ParseUrl(opt.baseUrl, base)) return 7;
    std::wstring basePathPrefix = StripTrailingSlash(base.path);  // "" or "/some-prefix"

    std::vector<Item> needed;
    uint64_t totalBytes = 0;
    for (const auto& f : manifest.files) {
        std::wstring relW = Utf8ToWide(f.path);
        for (auto& c : relW) if (c == L'/') c = L'\\';
        std::wstring localPath = opt.destDir + L"\\" + relW;
        bool need = opt.repair;
        if (!need) {
            std::string h;
            if (!Sha256File(localPath, h) || h != f.sha256) need = true;
        }
        if (need) {
            std::wstring shaW = Utf8ToWide(f.sha256);
            std::wstring urlPath = basePathPrefix + L"/blobs/" + shaW.substr(0, 2) + L"/" + shaW;
            needed.push_back({localPath, urlPath, f.size, f.sha256, f.path});
            totalBytes += f.size;
        }
    }

    const char* dlPhase = opt.repair ? "repair-download" : "download";

    // Parallel download via N workers, each holding a keep-alive Connection.
    WinHttpSession session;
    if (!session.handle()) { Logf("WinHttpOpen err=%lu", GetLastError()); return 7; }

    std::atomic<size_t> nextIndex{0};
    std::atomic<uint64_t> overallBytes{0};
    std::atomic<uint64_t> filesDone{0};
    std::atomic<bool> failed{false};

    int workerCount = opt.workers < 1 ? 1 : (opt.workers > 16 ? 16 : opt.workers);
    if ((size_t)workerCount > needed.size() && !needed.empty()) workerCount = (int)needed.size();

    auto runWorker = [&]() {
        Connection conn(session.handle(), base.host, base.port, base.https);
        if (!conn.ok()) { failed.store(true, std::memory_order_relaxed); return; }
        for (;;) {
            if (failed.load(std::memory_order_relaxed)) return;
            size_t i = nextIndex.fetch_add(1, std::memory_order_relaxed);
            if (i >= needed.size()) return;
            const Item& it = needed[i];
            EnsureParentDir(it.localPath);
            std::wstring tmpPath = it.localPath + L".part";
            uint64_t got = 0;
            if (!conn.GetToFile(it.urlPath, tmpPath, got)) {
                Logf("download failed %ls", it.urlPath.c_str());
                DeleteFileW(tmpPath.c_str());
                failed.store(true, std::memory_order_relaxed); return;
            }
            std::string h;
            if (!Sha256File(tmpPath, h) || h != it.sha) {
                Logf("sha mismatch %s expected=%s got=%s", it.relpath.c_str(), it.sha.c_str(), h.c_str());
                DeleteFileW(tmpPath.c_str());
                failed.store(true, std::memory_order_relaxed); return;
            }
            uint64_t newOverall = overallBytes.fetch_add(got, std::memory_order_relaxed) + got;
            uint64_t done = filesDone.fetch_add(1, std::memory_order_relaxed) + 1;
            EmitProgress(dlPhase, done, (uint64_t)needed.size(),
                         it.relpath, 0, newOverall, totalBytes);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve((size_t)workerCount);
    for (int i = 0; i < workerCount; ++i) threads.emplace_back(runWorker);
    for (auto& t : threads) t.join();

    if (failed.load(std::memory_order_relaxed)) return 4;

    WaitForParentExit(opt.parentPid);

    const char* applyPhase = opt.repair ? "repair" : "apply";
    EmitProgress(applyPhase, 0, (uint64_t)needed.size(), "start", 0, 0, 0);
    for (size_t i = 0; i < needed.size(); ++i) {
        const auto& it = needed[i];
        std::wstring tmpPath = it.localPath + L".part";
        std::wstring finalPath = it.localPath;
        size_t slash = finalPath.find_last_of(L"\\/");
        std::wstring leaf = slash == std::wstring::npos ? finalPath : finalPath.substr(slash + 1);
        if (_wcsicmp(leaf.c_str(), L"Updater.exe") == 0) {
            // launcher.cpp:590-619 handles Updater.new.exe -> Updater.exe swap on next boot
            std::wstring newName = (slash == std::wstring::npos ? std::wstring{} : finalPath.substr(0, slash + 1)) + L"Updater.new.exe";
            if (!ReplaceFileAtomic(tmpPath, newName)) return 6;
        } else {
            if (!ReplaceFileAtomic(tmpPath, finalPath)) return 6;
        }
        EmitProgress(applyPhase, (uint64_t)i + 1, (uint64_t)needed.size(), it.relpath, 0, 0, 0);
    }

    if (!opt.localVersionFile.empty()) {
        WriteAllBytes(opt.localVersionFile, latest);
    }
    return 0;
}

// --- Commands ---
int CmdUpdate(const Args& a) {
    std::wstring baseUrl = EnvW(L"MN_UPDATE_BASE_URL");
    if (baseUrl.empty()) { Logf("MN_UPDATE_BASE_URL not set"); return 7; }
    std::wstring dest = a.dataDir.empty() ? L"Data" : a.dataDir;
    SyncOptions opt;
    opt.baseUrl = baseUrl;
    opt.channel = L"data";
    opt.destDir = dest;
    opt.localVersionFile = dest + L"\\version";
    opt.isRuntime = false;
    if (a.workers > 0) opt.workers = a.workers;
    return RunSync(opt);
}

int CmdRuntimeUpdate(const Args& a) {
    if (a.baseUrl.empty()) { Logf("--base-url required"); return 7; }
    SyncOptions opt;
    opt.baseUrl = a.baseUrl;
    opt.channel = L"runtime";
    opt.destDir = a.installDir.empty() ? L"." : a.installDir;
    opt.localVersion = a.localVersion;
    opt.localVersionFile = L"runtime.version";
    opt.parentPid = a.parentPid;
    opt.checkOnly = a.checkOnly;
    opt.repair = a.repair;
    opt.isRuntime = true;
    if (a.workers > 0) opt.workers = a.workers;
    return RunSync(opt);
}

// Data-channel verify. Launcher (RunExternalVerifier, launcher.cpp:1036) spawns
// `Updater.exe verify --data-dir Data ...` with no --base-url and no --local-version
// — reads base URL from env, resolves local version from <data-dir>\version.
//
// Scope: only Mob/, Map/, Skill/ are verified. Other manifest entries are skipped
// to keep launch-time CRC fast; tamper risk on other dirs is accepted.
static bool IsVerifyScopedPath(const std::string& p) {
    return p.rfind("Mob/", 0) == 0
        || p.rfind("Map/", 0) == 0
        || p.rfind("Skill/", 0) == 0;
}

int CmdVerify(const Args& a) {
    std::wstring baseUrl = EnvW(L"MN_UPDATE_BASE_URL");
    if (baseUrl.empty()) { Logf("MN_UPDATE_BASE_URL not set"); return 7; }
    std::wstring dest = a.dataDir.empty() ? L"Data" : a.dataDir;

    std::wstring localVersion = a.localVersion;
    if (localVersion.empty()) {
        std::vector<uint8_t> vb;
        if (ReadAllBytes(dest + L"\\version", vb)) {
            std::string s(vb.begin(), vb.end());
            localVersion = Utf8ToWide(TrimAscii(s));
        }
    }
    if (localVersion.empty()) { Logf("data version unknown (no <data-dir>\\version)"); return 7; }

    EmitProgress("verify-manifest", 0, 1, "fetch", 0, 0, 0);
    std::wstring manifestUrl = JoinUrl(baseUrl, L"data/manifests/" + localVersion + L".json");
    Manifest m;
    if (!FetchManifest(manifestUrl, m)) return 3;
    EmitProgress("verify-manifest", 1, 1, "loaded", 0, 0, 0);

    std::vector<const ManifestEntry*> scoped;
    scoped.reserve(m.files.size());
    uint64_t totalBytes = 0;
    for (const auto& f : m.files) {
        if (!IsVerifyScopedPath(f.path)) continue;
        scoped.push_back(&f);
        totalBytes += f.size;
    }

    int workers = a.workers > 0 ? a.workers : 4;
    if (workers > (int)scoped.size()) workers = (int)scoped.size();
    if (workers < 1) workers = 1;

    std::atomic<size_t> next{0};
    std::atomic<uint64_t> doneBytes{0};
    std::atomic<size_t> doneFiles{0};
    std::atomic<bool> failed{false};
    std::mutex logMu;

    auto worker = [&]() {
        for (;;) {
            size_t i = next.fetch_add(1, std::memory_order_relaxed);
            if (i >= scoped.size() || failed.load(std::memory_order_relaxed)) break;
            const auto& f = *scoped[i];
            std::wstring relW = Utf8ToWide(f.path);
            for (auto& c : relW) if (c == L'/') c = L'\\';
            std::wstring localPath = dest + L"\\" + relW;
            std::string h;
            if (!Sha256File(localPath, h) || h != f.sha256) {
                {
                    std::lock_guard<std::mutex> g(logMu);
                    Logf("verify failed %s expected=%s got=%s", f.path.c_str(), f.sha256.c_str(), h.c_str());
                    EmitProgress("verify-failed", 0, 1, f.path, 0, 0, 0);
                }
                failed.store(true, std::memory_order_relaxed);
                return;
            }
            doneBytes.fetch_add(f.size, std::memory_order_relaxed);
            size_t df = doneFiles.fetch_add(1, std::memory_order_relaxed) + 1;
            EmitProgress("verify", (uint64_t)df, (uint64_t)scoped.size(), f.path, 0,
                         doneBytes.load(std::memory_order_relaxed), totalBytes);
        }
    };

    std::vector<std::thread> ts;
    ts.reserve(workers);
    for (int i = 0; i < workers; ++i) ts.emplace_back(worker);
    for (auto& t : ts) t.join();

    return failed.load(std::memory_order_relaxed) ? 1 : 0;
}

int CmdRuntimeVerify(const Args& a) {
    if (a.baseUrl.empty()) { Logf("--base-url required"); return 7; }
    if (a.localVersion.empty()) { Logf("--local-version required"); return 7; }
    std::wstring dest = a.installDir.empty() ? L"." : a.installDir;

    EmitProgress("verify-manifest", 0, 1, "fetch", 0, 0, 0);
    std::wstring manifestUrl = JoinUrl(a.baseUrl, L"runtime/manifests/" + a.localVersion + L".json");
    Manifest m;
    if (!FetchManifest(manifestUrl, m)) return 3;
    EmitProgress("verify-manifest", 1, 1, "loaded", 0, 0, 0);

    uint64_t totalBytes = 0;
    for (const auto& f : m.files) totalBytes += f.size;

    uint64_t done = 0;
    for (size_t i = 0; i < m.files.size(); ++i) {
        const auto& f = m.files[i];
        std::wstring relW = Utf8ToWide(f.path);
        for (auto& c : relW) if (c == L'/') c = L'\\';
        std::wstring localPath = dest + L"\\" + relW;
        std::string h;
        if (!Sha256File(localPath, h) || h != f.sha256) {
            Logf("verify failed %s expected=%s got=%s", f.path.c_str(), f.sha256.c_str(), h.c_str());
            EmitProgress("verify-failed", 0, 1, f.path, 0, 0, 0);
            return 1;
        }
        done += f.size;
        EmitProgress("verify", (uint64_t)i + 1, (uint64_t)m.files.size(), f.path, 0, done, totalBytes);
    }
    return 0;
}

} // namespace

int main() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) { Logf("CommandLineToArgvW failed"); return 64; }

    int rc = 64;
    Args a;
    if (!ParseArgs(argc, argv, a)) {
        Logf("usage: Updater.exe <update|runtime-update|runtime-verify> [...]");
    } else if (a.cmd == L"update")         rc = CmdUpdate(a);
    else      if (a.cmd == L"verify")         rc = CmdVerify(a);
    else      if (a.cmd == L"runtime-update") rc = CmdRuntimeUpdate(a);
    else      if (a.cmd == L"runtime-verify") rc = CmdRuntimeVerify(a);
    else      Logf("unknown command: %ls", a.cmd.c_str());

    LocalFree(argv);
    return rc;
}
