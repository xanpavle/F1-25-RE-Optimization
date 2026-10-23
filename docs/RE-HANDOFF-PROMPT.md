# Handoff prompt — F1 25 renderer optimization (for an RE agent with MCPs)

Copy everything below the line into the other AI.

---

## ROLE
You are a senior Windows / D3D12 reverse engineer and C tooling developer. Your job is to extend a
shipping performance mod for **F1 25 (Codemasters EGO / NeGfx2 renderer)**. You have MCP tooling
(Ghidra/Ghydra, REA, filesystem, shell, browser). Work autonomously, verify everything against the
binary, and report evidence (addresses, strings, file offsets), never guesses.

## GROUND RULES (non-negotiable)
1. **Offline / single-player only.** The game is protected by EA AntiCheat (kernel). Never run an
   online session, never touch EAC files/processes, never modify `F1_25.exe` on disk. The mod is an
   in-memory code-substitution DLL (`dinput8.dll` proxy) and must stay that way.
2. **No visible quality loss.** The target look is "Ultra High". Do not change `aa_quality`, do not
   change Frame Generation, do not disable mirrors or planar reflections, do not disable ray tracing
   except `rt_ao` + `rt_ddgi` (already approved).
3. **Never guess a hook site.** If a pattern/signature is ambiguous, skip it and log it. Wrong
   patches crash the game.
4. Do not run the game in a way that requires the user to be at the keyboard for long; use
   `-benchmark` CLI mode and be explicit when you need one manual launch.

## TARGET (verify before working)
```
C:\Program Files (x86)\Steam\steamapps\common\F1 25\F1_25.exe
build 1.0.141.7865 (Steam)   size 408,063,872
SHA256 D873A1C8D2044CEF2FD76C99C9D5A97649C5B815D055540995105F46619865AE
```
If the SHA differs, the game was patched: all hard-coded RVAs below are suspect and you must rely on
the pattern-scan path only.

Config the engine writes/reads:
```
%USERPROFILE%\Documents\My Games\F1 25\hardwaresettings\hardware_settings_config.xml
%USERPROFILE%\Documents\My Games\F1 25\hardwaresettings\hardware_settings_info.xml
```
Benchmark outputs: `%USERPROFILE%\Documents\My Games\F1 25\benchmark\`

## EXISTING ARTIFACTS (read these first, do not redo them)
```
C:\Users\Pavle\Documents\Default Project\f125-re\            <- main workspace
  repo\                     shipping mod source (v0.2.8) + README/CHANGELOG
    src\enforce.cpp         hook tables (int/bool call-site RVAs), pattern scan, watcher
    src\profile.cpp         tuned delta kOv + ApplyUpscaler
    src\tune.cpp            f125_tune.ini parsing, GPU->upscaler mapping, enforce_mode
    f125_tune.ini           shipped config
  codemod\                  older standalone copy of the same mod
  decomp\                   5 decompiled functions (mostly unrelated, see notes)
  ghidra_scripts\           DecompBulk.java, ExportNamedMap.java, renames.csv, autofire.ps1
  f125proj.rep\             BROKEN Ghidra project (db.1.gbf = exactly 2.0 GB, .gpr = 0 bytes)
  gpu_trace\                PresentMon.exe + a menu-only capture (NOT race data)
  shaders_extracted\        80 DXBC/DXIL blobs (generic blit/copy only, low value)
  REPORT.md, GPU_RENDERING.md, RUNTIME_CAPTURE.md, renderer_catalog.txt, ego_region_names.txt,
  cvars_renderer.txt (207 Renderer.* cvars), cvars_PerfOSD.txt, cvars_Game.txt, all_strings.txt,
  hardware_settings_config.xml, hardware_settings_info.xml
