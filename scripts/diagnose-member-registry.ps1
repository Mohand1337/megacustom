# Read-only inventory for locating split or backed-up MegaCustom member registries.
[CmdletBinding()]
param(
    [string[]]$SearchRoot = @(),
    [switch]$CreateSafetyCopies,
    [string]$SafetyCopyDirectory = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$candidateMap = @{}

function Add-RegistryCandidate {
    param(
        [string]$Path,
        [string]$Source
    )

    if ([string]::IsNullOrWhiteSpace($Path) -or !(Test-Path -LiteralPath $Path -PathType Leaf)) {
        return
    }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    if (!$candidateMap.ContainsKey($resolved)) {
        $candidateMap[$resolved] = $Source
    }
}

$directCandidates = @()
if ($env:LOCALAPPDATA) {
    $directCandidates += Join-Path $env:LOCALAPPDATA "MegaCustom\members.json"
    $directCandidates += Join-Path $env:LOCALAPPDATA "MegaCustom\MegaCustom\members.json"
}
if ($env:APPDATA) {
    $directCandidates += Join-Path $env:APPDATA "MegaCustom\members.json"
    $directCandidates += Join-Path $env:APPDATA "MegaCustom\MegaCustom\members.json"
}
foreach ($path in $directCandidates) {
    Add-RegistryCandidate -Path $path -Source "Known AppData path"
}

$roots = @($ProjectRoot)
if ($env:USERPROFILE) {
    $desktop = Join-Path $env:USERPROFILE "Desktop"
    if (Test-Path -LiteralPath $desktop -PathType Container) {
        $roots += $desktop
    }
}
$roots += $SearchRoot

foreach ($root in ($roots | Where-Object { $_ } | Select-Object -Unique)) {
    if (!(Test-Path -LiteralPath $root -PathType Container)) {
        Write-Warning "Search root does not exist: $root"
        continue
    }
    Get-ChildItem -LiteralPath $root -File -Recurse -Force -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name -eq "members.json" -or
            $_.Name -like "members.json.bak.*" -or
            $_.Name -like "members.json.pre-migration-*.bak"
        } |
        ForEach-Object {
            Add-RegistryCandidate -Path $_.FullName -Source "Recursive search"
        }
}

if ($candidateMap.Count -eq 0) {
    Write-Host "No member registry files were found." -ForegroundColor Yellow
    Write-Host "No files were modified. Add -SearchRoot with the folder that contains another app copy."
    exit 0
}

$results = @()
$allMemberIds = New-Object 'System.Collections.Generic.HashSet[string]'
$allGroupNames = New-Object 'System.Collections.Generic.HashSet[string]'

foreach ($entry in $candidateMap.GetEnumerator() | Sort-Object Name) {
    $path = $entry.Key
    $file = Get-Item -LiteralPath $path
    $valid = $false
    $memberCount = 0
    $groupCount = 0
    $latestRecordUtc = $null
    $parseStatus = "Valid"

    try {
        $document = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($null -eq $document -or $document -is [System.Array]) {
            throw "Root value is not a JSON object"
        }

        $members = @($document.members)
        $groups = @($document.groups)
        if ($null -eq $document.members) { $members = @() }
        if ($null -eq $document.groups) { $groups = @() }
        $memberCount = $members.Count
        $groupCount = $groups.Count

        $fileMemberIds = New-Object 'System.Collections.Generic.HashSet[string]'
        $fileGroupNames = New-Object 'System.Collections.Generic.HashSet[string]'
        $latestEpoch = 0L
        foreach ($member in $members) {
            $memberId = [string]$member.id
            if ([string]::IsNullOrWhiteSpace($memberId)) {
                throw "Member entry has a blank or missing ID"
            }
            if (!$fileMemberIds.Add($memberId)) {
                throw "Duplicate member ID detected"
            }
            foreach ($value in @($member.updatedAt, $member.createdAt)) {
                $epoch = 0L
                if ($null -ne $value -and [Int64]::TryParse([string]$value, [ref]$epoch)) {
                    if ($epoch -gt $latestEpoch) { $latestEpoch = $epoch }
                }
            }
        }
        foreach ($group in $groups) {
            $groupName = [string]$group.name
            if ([string]::IsNullOrWhiteSpace($groupName)) {
                throw "Group entry has a blank or missing name"
            }
            if (!$fileGroupNames.Add($groupName)) {
                throw "Duplicate group name detected"
            }
            foreach ($value in @($group.updatedAt, $group.createdAt)) {
                $epoch = 0L
                if ($null -ne $value -and [Int64]::TryParse([string]$value, [ref]$epoch)) {
                    if ($epoch -gt $latestEpoch) { $latestEpoch = $epoch }
                }
            }
        }
        if ($latestEpoch -gt 0) {
            $latestRecordUtc = [DateTimeOffset]::FromUnixTimeSeconds($latestEpoch).UtcDateTime
        }
        foreach ($memberId in $fileMemberIds) { [void]$allMemberIds.Add($memberId) }
        foreach ($groupName in $fileGroupNames) { [void]$allGroupNames.Add($groupName) }
        $valid = $true
    } catch {
        $parseStatus = "Invalid: $($_.Exception.Message)"
    }

    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    $results += [PSCustomObject]@{
        Path = $path
        Source = $entry.Value
        Valid = $valid
        Members = $memberCount
        Groups = $groupCount
        LatestRecordUtc = $latestRecordUtc
        ModifiedUtc = $file.LastWriteTimeUtc
        Bytes = $file.Length
        Sha256 = $hash
        Status = $parseStatus
    }
}

$results | Sort-Object Members, Groups, ModifiedUtc -Descending |
    Format-Table Valid, Members, Groups, LatestRecordUtc, ModifiedUtc, Bytes, Path -AutoSize

Write-Host ""
Write-Host "Registry files found: $($results.Count)"
Write-Host "Distinct member IDs across valid files: $($allMemberIds.Count)"
Write-Host "Distinct group names across valid files: $($allGroupNames.Count)"
Write-Host "Member names, emails, IDs, and group names were intentionally not printed."

if ($CreateSafetyCopies) {
    if ([string]::IsNullOrWhiteSpace($SafetyCopyDirectory)) {
        $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $recoveryRoot = if ($env:USERPROFILE) {
            Join-Path $env:USERPROFILE "Documents"
        } else {
            $env:TEMP
        }
        $SafetyCopyDirectory = Join-Path $recoveryRoot "MegaCustom-Member-Recovery-$stamp"
    }
    New-Item -ItemType Directory -Path $SafetyCopyDirectory -Force | Out-Null
    foreach ($result in $results) {
        $shortHash = $result.Sha256.Substring(0, 12)
        $copyName = "members-$shortHash-$([IO.Path]::GetFileName($result.Path))"
        Copy-Item -LiteralPath $result.Path -Destination (Join-Path $SafetyCopyDirectory $copyName) -Force
    }
    Write-Host "Safety copies created at: $SafetyCopyDirectory" -ForegroundColor Green
} else {
    Write-Host "No files were modified. Re-run with -CreateSafetyCopies only after reviewing this inventory."
}
