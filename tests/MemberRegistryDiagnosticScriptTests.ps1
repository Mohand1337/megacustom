$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$diagnosticScript = Join-Path $repositoryRoot "scripts\diagnose-member-registry.ps1"
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) (
    "MegaCustom-Diagnostic-Test-" + [Guid]::NewGuid().ToString("N"))
$validDirectory = Join-Path $temporaryRoot "valid"
$invalidDirectory = Join-Path $temporaryRoot "invalid"
$copyDirectory = Join-Path $temporaryRoot "copies"
$validPath = Join-Path $validDirectory "members.json"
$invalidPath = Join-Path $invalidDirectory "members.json"
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (!$Condition) { throw $Message }
}

try {
    New-Item -ItemType Directory -Path $validDirectory, $invalidDirectory -Force | Out-Null

    $validRegistry = @{
        template = @{}
        members = @(
            @{
                id = "valid-member"
                displayName = "Valid Member"
                active = $true
                createdAt = 100
                updatedAt = 100
            }
        )
        groups = @()
    } | ConvertTo-Json -Depth 10
    [IO.File]::WriteAllText($validPath, $validRegistry, $utf8WithoutBom)

    $invalidRegistry = '{"members":[{"id":"wrapped' + "`n  " +
        'value","displayName":"Wrapped Value"}],"groups":[]}'
    [IO.File]::WriteAllText($invalidPath, $invalidRegistry, $utf8WithoutBom)

    $validHashBefore = (Get-FileHash -LiteralPath $validPath -Algorithm SHA256).Hash
    $invalidHashBefore = (Get-FileHash -LiteralPath $invalidPath -Algorithm SHA256).Hash

    $inventory = & $diagnosticScript -SearchRoot $temporaryRoot -SearchRootOnly 6>&1 |
        Out-String -Width 4096
    Assert-True ($inventory -match "Strictly invalid registry files:\s+1") `
        "Diagnostic did not reject the unescaped JSON control character."
    Assert-True ($inventory -match "Distinct member IDs across valid files:\s+1") `
        "Diagnostic included records from the malformed registry."

    $copyOutput = & $diagnosticScript `
        -SearchRoot $temporaryRoot `
        -SearchRootOnly `
        -CreateSafetyCopies `
        -SafetyCopyDirectory $copyDirectory 6>&1 |
        Out-String -Width 4096
    Assert-True ($copyOutput -match [regex]::Escape($copyDirectory)) `
        "Diagnostic did not report the requested safety-copy directory."
    Assert-True (@(Get-ChildItem -LiteralPath $copyDirectory -File).Count -eq 2) `
        "Diagnostic did not preserve both valid and malformed candidates."
    Assert-True ((Get-FileHash -LiteralPath $validPath -Algorithm SHA256).Hash `
            -eq $validHashBefore) `
        "Diagnostic modified the valid source registry."
    Assert-True ((Get-FileHash -LiteralPath $invalidPath -Algorithm SHA256).Hash `
            -eq $invalidHashBefore) `
        "Diagnostic modified the malformed source registry."

    Write-Host "Member registry diagnostic script tests passed."
} finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}
