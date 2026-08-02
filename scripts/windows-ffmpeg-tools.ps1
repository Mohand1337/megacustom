function Resolve-MegaCustomFFmpegToolset {
    [CmdletBinding()]
    param(
        [string]$PortableDirectory = "",
        [string]$VcpkgRoot = "",
        [string]$ProjectRoot = "",
        [string]$ExplicitDirectory = "",
        [switch]$SkipSystemCandidates
    )

    $candidates = New-Object 'System.Collections.Generic.List[string]'

    if (![string]::IsNullOrWhiteSpace($ExplicitDirectory)) {
        $candidates.Add((Join-Path $ExplicitDirectory "ffmpeg.exe"))
    }
    if (![string]::IsNullOrWhiteSpace($PortableDirectory)) {
        $candidates.Add((Join-Path $PortableDirectory "ffmpeg.exe"))
        $candidates.Add((Join-Path $PortableDirectory "bin\ffmpeg.exe"))
    }
    if (![string]::IsNullOrWhiteSpace($VcpkgRoot)) {
        $candidates.Add((Join-Path $VcpkgRoot `
            "installed\x64-windows\tools\ffmpeg\ffmpeg.exe"))
    }

    if (![string]::IsNullOrWhiteSpace($ProjectRoot)) {
        $candidates.Add((Join-Path $ProjectRoot "bin\ffmpeg.exe"))
    }

    if (!$SkipSystemCandidates) {
        $candidates.Add("C:\ffmpeg\bin\ffmpeg.exe")
        if ($env:ProgramFiles) {
            $candidates.Add((Join-Path $env:ProgramFiles "ffmpeg\bin\ffmpeg.exe"))
        }
        if (${env:ProgramFiles(x86)}) {
            $candidates.Add((Join-Path ${env:ProgramFiles(x86)} `
                "ffmpeg\bin\ffmpeg.exe"))
        }

        $pathCommands = @(Get-Command "ffmpeg.exe" -All `
            -ErrorAction SilentlyContinue)
        foreach ($pathCommand in $pathCommands) {
            if ($pathCommand.Source) { $candidates.Add($pathCommand.Source) }
        }
    }

    $seen = @{}
    foreach ($candidate in $candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) { continue }

        try {
            $fullFFmpegPath = [IO.Path]::GetFullPath($candidate)
        } catch {
            continue
        }

        $candidateKey = $fullFFmpegPath.ToLowerInvariant()
        if ($seen.ContainsKey($candidateKey)) { continue }
        $seen[$candidateKey] = $true

        if (!(Test-Path -LiteralPath $fullFFmpegPath -PathType Leaf)) { continue }
        $ffprobePath = Join-Path (Split-Path -Parent $fullFFmpegPath) "ffprobe.exe"
        if (!(Test-Path -LiteralPath $ffprobePath -PathType Leaf)) { continue }

        return [PSCustomObject]@{
            FFmpeg = $fullFFmpegPath
            FFprobe = [IO.Path]::GetFullPath($ffprobePath)
        }
    }

    return $null
}
