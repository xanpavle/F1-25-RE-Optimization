#include "enforce.h"
#include "hook.h"
#include "scan.h"
#include "tune.h"
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace enforce {
void DumpPreset(void* root);
namespace {

// --- F1_25.exe (image base 0x140000000, no ASLR) -----------------------------
// FUN_14534dcd8: int attribute-value parser (valueString, default, flags).
static const uintptr_t kRvaParseInt  = 0x534dcd8u;
// FUN_14534d918: bool attribute check; returns 0 when the string is "true"
// (the callers turn that into a bool with `test eax,eax; sete`).
static const uintptr_t kRvaParseBool = 0x534d918u;

// FUN_145343ff4: two-argument thunk (`xor r8d,r8d; jmp`) into the real float
// parser at FUN_145343ffc, which has the MSVC `_strtof_s` shape:
//     int f(float* out, const char* value, char** end)
// Every float attribute site does `lea rcx,[out]; mov rdx,valueString; call
// thunk`, so call sites are validated against the *thunk* while the detour is
// installed on the core entry (the thunk is a branch and cannot be relocated).
static const uintptr_t kRvaParseFloatThunk = 0x5343ff4u;
static const uintptr_t kRvaParseFloat      = 0x5343ffcu;

// hardwareSettings::Options::apply / ::applyNode (verified via xrefs on the
// "[hardwareSettings::Options::apply]" / "[...applyNode]" log strings).
static const uintptr_t kRvaApply     = 0x40217f0u;
static const uintptr_t kRvaApplyNode = 0x40226c0u;

// The live preset tree is the *settings* document, not the hardware-info one.
//
// `hardwareSettings::Options::apply` is called as
//     apply(optionsDb, settingsDoc, "graphicsDetail"|"safe_mode", presetName)
// and reads the tree it writes into from `[settingsDoc + 0x10]`
// (0x144021d71: mov rax,[rbp+0x10], rbp = 2nd argument). The caller passes
// settingsDoc = 0x146044700, so the live root is the pointer stored at
// 0x146044710 (RVA 0x6044710), created in
// Application::initialiseHardwareSettings.
//
// 0x14604e628, which an earlier revision used, is a *different* document: the
// detected `hardware_settings_info` tree (graphics_card/systemInfo/...). That is
// why the previous dumps contained no preset values.
//
// Every hardware-settings document created during initialisation is listed
// below. Only the config tree is the preset tree; the others are reported when
// a capture is requested so the dump source can never be ambiguous.
struct RootCandidate {
    uintptr_t   rva;
    const char* what;
};
static const RootCandidate kRootCandidates[] = {
    {0x6044710u, "config"},    // hardware_settings_config - live preset tree
    {0x604e628u, "info"},      // hardware_settings_info - detected hardware
    {0x604e5f8u, "options"},   // hardware_settings_options database
    {0x6cb7a78u, "ratings"},   // hardware_settings_ratings
};
const int kRootCandidateCount =
    (int)(sizeof(kRootCandidates) / sizeof(kRootCandidates[0]));

// Prologue shapes, matched as patterns rather than raw bytes. The trailing
// stack-frame immediate is wildcarded: its value changes between game builds and
// the instruction length is unaffected, so the relocated bytes stay valid.
//
// Every prologue here is a sequence of whole instructions with no RIP-relative
// operand and no branch, which is what makes copying it into a trampoline safe.
static const char* kPatParseCheck =
    "48 89 5C 24 08 48 89 7C 24 10 55 48 8B EC 48 83 EC ??";  // 18 bytes
static const char* kPatParsePatch =
    "48 89 5C 24 08 48 89 7C 24 10 55 48 8B EC";              // 14 bytes
static const int   kLenParsePatch = 14;  // stops before `sub rsp, imm`
static const char* kPatApply =
    "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC ?? ?? ?? ??";  // 19 bytes
static const int   kLenApply = 19;
static const char* kPatApplyNode = kPatApply;  // same prologue shape
static const int   kLenApplyNode = 19;
static const char* kPatParseBool =
    "48 83 EC 28 83 3D ?? ?? ?? ?? 00 75 2D 48 85 C9";  // 16 bytes
static const char* kPatParseFloatThunk =
    "45 33 C0 E9 ?? ?? ?? ?? 48 89 5C 24 08 57";        // 13 bytes
static const char* kPatParseFloat =
    "48 89 5C 24 08 57 48 83 EC ?? 48 8B FA 48 8B D9";  // 16 bytes
static const int   kLenParseFloat = 16;

// Signatures used to re-derive the addresses on an updated game build. See
// src/scan.cpp. Frame sizes are wildcarded so a compiler change does not defeat
// the scan. The parsers share their prologue with other functions, so the
// scanner disambiguates them by name resolution rather than by bytes alone.
static const char* kSigParseInt =
    "48 89 5C 24 08 48 89 7C 24 10 55 48 8B EC 48 83 EC ?? 48 83 65 C0 00 "
    "83 3D ?? ?? ?? ?? 00 C6 45 D0 00 C6 45 E8 00 C6 45 F0 00 C6 45 F8 00 75 10 "
    "0F 10 05 ?? ?? ?? ?? C6 45 E8 01";
static const char* kSigParseBool =
    "48 83 EC 28 83 3D ?? ?? ?? ?? 00 75 2D 48 85 C9 75 1A E8 ?? ?? ?? ?? "
    "C7 00 16 00 00 00 E8 ?? ?? ?? ?? B8 FF FF FF 7F 48 83 C4 28 C3";
// Starts at the thunk, so the scanner can derive both the call-site target
// (the thunk) and the hook target (thunk + 8 + rel32 jmp).
static const char* kSigParseFloat =
    "45 33 C0 E9 ?? ?? ?? ?? 48 89 5C 24 08 57 48 83 EC ?? 48 8B FA 48 8B D9 "
    "48 85 C9";
static const char* kSigApply =
    "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC ?? ?? ?? ?? 4C 89 CE 4D 89 C4 "
    "48 89 D5 48 89 4C 24 78 80 3D ?? ?? ?? ?? 00 75 6A";
static const char* kSigApplyNode =
    "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC ?? ?? ?? ?? 48 89 D6 49 89 CE "
    "4C 8B 62 18 48 8B 59 78 48 2B 59 70 74 46";

static uintptr_t g_base = 0x140000000u;

// (parser return-address RVA -> tuned value). RVA = VA - 0x140000000.
// `key` is the attribute-name string constant the engine loads before the call;
// it is what the scanner uses to re-derive the site after a game update.
struct Enf {
    uintptr_t     ret_rva;     // resolved at startup (table value or discovered)
    uint32_t      value;
    const char*   name;        // display name (log, [enforce] section)
    const char*   key;         // attribute string constant
    int           disabled;    // [enforce] <name> = off
    volatile long logged;
};
Enf g_enf[] = {
    // shadows: half the map pixels, cheaper spot filtering
    {0x3dc3c7b,  2048, "worldShadowMapSize",      "worldShadowMapSize",     0, 0},
    {0x1271345b, 2048, "nightShadowMapSize",      "nightShadowMapSize",     0, 0},
    {0x3dc3d5b,  2,    "spotShadowSampling",      "spotShadowSampling",     0, 0},
    // volumetric fog: ~15x fewer fog pixels (fog is soft, invisible)
    {0x3ca508d,  512,  "volumetricFogMapWidth",   "volumetricFogMapWidth",  0, 0},
    {0x3ca519f,  288,  "volumetricFogMapHeight",  "volumetricFogMapHeight", 0, 0},
    // crowd billboards: shorter draw distance
    {0x3c6013b,  2500, "billboardDistance",       "billboardDistance",      0, 0},
    // Ultra-class values the preset clamp otherwise rewrites (no quality loss)
    {0x3dc3abb,  2048, "skyShadowMapSize",        "skyShadowMapSize",       0, 0},
    {0x3dc3b9b,  4,    "skyShadowCascadeCount",   "skyShadowCascadeCount",  0, 0},
    {0x3dc3ceb,  2048, "spotShadowMapSize",       "spotShadowMapSize",      0, 0},
    {0x127134db, 4,    "nightShadowSourceCount",  "nightShadowSourceCount", 0, 0},
    {0x127d0abd, 2,    "shadowSampling",          "sampling",               0, 0},
    {0x71f855b,   2048, "textureStreamingMiB",     "sizeInMiB",              0, 0},
};
const int kEnfCount = (int)(sizeof(g_enf) / sizeof(g_enf[0]));

// Bool attributes are parsed with `call strcmp(..., "true")` followed by
// `test eax,eax; sete`. The call is exactly 5 bytes, so it can be replaced by
// `mov eax, imm32` without touching any other instruction: eax == 0 is `true`,
// any non-zero value is `false`.
struct BoolEnf {
    uintptr_t   call_rva;   // resolved at startup
    bool        value;      // value the engine should see
    const char* name;
    const char* key;
    int         disabled;
};
BoolEnf g_bool[] = {
    // --- tuned delta (matches src/profile.cpp; the file alone can be reverted) ---
    {0x3dc3c08,  false, "skyShadowLowLod",            "skyShadowLowLod",            0},
    {0x3dc3b28,  false, "skyShadowMap32Bits",         "skyShadowMap32Bits",         0},
    {0x874aa08,  false, "crowdInShadows",             "crowdInShadows",             0},
    {0x2c8d638,  false, "godRays",                    "godRays",                    0},
    {0x2f811a8,  false, "godRays",                    "godRays",                    0},
    {0x2f81288,  false, "lensStreak",                 "lensStreak",                 0},
    {0x2f813d8,  false, "motionBlurV2",               "motionBlurV2",               0},
    // --- kept ON: weather, grass and night shadows are visible, not free ---
    {0x728dd18,  true,  "nightShadowCharacterCast",   "nightShadowCharacterCast",   0},
    {0x15abec,   true,  "rainSplashes",               "rainSplashes",               0},
    {0x15aadc,   true,  "rainBeads",                  "rainBeads",                  0},
    {0x15acdc,   true,  "rainHigh",                   "rainHigh",                   0},
    {0x15adcc,   true,  "trackHeatHaze",              "trackHeatHaze",              0},
    {0x1275b6ab, true,  "rainSheets",                 "rainSheets",                 0},
    {0x3ce4288,  true,  "groundCover.enabled",        "enabled",                    0},
    {0x3ce4308,  true,  "grassFlattening",            "grassFlattening",            0},
    {0x3ce438c,  true,  "groundCover.meshes",         "meshes",                     0},
    {0x3ca4f38,  true,  "volumetricFog",              "volumetricFog",              0},
};
const int kBoolCount = (int)(sizeof(g_bool) / sizeof(g_bool[0]));

// Float attributes use the same return-address substitution, but the parser
// writes the value through an out pointer and returns an errno_t. Lower is
// cheaper for both entries, so `cheaper` mode keeps min(preset, tuned).
struct FloatEnf {
    uintptr_t     ret_rva;     // return address of `call <float thunk>`
    float         value;
    const char*   name;
    const char*   key;
    int           disabled;
    volatile long logged;
};
FloatEnf g_float[] = {
    // far grass only (the ground-cover node is rewritten by the preset clamp)
    {0x3ce448b, 80.0f, "groundCover.drawDistance", "drawDistance",       0, 0},
    // ray-traced clouds: half the RT resolution (soft, barely visible)
    {0x15b2ef,  2.0f,  "proceduralCloudRTScale",   "proceduralCloudRTScale", 0, 0},
};
const int kFloatCount = (int)(sizeof(g_float) / sizeof(g_float[0]));

typedef int (*ParseIntFn)(const char*, int, int);
ParseIntFn g_parseTramp = nullptr;

// int f(float* out, const char* value, char** end)
typedef int (*ParseFloatFn)(float*, const char*, char**);
ParseFloatFn g_parseFloatTramp = nullptr;

volatile long g_dirty = 0;
volatile long g_parser_active = 0;   // set by the parser detours once settings are loading
volatile long g_boot_dump_done = 0;  // one boot-time root capture, if apply ran too early
volatile long g_stop  = 0;
HANDLE        g_thread = nullptr;
bool          g_enforce_on = false;
uintptr_t     g_parse_bool_va = 0;   // resolved bool parser (validated by the patcher)

// Lazily attached copy of the live module + LEA index, used only when a
// hard-coded address fails to validate (i.e. after a game update).
scan::PeImage  g_img;
bool           g_img_ok = false;
scan::LeaIndex g_idx;
bool           g_idx_ok = false;

void Note(const char* s) { tune::Note(s); }

// ---------------------------------------------------------------------------
// Image / signature helpers
// ---------------------------------------------------------------------------

bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    const DWORD prot = mbi.Protect & 0xFF;
    if (prot == PAGE_NOACCESS || prot == PAGE_GUARD) return false;
    if ((uintptr_t)p + n > (uintptr_t)mbi.BaseAddress + mbi.RegionSize)
        return false;
    return true;
}

// Shape check for the statically allocated settings root. This guards the
// boot-time capture: if the object has not been populated yet, or the layout
// changed in a game update, we skip the dump instead of walking garbage.
bool NodeLooksValid(const void* n) {
    if (!n || !Readable(n, 0x80)) return false;
    const char* name = *(const char**)((const char*)n + 0x18);
    if (!name || !Readable(name, 1)) return false;
    void** as = *(void***)((const char*)n + 0x50);
    void** ae = *(void***)((const char*)n + 0x58);
    void** cs = *(void***)((const char*)n + 0x70);
    void** ce = *(void***)((const char*)n + 0x78);
    if (!as || !ae || ae < as || (size_t)(ae - as) > 100000) return false;
    if (!cs || !ce || ce < cs || (size_t)(ce - cs) > 100000) return false;
    return true;
}

// True when a node has a direct child with exactly `want` as its name. Used to
// tell the config/preset tree (shadows, weather_effects, ...) apart from the
// hardware-info tree (graphics_card, systemInfo, ...), which has no such child.
bool NodeHasChildNamed(const void* n, const char* want) {
    if (!n || !Readable(n, 0x80)) return false;
    void** cs = *(void***)((const char*)n + 0x70);
    void** ce = *(void***)((const char*)n + 0x78);
    if (!Readable(cs, 0) || !Readable(ce, 0) || ce < cs ||
        (size_t)(ce - cs) > 4096) {
        return false;
    }
    for (void** p = cs; p < ce; ++p) {
        if (!Readable(p, sizeof(void*))) break;
        const void* c = *p;
        if (!c || !Readable(c, 0x20)) continue;
        const char* nm = *(const char**)((const char*)c + 0x18);
        if (!nm || !Readable(nm, 1)) continue;
        if (strncmp(nm, want, 127) == 0) return true;
    }
    return false;
}

// The preset (config) tree always carries at least one of these categories.
bool LooksLikePresetTree(const void* n) {
    return NodeHasChildNamed(n, "shadows") ||
           NodeHasChildNamed(n, "weather_effects") ||
           NodeHasChildNamed(n, "antialiasing") ||
           NodeHasChildNamed(n, "ground_cover");
}

void* RootAt(uintptr_t rva) {
    const uintptr_t slot = g_base + rva;
    if (!Readable((const void*)slot, sizeof(void*))) return nullptr;
    return *(void**)slot;
}

// Resolve the live settings root. The config document is preferred, but because
// initialisation order differs between launches, fall back to any
// shape-valid document so a capture can still be produced.
void* SettingsRoot() {
    void* first = nullptr;
    for (int i = 0; i < kRootCandidateCount; ++i) {
        void* cand = RootAt(kRootCandidates[i].rva);
        if (!NodeLooksValid(cand)) continue;
        if (LooksLikePresetTree(cand)) return cand;
        if (!first) first = cand;
    }
    return first;
}

// One-shot diagnostic: report every candidate document and whether it is the
// preset tree. Written to the mod log when a capture is requested.
void LogRootCandidates() {
    for (int i = 0; i < kRootCandidateCount; ++i) {
        void* cand = RootAt(kRootCandidates[i].rva);
        char line[256];
        _snprintf(line, sizeof(line),
                  "preset dump: root candidate %-8s %p valid=%s preset=%s",
                  kRootCandidates[i].what, cand,
                  NodeLooksValid(cand) ? "yes" : "no",
                  LooksLikePresetTree(cand) ? "yes" : "no");
        line[sizeof(line) - 1] = 0;
        Note(line);
    }
}

void EnsureImage() {
    if (!g_img_ok) g_img_ok = scan::AttachLive(&g_img);
}

// Wildcard prologue match (`?` = any byte) against the live module image.
bool MatchPrologue(uintptr_t va, const char* pattern) {
    EnsureImage();
    if (!g_img_ok) return false;
    return scan::MatchAt(g_img, va, pattern);
}

void EnsureIndex() {
    EnsureImage();
    if (g_img_ok && !g_idx_ok) g_idx_ok = scan::LeaBuild(g_img, &g_idx);
}

// A hard-coded int site is valid when the 5 bytes before it are a `call` into
// the resolved int parser.
bool IntSiteValid(uintptr_t ret_rva, uintptr_t parseInt) {
    if (ret_rva < 5) return false;
    const uintptr_t call_va = g_base + ret_rva - 5;
    if (!Readable((const void*)call_va, 5)) return false;
    const uint8_t* c = (const uint8_t*)call_va;
    if (c[0] != 0xE8) return false;
    int32_t rel = 0;
    memcpy(&rel, c + 1, 4);
    return (uintptr_t)(call_va + 5 + (intptr_t)rel) == parseInt;
}

bool BoolSiteValid(uintptr_t call_rva, uintptr_t parseBool) {
    const uintptr_t call_va = g_base + call_rva;
    if (!Readable((const void*)call_va, 5)) return false;
    const uint8_t* c = (const uint8_t*)call_va;
    if (c[0] != 0xE8) return false;
    int32_t rel = 0;
    memcpy(&rel, c + 1, 4);
    return (uintptr_t)(call_va + 5 + (intptr_t)rel) == parseBool;
}

// Same shape as the int sites, but the call targets the float-parser thunk.
bool FloatSiteValid(uintptr_t ret_rva, uintptr_t parseFloatThunk) {
    if (ret_rva < 5) return false;
    const uintptr_t call_va = g_base + ret_rva - 5;
    if (!Readable((const void*)call_va, 5)) return false;
    const uint8_t* c = (const uint8_t*)call_va;
    if (c[0] != 0xE8) return false;
    int32_t rel = 0;
    memcpy(&rel, c + 1, 4);
    return (uintptr_t)(call_va + 5 + (intptr_t)rel) == parseFloatThunk;
}

// Resolves the int parser: fast path is the known RVA with a matching prologue;
// the fallback scans the image and scores candidates by name resolution.
uintptr_t ResolveIntParser() {
    if (!tune::ForceScan() && MatchPrologue(g_base + kRvaParseInt, kPatParseCheck)) {
        Note("enforce: int parser at the known RVA (prologue verified)");
        return g_base + kRvaParseInt;
    }
    EnsureIndex();
    if (!g_img_ok) {
        Note("enforce: live image mapping failed - cannot self-heal");
        return 0;
    }
    if (!g_idx_ok) {
        Note("enforce: LEA index build failed - cannot self-heal");
        return 0;
    }
    {
        char line[128];
        _snprintf(line, sizeof(line), "enforce: live image mapped, LEA index %d entries",
                  g_idx.count);
        line[sizeof(line) - 1] = 0;
        Note(line);
    }
    Note("enforce: resolving the int parser by signature + attribute names");

    const char* keys[32];
    int nk = 0;
    for (int i = 0; i < kEnfCount && nk < 32; ++i) keys[nk++] = g_enf[i].key;
    int score = 0, cands = 0;
    const uintptr_t va = scan::ResolveParser(g_img, g_idx, kSigParseInt, keys, nk,
                                             g_base + kRvaParseInt, &score, &cands);
    if (va) {
        char line[192];
        _snprintf(line, sizeof(line),
                  "enforce: int parser re-derived by signature (score %d/%d, %d candidates)",
                  score, nk, cands);
        line[sizeof(line) - 1] = 0;
        Note(line);
    } else {
        Note("enforce: int parser not found (game build unsupported)");
    }
    return va;
}

uintptr_t ResolveBoolParser() {
    // The bool parser is not hooked; only its shape is verified.
    if (!tune::ForceScan() && MatchPrologue(g_base + kRvaParseBool, kPatParseBool)) {
        return g_base + kRvaParseBool;
    }
    EnsureIndex();
    if (!g_img_ok || !g_idx_ok) return 0;

    const char* keys[32];
    int nk = 0;
    for (int i = 0; i < kBoolCount && nk < 32; ++i) keys[nk++] = g_bool[i].key;
    int score = 0, cands = 0;
    const uintptr_t va = scan::ResolveParser(g_img, g_idx, kSigParseBool, keys, nk,
                                             g_base + kRvaParseBool, &score, &cands);
    if (va) {
        char line[192];
        _snprintf(line, sizeof(line),
                  "enforce: bool parser re-derived by signature (score %d/%d, %d candidates)",
                  score, nk, cands);
        line[sizeof(line) - 1] = 0;
        Note(line);
    }
    return va;
}

// Resolves the float parser. `*thunk_out` receives the address the engine
// actually calls (used to validate call sites); the return value is the core
// entry that gets detoured.
uintptr_t ResolveFloatParser(uintptr_t* thunk_out) {
    *thunk_out = 0;
    if (!tune::ForceScan() &&
        MatchPrologue(g_base + kRvaParseFloatThunk, kPatParseFloatThunk) &&
        MatchPrologue(g_base + kRvaParseFloat, kPatParseFloat)) {
        Note("enforce: float parser at the known RVA (prologue verified)");
        *thunk_out = g_base + kRvaParseFloatThunk;
        return g_base + kRvaParseFloat;
    }
    EnsureImage();
    if (!g_img_ok) {
        Note("enforce: live image mapping failed - cannot self-heal the float parser");
        return 0;
    }
    EnsureIndex();
    if (!g_idx_ok) {
        Note("enforce: LEA index build failed - cannot self-heal the float parser");
        return 0;
    }

    const char* keys[8];
    int nk = 0;
    for (int i = 0; i < kFloatCount && nk < 8; ++i) keys[nk++] = g_float[i].key;
    int score = 0, cands = 0;
    const uintptr_t thunk = scan::ResolveParser(g_img, g_idx, kSigParseFloat, keys, nk,
                                                g_base + kRvaParseFloatThunk, &score,
                                                &cands);
    if (!thunk) {
        Note("enforce: float parser not found (game build unsupported)");
        return 0;
    }
    int32_t rel = 0;
    memcpy(&rel, (const void*)(thunk + 4), sizeof(rel));
    const uintptr_t core = thunk + 8 + (intptr_t)rel;
    if (!MatchPrologue(core, kPatParseFloat)) {
        Note("enforce: float parser signature matched but the core prologue did not");
        return 0;
    }
    *thunk_out = thunk;
    char line[224];
    _snprintf(line, sizeof(line),
              "enforce: float parser re-derived by signature (score %d/%d, %d candidates, "
              "thunk %#llx -> core %#llx)",
              score, nk, cands, (unsigned long long)thunk, (unsigned long long)core);
    line[sizeof(line) - 1] = 0;
    Note(line);
    return core;
}

// Re-derives every site that no longer validates against the resolved parser.
void ResolveSites(uintptr_t parseInt, uintptr_t parseBool,
                  uintptr_t parseFloatThunk) {
    int table_int = 0, disc_int = 0, miss_int = 0;
    for (int i = 0; i < kEnfCount; ++i) {
        Enf& e = g_enf[i];
        const uintptr_t hard = e.ret_rva;
        if (parseInt && IntSiteValid(hard, parseInt)) {
            e.ret_rva = hard;
            ++table_int;
            continue;
        }
        if (!parseInt || !g_idx_ok) {
            e.ret_rva = 0;
            ++miss_int;
            continue;
        }
        uintptr_t s[8];
        const int n = scan::FindSites(g_img, g_idx, e.key, parseInt, 0x100, false, s, 8);
        if (n == 1) {
            e.ret_rva = s[0] - g_base;
            ++disc_int;
        } else {
            e.ret_rva = 0;
            ++miss_int;
            char line[192];
            _snprintf(line, sizeof(line),
                      "enforce: WARN %s unresolved (%d candidate sites) - skipped", e.name, n);
            line[sizeof(line) - 1] = 0;
            Note(line);
        }
    }

    int table_bool = 0, disc_bool = 0, miss_bool = 0;
    for (int i = 0; i < kBoolCount; ++i) {
        BoolEnf& b = g_bool[i];
        const uintptr_t hard = b.call_rva;
        if (parseBool && BoolSiteValid(hard, parseBool)) {
            b.call_rva = hard;
            ++table_bool;
            continue;
        }
        if (!parseBool || !g_idx_ok) {
            b.call_rva = 0;
            ++miss_bool;
            continue;
        }
        uintptr_t s[16];
        const int n = scan::FindSites(g_img, g_idx, b.key, parseBool, 0x100, false, s, 16);
        if (n == 1) {
            b.call_rva = s[0] - g_base - 5;   // store the call address
            ++disc_bool;
        } else {
            b.call_rva = 0;
            ++miss_bool;
            char line[192];
            _snprintf(line, sizeof(line),
                      "enforce: WARN %s unresolved (%d candidate sites) - skipped", b.name, n);
            line[sizeof(line) - 1] = 0;
            Note(line);
        }
    }

    int table_float = 0, disc_float = 0, miss_float = 0;
    for (int i = 0; i < kFloatCount; ++i) {
        FloatEnf& f = g_float[i];
        const uintptr_t hard = f.ret_rva;
        if (parseFloatThunk && FloatSiteValid(hard, parseFloatThunk)) {
            f.ret_rva = hard;
            ++table_float;
            continue;
        }
        if (!parseFloatThunk || !g_idx_ok) {
            f.ret_rva = 0;
            ++miss_float;
            continue;
        }
        uintptr_t s[8];
        const int n = scan::FindSites(g_img, g_idx, f.key, parseFloatThunk, 0x100, false, s, 8);
        if (n == 1) {
            f.ret_rva = s[0] - g_base;
            ++disc_float;
        } else {
            f.ret_rva = 0;
            ++miss_float;
            char line[192];
            _snprintf(line, sizeof(line),
                      "enforce: WARN %s unresolved (%d candidate sites) - skipped", f.name, n);
            line[sizeof(line) - 1] = 0;
            Note(line);
        }
    }

    if (tune::LogReport()) {
        char line[320];
        _snprintf(line, sizeof(line),
                  "report: int sites table=%d discovered=%d missing=%d | "
                  "bool sites table=%d discovered=%d missing=%d | "
                  "float sites table=%d discovered=%d missing=%d",
                  table_int, disc_int, miss_int, table_bool, disc_bool, miss_bool,
                  table_float, disc_float, miss_float);
        line[sizeof(line) - 1] = 0;
        Note(line);
    }
}

// Applies the [enforce] section overrides on top of the built-in table.
void ApplyOverrides() {
    int changed = 0, disabled = 0;
    for (int i = 0; i < kEnfCount; ++i) {
        int v = 0, d = 0;
        if (tune::LookupEnforce(g_enf[i].name, &v, &d)) {
            if (d) { g_enf[i].disabled = 1; ++disabled; }
            else { g_enf[i].value = (uint32_t)v; ++changed; }
        }
    }
    for (int i = 0; i < kBoolCount; ++i) {
        int v = 0, d = 0;
        if (tune::LookupEnforce(g_bool[i].name, &v, &d)) {
            if (d) { g_bool[i].disabled = 1; ++disabled; }
            else { g_bool[i].value = (v != 0); ++changed; }
        }
    }
    for (int i = 0; i < kFloatCount; ++i) {
        float v = 0.0f;
        int d = 0;
        if (tune::LookupEnforceFloat(g_float[i].name, &v, &d)) {
            if (d) { g_float[i].disabled = 1; ++disabled; }
            else { g_float[i].value = v; ++changed; }
        }
    }
    if (changed || disabled) {
        char line[160];
        _snprintf(line, sizeof(line),
                  "enforce: [enforce] overrides applied (%d set, %d disabled)", changed,
                  disabled);
        line[sizeof(line) - 1] = 0;
        Note(line);
    }
}

int DetourParseInt(const char* value, int def, int flags) {
    InterlockedExchange(&g_parser_active, 1);
    const uintptr_t rva = (uintptr_t)__builtin_return_address(0) - g_base;
    const int orig = g_parseTramp(value, def, flags);

    for (int i = 0; i < kEnfCount; ++i) {
        if (g_enf[i].ret_rva == rva && !g_enf[i].disabled) {
            // `target`: the mod defines the quality level, so the tuned value wins
            // even if the engine's preset is lower. `cheaper`: only ever remove
            // work (the tuned value is a ceiling).
            const int tuned = (int)g_enf[i].value;
            const int out = tune::EnforceTarget() ? tuned
                                                  : ((orig < tuned) ? orig : tuned);
            if (InterlockedExchange(&g_enf[i].logged, 1) == 0) {
                char line[192];
                _snprintf(line, sizeof(line),
                          "enforce: %s -> %d (preset gave %d, mode=%s)", g_enf[i].name,
                          out, orig, tune::EnforceTarget() ? "target" : "cheaper");
                line[sizeof(line) - 1] = 0;
                Note(line);
            }
            return out;
        }
    }
    if (tune::LogAll() && rva != 0) {
        static uintptr_t seen[256];
        static int seen_n = 0;
        bool dup = false;
        for (int i = 0; i < seen_n; ++i) {
            if (seen[i] == rva) { dup = true; break; }
        }
        if (!dup && seen_n < 256) {
            seen[seen_n++] = rva;
            char line[128];
            _snprintf(line, sizeof(line), "parse: site rva %#x -> %d",
                      (unsigned)rva, orig);
            line[sizeof(line) - 1] = 0;
            Note(line);
        }
    }
    return orig;
}

int DetourParseFloat(float* out, const char* value, char** end) {
    InterlockedExchange(&g_parser_active, 1);
    const uintptr_t rva = (uintptr_t)__builtin_return_address(0) - g_base;
    const int rc = g_parseFloatTramp(out, value, end);

    if (!out) return rc;
    for (int i = 0; i < kFloatCount; ++i) {
        if (g_float[i].ret_rva != rva || g_float[i].disabled) continue;
        const float orig = *out;
        const float tuned = g_float[i].value;
        // `cheaper`: the tuned value is a ceiling (lower is cheaper here too).
        const float val = tune::EnforceTarget() ? tuned
                                                : ((orig < tuned) ? orig : tuned);
        if (val != orig) *out = val;
        if (InterlockedExchange(&g_float[i].logged, 1) == 0) {
            char line[224];
            _snprintf(line, sizeof(line),
                      "enforce: %s -> %g (preset gave %g, mode=%s)", g_float[i].name,
                      val, orig, tune::EnforceTarget() ? "target" : "cheaper");
            line[sizeof(line) - 1] = 0;
            Note(line);
        }
        return rc;
    }
    if (tune::LogAll() && rva != 0) {
        static uintptr_t seen[256];
        static int seen_n = 0;
        bool dup = false;
        for (int i = 0; i < seen_n; ++i) {
            if (seen[i] == rva) { dup = true; break; }
        }
        if (!dup && seen_n < 256) {
            seen[seen_n++] = rva;
            char line[160];
            _snprintf(line, sizeof(line), "parse: float site rva %#x -> %g",
                      (unsigned)rva, *out);
            line[sizeof(line) - 1] = 0;
            Note(line);
        }
    }
    return rc;
}

DWORD WINAPI Watcher(LPVOID) {
    int boot_tries = 0;
    while (!g_stop) {
        Sleep(500);

        // Temporary/manual capture hook: create f125_dump_now.txt next to the
        // game while the settings screen is open. Used to validate the dump on
        // builds where the engine's apply hook is not reached.
        if (GetFileAttributesW(L"f125_dump_now.txt") != INVALID_FILE_ATTRIBUTES) {
            DeleteFileW(L"f125_dump_now.txt");
            LogRootCandidates();
            void* root = SettingsRoot();
            if (NodeLooksValid(root)) {
                DumpPreset(root);
                tune::Note("preset dump: manual trigger captured");
            } else if (Readable(root, 0x80)) {
                const uintptr_t* q = (const uintptr_t*)root;
                char line[320];
                _snprintf(line, sizeof(line),
                          "preset dump: manual root %p invalid (name=%p attrs=%p..%p kids=%p..%p)",
                          root, (void*)q[3], (void*)q[10], (void*)q[11],
                          (void*)q[14], (void*)q[15]);
                line[sizeof(line) - 1] = 0;
                tune::Note(line);
            } else {
                tune::Note("preset dump: manual root unreadable");
            }
        }

        // The boot parse runs before the UI exists, and on some launches before
        // the apply hook is reached. Once the int/float parser detours have
        // seen real settings, the verified static root is safe to capture. This
        // is event-driven (parser activity), not a blind startup delay.
        if (!g_boot_dump_done && g_parser_active) {
            Sleep(2500);
            if (g_stop) break;
            if (InterlockedCompareExchange(&g_boot_dump_done, 0, 0) == 0) {
                LogRootCandidates();
                void* root = SettingsRoot();
                if (NodeLooksValid(root)) {
                    DumpPreset(root);
                    InterlockedExchange(&g_boot_dump_done, 1);
                    tune::Note("preset dump: boot settings root captured");
                } else if (++boot_tries >= 6) {
                    InterlockedExchange(&g_boot_dump_done, 1);
                    tune::Note("preset dump: boot settings root never became valid");
                } else {
                    char line[320];
                    if (Readable(root, 0x80)) {
                        const uintptr_t* q = (const uintptr_t*)root;
                        _snprintf(line, sizeof(line),
                                  "preset dump: boot root %p not ready (name=%p attrs=%p..%p kids=%p..%p)",
                                  root, (void*)q[3], (void*)q[10], (void*)q[11],
                                  (void*)q[14], (void*)q[15]);
                    } else {
                        _snprintf(line, sizeof(line),
                                  "preset dump: boot root %p unreadable (base=%p)", root,
                                  (void*)g_base);
                    }
                    line[sizeof(line) - 1] = 0;
                    tune::Note(line);
                }
            }
        }

        if (InterlockedExchange(&g_dirty, 0)) {
            // The engine is mid-apply; give it a moment, then restore the file.
            Sleep(900);
            if (!g_stop) {
                tune::ApplyProfile();
                Note("guard: graphics preset re-applied - tuned delta restored");
            }
        }
    }
    return 0;
}

}  // namespace

