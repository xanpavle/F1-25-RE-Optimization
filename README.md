# F1 25 RE-Optimization

A runtime renderer optimisation for **F1 25** (EGO engine, Codemasters / EA), built
by reverse-engineering the shipped executable. It makes the renderer do **less work
for the same look** — no `F1_25.exe` bytes are patched on disk.

**v0.1.0** · [FAQ](docs/faq.md) · [Troubleshooting](docs/troubleshooting.md) · [Changelog](CHANGELOG.md)

Built and tested on a single machine, offline, single-player.

![F1 25 RE-Optimization — performance comparison](docs/poster.png)

---

## ⚠️ Disclaimer — read this before you do anything

- **Unofficial.** Not affiliated with, endorsed by, or supported by EA, Codemasters,
  or any of their partners. "F1" and "F1 25" are trademarks of their owners.
- **OFFLINE / SINGLE-PLAYER ONLY.** This is a runtime DLL. It does **not** bypass,
  disable, or evade anti-cheat, and it is **not** intended for online play. Playing
  online with it present may get you banned. Remove it before going online.
- **Use at your own risk.** No warranty of any kind. You are responsible for your own
  machine, your own game install, and your own account.
- **It edits your graphics settings file** (`hardware_settings_config.xml`) at launch.
  An untouched backup (`.untuned`) is kept so you can revert.
- Reverse engineering was performed on the author's own legally-owned copy of the game,
  for personal optimisation and educational purposes only. No game assets or code are
  redistributed here.
- **No results are guaranteed.** Benchmarks below are from one machine; your mileage
  will vary.

---

## Results (measured on the reference machine)

Reference system:

| | |
|---|---|
| GPU | AMD Radeon RX 7800 XT (16 GB) |
| CPU | AMD Ryzen 5 7600X |
| RAM | 32 GB |
| Display | 2560×1440 @ 165 Hz, Windows 10 |
| In-game | FSR 3 Quality, Frame Generation ON, Ray Tracing ON |

Test: **Time trial, Austria, dry, no other cars, one clean lap.**

| Preset | Before (stock) | After (mod) |
|---|---|---|
| **Ultra High** (raster + RT) | ~40–60 FPS | **avg ~140 / low ~120 / high ~150** |
| **Ultra Max** (path tracing) | ~15–25 FPS | ~60 FPS (path-tracing bound) |

> The *displayed* numbers include **Frame Generation**, which roughly doubles the
> on-screen figure. The underlying rendering gain is estimated at **≈ +30–60%**, which
> becomes **≈ 2.3–3.5×** once Frame Gen is counted.
>
> Real-race estimate (20 cars, ~10% drop): **≈110–125 avg** on Ultra High.

**Note on Ultra Max:** "Ultra Max" enables **Path Tracing**, whose official minimum is
an NVIDIA RTX 30-series or AMD RX 7900. The RX 7800 XT is below that, so Ultra Max is
path-tracing-bound and this mod cannot meaningfully help it. Ultra High is the sweet
spot for this class of GPU.

---

## What it does

The mod ships a **proxy `dinput8.dll`** (F1 25 imports exactly one symbol from it,
`DirectInput8Create`). Windows loads it at process start, *before* any game code runs.
On load it does three things:

### 1. Applies a tuned **delta** to your graphics settings

The real renderer quality is read at startup from
`%USERPROFILE%\Documents\My Games\F1 25\hardwaresettings\hardware_settings_config.xml`.

Before the engine reads it, the mod rewrites **only specific attributes** of that file
to cheaper values (`src/profile.cpp`). Everything else — resolution, your preset,
mirrors, ray tracing, rain — is left exactly as the game wrote it. Because it is a
*delta*, it stacks on **whatever preset you pick**.

