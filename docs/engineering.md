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

### 5-byte boolean pinning

The boolean check is `call parseBool` (5 bytes) followed by `test eax,eax; sete`.
The call is replaced in place with `mov eax, imm32` (also 5 bytes) — no other
instruction is disturbed, and `eax == 0` means `true`. This is used instead of a
detour because the parser's prologue contains RIP-relative code and cannot be
relocated by the simple hook engine.

### Detour engine

`src/hook.cpp` writes a 14-byte absolute jump (`FF 25 00000000 <ptr64>`) because an
`E9 rel32` cannot reach an arbitrary DLL. The stolen prologue is copied into a
trampoline followed by an absolute jump back to `target + patch_len`. Callers must
pass a `patch_len` that covers whole, RIP-relative-free instructions — the
prologues above were chosen for that property and are verified byte-for-byte before
patching.

### Signature scanning and name-based discovery (v0.2.0)

`src/scan.cpp`:

1. **Pattern match.** `FindPatternAll` searches the executable sections for a
   wildcard byte pattern. The parsers share their prologue with look-alike
   functions (11 and 3 candidates respectively), so bytes alone are not enough.
2. **Score by name resolution.** For each candidate, every known attribute name is
   resolved: find the string constant, find every `lea reg,[rip+str]` that loads it,
   and take the first `call <candidate>` after it. The candidate that the names
   resolve through most is the real parser (12/12 vs 0; 16/16 vs 0 here).
3. **Re-derive sites.** A table site is valid iff the bytes before/at it are a
   `call` into the resolved parser. Invalid sites are re-derived by name; a site is
   only accepted if exactly one candidate exists, otherwise it is skipped.

`FindCallAfter` is bounded by the containing section, and every memory read goes
through a range check, so malformed images cannot fault the game.

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
