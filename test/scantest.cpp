// Self-test / offline validator for the signature scanner (src/scan.cpp).
//
//   scantest [path-to-F1_25.exe]
//
// Loads the game image from disk (no game launch needed), re-derives the
// engine's settings parsers by signature, and checks that every enforcement
// site in src/enforce.cpp's table validates against them and is reproduced by
// the name-driven discovery. This is what lets the mod survive a game update.
#include "scan.h"
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// --- signatures (must stay in sync with src/enforce.cpp) --------------------
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
static const char* kSigParseFloat =
    "45 33 C0 E9 ?? ?? ?? ?? 48 89 5C 24 08 57 48 83 EC ?? 48 8B FA 48 8B D9 "
    "48 85 C9";
static const char* kSigBoolGet =
    "41 56 56 57 53 48 83 EC ?? 44 89 CB 4C 89 C7 49 89 CE 48 89 54 24 30 "
    "48 8D 71 10 48 89 F1 E8 ?? ?? ?? ??";
static const char* kSigScalarGet =
    "41 56 56 57 53 48 83 EC ?? 44 89 CF 4C 89 C3 49 89 CE 48 89 54 24 30 "
    "48 8D 71 10 48 89 F1 E8 ?? ?? ?? ??";

static const uintptr_t kBase = 0x140000000u;
static const uintptr_t kRvaParseInt = 0x534dcd8u;
static const uintptr_t kRvaParseBool = 0x534d918u;
static const uintptr_t kRvaParseFloatThunk = 0x5343ff4u;
static const uintptr_t kRvaParseFloat = 0x5343ffcu;
static const uintptr_t kRvaApply = 0x40217f0u;
static const uintptr_t kRvaApplyNode = 0x40226c0u;

struct IntEnf { uintptr_t ret_rva; const char* name; const char* key; };
struct BoolEnf { uintptr_t call_rva; const char* name; const char* key; };
struct FloatEnf { uintptr_t ret_rva; const char* name; const char* key; };

static const IntEnf kInt[] = {
    {0x3dc3c7b, "worldShadowMapSize", "worldShadowMapSize"},
    {0x1271345b, "nightShadowMapSize", "nightShadowMapSize"},
    {0x3dc3d5b, "spotShadowSampling", "spotShadowSampling"},
    {0x3ca508d, "volumetricFogMapWidth", "volumetricFogMapWidth"},
    {0x3ca519f, "volumetricFogMapHeight", "volumetricFogMapHeight"},
    {0x3c6013b, "billboardDistance", "billboardDistance"},
    {0x3dc3abb, "skyShadowMapSize", "skyShadowMapSize"},
    {0x3dc3b9b, "skyShadowCascadeCount", "skyShadowCascadeCount"},
    {0x3dc3ceb, "spotShadowMapSize", "spotShadowMapSize"},
    {0x127134db, "nightShadowSourceCount", "nightShadowSourceCount"},
    {0x127d0abd, "shadowSampling", "sampling"},
    {0x71f855b, "textureStreamingMiB", "sizeInMiB"},
};
static const BoolEnf kBool[] = {
    {0x3dc3c08, "skyShadowLowLod", "skyShadowLowLod"},
    {0x3dc3b28, "skyShadowMap32Bits", "skyShadowMap32Bits"},
    {0x874aa08, "crowdInShadows", "crowdInShadows"},
    {0x2c8d638, "godRays", "godRays"},
    {0x2f811a8, "godRays", "godRays"},
    {0x2f81288, "lensStreak", "lensStreak"},
    {0x2f813d8, "motionBlurV2", "motionBlurV2"},
    {0x728dd18, "nightShadowCharacterCast", "nightShadowCharacterCast"},
    {0x15abec, "rainSplashes", "rainSplashes"},
    {0x15aadc, "rainBeads", "rainBeads"},
    {0x15acdc, "rainHigh", "rainHigh"},
    {0x15adcc, "trackHeatHaze", "trackHeatHaze"},
    {0x1275b6ab, "rainSheets", "rainSheets"},
    {0x3ce4288, "groundCover.enabled", "enabled"},
    {0x3ce4308, "grassFlattening", "grassFlattening"},
    {0x3ce438c, "groundCover.meshes", "meshes"},
    {0x3ca4f38, "volumetricFog", "volumetricFog"},
};
static const FloatEnf kFloat[] = {
    {0x3ce448b, "groundCover.drawDistance", "drawDistance"},
    {0x15b2ef,  "proceduralCloudRTScale",   "proceduralCloudRTScale"},
};

static const char* kIntNames[] = {
    "worldShadowMapSize","nightShadowMapSize","spotShadowSampling",
    "volumetricFogMapWidth","volumetricFogMapHeight","billboardDistance",
    "skyShadowMapSize","skyShadowCascadeCount","spotShadowMapSize",
    "nightShadowSourceCount","sampling","sizeInMiB"};
