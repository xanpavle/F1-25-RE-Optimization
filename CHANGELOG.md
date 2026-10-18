# Changelog

All notable changes to this project are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [Semantic Versioning](https://semver.org/).

## [0.1.0] - 2026-10-18

First public, versioned release.

### Added

- Runtime renderer delta for **F1 25** through a proxy `dinput8.dll`, applied
  before the engine reads `hardware_settings_config.xml`.
- Engine-level overrides through the two generic settings accessors (VRS, async
  compute, ground-cover / static-instance caps).
- **GPU-aware upscaler selection** (`[upscaler] mode = auto`):
  NVIDIA RTX 20/30/40/50 → DLSS, NVIDIA GTX → FSR 3, AMD → FSR 3,
  Intel Arc → XeSS. `keep`, `dlss`, `fsr3`, `xess`, `taa`, `off` can be forced.
- `src/gpu.cpp`: dependency-free vendor/series detection (EnumDisplayDevices
  with a registry fallback). `build/gputest.exe` prints what the mod sees.
- Automatic one-time `.untuned` backup of `hardware_settings_config.xml`.
- Installer auto-detection for Steam libraries (`libraryfolders.vdf`), Epic
  manifests, EA App and common paths, plus an optional explicit folder.
- `test.bat`: hook test, ini test, GPU probe and a full upscaler-matrix test.
- `docs/faq.md` and `docs/troubleshooting.md`.
- `src/version.h` — the version is logged on every launch.

### Changed

- `Install-F125-Tune.bat` now falls back to `prebuilt/dinput8.dll`, so a plain
  download installs without mingw.
- Uninstall / Restore detect the game the same way and restore the previous
  `dinput8.dll` from `dinput8.dll.bak-f125` if one was backed up.
- `Restore-F125-Tune.ps1` no longer hardcodes a user path (uses `%USERPROFILE%`).
- README: GPU support table, upscaler section, test instructions.

### Fixed

- Corrected the `f125_tune.ini` / `src/tune.h` documentation that claimed a
  separate `f125_tune_profile.xml` was copied over the game file. The mod
  rewrites attributes in place (`profile::Transform`); no such file exists.
- `test/tunetest.cpp` now checks keys that are actually in the shipped ini.
- The "untouched backup (`.untuned`)" promised by the README is now actually
  created by the mod on first launch.

### Known limitations

- Applying a graphics preset **in-game** rewrites the settings file for that
  session; the file-based delta is lost until the next launch.
- The detours are tied to one `F1_25.exe` build; a game update makes them fail
  safe (no patching) until the RVAs are re-derived.
- The upscaler switch changes only the backend booleans; `aa_quality` (Quality /
  Balanced / Performance) is left exactly as you set it in-game.