C:\Users\Pavle\Desktop\F1 RE 3 H\   <- duplicate of the same workspace
```

### Verified code facts (image base 0x140000000; "VA" = absolute, "RVA" = VA-0x140000000)
```
VA 0x14534dcd8  hardwareSettings parse-int  attribute   (RVA 0x534dcd8)
VA 0x14534d918  hardwareSettings parse-bool attribute   (RVA 0x534d918)
VA 0x1440217f0  hardwareSettings::Options::apply
VA 0x1440226c0  hardwareSettings::Options::applyNode
VA 0x143a43b90  bool  getter
VA 0x143a43240  scalar getter
VA 0x146044710  hardware_settings_config root slot    (RVA 0x6044710) <- live preset tree
VA 0x14604e628  hardware_settings_info document       (RVA 0x604e628) - hardware detection only
VA 0x14604e5f8  hardware_settings_options document    (RVA 0x604e5f8)
VA 0x146cb7a78  hardware_settings_ratings document    (RVA 0x6cb7a78)
```
`Options::apply` is called as `apply(optionsDb, settingsDoc, "graphicsDetail"|"safe_mode",
presetName)` and reads the tree it applies from `[settingsDoc + 0x10]`. The settings document is
`0x146044700`; its root pointer is stored at `+0x10`, i.e. the global slot `0x146044710`.
Callers use `sete` on the bool parser result, therefore the mod rewrites the 5-byte
`call strcmp` with `mov eax,imm32` (eax=0 means "true") to force booleans.
12 live int call-sites and 17 bool call-sites (16 unique attributes) are already mapped in
`repo/src/enforce.cpp` — reuse them; do not re-derive from scratch.

Discovery strings differ from display names: `shadowSampling`->`"sampling"`,
`textureStreamingMiB`->`"sizeInMiB"`, `groundCover.enabled`->`"enabled"`,
`groundCover.meshes`->`"meshes"`.

### Verified engine behaviour (already proven by A/B)
- The engine *does* honour `<antialiasing>` and `<rt_* enabled>` from the config on launch.
- `fsr3` is **numeric** (`"0"/"1"`); `taa`/`dlss`/`xess` are `"true"/"false"`. `cas="1"` must be on
  for FSR 3 to engage. Writing `fsr3="true"` silently falls back to TAA (this was a real bug).
- `Renderer.maxStaticInstances` / `Renderer.maxPVSRenderInstances` are hard caps on submitted world
  instances. Lowering them to 20000 (engine defaults ≈24500/24800) dropped Imola's barrier / TecPro /
  guardrail meshes at that track (v0.2.8 removed the overrides). Do not lower them below the engine
  defaults in any profile.
- Presets are **not** static: `hardware_settings_options.xml` contains per-vendor blocks
  (`defaultForAMD`, `defaultForNvidia`, `defaultForNGX`), conditions (`lowEndCPU`,
  `featureConservativeRasterization`, `featureRasterizeOrderViews`) and a rules language
  (`hardware_settings_restrictions`: `restrict_set|int_range|float_range|bool|info|add|logical|if|else`,
  comparators `int_eq|int_ge|int_le|bo_eq|str_str|str_cmp`).
  Related files also exist: `hardware_settings_options_vr.xml`, `hardware_settings_ratings.xml`.
  These are packed inside `game.dat` (not readable statically) and only exist decrypted in memory.
- Engine logging strings you can target:
  `[hardwareSettings::Options::apply] Applying reference '%s:%s'`,
  `[hardwareSettings::Options::applyNode] 1 Setting attribute '%s:%s=%s'`.
- The engine has a **built-in profiler**: `Renderer.gpuprofiling`, `PerfOSD.*`
  (Tree.Up/Down/Expand/Collapse, CycleOSDLeft/Right, ToggleCapture, TogglePlayback, playback,
  JobQueue.CycleDisplayMode) and `Game.AutoProfile.enabled|framesToCapture|framesToWait`.
  No bindings found in `actionmaps\` — find out how they are bound/enabled.
- Relevant un-used cvars: `Renderer.Gfx.enableAsyncCompute`, `enableSER`, `enableVRS`,
  `enableDynamicResAutoScaling`, `resolutionMultiplier`, `fpsTarget`, `stateLockFrameRate`,
  `Renderer.Gfx.SSAO.assao.*` (qualityLevel, blurPassCount, adaptiveQualityLimit, horizonAngleThreshold),
  `Renderer.Gfx.SSAO.Hemi.*`, `Renderer.TextureStreaming.*` (textureCullScale, streamingCacheSize,
  enableHiRes, meshDensityThreshold), `Renderer.Gfx.groundCover2*`.
- NVIDIA stack present on disk: `sl.interposer.dll`, `sl.dlss.dll`, `sl.dlss_d.dll` (Ray
  Reconstruction), `sl.dlss_g.dll` (FG), `sl.nrd.dll`, `sl.reflex.dll`. Config key
  `rt_ray_reconstruction` exists. `ser` exists.

## KNOWN BAD STATE / FIX FIRST
1. The Ghidra project is unusable. `f125proj.gpr` is 0 bytes and the database hit exactly 2 GB.
   Recreate the project file (a Java `.properties` file containing `OWNER=Pavle`) or, more safely,
   **import fresh into a new project with a larger heap**:
   ```
   set MAXMEM=8G
   "C:\Users\Pavle\re-mcp\tools\ghidra\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat" ^
     "C:\Users\Pavle\Documents\Default Project\f125-re" f125proj2 ^
     -import "C:\Program Files (x86)\Steam\steamapps\common\F1 25\F1_25.exe" -noanalysis
   ```
   The previous failure was a batch-file bug: the exe path with spaces was not quoted inside the
   .bat (`\Steam\steamapps\common\F1 was unexpected at this time`). Quote every path.
2. `RUNTIME_CAPTURE.md` claims PresentMon was validated — but the CSV is menus (0.89 ms avg). Redo it
   during a real benchmark.
3. `raw.bin` / `raw.log` (UDP capture) are 0 bytes. No telemetry was ever captured.
4. `shaders_disasm\` is empty. `dxil.dll` already ships in the game folder, so DXIL disassembly is
   possible if it ever matters (it probably does not).

## INSTALL WHAT YOU NEED
Install and log whatever you use (versions + why): Python 3 with `pip install pefile capstone
pyelftools` (and `pyghidra` if you prefer scripting Ghidra), PresentMon (already vendored in
`gpu_trace\`), optionally LLVM/dxc for DXIL. Do **not** install kernel drivers, do not install
RenderDoc-for-injection against the protected client.

## MISSIONS (in priority order)

### M1 — Capture the real preset table (highest value)
Goal: know exactly what "Ultra High" (and every other preset) resolves to on this machine, without
cracking `game.dat`.
State: implemented in v0.2.7. The shipped mod hooks `Options::apply`/`applyNode`, resolves the live
config tree (root slot `0x146044710`, see the address facts above) and writes
`f125_preset_<preset>.txt`, one `category:attribute=value` line per attribute. The dump is bounded
(8192 nodes / 50000 attributes, visited set) and ends with `# attributes=N`, `# nodes=N`,
`# truncated=yes|no`; every candidate document is logged once as
`preset dump: root candidate <name> <ptr> valid=.. preset=..`. Remaining work:
- verify the dump against the full known attribute set and fix any missing categories;
- switch through Low/Medium/High/Ultra/Ultra High in the graphics menu (or reload configs) and
  collect one dump per preset;
