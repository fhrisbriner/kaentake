# Push a staged MapleNight release to the OVH bucket, uploading only blobs
# that aren't already there. Works around the missing s3:ListBucket perm
# (which would otherwise let `aws s3 sync` do this server-side).
#
# Strategy:
#   1. Read local manifest at <OutDir>/<Channel>/manifests/<Version>.json
#   2. Fetch the bucket's current <Channel>/latest -> prev version
#   3. Fetch the prev manifest, hash-diff against new -> delta blob set
#   4. Upload delta blobs (parallel) + new manifest + new latest pointer
#
# Usage:
#   .\push.ps1 -Version 1.0.3 -Channel runtime
#   .\push.ps1 -Version 1.0.2 -Channel data -OutDir D:\publish-clean\stage
#   .\push.ps1 -Version 1.0.3 -Channel runtime -Force         # upload every blob
#
# Defaults match the rest of the pipeline:
#   OutDir   D:\publish-clean\stage
#   Bucket   charming-holberton
#   BaseUrl  https://charming-holberton.s3.us-east-va.io.cloud.ovh.us
#   Profile  ovh

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][ValidateSet('runtime','data')][string]$Channel,
    [string]$OutDir = 'D:\publish-clean\stage',
    [string]$Bucket = 'charming-holberton',
    [string]$BaseUrl = 'https://charming-holberton.s3.us-east-va.io.cloud.ovh.us',
    [string]$Profile = 'ovh',
    [int]$Parallel = 8,
    [switch]$Force,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

$manifestPath = Join-Path $OutDir "$Channel/manifests/$Version.json"
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Manifest not found: $manifestPath (run publish.ps1 first)"
}
$latestPath = Join-Path $OutDir "$Channel/latest"
if (-not (Test-Path -LiteralPath $latestPath)) {
    throw "Latest pointer not found: $latestPath"
}

$newManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$newFiles = $newManifest.files
Write-Host "New manifest: $Channel/$Version  ($($newFiles.Count) files)"

# --- Compute delta ---------------------------------------------------------

$existingHashes = @{}
function Get-RemoteText([string]$Url, [int]$TimeoutSec = 15) {
    $resp = Invoke-WebRequest -Uri $Url -UseBasicParsing -TimeoutSec $TimeoutSec
    $body = $resp.Content
    if ($body -is [byte[]]) { $body = [System.Text.Encoding]::UTF8.GetString($body) }
    return $body
}

if (-not $Force) {
    $latestUrl = "$BaseUrl/$Channel/latest"
    try {
        $prevVersion = (Get-RemoteText $latestUrl).Trim()
    } catch {
        # Refuse to silently upload EVERY blob just because the pointer is unreadable. That is almost
        # always a transient/bucket problem, not a real first push -- and a full re-upload is the
        # surprise we want to avoid. Require an explicit -Force for a deliberate full/first push.
        throw "Could not read $latestUrl ($($_.Exception.Message)). Refusing to upload all blobs. " +
              "Use -Force for a deliberate full/first push."
    }

    if ($prevVersion -and $prevVersion -ne $Version) {
        $prevUrl = "$BaseUrl/$Channel/manifests/$prevVersion.json"
        try {
            $prev = (Get-RemoteText $prevUrl 30) | ConvertFrom-Json
            foreach ($f in $prev.files) { $existingHashes[$f.sha256] = $true }
            Write-Host "Diffing against $Channel/$prevVersion ($($prev.files.Count) prior files)"
        } catch {
            throw "Could not fetch prev manifest $prevUrl ($($_.Exception.Message)). Refusing to " +
                  "upload all blobs. Use -Force for a deliberate full push."
        }
    } elseif ($prevVersion -eq $Version) {
        Write-Host "Bucket already at $Version -- diffing against own remote manifest"
        try {
            $prev = (Get-RemoteText "$BaseUrl/$Channel/manifests/$Version.json" 30) | ConvertFrom-Json
            foreach ($f in $prev.files) { $existingHashes[$f.sha256] = $true }
        } catch {
            Write-Host "Remote manifest fetch failed -- uploading all blobs"
        }
    }
}

$seen = @{}
$delta = New-Object System.Collections.Generic.List[string]
foreach ($f in $newFiles) {
    if ($seen.ContainsKey($f.sha256)) { continue }
    $seen[$f.sha256] = $true
    if ($existingHashes.ContainsKey($f.sha256)) { continue }
    $delta.Add($f.sha256)
}

Write-Host ("Blobs to upload: {0} new / {1} total unique" -f $delta.Count, $seen.Count)

# --- Upload ----------------------------------------------------------------

if ($DryRun) {
    Write-Host "(dry-run) would upload $($delta.Count) blobs + manifest + latest"
    return
}

