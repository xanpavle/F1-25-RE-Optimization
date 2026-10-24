// F1 25 renderer mod â€” proxy dinput8.dll.
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
#include <stdio.h>
#include "hook.h"
#include "scan.h"
#include "tune.h"
#include "enforce.h"
#include "version.h"

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

// --- game build check -------------------------------------------------------
// The hard-coded RVAs above and in enforce.cpp were taken from this build
// (Steam 1.0.141.7865, SHA-256 d873a1c8...). Its PE header identifies it
// without hashing the 400 MB file.
static const uint32_t kKnownTimestamp   = 0x6AA3C898u;
static const uint32_t kKnownSizeOfImage = 0x193E6000u;

static bool IsKnownBuild(uint32_t* ts, uint32_t* size) {
    const BYTE* base = (const BYTE*)GetModuleHandleW(nullptr);
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    *ts = nt->FileHeader.TimeDateStamp;
    *size = nt->OptionalHeader.SizeOfImage;
    return *ts == kKnownTimestamp && *size == kKnownSizeOfImage;
}

// --- launch status banner ---------------------------------------------------
// A small topmost, click-through, non-activating window in the top-left corner
// for a few seconds at launch. It never takes focus and draws nothing into the
// game itself; in exclusive fullscreen it may simply not be visible.
static char     g_status[160];
static COLORREF g_status_color;
static const UINT kStatusMs = 8000;

static LRESULT CALLBACK StatusProc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT rc;
        GetClientRect(w, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(16, 16, 16));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        RECT bar = {0, 0, 6, rc.bottom};
        HBRUSH accent = CreateSolidBrush(g_status_color);
        FillRect(dc, &bar, accent);
        DeleteObject(accent);
        HFONT f = CreateFontA(20, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, "Segoe UI");
        HGDIOBJ old = SelectObject(dc, f);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(240, 240, 240));
        rc.left += 18;
        DrawTextA(dc, g_status, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        SelectObject(dc, old);
        DeleteObject(f);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_TIMER:
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, m, wp, lp);
}

static DWORD WINAPI StatusThread(LPVOID) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSA wc = {};
    wc.lpfnWndProc = StatusProc;
    wc.hInstance = inst;
    wc.lpszClassName = "F125TuneStatus";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassA(&wc);
    HWND w = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                                 WS_EX_LAYERED | WS_EX_TRANSPARENT,
                             wc.lpszClassName, "", WS_POPUP, 24, 24, 820, 44, nullptr,
                             nullptr, inst, nullptr);
    if (!w) return 0;
    SetLayeredWindowAttributes(w, 0, 230, LWA_ALPHA);
    ShowWindow(w, SW_SHOWNOACTIVATE);
    SetTimer(w, 1, kStatusMs, nullptr);
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) DispatchMessageA(&msg);
    UnregisterClassA(wc.lpszClassName, inst);
    return 0;
}

static void ShowStatus(const char* text, COLORREF color) {
    tune::Note(text);
    if (!tune::StatusPopup()) return;
    strncpy(g_status, text, sizeof(g_status) - 1);
    g_status_color = color;
    // A window cannot be created under the loader lock; use a thread.
    HANDLE t = CreateThread(nullptr, 0, StatusThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
}

// --- A/B hotkey ---------------------------------------------------------------
// Ctrl+F10 (only while the game window is in front) flips the A/B switch file.
// Almost all of the mod's effect is loaded once at launch, so the switch takes
// effect on the next launch; the banner says so.
static DWORD WINAPI HotkeyThread(LPVOID) {
    bool was_down = false;
    for (;;) {
        Sleep(50);
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        const bool down = pid == GetCurrentProcessId() &&
                          (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                          (GetAsyncKeyState(VK_F10) & 0x8000);
        if (down && !was_down) {
            const bool to_stock = !tune::StockMode();
            if (tune::SetStockMode(to_stock)) {
                ShowStatus(to_stock ? "A/B: STOCK settings on next launch (Ctrl+F10 to undo)"
                                    : "A/B: MOD settings on next launch (Ctrl+F10 to undo)",
                           to_stock ? RGB(235, 170, 30) : RGB(40, 200, 90));
            } else {
                ShowStatus("A/B: could not write the switch file - see f125_tune.log",
                           RGB(220, 40, 40));
            }
        }
        was_down = down;
    }
}

// Swofty Launcher installs its loader (version.dll) and swofty\settings.json
// into the game folder for a modded session. It ships its own port of this mod,
// and it times its addon loading on the game's first window, so running this
// DLL beside it (our banner is a window) loads its addons too early and crashes
// career loading. Step aside completely: no hooks, no banner, no profile write.
static bool SwoftyActive() {
    const wchar_t* dir = tune::ExeDir();
    if (!dir || !dir[0]) return false;
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%s\\swofty\\settings.json", dir);
    path[MAX_PATH - 1] = 0;
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES ||
           GetModuleHandleW(L"swofty_core.dll") != nullptr;
}

static void Startup() {
    if (SwoftyActive()) {
        tune::Note("Swofty Launcher session detected - standalone mod disabled for this launch "
                   "(use the RE-Optimization addon in Swofty Launcher instead)");
        return;
    }

    HANDLE hk = CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, nullptr);
    if (hk) CloseHandle(hk);

    if (tune::StockMode()) {
        tune::ApplyProfile();  // restores the untouched settings in stock mode
        ShowStatus(F125_MOD_NAME " v" F125_MOD_VERSION
                   " A/B: running STOCK - Ctrl+F10 for mod on next launch",
                   RGB(235, 170, 30));
        return;
    }

    uint32_t ts = 0, size = 0;
    const bool known = IsKnownBuild(&ts, &size);
    char line[192];
    _snprintf(line, sizeof(line),
              known ? "build: F1_25.exe 1.0.141.7865 verified (timestamp %08X, image %08X)"
                    : "build: UNKNOWN F1_25.exe (timestamp %08X, image %08X) - "
                      "hard-coded addresses not trusted",
              ts, size);
    line[sizeof(line) - 1] = 0;
    tune::Note(line);

    if (!known && tune::UnknownBuildOff()) {
        ShowStatus(F125_MOD_NAME " v" F125_MOD_VERSION
                   " DISABLED - unsupported game version, game runs stock",
                   RGB(220, 40, 40));
        return;
    }
    tune::SetUnknownBuild(!known);

    const bool profile = tune::ApplyProfile();
    enforce::Init();
    InstallHooks();

    const bool hooks = g_orig_bool && g_orig_scalar;
    if (hooks && enforce::Active() && profile) {
        char msg[160];
        _snprintf(msg, sizeof(msg), "%s v%s loaded successfully - %s preset%s",
                  F125_MOD_NAME, F125_MOD_VERSION,
                  profile::PresetName(tune::GetPreset()),
                  known ? "" : " (game updated - addresses re-derived)");
        msg[sizeof(msg) - 1] = 0;
        ShowStatus(msg, RGB(40, 200, 90));
    } else if (hooks || enforce::Active() || profile) {
        ShowStatus(F125_MOD_NAME " v" F125_MOD_VERSION
                   " loaded with warnings - see f125_tune.log",
                   RGB(235, 170, 30));
    } else {
        ShowStatus(F125_MOD_NAME " v" F125_MOD_VERSION
                   " ERROR - nothing applied, see f125_tune.log",
                   RGB(220, 40, 40));
    }
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
        Startup();
    }
    return TRUE;
}
