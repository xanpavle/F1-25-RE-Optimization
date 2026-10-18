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
#include "hook.h"
#include "tune.h"

// --- F1_25.exe (image base 0x140000000, no ASLR) -----------------------------
// FUN_143a43b90: bool attribute getter  (node, name, uint8_t* out, uint8_t def)
// FUN_143a43240: 32-bit attribute getter (node, name, uint32_t* out, uint32_t def)
static const uintptr_t kRvaBoolGet   = 0x03a43b90u;
static const uintptr_t kRvaScalarGet = 0x03a43240u;

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

static bool PrologueMatches(void* fn, const uint8_t* expected) {
    return memcmp(fn, expected, (size_t)kPatchLen) == 0;
}

static void InstallHooks() {
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    void* pb = (void*)(base + kRvaBoolGet);
    void* ps = (void*)(base + kRvaScalarGet);

    // Expected 15-byte prologues (verified against F1_25.exe 1.0.0).
    static const uint8_t kBoolSig[15]   = {0x41,0x56,0x56,0x57,0x53,0x48,0x83,0xEC,
                                           0x38,0x44,0x89,0xCB,0x4C,0x89,0xC7};
    static const uint8_t kScalarSig[15] = {0x41,0x56,0x56,0x57,0x53,0x48,0x83,0xEC,
                                           0x38,0x44,0x89,0xCF,0x4C,0x89,0xC3};

    if (!PrologueMatches(pb, kBoolSig)) {
        tune::Note("WARN: bool getter signature mismatch - not patching");
    } else {
        g_orig_bool = (bool_get_t)hook::Install(pb, (void*)DetourBool, kPatchLen);
        if (!g_orig_bool) tune::Note("WARN: bool getter hook FAILED");
    }

    if (!PrologueMatches(ps, kScalarSig)) {
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
        InstallHooks();
    }
    return TRUE;
}