if ($delta.Count -gt 0) {
    $jobs = @()
    $failures = New-Object System.Collections.Generic.List[string]
    $blobsDir = Join-Path $OutDir 'blobs'
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $i = 0
    foreach ($h in $delta) {
        $i++
        $prefix = $h.Substring(0,2)
        $local = Join-Path $blobsDir "$prefix\$h"
        if (-not (Test-Path -LiteralPath $local)) {
            throw "Missing blob in stage: $local"
        }
        $remote = "s3://$Bucket/blobs/$prefix/$h"

        while ($jobs.Count -ge $Parallel) {
            $done = Wait-Job -Job $jobs -Any
            $done | ForEach-Object {
                $r = Receive-Job -Job $_ -ErrorAction Continue
                if ($_.State -ne 'Completed') { $failures.Add("blob job $($_.Id) state=$($_.State): $r") }
                Remove-Job -Job $_
            }
            $jobs = @($jobs | Where-Object { $_.State -eq 'Running' })
        }

        $jobs += Start-Job -ScriptBlock {
            param($local, $remote, $profile)
            aws --profile $profile s3 cp $local $remote --acl public-read --only-show-errors 2>&1
            if ($LASTEXITCODE -ne 0) { throw "aws cp failed: $local -> $remote" }
        } -ArgumentList $local, $remote, $Profile

        if ($i % 25 -eq 0) { Write-Host ("  queued {0}/{1}" -f $i, $delta.Count) }
    }

    if ($jobs.Count -gt 0) {
        Wait-Job -Job $jobs | Out-Null
        $jobs | ForEach-Object {
            $r = Receive-Job -Job $_ -ErrorAction Continue
            if ($_.State -ne 'Completed') { Write-Warning "blob job $($_.Id) state=$($_.State): $r" }
            Remove-Job -Job $_
        }
    }
    $sw.Stop()
    if ($failures.Count -gt 0) {
        throw ("Blob upload FAILED ({0} of {1} jobs); latest NOT advanced:`n{2}" -f `
            $failures.Count, $delta.Count, ($failures -join "`n"))
    }
    Write-Host ("Uploaded {0} blobs in {1:N1}s" -f $delta.Count, $sw.Elapsed.TotalSeconds)
}

# Manifest + latest go last so a partial blob upload never points clients
# at a manifest whose blobs aren't all present yet.
aws --profile $Profile s3 cp $manifestPath "s3://$Bucket/$Channel/manifests/$Version.json" --acl public-read --cache-control no-cache --only-show-errors
if ($LASTEXITCODE -ne 0) { throw "manifest upload failed" }
Write-Host "Uploaded $Channel/manifests/$Version.json"

# Verify the content is actually on the bucket BEFORE advancing latest. A partial push that
# advances latest to a version whose manifest/blobs aren't all present breaks every client
# (they fetch latest -> 404 on manifest/blob -> update fails -> stale dll). HEAD the manifest and
# every just-uploaded delta blob; throw without touching latest if anything is missing.
Write-Host "Verifying manifest + $($delta.Count) delta blobs on bucket before advancing latest..."
aws --profile $Profile s3api head-object --bucket $Bucket --key "$Channel/manifests/$Version.json" 1>$null 2>$null
if ($LASTEXITCODE -ne 0) { throw "post-upload verify FAILED: manifest $Channel/manifests/$Version.json not on bucket; latest NOT advanced" }
$verifyMissing = New-Object System.Collections.Generic.List[string]
foreach ($h in $delta) {
    $prefix = $h.Substring(0,2)
    aws --profile $Profile s3api head-object --bucket $Bucket --key "blobs/$prefix/$h" 1>$null 2>$null
    if ($LASTEXITCODE -ne 0) { $verifyMissing.Add("blobs/$prefix/$h") }
}
if ($verifyMissing.Count -gt 0) {
    throw ("post-upload verify FAILED: {0} delta blobs missing on bucket; latest NOT advanced:`n{1}" -f `
        $verifyMissing.Count, ($verifyMissing -join "`n"))
}
Write-Host "Verify OK: manifest + all delta blobs present"

aws --profile $Profile s3 cp $latestPath "s3://$Bucket/$Channel/latest" --acl public-read --cache-control no-cache --only-show-errors
if ($LASTEXITCODE -ne 0) { throw "latest upload failed" }
Write-Host "Uploaded $Channel/latest -> $Version"

# --- Verify ----------------------------------------------------------------

try {
    $live = (Get-RemoteText "$BaseUrl/$Channel/latest").Trim()
    if ($live -eq $Version) {
        Write-Host "Verified live: $Channel/latest = $Version"
    } else {
        Write-Warning "Live latest = '$live' (expected $Version)"
    }
} catch {
    Write-Warning "Could not verify live latest: $($_.Exception.Message)"
}
