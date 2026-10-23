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
