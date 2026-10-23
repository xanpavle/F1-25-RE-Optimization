# Engineering notes

How the mod was derived and how the moving parts fit together. This is the
reference for anyone re-deriving the addresses after a large game update.

## Target

| | |
|---|---|
| Binary | `F1_25.exe` (EGO engine, Codemasters / EA) |
| Image base | `0x140000000` — **not** ASLR (`DllCharacteristics = 0x8120`, no `DYNAMIC_BASE`) |
| Reference exe | 408,063,872 bytes |
| Tooling | Ghidra (static), a standalone PE/byte scanner (`src/scan.cpp`) |

Because the image has no `DYNAMIC_BASE` the RVAs are stable for a given build, but
they move on every patch — hence the runtime re-derivation.

## Address map (reference build)

RVA = VA − `0x140000000`.

### Functions

| Symbol | RVA | Notes |
|---|---|---|
| int attribute parser `FUN_14534dcd8` | `0x534dcd8` | `(const char* value, int def, int flags) -> int` |
| bool attribute check `FUN_14534d918` | `0x534d918` | returns `0` for `"true"`; callers do `test eax,eax; sete` |
| float attribute parser thunk `FUN_145343ff4` | `0x5343ff4` | 2-arg thunk (`xor r8d,r8d; jmp`) used by every float call site |
| float attribute parser core `FUN_145343ffc` | `0x5343ffc` | `_strtof_s` shape: `(float* out, const char* value, char** end) -> errno_t` |
| bool attribute getter `FUN_143a43b90` | `0x3a43b90` | `(node, name, uint8_t* out, uint8_t def)` |
| 32-bit attribute getter `FUN_143a43240` | `0x3a43240` | `(node, name, uint32_t* out, uint32_t def)` |
| `hardwareSettings::Options::apply` | `0x40217f0` | prologue `… 48 81 EC F8 02 00 00` |
| `hardwareSettings::Options::applyNode` | `0x40226c0` | prologue `… 48 81 EC 98 00 00 00` |

`apply` is called as
`apply(optionsDb, settingsDoc, "graphicsDetail"|"safe_mode", presetName)`: the
second argument is the settings document and the function reads the tree it
applies from `[settingsDoc + 0x10]`. The other three documents are separate
during initialisation (see below).

### Hardware-settings documents (settings roots)

`Application::initialiseHardwareSettings` creates several hardware-settings
documents and keeps each behind a global pointer slot. Only the **config**
document is the live preset tree; the others hold hardware detection, the
options database and the ratings.

| Document | Global (VA) | RVA | Contents |
|---|---|---|---|
| `hardware_settings_config` | `0x146044710` | `0x6044710` | **live preset tree** — category nodes (`shadows`, `weather_effects`, `antialiasing`, `ground_cover`, …) with their attributes |
| `hardware_settings_info` | `0x14604e628` | `0x604e628` | detected hardware (`graphics_card` / `systemInfo` / `resolution`) |
| `hardware_settings_options` | `0x14604e5f8` | `0x604e5f8` | options database (`defaultForAMD` / `defaultForNvidia` blocks, restriction rules) |
| `hardware_settings_ratings` | `0x146cb7a78` | `0x6cb7a78` | per-preset ratings document |

The config document itself is `0x146044700`; its root-node pointer is stored at
`+0x10`, i.e. the global slot `0x146044710` (`RVA 0x6044710`).

### Enforced integer sites (return address of `call parseInt`)

| Attribute | return RVA | value |
|---|---|---|
| `worldShadowMapSize` | `0x3dc3c7b` | 2048 |
| `nightShadowMapSize` | `0x1271345b` | 2048 |
| `skyShadowMapSize` | `0x3dc3abb` | 2048 |
| `spotShadowMapSize` | `0x3dc3ceb` | 2048 |
| `skyShadowCascadeCount` | `0x3dc3b9b` | 4 |
| `nightShadowSourceCount` | `0x127134db` | 4 |
| `spotShadowSampling` | `0x3dc3d5b` | 2 |
| `sampling` (`shadowSampling`) | `0x127d0abd` | 2 |
| `volumetricFogMapWidth` | `0x3ca508d` | 512 |
| `volumetricFogMapHeight` | `0x3ca519f` | 288 |
| `billboardDistance` | `0x3c6013b` | 2500 |
| `sizeInMiB` (`textureStreamingMiB`) | `0x71f855b` | 2048 |

### Enforced float sites (return address of `call <float thunk>`)

| Attribute | return RVA | value |
|---|---|---|
| `groundCover.drawDistance` | `0x3ce448b` | 80.0 |
| `proceduralCloudRTScale` | `0x15b2ef` | 2.0 |

### Pinned boolean sites (call address of `call parseBool`)

