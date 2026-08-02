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
    [string]$FFmpegDir = "",
    [string]$ExpectedCommit = "",
    [switch]$ProvisionFFmpegIfMissing,
    [switch]$SkipPull
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$GuiPath = Join-Path $ProjectRoot "qt-gui"
$BuildDir = Join-Path $GuiPath "build-win64"
$SdkBuildDir = Join-Path $ProjectRoot "third_party\sdk\build_sdk"
$ReleaseDir = Join-Path $BuildDir "Release"
$BuiltExe = Join-Path $ReleaseDir "MegaCustomGUI.exe"
$FFmpegToolsScript = Join-Path $PSScriptRoot "windows-ffmpeg-tools.ps1"

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

function Find-FFmpegToolsetUnderDirectory {
    param(
        [string]$Directory
    )

    if (!(Test-Path -LiteralPath $Directory -PathType Container)) { return $null }
    $ffmpegCandidates = @(Get-ChildItem -LiteralPath $Directory -Filter "ffmpeg.exe" `
        -File -Recurse -ErrorAction SilentlyContinue)
    foreach ($candidate in $ffmpegCandidates) {
        $ffprobe = Join-Path $candidate.DirectoryName "ffprobe.exe"
        if (Test-Path -LiteralPath $ffprobe -PathType Leaf) {
            return [PSCustomObject]@{
                FFmpeg = $candidate.FullName
                FFprobe = [IO.Path]::GetFullPath($ffprobe)
            }
        }
    }
    return $null
}

function Get-VerifiedDownloadedFFmpegToolset {
    param(
        [string]$CacheDirectory
    )

    $baseUri = "https://www.gyan.dev/ffmpeg/builds"
    New-Item -ItemType Directory -Path $CacheDirectory -Force | Out-Null

    $oldProgressPreference = $ProgressPreference
    $ProgressPreference = "SilentlyContinue"
    try {
        [Net.ServicePointManager]::SecurityProtocol = `
            [Net.ServicePointManager]::SecurityProtocol -bor `
            [Net.SecurityProtocolType]::Tls12

        $versionFile = Join-Path $CacheDirectory "release-version.txt"
        Invoke-WebRequest -UseBasicParsing -Uri "$baseUri/ffmpeg-release-essentials.zip.ver" `
            -OutFile $versionFile
        $version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
        Assert-True ($version -match '^\d+\.\d+(?:\.\d+)?$') `
            "The FFmpeg provider returned an invalid release version: $version"

        $archiveName = "ffmpeg-$version-essentials_build.zip"
        $archive = Join-Path $CacheDirectory $archiveName
        $checksumFile = "$archive.sha256"
        $archiveUri = "$baseUri/packages/$archiveName"
        $checksumUri = "$archiveUri.sha256"

        Invoke-WebRequest -UseBasicParsing -Uri $checksumUri -OutFile $checksumFile
        $checksumText = Get-Content -LiteralPath $checksumFile -Raw
        $checksumMatch = [regex]::Match($checksumText, '(?i)\b[a-f0-9]{64}\b')
        Assert-True ($checksumMatch.Success) `
            "The FFmpeg provider returned an invalid SHA-256 checksum."
        $expectedHash = $checksumMatch.Value.ToUpperInvariant()

        if (Test-Path -LiteralPath $archive -PathType Leaf) {
            $cachedHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
            if ($cachedHash -ne $expectedHash) {
                Remove-Item -LiteralPath $archive -Force
            }
        }

        if (!(Test-Path -LiteralPath $archive -PathType Leaf)) {
            Write-Host "Downloading the verified FFmpeg essentials package..." `
                -ForegroundColor Yellow
            $partialArchive = "$archive.partial"
            Remove-Item -LiteralPath $partialArchive -Force -ErrorAction SilentlyContinue
            Invoke-WebRequest -UseBasicParsing -Uri $archiveUri -OutFile $partialArchive
            $downloadedHash = (Get-FileHash -LiteralPath $partialArchive `
                -Algorithm SHA256).Hash
            Assert-True ($downloadedHash -eq $expectedHash) `
                "Downloaded FFmpeg package failed SHA-256 verification."
            Move-Item -LiteralPath $partialArchive -Destination $archive -Force
        }

        $archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
        Assert-True ($archiveHash -eq $expectedHash) `
            "Cached FFmpeg package failed SHA-256 verification."

        $extractDirectory = Join-Path $CacheDirectory "ffmpeg-$version"
        $toolset = Find-FFmpegToolsetUnderDirectory -Directory $extractDirectory
        if ($null -eq $toolset) {
            $extractStage = "$extractDirectory.extracting"
            Remove-Item -LiteralPath $extractStage -Recurse -Force `
                -ErrorAction SilentlyContinue
            New-Item -ItemType Directory -Path $extractStage -Force | Out-Null
            Expand-Archive -LiteralPath $archive -DestinationPath $extractStage -Force
            Remove-Item -LiteralPath $extractDirectory -Recurse -Force `
                -ErrorAction SilentlyContinue
            Move-Item -LiteralPath $extractStage -Destination $extractDirectory
            $toolset = Find-FFmpegToolsetUnderDirectory -Directory $extractDirectory
        }

        Assert-True ($null -ne $toolset) `
            "The verified FFmpeg archive did not contain ffmpeg.exe and ffprobe.exe together."
        return $toolset
    } finally {
        $ProgressPreference = $oldProgressPreference
    }
}

try {
    Assert-True (Test-Path -LiteralPath $FFmpegToolsScript -PathType Leaf) `
        "FFmpeg resolver script is missing: $FFmpegToolsScript"
    . $FFmpegToolsScript

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

    $ffmpegToolset = Resolve-MegaCustomFFmpegToolset `
        -PortableDirectory $PortableDir `
        -VcpkgRoot $VcpkgPath `
        -ProjectRoot $ProjectRoot `
        -ExplicitDirectory $FFmpegDir
    if ($null -eq $ffmpegToolset -and $ProvisionFFmpegIfMissing) {
        $ffmpegCache = Join-Path $BuildDir "verified-tools\ffmpeg"
        $ffmpegToolset = Get-VerifiedDownloadedFFmpegToolset `
            -CacheDirectory $ffmpegCache
    }
    Assert-True ($null -ne $ffmpegToolset) `
        ("A complete FFmpeg toolset was not found. Fast segments require ffmpeg.exe " +
         "and ffprobe.exe in the same directory. Pass -FFmpegDir with a complete toolset " +
         "or use -ProvisionFFmpegIfMissing.")
    $ffmpeg = $ffmpegToolset.FFmpeg
    $ffprobe = $ffmpegToolset.FFprobe

    $ffmpegVersion = (& $ffmpeg -hide_banner -version 2>&1 | Select-Object -First 1)
    Assert-True ($LASTEXITCODE -eq 0) "The resolved ffmpeg.exe could not be executed: $ffmpeg"
    $ffprobeVersion = (& $ffprobe -hide_banner -version 2>&1 | Select-Object -First 1)
    Assert-True ($LASTEXITCODE -eq 0) "The resolved ffprobe.exe could not be executed: $ffprobe"

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

    $ffmpegResolverTest = Join-Path $ProjectRoot `
        "tests\WindowsFFmpegToolResolverTests.ps1"
    Assert-True (Test-Path -LiteralPath $ffmpegResolverTest -PathType Leaf) `
        "FFmpeg resolver regression script is missing: $ffmpegResolverTest"
    Write-Host "Running PowerShell FFmpeg resolver regression test..." `
        -ForegroundColor Yellow
    & $ffmpegResolverTest

    $runningApp = Get-Process -Name "MegaCustomGUI" -ErrorAction SilentlyContinue
    Assert-True (!$runningApp) `
        "MegaCustomGUI was opened during the build. Close it and rerun; no application files were replaced."
    $registryExistsBeforeInstall = Test-Path -LiteralPath $LiveRegistry -PathType Leaf
    Assert-True ($registryExistsBeforeInstall -eq $registryExistedBefore) `
        "The live member registry appeared or disappeared during the build. No application files were replaced."
    if ($registryExistsBeforeInstall) {
        $registryHashBeforeInstall = (Get-FileHash -LiteralPath $LiveRegistry `
            -Algorithm SHA256).Hash
        Assert-True ($registryHashBeforeInstall -eq $registryHashBefore) `
            "The live member registry changed during the build. No application files were replaced."
    }

    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $backupDir = Join-Path (Split-Path -Parent $PortableDir) `
        "MegaCustomGUI-Before-Update-$stamp"
    New-Item -ItemType Directory -Path $backupDir | Out-Null
    Copy-Item -LiteralPath $PortableExe -Destination $backupDir
    $PortableFFmpeg = Join-Path $PortableDir "ffmpeg.exe"
    $PortableFFprobe = Join-Path $PortableDir "ffprobe.exe"
    foreach ($runtimeTool in @($PortableFFmpeg, $PortableFFprobe)) {
        if (Test-Path -LiteralPath $runtimeTool -PathType Leaf) {
            Copy-Item -LiteralPath $runtimeTool -Destination $backupDir
        }
    }
    if (Test-Path -LiteralPath $LiveRegistry -PathType Leaf) {
        Get-ChildItem -LiteralPath $PortableDir -File |
            Where-Object { $_.Name -like "members.json*" } |
            Copy-Item -Destination $backupDir
    }

    $deploymentFiles = @(
        [PSCustomObject]@{
            Name = "ffprobe.exe"
            Source = $ffprobe
            Destination = $PortableFFprobe
        },
        [PSCustomObject]@{
            Name = "ffmpeg.exe"
            Source = $ffmpeg
            Destination = $PortableFFmpeg
        },
        [PSCustomObject]@{
            Name = "MegaCustomGUI.exe"
            Source = $BuiltExe
            Destination = $PortableExe
        }
    )

    foreach ($deploymentFile in $deploymentFiles) {
        $deploymentFile | Add-Member -NotePropertyName ExpectedHash `
            -NotePropertyValue ((Get-FileHash -LiteralPath $deploymentFile.Source `
                -Algorithm SHA256).Hash)
        $samePath = $deploymentFile.Source.Equals($deploymentFile.Destination, `
            [StringComparison]::OrdinalIgnoreCase)
        $deploymentFile | Add-Member -NotePropertyName RequiresInstall `
            -NotePropertyValue (!$samePath)
        $deploymentFile | Add-Member -NotePropertyName StagePath `
            -NotePropertyValue $null
        if (!$samePath) {
            $stagePath = "$($deploymentFile.Destination).update-$stamp"
            Copy-Item -LiteralPath $deploymentFile.Source -Destination $stagePath
            $stageHash = (Get-FileHash -LiteralPath $stagePath -Algorithm SHA256).Hash
            Assert-True ($stageHash -eq $deploymentFile.ExpectedHash) `
                "The staged $($deploymentFile.Name) hash does not match its verified source."
            $deploymentFile.StagePath = $stagePath
        }
    }

    foreach ($deploymentFile in $deploymentFiles) {
        if (!$deploymentFile.RequiresInstall) { continue }
        if (Test-Path -LiteralPath $deploymentFile.Destination -PathType Leaf) {
            $atomicBackup = Join-Path $backupDir `
                "$($deploymentFile.Name).atomic-old"
            [IO.File]::Replace($deploymentFile.StagePath, `
                $deploymentFile.Destination, $atomicBackup)
        } else {
            [IO.File]::Move($deploymentFile.StagePath, $deploymentFile.Destination)
        }
        $installedFileHash = (Get-FileHash -LiteralPath $deploymentFile.Destination `
            -Algorithm SHA256).Hash
        Assert-True ($installedFileHash -eq $deploymentFile.ExpectedHash) `
            "Installed $($deploymentFile.Name) verification failed. Restore from $backupDir before launching."
    }

    $installedHash = (Get-FileHash -LiteralPath $PortableExe -Algorithm SHA256).Hash
    $installedFFmpegHash = (Get-FileHash -LiteralPath $PortableFFmpeg `
        -Algorithm SHA256).Hash
    $installedFFprobeHash = (Get-FileHash -LiteralPath $PortableFFprobe `
        -Algorithm SHA256).Hash

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
        PowerShellFFmpegResolverTest = "PASSED"
        FFmpeg = $PortableFFmpeg
        FFmpegHash = $installedFFmpegHash
        FFprobe = $PortableFFprobe
        FFprobeHash = $installedFFprobeHash
        FFmpegVersion = $ffmpegVersion
        FFprobeVersion = $ffprobeVersion
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
