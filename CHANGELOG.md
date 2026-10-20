# Changelog

All notable changes to this project are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [Semantic Versioning](https://semver.org/).

## [0.2.0] - 2026-10-19

**The tuned delta applies in-engine, with self-healing addresses and user-tunable enforcement.**

### Added

- **In-engine enforcement.** The shared integer parser (`FUN_14534dcd8`,
  RVA `0x534dcd8`) is detoured and keyed on the return address of each call site,
  so the tuned value is substituted at the point of use, after the preset clamp.
  Enforced integers: `worldShadowMapSize` 2048, `nightShadowMapSize` 2048,
  `skyShadowMapSize` 2048, `spotShadowMapSize` 2048, `skyShadowCascadeCount` 4,
  `nightShadowSourceCount` 4, `spotShadowSampling` 2, `sampling` 2,
  `volumetricFogMapWidth` 512, `volumetricFogMapHeight` 288, crowd
  `billboardDistance` 2500 and texture streaming `sizeInMiB` 2048.
- **Boolean attributes are pinned too.** The shared boolean check
  (`FUN_14534d918`, RVA `0x534d918`) is called through a 5-byte `call` that the mod
  rewrites to a 5-byte `mov eax, imm32`, so the setting cannot be reverted by the
  preset. 17 attributes are pinned to the tuned profile: rain (splashes / sheets /
  beads / high / heat haze), ground cover, grass flattening, meshes, volumetric fog
  and night character shadows stay **on**; sky-shadow low-LOD, 16-bit sky shadows,
  crowd-in-shadows, god rays, lens streak and motion-blur v2 stay **off**. Boolean
  pinning only runs in `enforce_mode = target`.
- **Signature scanner (`src/scan.cpp`).** Maps the target PE (live process or a file
  on disk) and finds code by byte signature. The integer parser, boolean check,
  `apply` / `applyNode` and both attribute getters each have a unique signature.
- **Name-driven site discovery.** The parser signatures are shared with look-alike
  functions, so candidates are disambiguated by scoring: a candidate is only accepted
  if the known attribute names resolve into calls through it (12/12 vs 0 for the
  decoys; 16/16 for the boolean check). Enforcement sites that no longer validate
  against the resolved parser are re-derived by attribute name (string constant →
  `lea reg,[rip+str]` → first `call <parser>` after it).
- **`scantest` (`build\scantest.exe`).** Offline validator: loads the game image from
  disk and re-derives/re-checks all 12 integer + 17 boolean sites without launching
  the game. Wired into `test.bat`.
- **`[enforce]` ini section.** Override any enforced value by attribute name, or
  disable it with `off`; `bools = 0` disables the boolean pinning as a group.
  Unknown/ambiguous sites are always skipped, never guessed.
- **Session diagnostics.** The log records the game exe path + size, the ini path, and
  (with `[log] report = 1`) a per-run resolution report:
  `report: int sites table=… discovered=… missing=… | bool sites …`.
- **Graphics-menu preset guard.** `hardwareSettings::Options::apply` (`0x40217f0`) and
  `::applyNode` (`0x40226c0`) are detoured with stack-preserving pass-through thunks;
  after the engine finishes, the tuned delta is written back to the file.
- `[profile] enforce = 1` / `[profile] guard = 1` /
  `[profile] enforce_mode = target|cheaper|off` switches (`enforce`/`guard` on and
  `target` by default).
- `build/enforcetest.exe` verifies the enforcement module fails safe in a process
  where `F1_25.exe` is not present; `build.bat` / `test.bat` add the mingw `bin`
  directory to `PATH` themselves.

### Fixed

- **The delta was being silently overridden.** The engine re-derives every
  preset-restricted attribute at launch: it rewrites `hardware_settings_config.xml` with
  its own preset values and uses those in-engine, honouring only the attributes its
  preset does not manage. On a lower or custom preset the file-only tuning could
  therefore have no effect.
- **The settings file could be truncated by an interrupted write.** It is now written
  to a `.tmp` and atomically moved into place.
- **A wrong image base could fault at startup** because the getter prologues were
  compared with `memcmp` without a readability check. They are now guarded.
- **Hex literals (`0x1e`) were mis-parsed as floats** in the ini.
- **`enforce = true` silently meant *off*** because boolean ini values went through
  `atoi`. They now accept `true`/`false`/`on`/`off`/`yes`/`no`.

### Changed

- Enforcement and getter addresses are validated byte-for-byte against the resolved
  parser before patching; the hard-coded RVAs are still the fast path, so a matching
  build is unchanged and the scanner only runs when something does not validate.
- The discovery log and the override table are hash-indexed (no more O(n²) when
  `[log] all = 1`), and the log handle is kept open for the session instead of opening
  the file per line.
- Inline `;`/`#` comments are stripped from ini values.
- `src/enforce.{h,cpp}` contains the enforcement hooks, the runtime site table and the
  debounced preset guard; `build/enforcetest.exe` checks the fail-safe.

### Notes

- Enforcement is pure value substitution: no engine memory is written. Unknown call
  sites fall through to the original parser, and every hook still verifies its target
  prologue byte-for-byte before patching.
- Each enforced value follows `[profile] enforce_mode`:
  - `target` (default) — the tuned value always wins, so the mod defines the quality
    level even when the engine's preset is lower than what you selected.
  - `cheaper` — the **cheaper** of the preset's value and the tuned value, so the mod
    can never raise quality above your preset (and never costs FPS on a preset that is
    already lower than the tuning).
- **`hardware_settings_config.xml` is an engine export, not an input.** Verified with the
  mod fully disabled: writing `worldShadowMapSize="4096"`, fog `1920x1080` and
  `billboardDistance="4000"` into it changes nothing — the engine comes up with its own
  preset values (`1024`, `256x128`, `1500`) and rewrites the file to them. The same test
  with `<resolution width="1280" height="720">` still launched at 2560x1440. The settings
  file delta therefore cannot override a preset by itself; the in-engine enforcement is
  what moves those values.
- With `enforce_mode = cheaper` the tuning needs headroom, so on a preset that is already
  at or below the tuned values the mod is a no-op. `target` removes that dependency.
- Boolean and float settings beyond the pinned sets above (SSAO / SSR quality, cloud RT
  scale, grass distance) still come from the file delta; extending the enforcement table
  to them is tracked in the README roadmap.
- The v0.2.0 enforcement table was re-derived and verified against the live game: the
  integer entries key on the *return address* of each parser call, and the boolean sites
  were matched byte-for-byte to the shared `strcmp`-to-`"true"` check.

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
- The installer only backs up a `dinput8.dll` that is **not** ours, so upgrading
  never turns an old mod build into the "restore" target.
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