| Attribute | call RVA | value |
|---|---|---|
| `skyShadowLowLod` | `0x3dc3c08` | false |
| `skyShadowMap32Bits` | `0x3dc3b28` | false |
| `crowdInShadows` | `0x874aa08` | false |
| `godRays` | `0x2c8d638`, `0x2f811a8` | false |
| `lensStreak` | `0x2f81288` | false |
| `motionBlurV2` | `0x2f813d8` | false |
| `nightShadowCharacterCast` | `0x728dd18` | true |
| `rainSplashes` | `0x15abec` | true |
| `rainBeads` | `0x15aadc` | true |
| `rainHigh` | `0x15acdc` | true |
| `trackHeatHaze` | `0x15adcc` | true |
| `rainSheets` | `0x1275b6ab` | true |
| `enabled` (`groundCover.enabled`) | `0x3ce4288` | true |
| `grassFlattening` | `0x3ce4308` | true |
| `meshes` (`groundCover.meshes`) | `0x3ce438c` | true |
| `volumetricFog` | `0x3ca4f38` | true |

## Techniques

### Value substitution at the return address

Each restricted setting is parsed by calling the shared integer parser from one
call site. The mod detours the parser and compares `__builtin_return_address(0)`
against the table above, returning the tuned value for the sites it owns. The
original parser runs first, so every engine side effect is preserved; only the
returned value changes. Nothing is written to engine memory, so an unknown site
falls through harmlessly.

### Float attributes

Floats use the same idea with a different ABI. Every float call site targets the
thunk `FUN_145343ff4` (`xor r8d,r8d; jmp FUN_145343ffc`), and the core parser writes
its result through `rcx`:

```
lea  rcx,[rsp+out]        ; float* out
mov  rdx,[attr+0x50]      ; value string
call FUN_145343ff4
vmovss xmm0,[rsp+out]
```

The thunk is a branch and cannot be relocated, so the detour is installed on the
core entry (`FUN_145343ffc`, a 16-byte clean prologue) while call sites are
validated against the thunk. The detour calls the original first, then overwrites
`*out` only when the return address matches `0x3ce448b` or `0x15b2ef`. Lower is
cheaper for both attributes, so `cheaper` mode keeps `min(preset, tuned)`. The
signature starts at the thunk so the scanner can derive both addresses
(`core = thunk + 8 + rel32`).

### 5-byte boolean pinning

The boolean check is `call parseBool` (5 bytes) followed by `test eax,eax; sete`.
The call is replaced in place with `mov eax, imm32` (also 5 bytes) — no other
instruction is disturbed, and `eax == 0` means `true`. This is used instead of a
detour because the parser's prologue contains RIP-relative code and cannot be
relocated by the simple hook engine.

### Upscaler selection: the value type matters

`<antialiasing>` mixes types. `taa`, `dlss` and `xess` are booleans, but the engine
stores FSR 3 as a **number** (`fsr3="0"/"1"`). Writing `fsr3="true"` makes the engine
reject the whole element and fall back to TAA, so the auto-selected backend silently
never engaged. The profile writes `fsr3="1"` and also sets `cas="1"`, which the engine
checks before it will use FSR 3.

### Enforcement modes

`enforce_mode = target` substitutes the tuned integer/float unconditionally.
`enforce_mode = cheaper` uses it as a ceiling (`min(preset, tuned)`), so it only ever
removes work. Boolean pinning is independent of the mode: the table above already
encodes the intended look (rain / grass / fog on; god rays / lens streak / motion blur
off), so no mode silently drops a visible effect to a lower preset.

### Detour engine