static const char* kBoolNames[] = {
    "skyShadowLowLod","skyShadowMap32Bits","crowdInShadows","godRays","lensStreak",
    "motionBlurV2","nightShadowCharacterCast","rainSplashes","rainBeads","rainHigh",
    "trackHeatHaze","rainSheets","enabled","grassFlattening","meshes","volumetricFog"};
static const char* kFloatNames[] = {
    "drawDistance","proceduralCloudRTScale"};

static int g_fail = 0;
static void Chk(int cond, const char* what) {
    printf("  [%s] %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) ++g_fail;
}

static const uint8_t* Ptr(const scan::PeImage& img, uintptr_t rva) {
    return img.data + rva;
}

static bool IntSiteValid(const scan::PeImage& img, uintptr_t ret_rva, uintptr_t callee) {
    if (ret_rva < 5) return false;
    const uintptr_t call_va = img.base + ret_rva - 5;
    const uint8_t* p = Ptr(img, ret_rva - 5);
    if (p[0] != 0xE8) return false;
    int32_t rel = 0;
    memcpy(&rel, p + 1, 4);
    return (uintptr_t)(call_va + 5 + (intptr_t)rel) == callee;
}

static bool BoolSiteValid(const scan::PeImage& img, uintptr_t call_rva, uintptr_t callee) {
    const uint8_t* p = Ptr(img, call_rva);
    if (p[0] != 0xE8) return false;
    int32_t rel = 0;
    memcpy(&rel, p + 1, 4);
    return (uintptr_t)(img.base + call_rva + 5 + (intptr_t)rel) == callee;
}

// Float call sites target the thunk, not the core that the mod detours.
static bool FloatSiteValid(const scan::PeImage& img, uintptr_t ret_rva, uintptr_t callee) {
    return ret_rva >= 5 && IntSiteValid(img, ret_rva, callee);
}

static bool Unique(const scan::PeImage& img, const char* sig, uintptr_t expected_va) {
    uintptr_t hits[8];
    const int n = scan::FindPatternAll(img, sig, hits, 8);
    return n == 1 && hits[0] == expected_va;
}

// Synthetic check: a wildcard pattern must ignore the stack-frame size while
// still rejecting a changed instruction. Runs without the game installed.
static bool MatchAtSelfTest() {
    const char* shape =
        "48 89 5C 24 08 48 89 7C 24 10 55 48 8B EC 48 83 EC ??";
    const uint8_t pro[14] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,
                             0x7C,0x24,0x10,0x55,0x48,0x8B,0xEC};
    uint8_t buf[24] = {0};
    memcpy(buf, pro, sizeof(pro));
    buf[14] = 0x48; buf[15] = 0x83; buf[16] = 0xEC; buf[17] = 0x90;

    scan::PeImage img;
    img.data = buf;
    img.base = 0x10000000;
    img.size = sizeof(buf);

    bool ok = true;
    ok &= (scan::PatternLength(shape) == 18);
    ok &= scan::MatchAt(img, img.base, shape);
    ok &= scan::MatchAt(img, img.base,
                        "48 89 5C 24 08 48 89 7C 24 10 55 48 8B EC");
    buf[3] = 0x25;  // corrupt a fixed byte
    ok &= !scan::MatchAt(img, img.base, shape);
    return ok;
}

// AttachLive maps the current process; it must report the real SizeOfImage.
static bool AttachLiveSelfTest() {
    scan::PeImage img;
    if (!scan::AttachLive(&img)) return false;
    const uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    return img.data && img.base == base && img.size > 0x1000 &&
           scan::MatchAt(img, base, "4D 5A");  // MZ
}

