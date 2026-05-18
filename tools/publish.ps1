# Stage a MapleNight release for upload to OVH bucket.
#
# Content-addressed layout: file payloads stored once at blobs/<sha[0:2]>/<sha>,
# shared across versions AND channels (runtime + data). Manifests live per channel.
#
# Usage:
#   .\publish.ps1 -SourceDir <dir> -Version <ver> -Channel runtime|data -OutDir <staging>
#
# OutDir is persistent — invoke multiple times against the same directory to accumulate
# blobs from different channels/versions. Existing blobs in stage are not re-copied.
#
# Produces under <OutDir>:
#   blobs/<2>/<sha>                          content-addressed payloads (dedup)
#   <channel>/latest                         version stamp (raw text)
#   <channel>/manifests/<version>.json       file list with sha256 + size
#
# Upload everything in <OutDir> to the bucket root, preserving prefixes:
#   aws --profile ovh s3 cp <OutDir>\ s3://charming-holberton/ --recursive --acl public-read
#
# Cache-Control recommendations (set after upload or use --metadata-directive REPLACE):
#   <channel>/latest                          no-cache
#   <channel>/manifests/<ver>.json            no-cache  (immutable, but small)
#   blobs/**                                  public, max-age=31536000, immutable

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SourceDir,
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][ValidateSet('runtime','data')][string]$Channel,
    [Parameter(Mandatory=$true)][string]$OutDir,
    # Path segments to skip. Matches if any segment of the relative path equals one of these.
    [string[]]$ExcludeDir = @('.git', '.idea', '.vs', '.vscode', '__pycache__', 'node_modules'),
    # Filename patterns to skip (wildcards).
    [string[]]$ExcludeFile = @('Thumbs.db', '.DS_Store', '*.tmp', '*.log', '*.pdb', '*.bak', '*.swp',
                                '*.part', 'Updater.new.exe', 'MapleNight.exe.backup', 'MapleStory.exe.backup')
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $SourceDir)) {
    throw "SourceDir not found: $SourceDir"
}

$root = (Resolve-Path -LiteralPath $SourceDir).Path
$blobsDir = Join-Path $OutDir 'blobs'
New-Item -ItemType Directory -Force -Path $blobsDir | Out-Null

$entries = New-Object System.Collections.Generic.List[object]
$skipped = New-Object System.Collections.Generic.List[string]
$newBlobs = 0
$totalBytes = [int64]0

function Test-IsExcluded([string]$relPath) {
    $segments = $relPath -split '[/\\]'
    foreach ($seg in $segments[0..($segments.Length - 2)]) {
        foreach ($pat in $ExcludeDir) { if ($seg -ieq $pat) { return $true } }
    }
    $leaf = $segments[-1]
    foreach ($pat in $ExcludeFile) { if ($leaf -ilike $pat) { return $true } }
    return $false
}

$all = Get-ChildItem -LiteralPath $root -Recurse -File
$files = New-Object System.Collections.Generic.List[object]
foreach ($f in $all) {
    $rel = $f.FullName.Substring($root.Length).TrimStart('\','/')
    if (Test-IsExcluded $rel) { [void]$skipped.Add($rel); continue }
    [void]$files.Add($f)
}
$count = $files.Count
Write-Host "Including $count files; skipped $($skipped.Count) (exclusions)"
$i = 0
foreach ($f in $files) {
    $i++
    $rel = $f.FullName.Substring($root.Length).TrimStart('\','/') -replace '\\','/'
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $f.FullName).Hash.ToLower()
    $prefix = $hash.Substring(0, 2)
    $blobDir = Join-Path $blobsDir $prefix
    $blobPath = Join-Path $blobDir $hash
    if (-not (Test-Path -LiteralPath $blobPath)) {
        New-Item -ItemType Directory -Force -Path $blobDir | Out-Null
        Copy-Item -LiteralPath $f.FullName -Destination $blobPath -Force
        $newBlobs++
    }
    [void]$entries.Add([ordered]@{
        path   = $rel
        size   = [int64]$f.Length
        sha256 = $hash
    })
    $totalBytes += [int64]$f.Length
    if ($i % 200 -eq 0) { Write-Host "  hashed $i / $count" }
}

$manifest = [ordered]@{
    version    = $Version
    created_at = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    files      = $entries
}
$manifestDir = Join-Path $OutDir "$Channel/manifests"
New-Item -ItemType Directory -Force -Path $manifestDir | Out-Null
$manifestPath = Join-Path $manifestDir "$Version.json"
$json = $manifest | ConvertTo-Json -Depth 10
# PS 5.1 Out-File -Encoding utf8 writes a BOM; bypass with explicit no-BOM UTF-8.
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($manifestPath, $json, $utf8NoBom)

$latestPath = Join-Path $OutDir "$Channel/latest"
New-Item -ItemType Directory -Force -Path (Split-Path -LiteralPath $latestPath) | Out-Null
Set-Content -LiteralPath $latestPath -Value $Version -Encoding ascii -NoNewline

$summary = @"
Channel       : $Channel
Version       : $Version
Files         : $($entries.Count)
Logical bytes : $totalBytes
New blobs     : $newBlobs   (existing dedupe-hits: $($entries.Count - $newBlobs))

Upload prefixes (mirror to bucket root):
  $Channel/latest                          Cache-Control: no-cache
  $Channel/manifests/$Version.json         Cache-Control: no-cache
  blobs/**                                 Cache-Control: public, max-age=31536000, immutable

Recommended upload (re-uploads all stage; idempotent — blobs are content-addressed):
  aws --profile ovh s3 cp "$OutDir\" s3://charming-holberton/ --recursive --acl public-read

If your S3 user has s3:ListBucket, prefer sync to skip already-uploaded blobs:
  aws --profile ovh s3 sync "$OutDir\" s3://charming-holberton/ --acl public-read --size-only
"@
$summaryPath = Join-Path $OutDir '_upload.txt'
Set-Content -LiteralPath $summaryPath -Value $summary -Encoding utf8

Write-Host ""
Write-Host "Channel    : $Channel"
Write-Host "Version    : $Version"
Write-Host "Files      : $($entries.Count)"
Write-Host "New blobs  : $newBlobs"
Write-Host "Manifest   : $manifestPath"
Write-Host "Latest     : $latestPath"
Write-Host "Upload doc : $summaryPath"
