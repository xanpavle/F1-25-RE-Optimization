// F1 25 renderer mod — proxy dinput8.dll.
//
// The game imports exactly one symbol from dinput8.dll (DirectInput8Create), so
// we can transparently proxy the system DLL and run our own code at process
// start, before any of the game's static initialisers run.
//
// We detour the engine's two generic settings accessors and substitute values
// for settings named in f125_tune.ini. Originals are called first, so all normal
// engine side effects are preserved; we only overwrite the returned value.
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "hook.h"
#include "scan.h"
#include "tune.h"
#include "enforce.h"

// --- F1_25.exe (image base 0x140000000, no ASLR) -----------------------------
// FUN_143a43b90: bool attribute getter  (node, name, uint8_t* out, uint8_t def)
// FUN_143a43240: 32-bit attribute getter (node, name, uint32_t* out, uint32_t def)
static const uintptr_t kRvaBoolGet   = 0x03a43b90u;
static const uintptr_t kRvaScalarGet = 0x03a43240u;

// Signatures so the getters can be re-derived on an updated game build.
static const char* kSigBoolGet =
    "41 56 56 57 53 48 83 EC ?? 44 89 CB 4C 89 C7 49 89 CE 48 89 54 24 30 "
    "48 8D 71 10 48 89 F1 E8 ?? ?? ?? ??";
static const char* kSigScalarGet =
    "41 56 56 57 53 48 83 EC ?? 44 89 CF 4C 89 C3 49 89 CE 48 89 54 24 30 "
    "48 8D 71 10 48 89 F1 E8 ?? ?? ?? ??";

// Shared prologue (15 bytes): push r14; push rsi; push rdi; push rbx;
// sub rsp,0x38; mov ebx,r9d; mov rdi,r8  -- all whole instructions, no
// RIP-relative operands and no branches, so it can be relocated safely.
static const int kPatchLen = 15;

typedef uint64_t (*bool_get_t)(void*, const char*, uint8_t*, uint8_t);
typedef uint64_t (*scalar_get_t)(void*, const char*, uint32_t*, uint32_t);

static bool_get_t   g_orig_bool = nullptr;
static scalar_get_t g_orig_scalar = nullptr;

static uint64_t DetourBool(void* node, const char* name, uint8_t* out, uint8_t def) {
    uint64_t r = g_orig_bool(node, name, out, def);
    if (out) {
        uint8_t v;
        if (tune::LookupBool(name, &v)) *out = v;
        tune::LogRead(node, name, *out ? 1u : 0u, 1);
    }
    return r;
}

static uint64_t DetourScalar(void* node, const char* name, uint32_t* out, uint32_t def) {
    uint64_t r = g_orig_scalar(node, name, out, def);
    if (out) {
        uint32_t v;
        if (tune::LookupScalar(name, &v)) *out = v;
        tune::LogRead(node, name, *out, 0);
    }
    return r;
}

// Wildcard prologue match (`?` = any byte), so a changed stack-frame size does
// not invalidate an otherwise identical function.
static bool PrologueMatches(void* fn, const char* pattern) {
    scan::PeImage img;
    if (!scan::AttachLive(&img)) return false;
    return scan::MatchAt(img, (uintptr_t)fn, pattern);
}

// Fast path: the known RVA with a matching prologue. Fallback: re-derive the
// function by signature and re-check its prologue.
static void* ResolveGetter(uintptr_t rva, const char* sig, const char* pro) {
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    if (!tune::ForceScan() && PrologueMatches((void*)(base + rva), pro))
        return (void*)(base + rva);

    scan::PeImage img;
    if (!scan::AttachLive(&img)) return nullptr;
    const uintptr_t va = scan::FindPattern(img, sig);
    if (va && PrologueMatches((void*)va, pro)) {
        tune::Note(tune::ForceScan()
                       ? "hooks: getter re-derived by scan (force-scan)"
                       : "hooks: getter re-derived by signature (game updated?)");
        return (void*)va;
    }
    return nullptr;
}

static void InstallHooks() {
    // 15-byte prologues; the stack-frame immediate is wildcarded.
    static const char* kBoolPro =
        "41 56 56 57 53 48 83 EC ?? 44 89 CB 4C 89 C7";
    static const char* kScalarPro =
        "41 56 56 57 53 48 83 EC ?? 44 89 CF 4C 89 C3";

    void* pb = ResolveGetter(kRvaBoolGet, kSigBoolGet, kBoolPro);
    void* ps = ResolveGetter(kRvaScalarGet, kSigScalarGet, kScalarPro);

    if (!pb) {
        tune::Note("WARN: bool getter signature mismatch - not patching");
    } else {
        g_orig_bool = (bool_get_t)hook::Install(pb, (void*)DetourBool, kPatchLen);
        if (!g_orig_bool) tune::Note("WARN: bool getter hook FAILED");
    }

    if (!ps) {
        tune::Note("WARN: scalar getter signature mismatch - not patching");
    } else {
        g_orig_scalar = (scalar_get_t)hook::Install(ps, (void*)DetourScalar, kPatchLen);
        if (!g_orig_scalar) tune::Note("WARN: scalar getter hook FAILED");
    }

    if (g_orig_bool && g_orig_scalar) tune::Note("hooks installed OK");
}

// --- dinput8 proxy ----------------------------------------------------------
typedef HRESULT(WINAPI* DirectInput8Create_t)(HINSTANCE, DWORD, const GUID&, void**,
                                              void*);
static HMODULE             g_realDll = nullptr;
static DirectInput8Create_t g_realCreate = nullptr;

static void EnsureRealDll() {
    if (g_realCreate) return;
    wchar_t path[MAX_PATH] = {0};
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 16) return;
    wcscat(path, L"\\dinput8.dll");
    g_realDll = LoadLibraryW(path);
    if (g_realDll) {
        g_realCreate =
            (DirectInput8Create_t)GetProcAddress(g_realDll, "DirectInput8Create");
    }
}

extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst, DWORD version, const GUID& riid, void** out, void* outer) {
    EnsureRealDll();
    if (!g_realCreate) return (HRESULT)0x80004005L;  // E_FAIL
    return g_realCreate(hinst, version, riid, out, outer);
}

BOOL WINAPI DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        tune::SetSelf(hMod);
        tune::Init();
        tune::ApplyProfile();
        enforce::Init();
        InstallHooks();
    }
    return TRUE;
}