// Globals referenced from the naked thunks.
extern "C" {
void*     g_applyTramp     = nullptr;
void*     g_applyNodeTramp = nullptr;
uintptr_t g_applyRet       = 0;
uintptr_t g_applyNodeRet   = 0;
void*     g_applyRootNode  = nullptr;
int       g_applyDepth     = 0;
int       g_inApply        = 0;
unsigned char g_applyNodeTop = 0;
}

// Walks a node (and its children) and dumps every attribute as
// "category:attribute=value" to f125_preset_<preset>.txt. `node` is the root
// node handed to hardwareSettings::Options::apply (image-base-relative layout:
// +0x18 name, +0x50/+0x58 attribute array, +0x70/+0x78 child-node array).
void DumpPreset(void* root) {
    char dbg[128];
    _snprintf(dbg, sizeof(dbg), "DumpPreset called root=%p", root);
    dbg[sizeof(dbg)-1]=0;
    tune::Note(dbg);
    if (!root || !Readable(root, 0x80)) {
        tune::Note("DumpPreset: root null/unreadable");
        return;
    }

    // Find a "category" attribute anywhere in the tree; its value becomes the
    // preset name used in the dump file name.
    char preset[128] = {0};
    {
        // iterative-ish recursive lambda via a helper stack
        void* stack[64];
        int sp = 0;
        stack[sp++] = root;
        while (sp > 0 && preset[0] == 0) {
            void* n = stack[--sp];
            if (!n || !Readable(n, 0x80)) continue;
            void** s = *(void*** )((char*)n + 0x50);
            void** e = *(void*** )((char*)n + 0x58);
            if (Readable(s, 0) && Readable(e, 0) && e >= s &&
                (size_t)(e - s) < 100000) {
                for (void** p = s; p < e; ++p) {
                    if (!Readable(p, sizeof(void*))) break;
                    void* a = *p;
                    if (!a || !Readable(a, 0x60)) continue;
                    const char* an = *(const char**)((char*)a + 0x18);
                    if (!an || !Readable(an, 1)) continue;
                    if (strcmp(an, "category") == 0) {
                        const char* av = *(const char**)((char*)a + 0x50);
                        if (av && Readable(av, 1)) {
                            strncpy(preset, av, sizeof(preset) - 1);
                            preset[sizeof(preset) - 1] = 0;
                            break;
                        }
                    }
                }
            }
            if (preset[0]) break;
            void** cs = *(void*** )((char*)n + 0x70);
            void** ce = *(void*** )((char*)n + 0x78);
            if (Readable(cs, 0) && Readable(ce, 0) && ce >= cs &&
                (size_t)(ce - cs) < 100000) {
                for (void** p = cs; p < ce && sp < 63; ++p) {
                    if (!Readable(p, sizeof(void*))) break;
                    stack[sp++] = *p;
                }
            }
        }
    }
    if (preset[0] == 0) strcpy(preset, "unknown");

    char safe[128] = {0};
    size_t si = 0;
    for (const char* c = preset; *c && si + 1 < sizeof(safe); ++c) {
        if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
            (*c >= '0' && *c <= '9') || *c == '_' || *c == '-') {
            safe[si++] = *c;
        } else if (*c == ' ') {
            safe[si++] = '_';
        }
    }
    safe[si] = 0;
    if (si == 0) strcpy(safe, "unknown");

    wchar_t wname[128] = {0};
    MultiByteToWideChar(CP_ACP, 0, safe, -1, wname, 128);

    wchar_t path[MAX_PATH] = {0};
    _snwprintf(path, MAX_PATH, L"%s\\f125_preset_%s.txt", tune::ExeDir(), wname);

    FILE* f = _wfopen(path, L"w");
    if (!f) return;

    // Output caps keep a corrupt or unexpectedly deep tree from turning into a
    // multi-megabyte file (or an endless walk). Any hit is marked in the footer.
    const int kMaxNodes = 8192;
    const int kMaxAttrs = 50000;
    void** visited = (void**)malloc(sizeof(void*) * (size_t)kMaxNodes);
    int visited_n = 0;
    int nodes = 0, attrs = 0;
    bool truncated = false;

    fprintf(f, "# preset=%s\n", preset);
    fprintf(f, "# source=hardware_settings_config tree "
               "(category:attribute=value, applyNode order)\n");

    // Dump the whole tree. Use an explicit stack to avoid deep recursion.
    void* st[256];
    int sp = 0;
    st[sp++] = root;
    while (sp > 0) {
        void* n = st[--sp];
        if (!n || !Readable(n, 0x80)) { truncated = true; continue; }
        if (++nodes > kMaxNodes) { truncated = true; break; }

        // Settings are a tree; a repeated node means a cycle or a bad pointer.
        bool seen = false;
        for (int i = 0; i < visited_n; ++i) {
            if (visited[i] == n) { seen = true; break; }
        }
        if (seen) { truncated = true; continue; }
        if (visited && visited_n < kMaxNodes) visited[visited_n++] = n;

        const char* nname = *(const char**)((char*)n + 0x18);
        if (!nname || !Readable(nname, 1)) nname = "node";

        void** s = *(void*** )((char*)n + 0x50);
        void** e = *(void*** )((char*)n + 0x58);
        if (Readable(s, 0) && Readable(e, 0) && e >= s &&
            (size_t)(e - s) < 100000) {
            for (void** p = s; p < e; ++p) {
                if (!Readable(p, sizeof(void*))) { truncated = true; break; }
                if (attrs >= kMaxAttrs) { truncated = true; break; }
                void* a = *p;
                if (!a || !Readable(a, 0x60)) { truncated = true; continue; }
                const char* an = *(const char**)((char*)a + 0x18);
                const char* av = *(const char**)((char*)a + 0x50);
                if (!an || !Readable(an, 1)) { truncated = true; continue; }
                if (!av || !Readable(av, 1)) av = "";
                // Bounded strings: an unreasonably long or unterminated name
                // must not produce a giant line or read past the mapping.
                fprintf(f, "%.128s:%.128s=%.256s\n", nname, an, av);
                ++attrs;
            }
        } else if (s || e) {
            truncated = true;
        }

        void** cs = *(void*** )((char*)n + 0x70);
        void** ce = *(void*** )((char*)n + 0x78);
        if (Readable(cs, 0) && Readable(ce, 0) && ce >= cs &&
            (size_t)(ce - cs) < 100000) {
            // applyNode visits attributes before children and children in array
            // order, so push them in reverse: the LIFO stack then pops them in
            // document order (the reference sequence for the dump).
            for (void** p = ce; p > cs; --p) {
                if (!Readable(p - 1, sizeof(void*))) { truncated = true; break; }
                if (sp >= 255) { truncated = true; break; }
                st[sp++] = *(p - 1);
            }
        } else if (cs || ce) {
            truncated = true;
        }
        if (attrs >= kMaxAttrs) { truncated = true; break; }
    }

    fprintf(f, "# attributes=%d\n", attrs);
    fprintf(f, "# nodes=%d\n", nodes);
    fprintf(f, "# truncated=%s\n", truncated ? "yes" : "no");

    free(visited);
    fclose(f);

    char line[320];
    _snprintf(line, sizeof(line),
              "preset dump written: f125_preset_%s.txt (%d attributes, %d nodes, truncated=%s)",
              safe, attrs, nodes, truncated ? "yes" : "no");
    line[sizeof(line) - 1] = 0;
    tune::Note(line);
}