`src/hook.cpp` writes a 14-byte absolute jump (`FF 25 00000000 <ptr64>`) because an
`E9 rel32` cannot reach an arbitrary DLL. The stolen prologue is copied into a
trampoline followed by an absolute jump back to `target + patch_len`. Callers must
pass a `patch_len` that covers whole, RIP-relative-free instructions — the
prologues above were chosen for that property and are verified against a byte pattern
(with the compiler's stack-frame immediate wildcarded) before patching.

### Signature scanning and name-based discovery (v0.2.0)

`src/scan.cpp`:

1. **Pattern match.** `FindPatternAll` searches the executable sections for a
   wildcard byte pattern. Prologue checks and signatures wildcard the stack-frame
   immediate, so a compiler change that only resizes the frame does not break a hook.
   The parsers share their prologue with look-alike functions (21 and 3 candidates
   respectively), so bytes alone are not enough.
2. **Score by name resolution.** For each candidate, every known attribute name is
   resolved: find the string constant, find every `lea reg,[rip+str]` that loads it,
   and take the first `call <candidate>` after it. The candidate that the names
   resolve through most is the real parser (12/12 vs 0; 17/17 vs 0 here). String VAs
   are resolved once and reused across candidates; without that cache the fallback
   scanned the 408 MB image 252 times (~36 s); with it the scan is ~7 s offline and
   ~4 s live.
3. **Re-derive sites.** A table site is valid iff the bytes before/at it are a
   `call` into the resolved parser. Invalid sites are re-derived by name; a site is
   only accepted if exactly one candidate exists, otherwise it is skipped.

`FindCallAfter` is bounded by the containing section, and section sizes are clamped
to `SizeOfImage`. A live image maps sections without `IMAGE_SCN_MEM_READ` as
`PAGE_NOACCESS` (F1_25 has `.xpdata` and `.link`), so those are skipped; an offline
file buffer would hide that difference.

The known RVAs remain the fast path (0.6 s). Create `f125_force_scan.txt` next to
`F1_25.exe` (or set `F125_FORCE_SCAN=1`) to ignore them and exercise the full
self-healing path (~4 s); the log then records every re-derivation.

## Reproducing a re-derivation

```
build\scantest.exe "C:\path\to\F1_25.exe"
```

loads the image from disk and prints, for every parser/site, whether it was found by
signature and by name. It is part of `test.bat` and needs no game launch.

## Settings file: an export, not an input

`hardware_settings_config.xml` is written *by* the engine from its preset and read
back by it; writing tuned values into it does not survive the preset clamp. This was
proven by experiment (write `worldShadowMapSize="4096"`, fog `1920x1080`,
`billboardDistance="4000"`; the engine reads back `1024`, `256x128`, `1500` and
rewrites the file), so the in-engine substitution is the only lever for
preset-restricted attributes. The file delta still covers the attributes the preset
does not manage (SSAO/SSR quality, cloud RT scale, grass distance, reflection update
mode, …).

The file is the exported view of the live `hardware_settings_config` document
(root slot `0x146044710`). `hardwareSettings::Options::apply` applies a preset to
that document (see the call convention above) and `::applyNode` applies one node;
the mod detours both with stack-preserving thunks and restores the tuned delta to
the file afterwards. The same post-hook is where a preset capture dumps the
applied tree — see below.

### World-instance caps and Imola (v0.2.8)

`[override]` used to lower `Renderer.maxStaticInstances` /
`Renderer.maxPVSRenderInstances` to `20000` (engine defaults ≈ `24500` / `24800`).
Those are culling caps on the number of submitted world instances: Imola packs the
most static barrier / TecPro / guardrail instances of any track, so once a view
passed 20000 the engine simply stopped submitting the overflow and barrier meshes
flickered or disappeared. Other tracks stayed under the cap, which is why the
symptom was Imola-only and why removing the overrides costs effectively nothing
elsewhere. The lowered values are gone from the shipped ini; the mechanism itself
(and the optional commented-out lines) remains for advanced users who do not race
Imola. `cs_culling` is unrelated: it is still written `true` as before.

## Preset capture (v0.2.7)

A capture writes one file, `f125_preset_<preset>.txt`, next to `F1_25.exe`: one
`category:attribute=value` entry per line (real newlines). The tree is captured
once per launch after the settings have loaded, and from the post-apply hook when
that hook observes an apply. The preset name comes from a `category` attribute
found anywhere in the tree; the config tree does not carry one, so a launch
capture is written as `f125_preset_unknown.txt`.

### Root resolution

`src/enforce.cpp` resolves the live root from the four documents above. It prefers
the config document and only accepts it when the tree carries at least one of the
child categories `shadows`, `weather_effects`, `antialiasing` or `ground_cover`.
While the config tree is still being initialised it falls back to the first
shape-valid document. Every capture logs each candidate once, so the source of a
dump is visible in the log:

```
preset dump: root candidate config   <ptr> valid=yes preset=yes
preset dump: root candidate info     <ptr> valid=yes preset=no
preset dump: root candidate options  <ptr> valid=yes preset=no
preset dump: root candidate ratings  <ptr> valid=yes preset=no
```

A previous revision read only the info document at `0x14604e628`, so its dumps
contained `graphics_card` / `systemInfo` / `resolution` but no preset values.

### When a capture runs

The launch capture is **event-driven**: it waits until the integer/float parser
detours have seen real parser activity instead of sleeping for a fixed startup
delay, then retries a bounded number of times before giving up. A failed root is
logged, not guessed. When the `apply` / `applyNode` detours are installed early
enough to observe an apply, the same post-hook also captures the tree it was
handed; in practice the launch capture is the path that fires, because
`Application::initialiseHardwareSettings` can run before the detour scan finishes.

### Bounded walk and footer

The walk uses the engine node layout of the tree handed to `apply` / `applyNode`:
node name at `+0x18`, attribute pointer array at `+0x50`/`+0x58`, child-node
array at `+0x70`/`+0x78`; an attribute has its name at `+0x18` and value string
at `+0x50`.
It is bounded so a corrupt or unexpectedly large tree cannot produce a giant file
or an endless walk:

- strings are formatted with `%.128s` (node and attribute names) and `%.256s`
  (values), so an unterminated string cannot run away;
- at most **8192 nodes** and **50000 attributes** are written;
- a visited set stops cycles / repeated nodes, and unreadable pointers are
  treated as truncation;
- the file always ends with its counters:
  `# attributes=N`, `# nodes=N`, `# truncated=yes|no`.

`truncated=yes` means at least one bound (or an unreadable pointer) was hit, so a
partial dump is explicit rather than looking complete.