int main(int argc, char** argv) {
    Chk(MatchAtSelfTest(),
        "MatchAt wildcards the frame size and still pins the shape");
    Chk(AttachLiveSelfTest(), "AttachLive maps the live module");

    const wchar_t* path = L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\F1 25\\F1_25.exe";
    wchar_t wbuf[1024];
    if (argc > 1) {
        MultiByteToWideChar(CP_ACP, 0, argv[1], -1, wbuf, 1024);
        path = wbuf;
    }

    scan::PeImage img;
    if (!scan::LoadFile(path, &img)) {
        printf("SKIP: cannot load the game image (not installed here)\n");
        return g_fail ? 1 : 0;  // absent game is not a failure
    }
    printf("image: %zu bytes, base %#llx, timestamp %#x\n",
           img.size, (unsigned long long)img.base, img.timestamp);
    if (img.base != kBase) {
        printf("NOTE: image base %#llx != expected %#llx (ASLR build?) - skipping table checks\n",
               (unsigned long long)img.base, (unsigned long long)kBase);
        scan::Free(&img);
        return 0;
    }

    scan::LeaIndex idx;
    if (!scan::LeaBuild(img, &idx)) {
        printf("FAIL: could not build the LEA index\n");
        return 1;
    }
    printf("lea index: %d entries\n", idx.count);

    const int nInt = (int)(sizeof(kIntNames) / sizeof(*kIntNames));
    const int nBool = (int)(sizeof(kBoolNames) / sizeof(*kBoolNames));
    const int nFloat = (int)(sizeof(kFloatNames) / sizeof(*kFloatNames));
    int si = 0, sb = 0, sf = 0, nci = 0, ncb = 0, ncf = 0;
    const uintptr_t parseInt = scan::ResolveParser(
        img, idx, kSigParseInt, kIntNames, nInt, img.base + kRvaParseInt, &si, &nci);
    const uintptr_t parseBool = scan::ResolveParser(
        img, idx, kSigParseBool, kBoolNames, nBool, img.base + kRvaParseBool, &sb, &ncb);
    const uintptr_t parseFloat = scan::ResolveParser(
        img, idx, kSigParseFloat, kFloatNames, nFloat,
        img.base + kRvaParseFloatThunk, &sf, &ncf);
    printf("parseInt : %#llx (score %d/%d, %d candidates)\n",
           (unsigned long long)(parseInt - img.base), si, nInt, nci);
    printf("parseBool: %#llx (score %d/%d, %d candidates)\n",
           (unsigned long long)(parseBool - img.base), sb, nBool, ncb);

    printf("parseFloat: %#llx (score %d/%d, %d candidates)\n",
           (unsigned long long)(parseFloat - img.base), sf, nFloat, ncf);

    Chk(parseInt == img.base + kRvaParseInt, "int parser resolved by signature");
    Chk(parseBool == img.base + kRvaParseBool, "bool parser resolved by signature");
    Chk(parseFloat == img.base + kRvaParseFloatThunk,
        "float parser thunk resolved by signature");
    {
        int32_t rel = 0;
        memcpy(&rel, Ptr(img, kRvaParseFloatThunk + 4), 4);
        Chk(parseFloat && (parseFloat + 8 + (intptr_t)rel) == img.base + kRvaParseFloat,
            "float thunk points at the known core entry");
    }
    Chk(Unique(img, kSigApply, img.base + kRvaApply), "apply signature unique at known RVA");
    Chk(Unique(img, kSigApplyNode, img.base + kRvaApplyNode), "applyNode signature unique at known RVA");
    Chk(Unique(img, kSigBoolGet, img.base + 0x3a43b90), "bool getter signature unique");
    Chk(Unique(img, kSigScalarGet, img.base + 0x3a43240), "scalar getter signature unique");

    int okInt = 0, discInt = 0, okBool = 0, discBool = 0, okFloat = 0, discFloat = 0;
    const int nIntTab = (int)(sizeof(kInt) / sizeof(*kInt));
    const int nBoolTab = (int)(sizeof(kBool) / sizeof(*kBool));
    const int nFloatTab = (int)(sizeof(kFloat) / sizeof(*kFloat));
    for (int k = 0; k < nIntTab; ++k) {
        if (IntSiteValid(img, kInt[k].ret_rva, parseInt)) ++okInt;
        uintptr_t s[8];
        const int n = scan::FindSites(img, idx, kInt[k].key, parseInt, 0x100, false, s, 8);
        for (int i = 0; i < n; ++i) {
            if (s[i] == img.base + kInt[k].ret_rva) { ++discInt; break; }
        }
    }
    for (int k = 0; k < nBoolTab; ++k) {
        if (BoolSiteValid(img, kBool[k].call_rva, parseBool)) ++okBool;
        uintptr_t s[16];
        const int n = scan::FindSites(img, idx, kBool[k].key, parseBool, 0x100, false, s, 16);
        for (int i = 0; i < n; ++i) {
            if (s[i] == img.base + kBool[k].call_rva + 5) { ++discBool; break; }
        }
    }
    for (int k = 0; k < nFloatTab; ++k) {
        if (FloatSiteValid(img, kFloat[k].ret_rva, parseFloat)) ++okFloat;
        uintptr_t s[8];
        const int n = scan::FindSites(img, idx, kFloat[k].key, parseFloat, 0x100, false, s, 8);
        for (int i = 0; i < n; ++i) {
            if (s[i] == img.base + kFloat[k].ret_rva) { ++discFloat; break; }
        }
    }
    printf("int sites : %d/%d validated, %d/%d rediscovered by name\n",
           okInt, nIntTab, discInt, nIntTab);
    printf("bool sites: %d/%d validated, %d/%d rediscovered by name\n",
           okBool, nBoolTab, discBool, nBoolTab);
    printf("float sites: %d/%d validated, %d/%d rediscovered by name\n",
           okFloat, nFloatTab, discFloat, nFloatTab);
    Chk(okInt == nIntTab, "all int table sites validate");
    Chk(okBool == nBoolTab, "all bool table sites validate");
    Chk(okFloat == nFloatTab, "all float table sites validate");
    Chk(discInt == nIntTab, "all int sites rediscovered by name");
    Chk(discBool == nBoolTab, "all bool sites rediscovered by name");
    Chk(discFloat == nFloatTab, "all float sites rediscovered by name");

    scan::LeaFree(&idx);
    scan::Free(&img);
    if (g_fail) printf("\nSCANTEST FAILED (%d)\n", g_fail);
    else printf("\nSCANTEST PASSED\n");
    return g_fail ? 1 : 0;
}