// Runs *after* hardwareSettings::Options::apply / applyNode. Debounced: it only
// flags the settings file as dirty; the watcher thread does the disk work.
extern "C" void OnPresetApplied(void) { InterlockedExchange(&g_dirty, 1); }

extern "C" void OnApplyPost(void) {
    OnPresetApplied();
    DumpPreset(g_applyRootNode);
}

extern "C" void NoteApplyNodeEntry(void) {
    tune::Note("ApplyNodeDetour entry");
}
extern "C" void NoteApplyEntry(void) {
    tune::Note("ApplyDetour entry");
}

// Naked pass-through detours: run the original with its exact stack, then drop
// into our post-hook and finally jump to the real caller.
extern "C" void ApplyDetour(void);
extern "C" void ApplyNodeDetour(void);

asm(
    ".text\n"
    ".globl ApplyDetour\n"
    "ApplyDetour:\n"
    "  pushq %rdx\n"
    "  movl $1, g_inApply(%rip)\n"
    "  call NoteApplyEntry\n"
    "  popq %rdx\n"
    "  movq (%rsp), %rax\n"
    "  movq %rax, g_applyRet(%rip)\n"
    "  leaq .LApplyPost(%rip), %rax\n"
    "  movq %rax, (%rsp)\n"
    "  jmp *g_applyTramp(%rip)\n"
    ".LApplyPost:\n"
    "  pushq %rax\n"
    "  subq $0x28, %rsp\n"
    "  movl $0, g_inApply(%rip)\n"
    "  call OnPresetApplied\n"
    "  addq $0x28, %rsp\n"
    "  popq %rax\n"
    "  movq g_applyRet(%rip), %rcx\n"
    "  jmp *%rcx\n"
    ".globl ApplyNodeDetour\n"
    "ApplyNodeDetour:\n"
    "  pushq %rdx\n"
    "  call NoteApplyNodeEntry\n"
    "  popq %rdx\n"
    "  cmpl $0, g_applyDepth(%rip)\n"
    "  jne .LNodeNotTop\n"
    "  movq %rdx, g_applyRootNode(%rip)\n"
    "  movb $1, g_applyNodeTop(%rip)\n"
    ".LNodeNotTop:\n"
    "  addl $1, g_applyDepth(%rip)\n"
    "  movq (%rsp), %rax\n"
    "  movq %rax, g_applyNodeRet(%rip)\n"
    "  leaq .LApplyNodePost(%rip), %rax\n"
    "  movq %rax, (%rsp)\n"
    "  jmp *g_applyNodeTramp(%rip)\n"
    ".LApplyNodePost:\n"
    "  pushq %rax\n"
    "  subq $0x28, %rsp\n"
    "  subl $1, g_applyDepth(%rip)\n"
    "  cmpb $0, g_applyNodeTop(%rip)\n"
    "  je .LNodePostSkip\n"
    "  cmpq $0, g_applyRootNode(%rip)\n"
    "  je .LNodePostSkip\n"
    "  cmpl $0, g_applyDepth(%rip)\n"
    "  jne .LNodePostSkip\n"
    "  movb $0, g_applyNodeTop(%rip)\n"
    "  call OnApplyPost\n"
    "  jmp .LNodePostDone\n"
    ".LNodePostSkip:\n"
    "  call OnPresetApplied\n"
    ".LNodePostDone:\n"
    "  addq $0x28, %rsp\n"
    "  popq %rax\n"
    "  movq g_applyNodeRet(%rip), %rcx\n"
    "  jmp *%rcx\n");

