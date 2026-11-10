# Changelog

All notable changes to this project are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [Semantic Versioning](https://semver.org/).

## [0.3.7] - 2026-11-11

From the first 0.3.6 logs (14 reports). No crash stack contained the mod.

### Added

- **Adaptive VRAM guard.** The 0.3.6 logs showed 8 GB cards (an RTX 5070
  Laptop GPU, an RTX 3050) using 111-165 % of their video memory budget on
  Balanced, with frames of a quarter second and more as Windows moved textures
  to system RAM. The mod now measures the game's video memory 3 and 10 minutes
  in. When the game went over the budget, the next launch puts textures, shadow
  maps, fog maps and car reflections at Potato's sizes (effects that only cost
  GPU time stay at your preset). It stays on while it is needed, and is tried
  off again once the game fits easily (under 75 %). Never above your own
  settings; another GPU starts fresh. The memory is kept in `f125_vram.txt`.

### Changed

- Version `0.3.7` in the DLL, installer output and docs.

## [0.3.6] - 2026-11-10

From 46 sessions of 0.3.5 logs (22 players). No crash stack contained the mod.

### Fixed

- **Crash safe mode works when the game folder can't be written.** It used to
  switch itself off for that launch; the launch state now goes to
  `%LOCALAPPDATA%\F1 25 RE-Optimization` instead.
- **Crash safe mode explains itself.** Each launch logs how the last one ended:
  quit cleanly, passed the 90 s check but did not quit cleanly (a crash after
  90 s, which crash safe mode does not act on), or never got past 90 s.
- **Recommended no longer steps a mid-range GPU down to Potato at 1440p on a weak
  CPU.** On an older or 4-thread CPU the CPU is the limit, so Potato only made
  the game look worse (an RTX 4060 with a 4th-gen Intel CPU stayed at ~30 fps).

### Added

- **Weak-CPU guard.** On a CPU with 4 threads or fewer, an Intel Core of the 7th
  generation or older, or an older AMD FX / Phenom / Athlon / A-series, every
  preset also gets the cuts that save CPU time: crowd distance and billboards,
  particle rate and distance, lower-detail trees, ground cover distance, night
  shadow casters. Never above your own settings.
- **4 GB texture guard.** A discrete card with 4 GB or less streams textures at
  Potato's level on every preset (4 GB cards had 1-2 second hitches).
- **Log lines:** CPU name, cores and threads; GPU driver version; laptop power
  (on battery, battery saver) with a note when it costs fps; Wine / Proton.

### Changed

- Version `0.3.6` in the DLL, installer output and docs.

## [0.3.5] - 2026-11-08

From the first 0.3.4 logs.

### Fixed

- **Gaming laptops were tuned for the CPU's graphics instead of the GPU.** On a
  hybrid laptop (for example an RTX 3050 Laptop GPU with AMD or Intel graphics
  in the CPU) Windows often leaves the NVIDIA card out of the display list, so
  the mod saw only the integrated graphics: Recommended then picked Potato and
  Auto left the upscaler alone. Detection now also reads the video device map
  (present adapters only, safe at DLL load), finds the render-only GPU and picks
  it. The log lists every adapter it found.
- **Frame generation is never switched off by the upscaler.** In F1 25 frame
  generation belongs to the upscaler; when your settings have frame generation
  on, the mod now keeps your upscaler instead of switching to another one
  (switching can turn frame generation off and cost far more fps than it
  gains). The log says when this happens.

### Changed

- **Recommended also looks at your resolution.** A GPU in the Balanced tier at
  1440p or more (3.5 megapixels and up, including 3440x1440 and 4K) gets
  Potato, because those resolutions cost far more GPU time. Quality-tier GPUs
  are not stepped down. The log says the tier and the resolution.
- Version `0.3.5` in the DLL, installer output and docs.

## [0.3.4] - 2026-11-08

### Fixed

- **Sky shadow low LOD was switched off on every preset**, which costs a little
  more than the player's own setting when they had it on. It is now set only by
  the Potato preset, and then ON (the cheaper value), as the file profile always
  said. Balanced and Quality leave it as yours. A value you set yourself in
  `f125_tune.ini` still applies.
