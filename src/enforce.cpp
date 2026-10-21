#include "enforce.h"
#include "hook.h"
#include "scan.h"
#include "tune.h"
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace enforce {
namespace {

// --- F1_25.exe (image base 0x140000000, no ASLR) -----------------------------
// FUN_14534dcd8: int attribute-value parser (valueString, default, flags).
static const uintptr_t kRvaParseInt  = 0x534dcd8u;
// FUN_14534d918: bool attribute check; returns 0 when the string is "true"
// (the callers turn that into a bool with `test eax,eax; sete`).
static const uintptr_t kRvaParseBool = 0x534d918u;

// hardwareSettings::Options::apply / ::applyNode (verified via xrefs on the
// "[hardwareSettings::Options::apply]" / "[...applyNode]" log strings).
static const uintptr_t kRvaApply     = 0x40217f0u;
static const uintptr_t kRvaApplyNode = 0x40226c0u;

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

typedef int (*ParseIntFn)(const char*, int, int);
ParseIntFn g_parseTramp = nullptr;

volatile long g_dirty = 0;
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

// Re-derives every site that no longer validates against the resolved parser.
void ResolveSites(uintptr_t parseInt, uintptr_t parseBool) {
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

    if (tune::LogReport()) {
        char line[256];
        _snprintf(line, sizeof(line),
                  "report: int sites table=%d discovered=%d missing=%d | "
                  "bool sites table=%d discovered=%d missing=%d",
                  table_int, disc_int, miss_int, table_bool, disc_bool, miss_bool);
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

DWORD WINAPI Watcher(LPVOID) {
    while (!g_stop) {
        Sleep(500);
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
}

// Runs *after* hardwareSettings::Options::apply / applyNode. Debounced: it only
// flags the settings file as dirty; the watcher thread does the disk work.
extern "C" void OnPresetApplied(void) { InterlockedExchange(&g_dirty, 1); }

// Naked pass-through detours: run the original with its exact stack, then drop
// into our post-hook and finally jump to the real caller.
extern "C" void ApplyDetour(void);
extern "C" void ApplyNodeDetour(void);

asm(
    ".text\n"
    ".globl ApplyDetour\n"
    "ApplyDetour:\n"
    "  movq (%rsp), %rax\n"
    "  movq %rax, g_applyRet(%rip)\n"
    "  leaq .LApplyPost(%rip), %rax\n"
    "  movq %rax, (%rsp)\n"
    "  jmp *g_applyTramp(%rip)\n"
    ".LApplyPost:\n"
    "  pushq %rax\n"
    "  subq $0x28, %rsp\n"
    "  call OnPresetApplied\n"
    "  addq $0x28, %rsp\n"
    "  popq %rax\n"
    "  movq g_applyRet(%rip), %rcx\n"
    "  jmp *%rcx\n"
    ".globl ApplyNodeDetour\n"
    "ApplyNodeDetour:\n"
    "  movq (%rsp), %rax\n"
    "  movq %rax, g_applyNodeRet(%rip)\n"
    "  leaq .LApplyNodePost(%rip), %rax\n"
    "  movq %rax, (%rsp)\n"
    "  jmp *g_applyNodeTramp(%rip)\n"
    ".LApplyNodePost:\n"
    "  pushq %rax\n"
    "  subq $0x28, %rsp\n"
    "  call OnPresetApplied\n"
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
    g_parse_bool_va = parseBool;

    if (tune::WantEnforce() && parseInt) {
        ResolveSites(parseInt, parseBool);
        ApplyOverrides();

        void* t = PatchAt(parseInt, kPatParsePatch, kLenParsePatch,
                          (void*)&DetourParseInt);
        if (t) {
            g_parseTramp = (ParseIntFn)t;
            g_enforce_on = true;
            Note("enforce: settings parser hooked (tuned values applied in-engine)");
        } else {
            Note("enforce: settings parser signature mismatch - skipped (game updated?)");
        }
        // Bool attributes are pinned in every enforce mode. The table already
        // encodes the intended look (rain / grass / volumetric fog stay ON,
        // god rays / lens streak / motion blur stay OFF), so this never
        // silently drops an effect to a lower preset - it just makes the
        // mod's look independent of which numeric mode is active.
        if (tune::WantEnforceBools()) PatchBoolSites();
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