// Verifies a prologue and installs `detour` over it. Returns the trampoline.
static void* PatchAt(uintptr_t va, const char* pro, size_t n, void* detour) {
    void* target = (void*)va;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(target, &mbi, sizeof(mbi)) == 0) return nullptr;
    if (mbi.State != MEM_COMMIT) return nullptr;
    const DWORD prot = mbi.Protect & 0xFF;
    if (prot != PAGE_EXECUTE && prot != PAGE_EXECUTE_READ &&
        prot != PAGE_EXECUTE_READWRITE && prot != PAGE_EXECUTE_WRITECOPY &&
        prot != PAGE_READONLY && prot != PAGE_READWRITE) {
        return nullptr;
    }
    if ((uintptr_t)target + n > (uintptr_t)mbi.BaseAddress + mbi.RegionSize) {
        return nullptr;
    }
    if (!MatchPrologue(va, pro)) return nullptr;
    char why[64];
    if (!hook::SafeToRelocate(target, (int)n, why)) {
        char line[160];
        _snprintf(line, sizeof(line), "hook: prologue not relocatable (%s) - skipped", why);
        line[sizeof(line) - 1] = 0;
        Note(line);
        return nullptr;
    }
    return hook::Install(target, detour, (int)n);
}

// Pins bool attributes by rewriting the 5-byte `call strcmp` with
// `mov eax, imm32`. Returns the number of sites patched.
static int PatchBoolSites() {
    int done = 0, skipped = 0, disabled = 0;
    for (int i = 0; i < kBoolCount; ++i) {
        const BoolEnf& e = g_bool[i];
        if (e.disabled || e.call_rva == 0) {
            ++disabled;
            continue;
        }
        BYTE* site = (BYTE*)(g_base + e.call_rva);
        if (!Readable(site, 5) || site[0] != 0xE8) {
            ++skipped;
            continue;
        }
        int32_t rel = 0;
        memcpy(&rel, site + 1, sizeof(rel));
        const uintptr_t callee = (uintptr_t)(site + 5) + (intptr_t)rel;
        if (callee != g_parse_bool_va) {
            ++skipped;
            continue;
        }
        DWORD old = 0;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) {
            ++skipped;
            continue;
        }
        // eax == 0 -> "true" (callers use `sete`); non-zero -> "false".
        site[0] = 0xB8;                       // mov eax, imm32
        site[1] = e.value ? 0x00 : 0x01;
        site[2] = 0x00;
        site[3] = 0x00;
        site[4] = 0x00;
        VirtualProtect(site, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
        ++done;
    }
    {
        char line[192];
        _snprintf(line, sizeof(line),
                  "enforce: %d bool attributes pinned to the tuned profile "
                  "(%d disabled, %d unavailable)",
                  done, disabled, skipped);
        line[sizeof(line) - 1] = 0;
        Note(line);
    }
    return done;
}