- Version `0.3.4` in the DLL, installer output and docs.

## [0.3.3] - 2026-11-08

**Follow-up from the first 0.3.2 Send Logs.** Crashes in those logs happened
just as often with the mod off, so none is pinned on the mod; these are the
fixes the data pointed at.

### Changed

- **VRAM guard is now for 6 GB or less** (was 8 GB). 8 GB cards handle the
  tuned values fine; 6 GB and smaller cards, and integrated graphics, still
  never get memory-heavy settings raised.
- **Ray-traced AO and DDGI are never switched on for a 6 GB or smaller card**,
  even with the Quality preset in `target` mode (seen on an RTX 2060 6 GB).
- **Potato no longer writes `skyShadowCascadeCount = 2`.** Below 4 it is a
  known crash risk in-engine, and it saves almost nothing.
- **Clearer crash safe mode line** when the stock launch crashed too: "the
  last launch crashed with the mod OFF too, so the crash is likely not this
  mod (check other mods, drivers or the game)".
- Version `0.3.3` in the DLL, installer output and docs.

## [0.3.2] - 2026-11-07

**Fixes from the players' Send Logs data: the mod no longer makes any setting
more expensive than your own, integrated GPUs keep their upscaler, and a crash
can no longer turn into a crash loop.** Same version number as the Swofty
Launcher addon from now on.

### Fixed

- **The settings delta could raise settings on Low presets.** It wrote fixed
  values (shadows 2048, fog 512×288, grass 80 ...) whatever you had, so a
  player on Low got bigger shadow and fog maps than their own preset. Now a
  tuned value is written only when it costs the same or less than yours; the
  rest is kept and logged as `kept <setting> = <yours>`. Raising is only done
  in `enforce_mode = target`.
- **Rain, grass, fog and night character shadows were pinned ON in every
  mode**, which also raised cost on Low. They are now only pinned in `target`
  mode, and never under Potato. God rays, lens streak and motion blur are
  still pinned OFF (that only removes work).
- **Potato could be slower than Balanced.** Its cloud scale was 4.0, which is
  the Ultra value (the scale is the cloud render resolution, so lower is
  cheaper; Balanced uses 2.0). It is now 1.0, and a test checks that no
  Potato value costs more than Balanced's.
- **Integrated GPUs (Intel Iris Xe / UHD, Radeon APU graphics) no longer get
  FSR 3 forced.** `upscaler = auto` leaves your in-game choice there. A
  discrete card is now always picked over the CPU's graphics.
- **First 0.3.2 launch puts your own values back** for the settings the mod
  owns (from the `.untuned` copy) before applying the new delta, so values an
  older version raised do not stick. Your resolution and other settings stay.

### Added

- **`preset = recommended`** (new default, also menu option 4 in the
  installer): picked from your GPU and its VRAM at every launch. Integrated
  graphics, GTX, RX 5000 and 4 GB cards get Potato; RTX 4080+, 5070 Ti+,
  RX 7900 and 9070 get Quality; the rest get Balanced.
- **VRAM guard.** On GPUs with 8 GB or less, and on integrated graphics,
  shadow maps, fog maps, car reflections and texture streaming are never
  raised, even in `target` mode.
- **Crash safe mode** (`crash_safe_mode = 1`, default). If a launch with the
  mod on crashes or is killed before the game has run for 90 s, the next
  launch runs stock and the log says why. Two in a row keep it stock for three
  launches, then the mod is tried again. If the stock launch crashes too, the
  crash was not the mod's and the mod runs again.
- **Log header** with everything needed to compare two logs: version, GPU,
  vendor, integrated, VRAM, preset (and what Recommended picked), mode,
  upscaler, resolution, display mode, vsync, frame cap, frame generation,
  upscaler quality, dynamic resolution, and every value changed as
  `set <setting>: <yours> -> <tuned>`.
- **Notes when the mod can't show a gain:** the log says so when vsync or the
  in-game frame cap is on.

