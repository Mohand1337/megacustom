$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$resolverScript = Join-Path $projectRoot "scripts\windows-ffmpeg-tools.ps1"
if (!(Test-Path -LiteralPath $resolverScript -PathType Leaf)) {
    throw "FFmpeg resolver script is missing: $resolverScript"
}
. $resolverScript

$root = Join-Path ([IO.Path]::GetTempPath()) `
    ("megacustom-ffmpeg-resolver-" + [Guid]::NewGuid().ToString("N"))
$portable = Join-Path $root "portable"
$vcpkg = Join-Path $root "vcpkg"
$vcpkgTools = Join-Path $vcpkg "installed\x64-windows\tools\ffmpeg"
$explicit = Join-Path $root "explicit"

try {
    New-Item -ItemType Directory -Path $portable -Force | Out-Null
    New-Item -ItemType Directory -Path $vcpkgTools -Force | Out-Null
    New-Item -ItemType Directory -Path $explicit -Force | Out-Null

    Set-Content -LiteralPath (Join-Path $portable "ffmpeg.exe") -Value "orphan"
    Set-Content -LiteralPath (Join-Path $vcpkgTools "ffmpeg.exe") -Value "paired"
    Set-Content -LiteralPath (Join-Path $vcpkgTools "ffprobe.exe") -Value "paired"

    $resolved = Resolve-MegaCustomFFmpegToolset `
        -PortableDirectory $portable `
        -VcpkgRoot $vcpkg `
        -SkipSystemCandidates
    if ($null -eq $resolved) {
        throw "Resolver did not find the complete fallback toolset."
    }
    $expectedFFmpeg = [IO.Path]::GetFullPath((Join-Path $vcpkgTools "ffmpeg.exe"))
    if (!$resolved.FFmpeg.Equals($expectedFFmpeg, `
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Resolver selected an orphaned FFmpeg instead of the complete fallback pair."
    }

    Set-Content -LiteralPath (Join-Path $explicit "ffmpeg.exe") -Value "explicit"
    Set-Content -LiteralPath (Join-Path $explicit "ffprobe.exe") -Value "explicit"
    $resolvedExplicit = Resolve-MegaCustomFFmpegToolset `
        -PortableDirectory $portable `
        -VcpkgRoot $vcpkg `
        -ExplicitDirectory $explicit `
        -SkipSystemCandidates
    $expectedExplicit = [IO.Path]::GetFullPath((Join-Path $explicit "ffmpeg.exe"))
    if ($null -eq $resolvedExplicit -or
        !$resolvedExplicit.FFmpeg.Equals($expectedExplicit, `
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Resolver did not prefer the explicitly selected complete toolset."
    }

    Remove-Item -LiteralPath (Join-Path $explicit "ffprobe.exe") -Force
    Remove-Item -LiteralPath (Join-Path $vcpkgTools "ffprobe.exe") -Force
    $missingPair = Resolve-MegaCustomFFmpegToolset `
        -PortableDirectory $portable `
        -VcpkgRoot $vcpkg `
        -ExplicitDirectory $explicit `
        -SkipSystemCandidates
    if ($null -ne $missingPair) {
        throw "Resolver accepted FFmpeg without a neighboring ffprobe.exe."
    }

    $filterOutput = @"
Filters:
 T.C drawtext          V->V       Draw text on top of video frames.
 TS.C futurefilter     V->V       Example with a wider flag column.
"@
    if (!(Find-MegaCustomFFmpegListEntry `
            -Output $filterOutput -Kind Filter -Name drawtext)) {
        throw "Capability parser did not recognize the drawtext filter."
    }
    if (!(Find-MegaCustomFFmpegListEntry `
            -Output $filterOutput -Kind Filter -Name futurefilter)) {
        throw "Capability parser depends on a fixed-width FFmpeg filter flag column."
    }
    if (Find-MegaCustomFFmpegListEntry `
            -Output $filterOutput -Kind Filter -Name text) {
        throw "Capability parser accepted a partial filter name."
    }

    $encoderOutput = " V....D libx264              H.264 encoder"
    if (!(Find-MegaCustomFFmpegListEntry `
            -Output $encoderOutput -Kind VideoEncoder -Name libx264)) {
        throw "Capability parser did not recognize the libx264 video encoder."
    }

    Write-Host "PASS: FFmpeg resolver and capability parser are strict and format-tolerant."
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
