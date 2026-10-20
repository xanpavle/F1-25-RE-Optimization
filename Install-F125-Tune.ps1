# F1 25 RE-Optimization - installer / uninstaller / settings restore.
#
#   Install-F125-Tune.bat                  install (auto-detects the game)
#   Install-F125-Tune.bat "D:\Games\F1 25"  install to a specific folder
#   Uninstall-F125-Tune.bat                remove the mod (keeps settings)
#   Restore-F125-Tune.ps1                  remove the mod + restore stock settings
#
# Auto-detection order: -Game argument, %F125_GAME%, every Steam library,
# Epic manifests, EA App / common install paths. Nothing is downloaded.

param(
    [ValidateSet('Install', 'Uninstall', 'Restore')]
    [string]$Action = 'Install',
    [string]$Game = ''
)

$ErrorActionPreference = 'Stop'

function Write-Step($msg) { Write-Host "  $msg" }

function Test-GameDir([string]$p) {
    if ([string]::IsNullOrWhiteSpace($p)) { return $false }
    return (Test-Path (Join-Path $p 'F1_25.exe'))
}

function Find-F125Game {
    $candidates = New-Object System.Collections.Generic.List[string]
    if ($Game)          { $candidates.Add($Game) }
    if ($env:F125_GAME) { $candidates.Add($env:F125_GAME) }

    # --- Steam: registry -> steamapps\libraryfolders.vdf -> every library ---
    $steamRoots = New-Object System.Collections.Generic.List[string]
    foreach ($key in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam')) {
        try {
            $p = Get-ItemProperty -Path $key -ErrorAction Stop
            if ($p.SteamPath)   { $steamRoots.Add(($p.SteamPath   -replace '/', '\')) }
            if ($p.InstallPath) { $steamRoots.Add(($p.InstallPath -replace '/', '\')) }
        } catch { }
    }
    $steamRoots.Add('C:\Program Files (x86)\Steam')
    $steamRoots.Add('C:\Program Files\Steam')

    $libraries = New-Object System.Collections.Generic.List[string]
    foreach ($r in $steamRoots) {
        if (-not $r) { continue }
        $r = $r.TrimEnd('\')
        $libraries.Add($r)
        $vdf = Join-Path $r 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            foreach ($line in Get-Content $vdf -ErrorAction SilentlyContinue) {
                $m = [regex]::Match($line, '"path"\s+"([^"]+)"')
                if ($m.Success) {
                    $libraries.Add(($m.Groups[1].Value -replace '\\\\', '\'))
                }
            }
        }
    }
    foreach ($l in ($libraries | Select-Object -Unique)) {
        $candidates.Add((Join-Path $l 'steamapps\common\F1 25'))
    }

    # --- Epic Games Launcher manifests ---
    $epicDir = Join-Path $env:ProgramData 'Epic\EpicGamesLauncher\Data\Manifests'
    if (Test-Path $epicDir) {
        Get-ChildItem $epicDir -Filter *.item -ErrorAction SilentlyContinue | ForEach-Object {
            try {
                $j = Get-Content $_.FullName -Raw -ErrorAction Stop | ConvertFrom-Json
                if ($j.InstallLocation -and $j.DisplayName -match 'F1') {
                    $candidates.Add($j.InstallLocation)
                }
            } catch { }
        }
    }

    # --- EA App and the usual manual locations ---
    foreach ($p in @(
        'C:\Program Files\EA Games\F1 25',
        'C:\Program Files (x86)\EA Games\F1 25',
        'C:\Program Files\Epic Games\F125',
        'D:\SteamLibrary\steamapps\common\F1 25',
        'E:\SteamLibrary\steamapps\common\F1 25'
    )) { $candidates.Add($p) }

    foreach ($c in ($candidates | Select-Object -Unique)) {
        if (Test-GameDir $c) { return (Resolve-Path $c).Path }
    }
    return $null
}

function Get-SettingsPath {
    # Same source of truth as the mod (SHGetFolderPath CSIDL_PERSONAL), so a
    # redirected / OneDrive Documents folder still resolves correctly.
    $docs = [Environment]::GetFolderPath('MyDocuments')
    return (Join-Path $docs 'My Games\F1 25\hardwaresettings\hardware_settings_config.xml')
}

$game = Find-F125Game

if (-not $game) {
    Write-Host ''
    Write-Host 'ERROR: could not find F1 25 (F1_25.exe).' -ForegroundColor Red
    Write-Host 'Pass the folder explicitly, for example:'
    Write-Host '  Install-F125-Tune.bat "D:\Games\F1 25"'
    Write-Host ''
    exit 1
}

Write-Host ''
Write-Host "F1 25 RE-Optimization - $Action" -ForegroundColor Cyan
Write-Host "  game: $game"
Write-Host ''

switch ($Action) {

    'Install' {
        $root = $PSScriptRoot
        # Search order: next to this script (release ZIP layout), then the
        # developer build, then the prebuilt copy that lives in the repo.
        $dll = $null
        foreach ($cand in @(
            (Join-Path $root 'dinput8.dll'),
            (Join-Path $root 'build\dinput8.dll'),      # developer build wins
            (Join-Path $root 'prebuilt\dinput8.dll')
        )) {
            if (Test-Path $cand) { $dll = $cand; break }
        }
        $ini = Join-Path $root 'f125_tune.ini'

        if (-not $dll) { throw "dinput8.dll not found (looked next to the script, in build\ and prebuilt\)." }
        if (-not (Test-Path $ini)) { throw 'f125_tune.ini not found next to this script.' }

        $targetDll = Join-Path $game 'dinput8.dll'
        $targetIni = Join-Path $game 'f125_tune.ini'

        try {
            # Only back up a dinput8.dll that is NOT ours. Our own installs always
            # have f125_tune.ini next to them, so an upgrade must not turn the
            # previous mod build into a "restore" target.
            $looksLikeOurs = Test-Path (Join-Path $game 'f125_tune.ini')
            if ((Test-Path $targetDll) -and (-not $looksLikeOurs)) {
                $bak = "$targetDll.bak-f125"
                if (-not (Test-Path $bak)) {
                    Copy-Item $targetDll $bak -Force
                    Write-Step 'backed up the existing dinput8.dll -> dinput8.dll.bak-f125'
                }
            }
            if (Test-Path $targetIni) {
                Copy-Item $targetIni "$targetIni.bak" -Force
                Write-Step 'backed up the existing f125_tune.ini -> f125_tune.ini.bak'
            }

            Copy-Item $dll $targetDll -Force
            Copy-Item $ini $targetIni -Force
        } catch {
            Write-Host ''
            Write-Host 'ERROR: could not write to the game folder.' -ForegroundColor Red
            Write-Host 'The game is probably in Program Files - run this installer as administrator.'
            Write-Host $_.Exception.Message
            exit 1
        }

        Write-Host 'Installed:' -ForegroundColor Green
        Write-Step $targetDll
        Write-Step $targetIni
        Write-Host ''
        Write-Host 'Next:'
        Write-Host '  1) Launch F1 25 offline (single-player).'
        Write-Host "  2) Open $game\f125_tune.log - it should contain"
        Write-Host '       [mod] F1 25 RE-Optimization v0.2.0'
        Write-Host '       [mod] hooks installed OK'
        Write-Host '       [mod] GPU: ...'
        Write-Host '       [mod] upscaler: mode=auto -> ...'
        Write-Host '  3) Online play: run Uninstall-F125-Tune.bat first.'
        Write-Host ''
    }

    'Uninstall' {
        $removed = 0
        try {
            foreach ($f in @('dinput8.dll', 'f125_tune.ini', 'f125_tune.log')) {
                $p = Join-Path $game $f
                if (Test-Path $p) {
                    Remove-Item $p -Force
                    Write-Step "removed $f"
                    $removed++
                }
            }
            $bak = Join-Path $game 'dinput8.dll.bak-f125'
            if (Test-Path $bak) {
                Copy-Item $bak (Join-Path $game 'dinput8.dll') -Force
                Write-Step 'restored the previous dinput8.dll from dinput8.dll.bak-f125'
            }
        } catch {
            Write-Host ''
            Write-Host 'ERROR: could not modify the game folder.' -ForegroundColor Red
            Write-Host 'Run this as administrator (the game is probably in Program Files).'
            Write-Host $_.Exception.Message
            exit 1
        }
        if ($removed -eq 0) { Write-Step 'nothing to remove (already clean)' }
        Write-Host ''
        Write-Host 'Mod removed. Your graphics settings are untouched.' -ForegroundColor Green
        Write-Host 'Use Restore-F125-Tune.ps1 to also restore stock graphics settings.'
        Write-Host ''
    }

    'Restore' {
        & $PSCommandPath -Action Uninstall -Game $game
        $cfg = Get-SettingsPath
        if (Test-Path "$cfg.untuned") {
            Copy-Item "$cfg.untuned" $cfg -Force
            Write-Host 'Restored stock hardware_settings_config.xml from .untuned' -ForegroundColor Green
        } else {
            Write-Host 'No .untuned backup found - re-pick a graphics preset in-game to reset.'
        }
    }
}