### Changed

- A/B stock mode (Ctrl+F10) now restores only the settings the mod owns from
  `.untuned`, so your current resolution and display settings stay.
- Version `0.3.2` in the DLL, installer output and docs.

## [0.3.0.5] - 2026-10-24

**Crash fix for players who also use Swofty Launcher. No change otherwise.**

### Fixed

- **Crash in career loading (`Career_Load_DepartmentEntries`) when the
  standalone mod and Swofty Launcher ran in the same session.** Swofty Launcher
  starts loading its addons when the game shows its first window if its normal
  trigger has not fired yet. The standalone launch banner is a window, so its
  appearance made Swofty load every addon too early, and the career data addons
  crashed. The standalone mod now checks for a Swofty session
  (`<game>\swofty\settings.json`, or `swofty_core.dll` already loaded) and,
  when it finds one, installs nothing: no hooks, no banner, no profile write,
  no hotkey. It logs `Swofty Launcher session detected - standalone mod
  disabled for this launch`.

### Changed

- Version bumped to `0.3.0.5` in the DLL, installer output and docs.

## [0.3.0] - 2026-10-24

**Presets, an installer preset menu and an A/B switch. The Balanced preset is
the 0.2.9 tuning, unchanged.**

### Added

- **`[profile] preset = quality | balanced | potato`.**
  - *Quality* (Ultra Max / Ultra High): Balanced plus ray-traced AO and DDGI,
    grass, hair, 4096 world shadows, full SSAO/SSRT and 960×540 volumetric fog.
    The in-engine enforcement allows the higher shadow, fog and crowd values.
  - *Balanced* (High / Medium): the 0.2.9 delta, unchanged. Default.
  - *Potato* (Low / Ultra Low): ray tracing off, shadows 1024 with 2 cascades,
    fog 256×128 and off, no SSAO/SSRT/DOF/lens flare/motion blur, lower crowd,
    particles, texture streaming and anisotropic filtering, no grass,
    half-resolution planar reflections. Mirrors are untouched. The in-engine
    enforcement uses matching low values, except the sky shadow cascade count:
    forcing it below 4 in-engine makes the game quit at launch, so it stays at
    the preset value.
  - An explicit `[enforce]` line in the ini still overrides the preset.
- **Installer preset menu.** `Install-F125-Tune.bat` asks 1) Quality,
  2) Balanced, 3) Potato or 4) Recommended for your PC, and writes the choice
  into `f125_tune.ini`. *Recommended* reads the graphics card name and prefers a
  dedicated card over integrated graphics. `-Preset <name>` skips the question.
- **A/B switch (Ctrl+F10).** Only while the game window is in front. It creates
  or removes `f125_ab_stock.txt` next to the game. While that file exists, the
  next launch restores the `.untuned` settings and installs no hooks, and the
  banner shows *A/B: running STOCK*. The switch applies on the next launch
  because the mod's settings are loaded at startup.
- `presettest` checks each preset against a real Ultra settings file.

### Changed

- **Switching presets starts from the untouched settings.** The last preset is
  stored in `f125_last_preset.txt`. When it differs, the delta is applied to the
  `.untuned` copy instead of the current file.