Tuned attributes include: volumetric fog map (1920×1080 → 512×288), shadow-map sizes
(4096 → 2048), shadow sampling (3 → 2), SSR quality (4 → 2), SSAO (3 → 1), grass draw
distance (120 m → 80 m), crowd billboards, car-reflection scale and cube-map update
rate, particles, hair, god rays / lens streak / motion-blur v2, cloud RT scale, plus
`cs_culling` (the engine's generic GPU culling, default **off**) and a texture-streaming
bias.

### 2. Overrides engine-level renderer flags

Some constants are not in the settings file at all; the engine reads them by name
through two generic accessors:

| accessor | RVA (`F1_25.exe`, base `0x140000000`) | signature |
|---|---|---|
| boolean getter | `0x03a43b90` | `(node, name, uint8_t* out, uint8_t def)` |
| 32-bit getter | `0x03a43240` | `(node, name, uint32_t* out, uint32_t def)` |

The mod detours both, lets the originals run (preserving engine side-effects), then
overwrites the returned value for names listed under `[override]` in `f125_tune.ini`
(grass/instance caps, VRS, async compute, and — if you enable it — the raytracing
toggles, which are **immune** to the settings-file reset below).

### 3. Selects the upscaler your GPU actually supports

The settings file carries three boolean upscaler switches (`dlss`, `fsr3`, `xess`)
plus `taa`. Before writing it, the mod detects the GPU and picks the right backend:

| GPU | Backend chosen by `mode = auto` |
|---|---|
| NVIDIA RTX 20 / 30 / 40 / 50 | **DLSS** |
| NVIDIA GTX 10 / 16 | **FSR 3** (no DLSS hardware) |
| AMD Radeon | **FSR 3** |
| Intel Arc | **XeSS** |
| Intel iGPU / unknown | FSR 3 / leave the in-game choice alone |

`[upscaler] mode = auto` is the default; `dlss`, `fsr3`, `xess`, `taa`, `off` and
`keep` can be forced. The **quality level** (`aa_quality`: Quality / Balanced /
Performance) is never changed — it stays what you picked in-game. Detection is in
`src/gpu.cpp`, uses no extra runtime dependency, and `build\gputest.exe` prints
exactly what the mod sees on your machine.

---

## Requirements

- Windows 10/11 x64 (the proxy is a standard Win32 DLL).
- Any DX12-capable GPU. The upscaler is selected automatically (DLSS / FSR 3 /
  XeSS / TAA) — see *GPU support* above.
- **F1 25** PC build from the same game version this was derived from. If the game has
  been updated, the detours will report a signature mismatch and skip (see
  *Compatibility*).
- No runtime dependencies — the DLL links only against Windows system libraries
  (`KERNEL32`, `msvcrt`, `SHELL32`, `ADVAPI32`).
- To build from source: mingw-w64 GCC.

---

## Install

**Quickest:** run `Install-F125-Tune.bat`. It auto-detects Steam libraries
(including secondary drives via `libraryfolders.vdf`), Epic manifests, EA App and
the default paths. If detection fails, pass the folder:
`Install-F125-Tune.bat "D:\Games\F1 25"`.

It copies `dinput8.dll` (from `build\` if you built it, otherwise `prebuilt\`) and
`f125_tune.ini` next to `F1_25.exe`, and backs up an existing `dinput8.dll` as
`dinput8.dll.bak-f125`.

**Manual:** copy `prebuilt\dinput8.dll` and `f125_tune.ini` next to `F1_25.exe`.

Then:

1. Launch F1 25 **offline**.
2. Check `f125_tune.log` in the same folder:
   - `[mod] hooks installed OK` and `[mod] profile applied …` → working.
   - `[mod] GPU: …` / `[mod] upscaler: mode=auto -> …` → what it detected.
   - `[mod] WARN: … signature mismatch` → different game build (see *Compatibility*).

Convenience scripts:
- `Install-F125-Tune.bat` — install (auto-detect).
- `Uninstall-F125-Tune.bat` — removes the mod, keeps your settings.
- `Restore-F125-Tune.bat` — removes the mod **and** restores the stock settings
  from the `.untuned` backup.

---

## Usage

- **Set your preset, then relaunch.** The mod tunes whatever the settings file contains
  when the game starts.
- **Don't open the graphics settings screen during a test run.** Applying a preset
  makes the engine rewrite its canonical values, which wipes the file-based delta for
  that session. Fully fixing this needs an in-memory hook (see *Roadmap*).
- **Offline only.** Remove the DLL before playing online.
- **Upscaler:** `[upscaler] mode = auto` picks DLSS / FSR 3 / XeSS for your GPU
  and leaves your quality level alone. Force one with `dlss`, `fsr3`, `xess`,
  `taa`, `off`, or change nothing with `keep`.
- Edit `f125_tune.ini` to add/remove engine-level `[override]`s. Set
  `[profile] apply = 0` to leave your settings file alone.

---

## How it works (technical)

- **Proxy load:** the game's import table contains exactly one symbol from
  `dinput8.dll` (`DirectInput8Create`). Shipping a proxy DLL that forwards that symbol
  gets our code loaded at process start, before any game initialiser runs.
- **Detour engine:** a minimal x64 inline hook (`src/hook.cpp`) using 14-byte absolute
  jumps (`FF 25 00000000 <ptr64>`) because `E9 rel32` cannot reach an arbitrary loaded
  DLL. It patches a 15-byte prologue and relocates it into a trampoline.
- **Safety:** both target prologues are verified **byte-for-byte** before patching. On
  mismatch the mod logs `signature mismatch` and skips — it never patches blindly.
- **Stable RVAs:** `F1_25.exe` has `DllCharacteristics = 0x8120` (no `DYNAMIC_BASE`),
  so it loads at its preferred base `0x140000000` and the RVAs are constant.
- **CFG:** the shellcode uses absolute indirect jumps with no CFG check instruction, so
  Control Flow Guard is not tripped.

---

## Building from source

Requires mingw-w64 GCC (e.g. `C:\msys64\mingw64\bin`, prepend it to `PATH`):

```
gcc -O2 -shared -static -static-libgcc -o build\dinput8.dll ^
    src\proxy.cpp src\hook.cpp src\tune.cpp src\profile.cpp src\gpu.cpp ^
    -luser32 -lkernel32 -lshell32 -ladvapi32
```

Or run `build.bat`.

Self-tests (hook, ini parser, GPU probe, upscaler matrix):

```
test.bat
```

Layout:

```
src/proxy.cpp        proxy dinput8 + detours + DllMain
src/hook.{h,cpp}     x64 inline-detour engine
src/tune.{h,cpp}     ini parsing, override table, profile apply, discovery log
src/profile.{h,cpp}  the settings-file delta + upscaler switch (pure, testable)
src/gpu.{h,cpp}      vendor / series detection for `[upscaler] auto`
src/version.h        version string (also written to the log)
test/                self-tests + sample settings file
test.bat             builds and runs every test
f125_tune.ini        profile switch + upscaler + engine overrides
```

---

## Compatibility

- The detours are tied to the **exact** `F1_25.exe` build this was derived from. A game
  update can change the prologues; the mod then fails safe (no patching) and needs the
  addresses re-derived.
- The settings delta targets attribute names that are stable within a game version.
- **GPU support is vendor-independent.** NVIDIA RTX gets DLSS, NVIDIA GTX and AMD get
  FSR 3, Intel Arc gets XeSS, all chosen automatically. The *tuned values* were chosen
  for a mid-high AMD GPU (RX 7800 XT): on weaker GPUs expect different gains and lower
  the preset; on stronger GPUs you may not want them lowered at all.
- `aa_quality` (Quality / Balanced / Performance) is never touched — only the backend
  switch changes, so your quality choice survives.

---

## Roadmap / known issues

- [x] GPU-aware upscaler selection — DLSS / FSR 3 / XeSS / TAA, chosen from the
      detected GPU (`v0.1.0`).
- [ ] Graphics menu re-applying a preset wipes the file delta for that session — fix by
      hooking `hardwareSettings::Options::apply` in memory.
- [ ] Pattern-scan the prologues instead of hardcoding RVAs, so one DLL survives game
      updates.
- [ ] Optional "RT-lite" override profile (RT AO / DDGI / shadows off via the engine
      knobs — menu-proof).
- [ ] Per-GPU preset table (`safe` / `balanced` / `aggressive`) instead of the single
      reference tuning.

---

## License

MIT — see [LICENSE](LICENSE). Provided "as is", without warranty; see the disclaimer
above.

Version **0.1.0** — changes in [CHANGELOG.md](CHANGELOG.md).
