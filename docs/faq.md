# FAQ

Short answers first, details below.

| Question | Answer |
|---|---|
| Is it safe? | It is a normal Win32 DLL that rewrites one settings file. Source is public. |
| Can I play online with it? | **No.** Offline / single-player only. |
| Does it work on my GPU? | Yes, on any DX12 GPU — it picks DLSS / FSR 3 / XeSS / TAA for you. |
| Do I lose visual quality? | The expensive-but-soft parts are reduced (fog resolution, shadow map size, god rays). Preset and resolution stay yours. |
| Why 140 FPS and not 300? | The on-screen number includes Frame Generation; the real render gain is ~30–60%. |
| Does it survive a game update? | Not automatically yet — the hooks fail safe and need new addresses. |
| How do I know it worked? | `f125_tune.log` says `hooks installed OK` and `profile applied`. |
| How do I remove it? | Run `Uninstall-F125-Tune.bat`, or `Restore-F125-Tune.bat` to also reset settings. |

---

## Is this safe? Is it malware?

The source is in this repository and the shipped DLL is built from it. The mod:

- loads as a proxy `dinput8.dll` (the game imports one function from it),
- rewrites a fixed list of attributes in **your own** `hardware_settings_config.xml`,
- hooks two engine functions in memory to answer a few settings reads differently,
- writes a log file next to the game.

It does not touch `F1_25.exe` on disk, does not download anything, does not
phone home, and does not include any game files. If you do not trust the binary,
build it yourself (`build.bat`) and compare hashes.

## Can I play online with it?

**No.** Use it offline / single-player only, and remove it before going online.
It is not an anti-cheat bypass and does not try to hide from one. F1 25 uses EA
anti-cheat for online play; a runtime DLL is exactly the kind of thing that can
get an account banned. This project does not support or condone that use.

## Does it work in VR?

Probably, but it is **not officially tested yet**.

F1 25 renders VR through the same PC renderer, and this mod changes PC-side
renderer settings before the engine starts, so the changes should carry over to
a PCVR session (Quest Link / Air Link / SteamVR / OpenXR). Two caveats:

- VR has its own performance budget; the tuned values were chosen for a flat
  1440p display.
- The mod cannot help with encode / streaming latency or reprojection judder.
  It only reduces GPU render cost.

If you test it, post the result with your headset and GPU — that is how the
compatibility table gets better.

## Steam Deck / Linux?

Windows only. It is a Win32 DLL, so it will only load in a Windows process.
Under Proton the game runs as a Windows process, so the DLL *may* load, but this
is untested and the tuned values assume a desktop GPU. Not recommended on Deck
without testing.

## Will it work on my GPU?

Yes. There is nothing vendor-specific in the renderer delta, and the upscaler is
chosen automatically:

| GPU | Upscaler chosen by `mode = auto` | Notes |
|---|---|---|
| NVIDIA RTX 20 / 30 / 40 / 50 | **DLSS** | RTX 20-series and newer have DLSS hardware |
| NVIDIA GTX 10 / 16 | **FSR 3** | no DLSS hardware; FSR 3 runs on anything |
| AMD Radeon (RX 4000–9000) | **FSR 3** | native path |
| Intel Arc A / B | **XeSS** | best on Arc |
| Intel iGPU | **FSR 3** | XeSS would be too heavy |
| Unknown / VM | unchanged | the mod leaves your in-game choice alone |

The **quality level** (Quality / Balanced / Performance) is never changed — it
stays whatever you picked in-game. On weaker GPUs, lower it yourself; on
stronger GPUs, raise it.

**Frame Generation is also left untouched.** It is a separate setting from
upscaling. On NVIDIA, if auto mode picks DLSS, set Frame Generation to **DLSS**
in-game once — mixing DLSS upscaling with FSR frame generation can make the game
fall back or disable it.

You can force a backend in `f125_tune.ini`:

```ini
[upscaler]
mode = auto   ; or dlss | fsr3 | xess | taa | off | keep
```

Run `build\gputest.exe` to see exactly what the mod detects on your machine.

## Why do I get ~140 FPS and not 300?

Two things:

1. **The displayed number includes Frame Generation**, which roughly doubles the
   on-screen figure. The underlying render gain from this mod is about **+30–60%**.
2. **The rest of the frame is not free.** In a race with 20 cars, the CPU, the
   simulation and the 165 Hz cap all matter. If you are CPU-bound, no renderer
   tweak will push you past that.

The Ultra Max / path-tracing preset is a separate story: it is officially rated
for an RTX 30-series or RX 7900 and above, so on an RX 7800 XT it is
path-tracing-bound and this mod cannot meaningfully help it.

## Will I notice the quality loss?

The delta targets things that are expensive but visually soft:

- volumetric fog map resolution (1920×1080 → 512×288),
- shadow map size (4096 → 2048) and sampling,
- SSR / SSAO quality steps,
- god rays, lens streaks, motion blur,
- grass draw distance, crowd billboards, particle and hair detail,
- car-reflection cube-map update rate.

Your resolution, preset, mirrors, ray tracing and rain are left alone. If you
want the old look back, set `[profile] apply = 0` or run
`Restore-F125-Tune.bat`.

## The game updated — does it still work?

The two in-memory hooks are tied to the exact `F1_25.exe` build they were
derived from. After a game update:

- the mod verifies both function prologues **byte-for-byte** before patching;
- on a mismatch it logs `signature mismatch`, patches nothing, and the game runs
  normally;
- the settings-file delta still applies (it uses attribute names, not addresses).

Pattern-scanning the prologues is on the roadmap, which would make one DLL
survive updates.

## Did it actually work?

Open `f125_tune.log` in the game folder. A healthy launch looks like:

```
[mod] F1 25 RE-Optimization v0.1.0
[mod] profile: saved stock backup hardware_settings_config.xml.untuned   (first run only)
[mod] GPU: AMD Radeon RX 7800 XT (AMD 7000-series)
[mod] upscaler: mode=auto -> FSR 3
[mod] profile applied (tuned delta on hardware_settings_config.xml)
[mod] hooks installed OK
```

Anything else — see [troubleshooting](troubleshooting.md).

## I opened the graphics menu and the mod stopped working

Applying a preset in the graphics menu makes the engine rewrite its canonical
values, which wipes the file-based delta **for that session**. The mod does not
fight it; it re-applies on the next launch. The engine-level `[override]` values
(VRS, instance caps, and ray-tracing toggles if enabled) are immune because they
are hooked in memory.

A permanent fix (hooking the in-memory `apply` function) is on the roadmap.

## Can I use it with other mods?

Usually yes, with two exceptions:

- **Another `dinput8.dll` proxy** (Reshade, Special K, some input mods). Only one
  file can own that name. The installer backs up an existing DLL as
  `dinput8.dll.bak-f125`; chain them manually if you need both.
- **Another tool that rewrites `hardware_settings_config.xml`.** The last writer
  wins.

## Does it need an internet connection?

No. The mod is fully offline and has no network code.

## How do I uninstall it?

- `Uninstall-F125-Tune.bat` — removes the mod, keeps your settings.
- `Restore-F125-Tune.bat` — removes the mod and restores the stock
  `hardware_settings_config.xml` from the `.untuned` backup.

Both auto-detect the game folder; pass it as an argument if detection fails.

## Can I share my benchmark?

Yes — open an issue with your GPU, CPU, resolution, preset, and before/after
numbers. Single-machine results are the main weakness of this project, and
community data fixes that faster than anything else.