- The launch banner names the active preset ("loaded successfully - balanced
  preset") and is wider.
- `Uninstall` also removes `f125_ab_stock.txt` and `f125_last_preset.txt`.
- Version bumped to `0.3.0`.

## [0.2.9] - 2026-10-24

**Safety release: a game-build check, a hook engine that refuses unsafe patches,
and a launch status banner. The shipped look and tuning are unchanged.**

### Added

- **Game build check.** The PE header of `F1_25.exe` (TimeDateStamp `6AA3C898`,
  SizeOfImage `193E6000`) identifies build 1.0.141.7865. The result is logged
  as `build: ... verified` or `build: UNKNOWN ...`. On an unknown build the
  known RVAs are never trusted: every hook is resolved by signature, as with
  `f125_force_scan.txt`.
- **`[profile] unknown_build = scan | off`.** `scan` (default) re-derives the
  hooks on an updated game; `off` disables the whole mod (no profile rewrite,
  no hooks) and shows a red *DISABLED* banner.
- **Launch status banner** (`[profile] status_popup = 1`). For 8 seconds at
  launch it shows *loaded successfully*, *loaded with warnings* or
  *ERROR / DISABLED*. The window is topmost, click-through and never takes
  focus.
- `hooktest` cases for the real game prologues plus a split instruction, a
  RIP-relative operand and a branch.

### Changed

- **`hook::Install` validates the bytes it relocates.** A small x64 decoder
  (`hook::SafeToRelocate`) checks that `patch_len` ends on an instruction
  boundary and that the copied prologue has no branch and no RIP-relative
  operand. Otherwise nothing is patched, and `enforce` logs
  `hook: prologue not relocatable (...) - skipped`.
- `hook.h` now describes the jump that is actually emitted (14-byte `FF 25`
  absolute, not `E9`).
- Version bumped to `0.2.9` in the DLL, installer output and docs.

## [0.2.8] - 2026-10-23

**Imola barrier fix: the lowered world-instance caps are removed. Nothing else
changes visually or in the shipped tuning.**

### Fixed

- **Imola barriers glitched or disappeared.** The `[override]` block lowered the
  engine's world-culling instance caps to `Renderer.maxStaticInstances = 20000`
  and `Renderer.maxPVSRenderInstances = 20000` (engine defaults are about
  24500 / 24800). Imola has the densest static barrier / TecPro / guardrail
  scenery in the game, so once a view went past that cap the engine stopped
  submitting those meshes — walls and fences flickered or vanished on Imola
  only. The two overrides were removed, so the engine defaults are used again.
  Every other track was below the cap, which is why removing it costs
  effectively nothing there.

### Changed

- **`[override]` note updated.** The two cap lines are now documented (commented
  out) as an optional, deliberately aggressive setting; uncomment them only if
  you never race Imola.
- Version bumped to `0.2.8` in the DLL, installer output, README and docs.

## [0.2.7] - 2026-10-23

**Preset capture fixed: the right settings tree, real line breaks and a bounded
dump. The shipped look and the enforcer are unchanged.**

### Added

- **Settings-root candidate log.** A capture now records every hardware-settings
  document it found, once, before choosing one:
  `preset dump: root candidate <name> <ptr> valid=.. preset=..` for the config,
  info, options and ratings documents, so the source of a dump is never
  ambiguous in the log.
- **Dump footer and truncation flag.** Every dump ends with `# attributes=N`,
  `# nodes=N` and `# truncated=yes|no`, so a bounded capture is visible as such
  instead of looking complete.
- **Applied sequence in the dump.** The tree is written in the same order the
  engine applies it (`applyNode`: a node's attributes first, then its children in
  document order), which is the reference sequence `presets.json` needs. The
  engine's own `"Applying reference '%s:%s'"` log strings are not captured.

### Changed

- **Boot capture is event-driven.** The launch capture no longer waits a blind
  20 seconds; it starts once the integer/float parser detours have seen real
  parser activity, then allows a short settle window and a bounded number of
  retries before giving up.
- Version bumped to `0.2.7` in the DLL, installer output, README and docs.

### Fixed

- **The dump was written with a literal `\n`.** Every line break was escaped as
  text, so the whole preset tree landed on a single line. The writer now emits
  real newlines, one `category:attribute=value` entry per line.
- **The stale hard-coded probe and the blind watcher fallback are gone.** The
  old `TryDumpPreset` still read the fixed address `0x146044700` and the watcher
  called it after 20 seconds, which could dump an unrelated pointer. That code
  (`TryDumpPreset`, `g_dump_done`, the probe) was removed; the capture now uses
  the verified live settings root only.
- **The tree walk was unbounded.** A corrupt or unexpectedly large tree could
  read unterminated strings, repeat/cycle through nodes, or write a
  multi-megabyte file. The walk now formats with `%.128s` for node and
  attribute names and `%.256s` for values, caps itself at 8192 nodes and 50000
  attributes, keeps a visited set to stop cycles, and reports any cap hit in the
  `# truncated=` footer.
- **The capture resolved the wrong settings root.** It used the hardware-info
  document at `0x14604e628`, so dumps only contained
  `graphics_card` / `systemInfo` / `resolution` data and never preset values.
  The live preset tree is the `hardware_settings_config` document:
  `hardwareSettings::Options::apply` is called as
  `apply(optionsDb, settingsDoc, "graphicsDetail"|"safe_mode", presetName)` and
  reads the tree from `[settingsDoc + 0x10]`. With `settingsDoc` at
  `0x146044700` the root pointer lives at `0x146044710` (RVA `0x6044710`). The
  capture now prefers that tree, content-checked for a `shadows` /
  `weather_effects` / `antialiasing` / `ground_cover` child, and falls back to
  the first shape-valid candidate only while the config tree is not ready.

## [0.2.6] - 2026-10-23

**Float settings are now enforced in-engine too.**

### Added

- **In-engine enforcement for the remaining float attributes.** The two float
  entries that the file profile already wrote but the preset clamp could still
  revert are now substituted at the point of use:
  - `weather_effects.proceduralCloudRTScale` = `2.0`
  - `ground_cover.drawDistance` = `80.0`

  The engine parses float attributes with a shared `_strtof_s`-shaped parser
  (`FUN_145343ffc`, reached through the two-argument thunk `FUN_145343ff4`) that
  writes the value through an out pointer. The mod detours the core entry and
  compares the call site's return address, exactly like the integer table, so only
  the two known attributes are replaced and every other float setting falls
  through unchanged. `cheaper` mode keeps `min(preset, tuned)`; `target` forces the
  tuned value.
- **`[enforce]` accepts decimal values.** `LookupEnforceFloat` distinguishes an
  IEEE-754 literal from an integer, so `proceduralCloudRTScale = 2.0` and
  `groundCover.drawDistance = 80.0` can be overridden or disabled (`= off`)
  without rebuilding.
- **Float self-healing + tests.** The float parser is validated by prologue on the
  known RVAs and otherwise re-derived from its 26-byte signature, scoring the two
  attribute names through the thunk. `scantest` now validates and re-discovers
  12 integer, 17 boolean and 2 float sites (31 total).

### Changed

- Version bumped to `0.2.6` in the DLL, installer output, README and docs.

## [0.2.5] - 2026-10-21

**The auto-selected upscaler now actually applies — the single biggest FPS lever.**

### Fixed

- **Upscaler selection took effect only for TAA.** `ApplyUpscaler` wrote
  `fsr3="true"`, but the engine stores FSR 3 as a number (`fsr3="0"/"1"`). The invalid
  value made the engine reject the whole `<antialiasing>` element and silently fall
  back to TAA, so `[upscaler] mode=auto` never enabled FSR 3 / DLSS / XeSS. It now
  writes `fsr3="1"` and also sets `cas="1"`, which the engine requires before it will
  engage FSR 3.
- Measured on an AMD RX 7800 XT at 1440p (Austria, wet, 20 cars): on-screen
  (Frame Gen on) **97 → 177 FPS (+82%)**; frame-time (rendered) ~46 → ~82 FPS
  (+~76%), median ~89 FPS. With FSR 3 alone ~66 FPS rendered.

### Changed

- **`enforce_mode = cheaper` is now the default** (was `target`). It only ever
  removes work and never raises the numeric shadow/fog settings above the in-game
  preset, which measured slightly faster than `target`. `target` is unchanged and
  still available.
- **Boolean attributes are pinned in every mode**, not just `target`. The table
  already encodes the intended look (rain / grass / volumetric fog stay on; god rays,
  lens streak, motion blur v2 stay off), so `cheaper` no longer risks dropping those
  visible effects to a lower preset.
- **`rt_ao` and `rt_ddgi` are switched off** in the profile delta. Both are
  full-screen ray-traced passes that cost far more than they show (~+10% measured);
  RT shadows and reflections stay on.

## [0.2.0] - 2026-10-20

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
  preset. 17 boolean call sites (16 attributes; `godRays` is read twice) are pinned to
  the tuned profile: rain (splashes / sheets / beads / high), track heat haze, ground
  cover, grass flattening, ground-cover meshes, volumetric fog and night character
  shadows stay **on**; sky-shadow low-LOD, 16-bit sky shadows,
  crowd-in-shadows, god rays, lens streak and motion-blur v2 stay **off**. Boolean
  pinning only runs in `enforce_mode = target`.
- **Signature scanner (`src/scan.cpp`).** Maps the target PE (live process or a file
  on disk) and finds code by byte signature. The integer parser, boolean check,
  `apply` / `applyNode` and both attribute getters each have a unique signature.
- **Name-driven site discovery.** The parser signatures are shared with look-alike
  functions, so candidates are disambiguated by scoring: a candidate is only accepted
  if the known attribute names resolve into calls through it (12/12 vs 0 for the
  decoys; 17/17 for the boolean check). Enforcement sites that no longer validate
  against the resolved parser are re-derived by attribute name (string constant →
  `lea reg,[rip+str]` → first `call <parser>` after it).
- **`scantest` (`build\scantest.exe`).** Offline validator: loads the game image from
  disk and re-derives/re-checks all 12 integer + 17 boolean sites without launching
  the game. Wired into `test.bat`.
- **Force-scan recovery switch.** Create `f125_force_scan.txt` next to `F1_25.exe`
  (or set `F125_FORCE_SCAN=1`) to ignore the known addresses and resolve the parsers,
  getters, `apply` / `applyNode` and every enforcement site by signature/name.
  Verified live: 12/12 integer names, 17/17 boolean, all 29 sites, ~4 s.
- **GPU-mapping test.** `build\tunetest.exe` checks the auto matrix (RTX -> DLSS,
  GTX -> FSR 3, AMD -> FSR 3, Arc -> XeSS, unknown -> keep) and every forced mode,
  alongside the ini/override checks.
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
- **The self-healing path could terminate the game.** Live string scans walked
  sections mapped `PAGE_NOACCESS` (`.xpdata` and `.link` have no `MEM_READ` flag);
  those are now skipped, and section sizes are clamped to `SizeOfImage`.
- **`AttachLive` always failed** because it validated the PE headers against a zero
  size; the fast path hid it, so the scanner had never actually run in-process. It now
  reports the real `SizeOfImage`, and `scantest` covers it.
- **A wrong image base could fault at startup** because the getter prologues were
  compared with `memcmp` without a readability check. They are now guarded.
- **Hex literals (`0x1e`) were mis-parsed as floats** in the ini.
- **`enforce = true` silently meant *off*** because boolean ini values went through
  `atoi`. They now accept `true`/`false`/`on`/`off`/`yes`/`no`.

### Changed

- Enforcement and getter addresses are validated against the resolved parser before
  patching; the hard-coded RVAs are still the fast path, so a matching build is
  unchanged and the scanner only runs when something does not validate.
- Prologue matching uses wildcard patterns: the stack-frame immediate is ignored, so
  a build that only changes a frame size still hooks. The preset guard now arms when
  either `apply` or `applyNode` resolves (previously both were required).
- `ResolveParser` caches the attribute-string VAs instead of rescanning the 408 MB
  image per candidate (full fallback: 36 s -> ~4 s).
- The discovery log and the override table are hash-indexed (no more O(n²) when
  `[log] all = 1`), and the log handle is kept open for the session instead of opening
  the file per line.
- Inline `;`/`#` comments are stripped from ini values.
- `src/enforce.{h,cpp}` contains the enforcement hooks, the runtime site table and the
  debounced preset guard; `build/enforcetest.exe` checks the fail-safe.

### Notes

- Enforcement is pure value substitution: no engine memory is written. Unknown call
  sites fall through to the original parser, and every hook still verifies its target
  prologue against a byte pattern (stack-frame immediate wildcarded) before patching.
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
  were matched against the shared `strcmp`-to-`"true"` pattern (stack-frame immediate
  wildcarded).

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
