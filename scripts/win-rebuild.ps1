# MegaCustomGUI verified Windows rebuild and portable update.
# First-time setup: Run build-windows-local.ps1 first.
#
# Usage:
#   .\scripts\win-rebuild.ps1
#   .\scripts\win-rebuild.ps1 `
#       -PortableDir "C:\Users\Administrator\Desktop\MegaCustomGUI-Portable" `
#       -ExpectedCommit "full-40-character-commit"

[CmdletBinding()]
param(
    [string]$QtPath = "C:\Qt\6.6.0\msvc2019_64",
    [string]$VcpkgPath = "C:\vcpkg",
    [string]$PortableDir = "",
    [string]$ExpectedCommit = "",
    [switch]$SkipPull
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$GuiPath = Join-Path $ProjectRoot "qt-gui"
$BuildDir = Join-Path $GuiPath "build-win64"
$SdkBuildDir = Join-Path $ProjectRoot "third_party\sdk\build_sdk"
$ReleaseDir = Join-Path $BuildDir "Release"
$BuiltExe = Join-Path $ReleaseDir "MegaCustomGUI.exe"

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (!$Condition) { throw $Message }
}

function Invoke-NativeCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$FailureMessage
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FailureMessage (exit code $LASTEXITCODE)"
    }
}

function Resolve-CMakeExecutable {
    param(
        [string]$BuildDirectory,
        [string]$VcpkgRoot
    )

    $candidates = New-Object 'System.Collections.Generic.List[string]'
    $cachePath = Join-Path $BuildDirectory "CMakeCache.txt"
    if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
        $cacheLine = Get-Content -LiteralPath $cachePath |
            Where-Object { $_ -like "CMAKE_COMMAND:INTERNAL=*" } |
            Select-Object -First 1
        if ($cacheLine) {
            $candidates.Add($cacheLine.Substring("CMAKE_COMMAND:INTERNAL=".Length))
        }
    }

    $pathCommand = Get-Command "cmake.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($pathCommand) { $candidates.Add($pathCommand.Source) }

    $knownVcpkgCMake = Join-Path $VcpkgRoot (
        "downloads\tools\cmake-3.31.10-windows\" +
        "cmake-3.31.10-windows-x86_64\bin\cmake.exe")
    $candidates.Add($knownVcpkgCMake)

    $vcpkgTools = Join-Path $VcpkgRoot "downloads\tools"
    if (Test-Path -LiteralPath $vcpkgTools -PathType Container) {
        Get-ChildItem -LiteralPath $vcpkgTools -Filter "cmake.exe" -File -Recurse `
                -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTimeUtc -Descending |
            ForEach-Object { $candidates.Add($_.FullName) }
    }

    $vsWhere = Join-Path ${env:ProgramFiles(x86)} `
        "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vsWhere -PathType Leaf) {
        $vsRoot = (& $vsWhere -latest -property installationPath | Select-Object -First 1)
        if ($vsRoot) {
            $candidates.Add((Join-Path $vsRoot `
                "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"))
        }
    }

    $programFilesCMake = Join-Path $env:ProgramFiles "CMake\bin\cmake.exe"
    $candidates.Add($programFilesCMake)

    return $candidates |
        Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
        Select-Object -First 1
}

function Resolve-FFmpegExecutable {
    param(
        [string]$PortableDirectory,
        [string]$VcpkgRoot
    )

    $candidates = New-Object 'System.Collections.Generic.List[string]'
    if ($PortableDirectory) {
        $candidates.Add((Join-Path $PortableDirectory "ffmpeg.exe"))
        $candidates.Add((Join-Path $PortableDirectory "bin\ffmpeg.exe"))
    }

    $candidates.Add("C:\ffmpeg\bin\ffmpeg.exe")
    if ($env:ProgramFiles) {
        $candidates.Add((Join-Path $env:ProgramFiles "ffmpeg\bin\ffmpeg.exe"))
    }
    if (${env:ProgramFiles(x86)}) {
        $candidates.Add((Join-Path ${env:ProgramFiles(x86)} `
            "ffmpeg\bin\ffmpeg.exe"))
    }
    if ($env:USERPROFILE) {
        $candidates.Add((Join-Path $env:USERPROFILE `
            "projects\Mega - SDK\mega-custom-app\bin\ffmpeg.exe"))
    }

    $pathCommand = Get-Command "ffmpeg.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($pathCommand) { $candidates.Add($pathCommand.Source) }

    $candidates.Add((Join-Path $VcpkgRoot `
        "installed\x64-windows\tools\ffmpeg\ffmpeg.exe"))

    return $candidates |
        Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
        Select-Object -First 1
}

try {
    $runningApp = Get-Process -Name "MegaCustomGUI" -ErrorAction SilentlyContinue
    Assert-True (!$runningApp) `
        "MegaCustomGUI is running. Close every app window, wait 10 seconds, and retry."

    Assert-True (Test-Path -LiteralPath $BuildDir -PathType Container) `
        "The incremental build directory is missing: $BuildDir. Run build-windows-local.ps1 first."
    Assert-True (Test-Path -LiteralPath (Join-Path $BuildDir "CMakeCache.txt") -PathType Leaf) `
        "CMakeCache.txt is missing from $BuildDir. Run build-windows-local.ps1 first."

    if ([string]::IsNullOrWhiteSpace($PortableDir)) {
        $PortableDir = Join-Path $GuiPath "MegaCustomGUI-Portable"
    }
    $PortableDir = [IO.Path]::GetFullPath($PortableDir)
    Assert-True (Test-Path -LiteralPath $PortableDir -PathType Container) `
        "Portable directory not found: $PortableDir"

    $PortableExe = Join-Path $PortableDir "MegaCustomGUI.exe"
    $PortableMarker = Join-Path $PortableDir "portable.marker"
    $PortableSettings = Join-Path $PortableDir "settings.ini"
    $LiveRegistry = Join-Path $PortableDir "members.json"
    Assert-True (Test-Path -LiteralPath $PortableExe -PathType Leaf) `
        "Portable executable not found: $PortableExe"
    Assert-True ((Test-Path -LiteralPath $PortableMarker -PathType Leaf) -or
                 (Test-Path -LiteralPath $PortableSettings -PathType Leaf)) `
        "The selected folder does not appear to be a portable installation: $PortableDir"

    $registryExistedBefore = Test-Path -LiteralPath $LiveRegistry -PathType Leaf
    $registryHashBefore = $null
    if ($registryExistedBefore) {
        $registryHashBefore = (Get-FileHash -LiteralPath $LiveRegistry `
            -Algorithm SHA256).Hash
    }

    $gitCommand = Get-Command "git.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    Assert-True ($null -ne $gitCommand) "git.exe was not found on PATH."

    $trackedChanges = (& $gitCommand.Source -C $ProjectRoot status --porcelain `
        --untracked-files=no | Out-String).Trim()
    Assert-True ($LASTEXITCODE -eq 0) "Could not inspect the Git worktree."
    Assert-True ([string]::IsNullOrWhiteSpace($trackedChanges)) `
        "The repository has tracked local changes. Commit or preserve them before rebuilding."

    if (!$SkipPull) {
        Write-Host "Pulling the latest fast-forward update..." -ForegroundColor Yellow
        Invoke-NativeCommand -FilePath $gitCommand.Source `
            -Arguments @("-C", $ProjectRoot, "pull", "--ff-only") `
            -FailureMessage "Git pull failed; no application files were replaced"
    }

    $head = (& $gitCommand.Source -C $ProjectRoot rev-parse HEAD | Out-String).Trim()
    Assert-True ($LASTEXITCODE -eq 0) "Could not read the repository revision."
    if (![string]::IsNullOrWhiteSpace($ExpectedCommit)) {
        Assert-True ($head.Equals($ExpectedCommit, [StringComparison]::OrdinalIgnoreCase)) `
            "Unexpected revision $head. Expected $ExpectedCommit. No application files were replaced."
    }

    $sdkArtifact = Join-Path $SdkBuildDir "Release\SDKlib.lib"
    $ccronexprCandidates = @(
        (Join-Path $SdkBuildDir `
            "third_party\ccronexpr\ccronexpr.dir\Release\ccronexpr.lib"),
        (Join-Path $SdkBuildDir "third_party\ccronexpr\Release\ccronexpr.lib")
    )
    $ccronexprArtifact = $ccronexprCandidates |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
    Assert-True (Test-Path -LiteralPath $sdkArtifact -PathType Leaf) `
        "Required MEGA SDK artifact is missing: $sdkArtifact"
    Assert-True (![string]::IsNullOrWhiteSpace($ccronexprArtifact)) `
        "Required ccronexpr artifact is missing under $SdkBuildDir."

    $cmake = Resolve-CMakeExecutable -BuildDirectory $BuildDir -VcpkgRoot $VcpkgPath
    Assert-True (![string]::IsNullOrWhiteSpace($cmake)) `
        "CMake could not be resolved from CMakeCache.txt, PATH, Visual Studio, or $VcpkgPath."
    $ctest = Join-Path (Split-Path -Parent $cmake) "ctest.exe"
    Assert-True (Test-Path -LiteralPath $ctest -PathType Leaf) `
        "ctest.exe was not found next to the resolved CMake executable: $cmake"

    $ffmpeg = Resolve-FFmpegExecutable -PortableDirectory $PortableDir `
        -VcpkgRoot $VcpkgPath
    Assert-True (![string]::IsNullOrWhiteSpace($ffmpeg)) `
        "FFmpeg was not found beside the app, in its bin folder, on PATH, or under vcpkg."
    $ffprobe = Join-Path (Split-Path -Parent $ffmpeg) "ffprobe.exe"
    Assert-True (Test-Path -LiteralPath $ffprobe -PathType Leaf) `
        "ffprobe.exe was not found next to FFmpeg: $ffmpeg"

    $runtimePaths = @(
        $PortableDir,
        (Split-Path -Parent $ffmpeg),
        (Join-Path $VcpkgPath "installed\x64-windows\bin"),
        (Join-Path $QtPath "bin")
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Container } |
        Select-Object -Unique
    if ($runtimePaths.Count -gt 0) {
        $env:Path = ($runtimePaths -join ";") + ";" + $env:Path
    }

    Write-Host "Building revision $head..." -ForegroundColor Yellow
    Invoke-NativeCommand -FilePath $cmake `
        -Arguments @("--build", $BuildDir, "--config", "Release", "--parallel") `
        -FailureMessage "Windows build failed; no application files were replaced"

    Assert-True (Test-Path -LiteralPath $BuiltExe -PathType Leaf) `
        "The build completed without producing $BuiltExe"

    $testInventoryText = (& $ctest --test-dir $BuildDir -C Release `
        --show-only=json-v1 | Out-String)
    Assert-True ($LASTEXITCODE -eq 0) `
        "CTest could not enumerate the registered integration tests."
    $testInventory = $testInventoryText | ConvertFrom-Json
    $registeredTests = @($testInventory.tests | ForEach-Object { $_.name })
    $requiredTests = @(
        "persistence_integration",
        "watermarker_integration",
        "member_registry_integration"
    )
    foreach ($testName in $requiredTests) {
        Assert-True ($registeredTests -contains $testName) `
            "Required integration test is not registered: $testName"
    }

    Write-Host "Running C++ integration tests..." -ForegroundColor Yellow
    Invoke-NativeCommand -FilePath $ctest `
        -Arguments @("--test-dir", $BuildDir, "-C", "Release", "--output-on-failure") `
        -FailureMessage "Integration tests failed; no application files were replaced"

    $diagnosticTest = Join-Path $ProjectRoot `
        "tests\MemberRegistryDiagnosticScriptTests.ps1"
    Assert-True (Test-Path -LiteralPath $diagnosticTest -PathType Leaf) `
        "Registry diagnostic regression script is missing: $diagnosticTest"
    Write-Host "Running PowerShell registry regression test..." -ForegroundColor Yellow
    & $diagnosticTest

    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $backupDir = Join-Path (Split-Path -Parent $PortableDir) `
        "MegaCustomGUI-Before-Update-$stamp"
    New-Item -ItemType Directory -Path $backupDir | Out-Null
    Copy-Item -LiteralPath $PortableExe -Destination $backupDir
    if (Test-Path -LiteralPath $LiveRegistry -PathType Leaf) {
        Get-ChildItem -LiteralPath $PortableDir -File |
            Where-Object { $_.Name -like "members.json*" } |
            Copy-Item -Destination $backupDir
    }

    $builtHash = (Get-FileHash -LiteralPath $BuiltExe -Algorithm SHA256).Hash
    $stagedExe = Join-Path $PortableDir "MegaCustomGUI.exe.update-$stamp"
    Copy-Item -LiteralPath $BuiltExe -Destination $stagedExe
    $stagedHash = (Get-FileHash -LiteralPath $stagedExe -Algorithm SHA256).Hash
    Assert-True ($stagedHash -eq $builtHash) `
        "The staged executable hash does not match the verified build. The old executable remains active."

    $atomicBackup = Join-Path $backupDir "MegaCustomGUI.exe.atomic-old"
    [IO.File]::Replace($stagedExe, $PortableExe, $atomicBackup)
    $installedHash = (Get-FileHash -LiteralPath $PortableExe -Algorithm SHA256).Hash
    Assert-True ($installedHash -eq $builtHash) `
        "Installed executable verification failed. Restore from $backupDir before launching."

    $registryExistsAfter = Test-Path -LiteralPath $LiveRegistry -PathType Leaf
    Assert-True ($registryExistsAfter -eq $registryExistedBefore) `
        "The live member registry appeared or disappeared during the update. Restore from $backupDir before launching."

    $registryMembers = $null
    $registryGroups = $null
    $registryHashAfter = $null
    if ($registryExistsAfter) {
        $registryHashAfter = (Get-FileHash -LiteralPath $LiveRegistry `
            -Algorithm SHA256).Hash
        Assert-True ($registryHashAfter -eq $registryHashBefore) `
            "The live member registry changed during the executable update. Restore from $backupDir before launching."
        $registry = Get-Content -LiteralPath $LiveRegistry -Raw -Encoding UTF8 |
            ConvertFrom-Json
        $registryMembers = @($registry.members).Count
        $registryGroups = @($registry.groups).Count
    }

    [PSCustomObject]@{
        Result = "VERIFIED UPDATE INSTALLED - READY FOR CONTROLLED FIRST LAUNCH"
        RepositoryCommit = $head
        CppIntegrationTests = "PASSED"
        PowerShellRegistryTest = "PASSED"
        FFmpeg = $ffmpeg
        InstalledExecutable = $PortableExe
        InstalledExecutableHash = $installedHash
        RegistryWasModified = ($registryHashAfter -ne $registryHashBefore)
        RegistryHash = $registryHashAfter
        MembersBeforeFirstLaunch = $registryMembers
        GroupsBeforeFirstLaunch = $registryGroups
        SafetyBackup = $backupDir
    } | Format-List
} catch {
    Write-Host ""
    Write-Host "UPDATE STOPPED: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Do not launch MegaCustomGUI until this error is reviewed." -ForegroundColor Yellow
    exit 1
}