- diff the presets and produce a machine-readable `presets.json`.
Acceptance: dumps contain the full known attribute set (>= 80 attributes), are reproducible, and
include `defaultForAMD`-specific values where they differ.

### M2 — Per-pass timing (find the real bottleneck)
Try, in this order, and document which works:
1. `Renderer.gpuprofiling=true` + `PerfOSD.*` bindings. Find where PerfOSD key bindings live
   (action maps, dev config, `-test`/`-dev` switches, or a cvars file) and how to enable the OSD.
2. `Game.AutoProfile.enabled=true` with `framesToCapture/framesToWait`; find where it writes output.
3. PresentMon race capture (ETW, anti-cheat safe) around one `-benchmark` run:
   ```
   gpu_trace\PresentMon.exe --timed 120 --terminate_after_timed --process_name F1_25.exe --output_file race.csv
   ```
   Report per-frame `MsBetweenPresents` / `MsGPUTime` percentiles (p50/p95/p99) and GPU busy %.
Acceptance: a table of where frame time goes (per pass if possible, else GPU vs CPU-bound split)
with the exact commands used.

### M3 — Cheap, invisible quality levers
Only after M1/M2 give data. Candidates (validate each A/B, keep only net wins):
- SSAO: `assao.qualityLevel`, `blurPassCount`, `adaptiveQualityLimit` (ASSAO is already the selected
  algorithm; algorithm="1"). Target: lower cost at identical visual result.
- Texture streaming: `textureCullScale`, `streamingCacheSize` — target 1% lows / stutter, not avg.
- `enableAsyncCompute` (verify it is actually benefiting AMD), `vendor_shader_pack`.
- `groundCover2MaxInstances` / `groundCover2MaxMeshInstances` if the config uses ground cover 2.
- NVIDIA-only branch: `rt_ray_reconstruction`, `ser`, `Renderer.RayTracing.enableRelaxDenoiserOption`.
  Must be gated on vendor, and must not fire on AMD/Intel.
Do NOT touch: `aa_quality`, `frame_gen`, `multi_frame_gen`, mirrors, planar reflections,
`rt_shadows`, `rt_reflections`, `rt_transparent_reflections`.

### M4 — Reproducible A/B + quality gate
Build a `bench.ps1` that, for each variant:
1. restores a known config, 2) runs `F1_25.exe -benchmark <file>.xml`, 3) collects the benchmark
CSV + optional PresentMon CSV, 4) writes avg/p50/p1% and a per-variant JSON.
Add a **quality gate**: capture 3 fixed frames per variant and compare SSIM/PSNR against the stock
Ultra reference; fail the variant if SSIM < 0.99. This turns "no visible quality loss" into a test.

## DELIVERABLES (write into `docs/` and print a summary)
1. `docs/FINDINGS-v0.3.md` — every new fact with file offset / VA / string evidence.
2. `presets.json` + one `f125_preset_*.txt` per preset.
3. `docs/PROFILE.md` — whichever profiler worked, exact enable steps, and the timing table.
4. A patch (or a precise spec) for the mod: new ini keys, new `kOv` entries, hook changes.
5. `docs/bench.ps1` + `docs/quality_gate.ps1` and their outputs.
6. Any dead end, stated plainly, with why.

## DO NOT
- Do not write to the game install directory except the mod's `dinput8.dll` / `f125_tune.ini`.
- Do not disable anti-cheat, patch EAC, or launch the game without EAC.
- Do not change values that the user has already excluded (see ground rules).
- Do not claim a speed-up without a stock-vs-modded A/B on the same run (same track/weather/cars).
- Do not "fix" the existing 12 int / 17 bool call-site table unless a benchmark proves it wrong.

## FIRST REPLY YOU SHOULD GIVE
State: target SHA verified or not, which mission you start with, what you installed, and the single
command you will run first.