// Resolves hardwareSettings::Options::apply / applyNode.
static uintptr_t ResolveApply(bool node) {
    const uintptr_t rva = node ? kRvaApplyNode : kRvaApply;
    const char* pro = node ? kPatApplyNode : kPatApply;
    if (!tune::ForceScan() && MatchPrologue(g_base + rva, pro)) return g_base + rva;

    EnsureImage();
    if (!g_img_ok) return 0;
    const uintptr_t va = scan::FindPattern(g_img, node ? kSigApplyNode : kSigApply);
    if (va && MatchPrologue(va, pro)) {
        Note(node ? (tune::ForceScan()
                         ? "guard: applyNode re-derived by scan (force-scan)"
                         : "guard: applyNode re-derived by signature (game updated?)")
                  : (tune::ForceScan()
                         ? "guard: apply re-derived by scan (force-scan)"
                         : "guard: apply re-derived by signature (game updated?)"));
        return va;
    }
    return 0;
}

void Init() {
    g_base = (uintptr_t)GetModuleHandleW(nullptr);
    Note(tune::ForceScan()
             ? "enforce: force-scan enabled - known addresses ignored, resolving by signature"
             : "enforce: using known addresses (prologue verified)");

    const uintptr_t parseInt = tune::WantEnforce() ? ResolveIntParser() : 0;
    const uintptr_t parseBool = tune::WantEnforce() ? ResolveBoolParser() : 0;
    uintptr_t parseFloatThunk = 0;
    const uintptr_t parseFloat =
        tune::WantEnforce() ? ResolveFloatParser(&parseFloatThunk) : 0;
    g_parse_bool_va = parseBool;

    if (tune::WantEnforce() && (parseInt || parseBool || parseFloat)) {
        ResolveSites(parseInt, parseBool, parseFloatThunk);
        ApplyOverrides();

        if (parseInt) {
            void* t = PatchAt(parseInt, kPatParsePatch, kLenParsePatch,
                              (void*)&DetourParseInt);
            if (t) {
                g_parseTramp = (ParseIntFn)t;
                g_enforce_on = true;
                Note("enforce: settings parser hooked (tuned values applied in-engine)");
            } else {
                Note("enforce: settings parser signature mismatch - skipped (game updated?)");
            }
        } else {
            Note("enforce: int parser unavailable - integer values stay at the preset");
        }
        // Bool attributes are pinned in every enforce mode. The table already
        // encodes the intended look (rain / grass / volumetric fog stay ON,
        // god rays / lens streak / motion blur stay OFF), so this never
        // silently drops an effect to a lower preset - it just makes the
        // mod's look independent of which numeric mode is active.
        if (tune::WantEnforceBools()) PatchBoolSites();

        if (parseFloat) {
            void* tf = PatchAt(parseFloat, kPatParseFloat, kLenParseFloat,
                               (void*)&DetourParseFloat);
            if (tf) {
                g_parseFloatTramp = (ParseFloatFn)tf;
                g_enforce_on = true;
                Note("enforce: float parser hooked (cloud RT scale / grass distance)");
            } else {
                Note("enforce: float parser signature mismatch - skipped (game updated?)");
            }
        } else {
            Note("enforce: float parser unavailable - float values stay at the preset");
        }
    }

    if (tune::WantGuard()) {
        const uintptr_t apply = ResolveApply(false);
        const uintptr_t applyNode = ResolveApply(true);
        void* ta = apply ? PatchAt(apply, kPatApply, kLenApply, (void*)&ApplyDetour)
                         : nullptr;
        void* tn = applyNode ? PatchAt(applyNode, kPatApplyNode, kLenApplyNode,
                                       (void*)&ApplyNodeDetour)
                             : nullptr;
        // Either entry point is enough: both lead to the same preset re-apply.
        if (ta) g_applyTramp = ta;
        if (tn) g_applyNodeTramp = tn;
        if (ta || tn) {
            g_thread = CreateThread(nullptr, 0, Watcher, nullptr, 0, nullptr);
            char line[192];
            _snprintf(line, sizeof(line),
                      "guard: preset hooks installed (apply=%s, applyNode=%s) - "
                      "delta auto-restored after menu changes",
                      ta ? "yes" : "no", tn ? "yes" : "no");
            line[sizeof(line) - 1] = 0;
            Note(line);
        } else {
            Note("guard: preset hooks unavailable on this build - file delta only");
        }
    }
}

bool Active() { return g_enforce_on; }

}  // namespace enforce
