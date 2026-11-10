#include "tune.h"
#include "profile.h"
#include "gpu.h"
#include "sys.h"
#include "version.h"
#include <windows.h>
#include <shlobj.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

namespace tune {
namespace {

// ---------------------------------------------------------------------------
// [override] table: engine attributes replaced through the accessor hooks.
// ---------------------------------------------------------------------------
struct Entry {
    char     name[96];
    uint32_t u32;
    int      is_float;
};

const int kMaxEntries = 512;
Entry     g_entries[kMaxEntries];
int       g_entry_count = 0;

const int kIndexSize = 1024;         // power of two
int       g_index[kIndexSize];       // -1 empty, else entry index

// ---------------------------------------------------------------------------
// [enforce] table: per-attribute overrides of the in-engine enforcement table.
// ---------------------------------------------------------------------------
struct EnfOv {
    char    name[96];
    int32_t value;
    int     disable;
    int     is_float;   // value holds IEEE-754 bits (ini used a decimal literal)
};
const int kMaxEnfOv = 256;
EnfOv     g_enfov[kMaxEnfOv];
int       g_enfov_count = 0;

enum EnfMode { kEnfOff = 0, kEnfCheaper, kEnfTarget };

EnfMode   g_enf_mode        = kEnfCheaper;
bool      g_enforce_enabled = true;
bool      g_enforce_bools   = true;
bool      g_log_all         = false;
bool      g_log_report      = false;
char      g_log_prefix[64]  = {0};
bool      g_profile_apply   = false;
bool      g_want_guard      = true;
char      g_upscaler_mode[16] = "auto";
bool      g_unknown_build_off = false;
profile::Preset g_preset    = profile::Preset::Balanced;
bool      g_preset_recommended = true;
char      g_gpu_seen[512] = "";
int       g_res_w = 0, g_res_h = 0;
sys::Cpu  g_cpu             = {};
bool      g_weak_cpu        = false;
const char* g_weak_why      = nullptr;
bool      g_crash_guard     = true;
gpu::Info g_gpu             = {};
bool      g_gpu_ready       = false;
bool      g_header_done     = false;

// Per-preset defaults for the in-engine enforcement table. An explicit
// [enforce] line in the ini always wins over these.
struct PresetEnf {
    const char* name;
    float       value;
};
const PresetEnf kQualityEnf[] = {
    {"worldShadowMapSize", 4096}, {"nightShadowMapSize", 4096},
    {"skyShadowMapSize", 2048},   {"spotShadowMapSize", 2048},
    {"spotShadowSampling", 3},    {"shadowSampling", 3},
    {"nightShadowSourceCount", 4},
    {"volumetricFogMapWidth", 960}, {"volumetricFogMapHeight", 540},
    {"billboardDistance", 4000},
    {"groundCover.drawDistance", 120.0f},
};
const PresetEnf kPotatoEnf[] = {
    {"worldShadowMapSize", 1024}, {"nightShadowMapSize", 1024},
    // skyShadowCascadeCount stays at the preset value: forcing it below 4
    // in-engine makes the game quit at launch (other code still expects 4).
    {"skyShadowMapSize", 1024},
    {"spotShadowMapSize", 512},   {"spotShadowSampling", 0},
    {"shadowSampling", 0},        {"nightShadowSourceCount", 1},
    {"volumetricFogMapWidth", 256}, {"volumetricFogMapHeight", 128},
    {"billboardDistance", 1000},  {"textureStreamingMiB", 1024},
    {"groundCover.drawDistance", 40.0f},
    // 0.3.2: was 4.0 (the Ultra value), which made Potato heavier than Balanced.
    {"proceduralCloudRTScale", 1.0f},
};

const PresetEnf* FindPresetEnf(const char* name) {
    const PresetEnf* t = nullptr;
    int n = 0;
    if (g_preset == profile::Preset::Quality) {
        t = kQualityEnf;
        n = (int)(sizeof(kQualityEnf) / sizeof(kQualityEnf[0]));
    } else if (g_preset == profile::Preset::Potato) {
        t = kPotatoEnf;
        n = (int)(sizeof(kPotatoEnf) / sizeof(kPotatoEnf[0]));
    }
    for (int i = 0; i < n; ++i) {
        if (strcmp(t[i].name, name) == 0) return &t[i];
    }
    return nullptr;
}
bool      g_status_popup    = true;

HMODULE   g_self = nullptr;
wchar_t   g_exe_dir[MAX_PATH] = {0};
wchar_t   g_ini_path[MAX_PATH] = {0};
CRITICAL_SECTION g_cs;
bool      g_cs_ready = false;

// (node, name) pairs seen, with the last value written to the log. Indexed with
// a simple open-addressed hash so discovery mode is not quadratic.
struct Seen {
    const void* node;
    char        name[96];
    uint32_t    last;
    int         used;
    int         next;     // chain within the bucket
};
const int kMaxSeen = 16384;
Seen      g_seen[kMaxSeen];
int       g_seen_count = 0;
const int kSeenBuckets = 4096;
int       g_seen_head[kSeenBuckets];

wchar_t   g_log_path[MAX_PATH] = {0};
HANDLE    g_log = INVALID_HANDLE_VALUE;

uint32_t HashStr(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

uint32_t HashSeen(const void* node, const char* name) {
    uint32_t h = (uint32_t)((uintptr_t)node >> 4) ^ 2166136261u;
    while (*name) {
        h ^= (uint8_t)*name++;
        h *= 16777619u;
    }
    return h;
}

void Trim(char* s) {
    char* p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' ||
                     s[n - 1] == '\n')) {
        s[--n] = 0;
    }
}

// Cuts an inline `; comment` / `# comment`. Our values never contain them.
void StripComment(char* s) {
    for (char* p = s; *p; ++p) {
        if (*p == ';' || *p == '#') { *p = 0; break; }
    }
    Trim(s);
}

bool ParseBoolStr(const char* s, bool* out) {
    if (!_stricmp(s, "1") || !_stricmp(s, "true") || !_stricmp(s, "on") ||
        !_stricmp(s, "yes")) {
        *out = true;
        return true;
    }
    if (!_stricmp(s, "0") || !_stricmp(s, "false") || !_stricmp(s, "off") ||
        !_stricmp(s, "no")) {
        *out = false;
        return true;
    }
    return false;
}

bool ReadFileAll(const wchar_t* path, char* out, size_t cap) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD got = 0;
    BOOL ok = ReadFile(h, out, (DWORD)(cap - 1), &got, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    out[got] = 0;
    return true;
}

bool WriteFileAllNew(const wchar_t* path, const char* data, size_t n) {
    // CREATE_NEW: never clobber an existing backup.
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, data, (DWORD)n, &wrote, nullptr);
    if (ok) FlushFileBuffers(h);
    CloseHandle(h);
    return ok && wrote == n;
}

// Writes to a sibling .tmp and atomically replaces the target, so a crash or a
// racing writer can never leave a truncated settings file behind.
bool WriteFileAtomic(const wchar_t* path, const char* data, size_t n) {
    wchar_t tmp[MAX_PATH] = {0};
    _snwprintf(tmp, MAX_PATH, L"%s.tmp", path);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, data, (DWORD)n, &wrote, nullptr);
    if (ok) FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || wrote != n) {
        DeleteFileW(tmp);
        return false;
    }
    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp);
        return false;
    }
    return true;
}

bool ParseFloat(const char* s, uint32_t* bits) {
    char* end = nullptr;
    float f = (float)strtod(s, &end);
    if (end == s) return false;
    memcpy(bits, &f, 4);
    return true;
}

bool ParseInt(const char* s, uint32_t* out) {
    char* end = nullptr;
    long v = strtol(s, &end, 0);
    if (end == s) return false;
    *out = (uint32_t)v;
    return true;
}

// Decides whether a value string is a float. A hex literal (0x1e) must not be
// mistaken for one just because it contains an 'e'.
bool LooksFloat(const char* v) {
    if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) return false;
    return strchr(v, '.') != nullptr || strchr(v, 'e') != nullptr ||
           strchr(v, 'E') != nullptr;
}

bool ParseValue(const char* value, uint32_t* out, int* is_float) {
    char v[64];
    strncpy(v, value, sizeof(v) - 1);
    v[sizeof(v) - 1] = 0;
    Trim(v);
    {
        bool b = false;
        if (ParseBoolStr(v, &b)) {
            *out = b ? 1u : 0u;
            *is_float = 0;
            return true;
        }
    }
    if (LooksFloat(v)) {
        if (!ParseFloat(v, out)) return false;
        *is_float = 1;
        return true;
    }
    if (!ParseInt(v, out)) return false;
    *is_float = 0;
    return true;
}

void AddEntry(const char* name, const char* value) {
    if (g_entry_count >= kMaxEntries) return;
    Entry e;
    strncpy(e.name, name, sizeof(e.name) - 1);
    e.name[sizeof(e.name) - 1] = 0;
    if (!ParseValue(value, &e.u32, &e.is_float)) return;
    g_entries[g_entry_count++] = e;
}

void AddEnfOv(const char* name, const char* value) {
    if (g_enfov_count >= kMaxEnfOv) return;
    EnfOv e;
    strncpy(e.name, name, sizeof(e.name) - 1);
    e.name[sizeof(e.name) - 1] = 0;
    e.disable = 0;
    e.value = 0;
    e.is_float = 0;
    if (!_stricmp(value, "off") || !_stricmp(value, "disable") ||
        !_stricmp(value, "disabled") || !_stricmp(value, "none") ||
        !_stricmp(value, "default") || !strcmp(value, "-")) {
        e.disable = 1;
        g_enfov[g_enfov_count++] = e;
        return;
    }
    uint32_t raw = 0;
    int is_float = 0;
    if (!ParseValue(value, &raw, &is_float)) return;
    e.value = (int32_t)raw;
    e.is_float = is_float;
    g_enfov[g_enfov_count++] = e;
}

void BuildIndex() {
    for (int i = 0; i < kIndexSize; ++i) g_index[i] = -1;
    for (int i = 0; i < g_entry_count; ++i) {
        uint32_t h = HashStr(g_entries[i].name) & (kIndexSize - 1);
        while (g_index[h] != -1) h = (h + 1) & (kIndexSize - 1);
        g_index[h] = i;
    }
}

const Entry* Find(const char* name) {
    if (!name || g_entry_count == 0) return nullptr;
    uint32_t h = HashStr(name) & (kIndexSize - 1);
    while (g_index[h] != -1) {
        const Entry& e = g_entries[g_index[h]];
        if (strcmp(e.name, name) == 0) return &e;
        h = (h + 1) & (kIndexSize - 1);
    }
    return nullptr;
}

void ParseIni(const char* text) {
    enum { kNone = 0, kLog, kOverride, kProfile, kUpscaler, kEnforce };
    int section = kNone;
    char line[512];
    const char* p = text;

    while (*p) {
        size_t n = 0;
        while (*p && *p != '\n' && n < sizeof(line) - 1) line[n++] = *p++;
        if (*p == '\n') ++p;
        line[n] = 0;
        Trim(line);
        if (line[0] == 0 || line[0] == '#' || line[0] == ';') continue;

        if (line[0] == '[') {
            if (strstr(line, "log")) section = kLog;
            else if (strstr(line, "override")) section = kOverride;
            else if (strstr(line, "profile")) section = kProfile;
            else if (strstr(line, "upscaler")) section = kUpscaler;
            else if (strstr(line, "enforce")) section = kEnforce;
            else section = kNone;
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = line;
        char* val = eq + 1;
        Trim(key);
        StripComment(val);

        if (section == kLog) {
            if (!_stricmp(key, "all")) {
                bool b = false;
                if (ParseBoolStr(val, &b)) g_log_all = b; else g_log_all = atoi(val) != 0;
            } else if (!_stricmp(key, "report")) {
                bool b = false;
                if (ParseBoolStr(val, &b)) g_log_report = b; else g_log_report = atoi(val) != 0;
            } else if (!_stricmp(key, "prefix")) {
                strncpy(g_log_prefix, val, sizeof(g_log_prefix) - 1);
                g_log_prefix[sizeof(g_log_prefix) - 1] = 0;
            }
        } else if (section == kOverride) {
            AddEntry(key, val);
        } else if (section == kProfile) {
            bool b = false;
            if (!_stricmp(key, "apply")) {
                if (ParseBoolStr(val, &b)) g_profile_apply = b; else g_profile_apply = atoi(val) != 0;
            } else if (!_stricmp(key, "enforce")) {
                if (ParseBoolStr(val, &b)) g_enforce_enabled = b; else g_enforce_enabled = atoi(val) != 0;
            } else if (!_stricmp(key, "guard")) {
                if (ParseBoolStr(val, &b)) g_want_guard = b; else g_want_guard = atoi(val) != 0;
            } else if (!_stricmp(key, "enforce_mode")) {
                if (!_stricmp(val, "off") || !_stricmp(val, "none")) g_enf_mode = kEnfOff;
                else if (!_stricmp(val, "cheaper")) g_enf_mode = kEnfCheaper;
                else g_enf_mode = kEnfTarget;
            } else if (!_stricmp(key, "preset")) {
                g_preset_recommended = !_stricmp(val, "recommended") || !_stricmp(val, "auto");
                if (!_stricmp(val, "quality")) g_preset = profile::Preset::Quality;
                else if (!_stricmp(val, "potato")) g_preset = profile::Preset::Potato;
                else g_preset = profile::Preset::Balanced;
            } else if (!_stricmp(key, "crash_safe_mode")) {
                if (ParseBoolStr(val, &b)) g_crash_guard = b; else g_crash_guard = atoi(val) != 0;
            } else if (!_stricmp(key, "unknown_build")) {
                g_unknown_build_off = !_stricmp(val, "off") || !_stricmp(val, "disable");
            } else if (!_stricmp(key, "status_popup")) {
                if (ParseBoolStr(val, &b)) g_status_popup = b; else g_status_popup = atoi(val) != 0;
            }
        } else if (section == kUpscaler) {
            if (!_stricmp(key, "mode")) {
                strncpy(g_upscaler_mode, val, sizeof(g_upscaler_mode) - 1);
                g_upscaler_mode[sizeof(g_upscaler_mode) - 1] = 0;
            }
        } else if (section == kEnforce) {
            if (!_stricmp(key, "bools")) {
                bool b = false;
                if (ParseBoolStr(val, &b)) g_enforce_bools = b; else g_enforce_bools = atoi(val) != 0;
            } else if (!_stricmp(key, "enabled")) {
                if (!_stricmp(val, "off")) g_enforce_enabled = false;
                else {
                    bool b = false;
                    if (ParseBoolStr(val, &b)) g_enforce_enabled = b; else g_enforce_enabled = atoi(val) != 0;
                }
            } else {
                AddEnfOv(key, val);
            }
        }
    }
}

void CloseLog() {
    if (g_log != INVALID_HANDLE_VALUE) {
        CloseHandle(g_log);
        g_log = INVALID_HANDLE_VALUE;
    }
}

void AppendLog(const char* line) {
    if (!g_log_path[0]) return;
    EnterCriticalSection(&g_cs);
    if (g_log == INVALID_HANDLE_VALUE) {
        g_log = CreateFileW(g_log_path, FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (g_log != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0;
        WriteFile(g_log, line, (DWORD)strlen(line), &wrote, nullptr);
    }
    LeaveCriticalSection(&g_cs);
}

// Start every launch with a fresh log, so the file always describes the current
// session instead of accumulating forever.
void ResetLog() {
    if (!g_log_path[0]) return;
    EnterCriticalSection(&g_cs);
    CloseLog();
    HANDLE h = CreateFileW(g_log_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    LeaveCriticalSection(&g_cs);
}

// The game's resolution from the player's settings file (0 x 0 when unknown).
void GameResolution(int* w, int* h) {
    *w = *h = 0;
    wchar_t docs[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs))) return;
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%s\\My Games\\F1 25\\hardwaresettings\\hardware_settings_config.xml", docs);
    static char buf[1 << 16];
    if (!ReadFileAll(path, buf, sizeof(buf))) return;
    char v[16];
    if (profile::ReadAttr(buf, "resolution", "width", v, sizeof(v))) *w = atoi(v);
    if (profile::ReadAttr(buf, "resolution", "height", v, sizeof(v))) *h = atoi(v);
}

const char* UpName(profile::Upscaler up) {
    switch (up) {
        case profile::Upscaler::Taa:  return "TAA";
        case profile::Upscaler::Dlss: return "DLSS";
        case profile::Upscaler::Fsr3: return "FSR 3";
        case profile::Upscaler::Xess: return "XeSS";
        case profile::Upscaler::Off:  return "off (no AA)";
        default:                      return "keep (unchanged)";
    }
}

profile::Upscaler ChooseUpscalerFor(const gpu::Info& g, const char* mode) {
    const char* m = (mode && *mode) ? mode : "auto";

    if (!_stricmp(m, "keep") || !_stricmp(m, "none")) return profile::Upscaler::Keep;
    if (!_stricmp(m, "off"))  return profile::Upscaler::Off;
    if (!_stricmp(m, "taa"))  return profile::Upscaler::Taa;
    if (!_stricmp(m, "dlss")) return profile::Upscaler::Dlss;
    if (!_stricmp(m, "fsr3") || !_stricmp(m, "fsr")) return profile::Upscaler::Fsr3;
    if (!_stricmp(m, "xess")) return profile::Upscaler::Xess;

    // 0.3.2: on integrated graphics an upscaler pass costs a large share of the
    // frame, so auto leaves the player's in-game choice alone.
    if (g.integrated) return profile::Upscaler::Keep;
    switch (g.vendor) {
        case gpu::VendorNvidia:
            return g.supportsDlss ? profile::Upscaler::Dlss : profile::Upscaler::Fsr3;
        case gpu::VendorAmd:
            return profile::Upscaler::Fsr3;
        case gpu::VendorIntel:
            return g.supportsXess ? profile::Upscaler::Xess : profile::Upscaler::Fsr3;
        default:
            return profile::Upscaler::Keep;
    }
}

profile::Upscaler ChooseUpscaler(const gpu::Info& g) {
    return ChooseUpscalerFor(g, g_upscaler_mode);
}

void SessionInfo() {
    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    HANDLE h = CreateFileW(exe, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER sz = {};
        GetFileSizeEx(h, &sz);
        CloseHandle(h);
        char line[320];
        _snprintf(line, sizeof(line), "session: exe=%ls (%lld bytes)",
                  exe, (long long)sz.QuadPart);
        line[sizeof(line) - 1] = 0;
        Note(line);
    }
    char line[320];
    _snprintf(line, sizeof(line), "session: ini=%ls", g_ini_path);
    line[sizeof(line) - 1] = 0;
    Note(line);
}

}  // namespace

void Init() {
    if (!g_cs_ready) {
        InitializeCriticalSection(&g_cs);
        g_cs_ready = true;
    }
    for (int i = 0; i < kIndexSize; ++i) g_index[i] = -1;
    for (int i = 0; i < kSeenBuckets; ++i) g_seen_head[i] = -1;

    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (slash) *slash = 0;  // exe directory
    wcsncpy(g_exe_dir, exe, MAX_PATH - 1);
    g_exe_dir[MAX_PATH - 1] = 0;

    wchar_t ini[MAX_PATH] = {0};
    _snwprintf(ini, MAX_PATH, L"%s\\f125_tune.ini", exe);

    char buf[65536] = {0};
    if (!ReadFileAll(ini, buf, sizeof(buf))) {
        wchar_t dll[MAX_PATH] = {0};
        if (g_self) GetModuleFileNameW(g_self, dll, MAX_PATH);
        wchar_t* ds = wcsrchr(dll, L'\\');
        if (ds) *ds = 0;
        _snwprintf(ini, MAX_PATH, L"%s\\f125_tune.ini", dll);
        if (!ReadFileAll(ini, buf, sizeof(buf))) {
            g_log_all = false;
            return;
        }
    }

    wcsncpy(g_ini_path, ini, MAX_PATH - 1);
    g_ini_path[MAX_PATH - 1] = 0;

    ParseIni(buf);
    BuildIndex();

    g_gpu = gpu::Detect(g_gpu_seen, sizeof(g_gpu_seen));
    g_gpu_ready = true;
    g_cpu = sys::ReadCpu();
    g_weak_cpu = sys::WeakCpu(g_cpu.name, g_cpu.cores, g_cpu.threads, &g_weak_why);
    if (g_preset_recommended) {
        GameResolution(&g_res_w, &g_res_h);
        g_preset = gpu::RecommendAt(g_gpu, g_res_w, g_res_h, g_weak_cpu);
    }

    _snwprintf(g_log_path, MAX_PATH, L"%s\\f125_tune.log", exe);
    ResetLog();
    Note(F125_MOD_NAME " v" F125_MOD_VERSION);
    SessionInfo();
    {
        char line[320];
        _snprintf(line, sizeof(line), "CPU: %s, %d cores / %d threads%s%s%s",
                  g_cpu.name[0] ? g_cpu.name : "unknown", g_cpu.cores, g_cpu.threads,
                  g_weak_cpu ? "; weak-CPU guard on (" : "", g_weak_cpu ? g_weak_why : "",
                  g_weak_cpu ? "): CPU-side cuts on any preset, no 1440p step-down" : "");
        line[sizeof(line) - 1] = 0;
        Note(line);
        const sys::Power pw = sys::ReadPower();
        char pt[96];
        sys::PowerText(pw, pt, sizeof(pt));
        _snprintf(line, sizeof(line), "power: %s", pt);
        line[sizeof(line) - 1] = 0;
        Note(line);
        if (const char* pn = sys::PowerNote(pw)) {
            _snprintf(line, sizeof(line), "note: %s", pn);
            line[sizeof(line) - 1] = 0;
            Note(line);
        }
        if (const char* wine = sys::WineVersion()) {
            _snprintf(line, sizeof(line),
                      "running under Wine / Proton %s: GPU, VRAM and driver details can differ from Windows",
                      wine);
            line[sizeof(line) - 1] = 0;
            Note(line);
        }
    }
}

bool LookupBool(const char* name, uint8_t* out) {
    const Entry* e = Find(name);
    if (!e) return false;
    if (e->is_float) {
        float f;
        memcpy(&f, &e->u32, 4);
        *out = (f != 0.0f) ? 1 : 0;
    } else {
        *out = (e->u32 != 0) ? 1 : 0;
    }
    return true;
}

bool LookupScalar(const char* name, uint32_t* out) {
    const Entry* e = Find(name);
    if (!e) return false;
    *out = e->u32;
    return true;
}

void LogRead(const void* node, const char* name, uint32_t raw, int is_bool) {
    if (!name || !g_log_path[0]) return;
    if (!g_log_all) {
        if (g_log_prefix[0] == 0) return;
        if (strncmp(name, g_log_prefix, strlen(g_log_prefix)) != 0) return;
    }

    int changed = 0;
    EnterCriticalSection(&g_cs);
    const uint32_t bucket = HashSeen(node, name) & (kSeenBuckets - 1);
    int i = g_seen_head[bucket];
    while (i != -1) {
        if (g_seen[i].node == node && strcmp(g_seen[i].name, name) == 0) break;
        i = g_seen[i].next;
    }
    if (i == -1) {
        if (g_seen_count < kMaxSeen) {
            i = g_seen_count++;
            g_seen[i].node = node;
            strncpy(g_seen[i].name, name, sizeof(g_seen[i].name) - 1);
            g_seen[i].name[sizeof(g_seen[i].name) - 1] = 0;
            g_seen[i].last = raw;
            g_seen[i].used = 1;
            g_seen[i].next = g_seen_head[bucket];
            g_seen_head[bucket] = i;
            changed = 1;
        }
    } else if (g_seen[i].last != raw) {
        g_seen[i].last = raw;
        changed = 1;
    }
    LeaveCriticalSection(&g_cs);
    if (!changed) return;

    char line[224];
    if (is_bool) {
        _snprintf(line, sizeof(line), "[%p] %s = %u\n", node, name, raw != 0 ? 1u : 0u);
    } else {
        float f;
        memcpy(&f, &raw, 4);
        _snprintf(line, sizeof(line), "[%p] %s = %d  (%g)\n", node, name, (int)raw, f);
    }
    line[sizeof(line) - 1] = 0;
    AppendLog(line);
}

bool WantEnforce() { return g_enforce_enabled && g_enf_mode != kEnfOff; }
bool WantGuard()   { return g_want_guard && g_enf_mode != kEnfOff; }
bool EnforceTarget() { return g_enf_mode != kEnfCheaper; }
bool WantEnforceBools() { return g_enforce_bools; }
bool LogAll()      { return g_log_all; }
bool LogReport()   { return g_log_report; }
bool UnknownBuildOff() { return g_unknown_build_off; }
bool StatusPopup() { return g_status_popup; }

static bool g_unknown_build = false;
void SetUnknownBuild(bool unknown) { g_unknown_build = unknown; }

bool ForceScan() {
    // The known RVAs belong to one build; on any other build a generic
    // prologue could match an unrelated function there, so always re-derive.
    if (g_unknown_build) return true;
    static int cached = -1;
    if (cached < 0) {
        char v[16] = {0};
        const DWORD n = GetEnvironmentVariableA("F125_FORCE_SCAN", v, sizeof(v));
        int on = (n > 0 && v[0] != '0' && v[0] != 'n' && v[0] != 'N') ? 1 : 0;
        if (!on && g_exe_dir[0]) {
            wchar_t path[MAX_PATH];
            lstrcpyW(path, g_exe_dir);
            lstrcatW(path, L"\\f125_force_scan.txt");
            on = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES ? 1 : 0;
        }
        cached = on;
    }
    return cached != 0;
}

bool LookupEnforce(const char* name, int* value, int* disable) {
    if (!name) return false;
    for (int i = 0; i < g_enfov_count; ++i) {
        if (strcmp(g_enfov[i].name, name) == 0) {
            if (value) *value = g_enfov[i].value;
            if (disable) *disable = g_enfov[i].disable;
            return true;
        }
    }
    if (const PresetEnf* pe = FindPresetEnf(name)) {
        if (value) *value = (int)pe->value;
        if (disable) *disable = 0;
        return true;
    }
    return false;
}

bool LookupEnforceFloat(const char* name, float* value, int* disable) {
    if (!name) return false;
    for (int i = 0; i < g_enfov_count; ++i) {
        if (strcmp(g_enfov[i].name, name) != 0) continue;
        if (value) {
            if (g_enfov[i].is_float) {
                memcpy(value, &g_enfov[i].value, sizeof(float));
            } else {
                *value = (float)g_enfov[i].value;
            }
        }
        if (disable) *disable = g_enfov[i].disable;
        return true;
    }
    if (const PresetEnf* pe = FindPresetEnf(name)) {
        if (value) *value = pe->value;
        if (disable) *disable = 0;
        return true;
    }
    return false;
}

profile::Preset GetPreset() { return g_preset; }

bool StockMode() {
    if (!g_exe_dir[0]) return false;
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%s\\f125_ab_stock.txt", g_exe_dir);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

bool SetStockMode(bool on) {
    if (!g_exe_dir[0]) return false;
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%s\\f125_ab_stock.txt", g_exe_dir);
    if (!on) return DeleteFileW(path) || GetLastError() == ERROR_FILE_NOT_FOUND;
    const char msg[] = "A/B: F1 25 RE-Optimization starts in STOCK mode while this file exists.\r\n";
    return WriteFileAtomic(path, msg, sizeof(msg) - 1);
}

const wchar_t* ExeDir() { return g_exe_dir; }
const wchar_t* IniPath() { return g_ini_path; }

// Called from DllMain so the DLL-directory fallback works.
void SetSelf(void* m) { g_self = (HMODULE)m; }

void Note(const char* msg) {
    if (!msg) return;
    char line[320];
    _snprintf(line, sizeof(line), "[mod] %s\n", msg);
    line[sizeof(line) - 1] = 0;
    AppendLog(line);
}

bool ApplyProfile() {
    if (!g_profile_apply) return false;

    wchar_t docs[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs))) {
        Note("profile: could not resolve Documents folder - skipping");
        return false;
    }

    wchar_t dst[MAX_PATH] = {0};
    _snwprintf(dst, MAX_PATH,
               L"%s\\My Games\\F1 25\\hardwaresettings\\hardware_settings_config.xml",
               docs);
    if (GetFileAttributesW(dst) == INVALID_FILE_ATTRIBUTES) {
        Note("profile: hardware_settings_config.xml not found - skipping");
        return false;
    }

    if (StockMode()) {
        // A/B stock run: put the player's own values back so the game really
        // runs stock, and leave everything else alone.
        RestoreStockSettings("A/B: STOCK mode");
        return false;
    }

    const size_t kCap = 1 << 20;
    char* in = (char*)malloc(kCap);
    char* out = (char*)malloc(kCap);
    if (!in || !out) {
        free(in);
        free(out);
        Note("profile: out of memory");
        return false;
    }

    if (!ReadFileAll(dst, in, kCap)) {
        free(in);
        free(out);
        Note("profile: read failed");
        return false;
    }

    // Keep exactly one untouched copy of the original settings, so
    // Restore-F125-Tune.ps1 can put the file back. CREATE_NEW means we never
    // overwrite the first (stock) backup on later launches.
    {
        wchar_t bak[MAX_PATH] = {0};
        _snwprintf(bak, MAX_PATH, L"%s.untuned", dst);
        if (GetFileAttributesW(bak) == INVALID_FILE_ATTRIBUTES) {
            if (WriteFileAllNew(bak, in, strlen(in))) {
                Note("profile: saved stock backup hardware_settings_config.xml.untuned");
            } else {
                Note("WARN: could not write the .untuned settings backup");
            }
        }
    }

    const gpu::Info& g = Gpu();
    const profile::Upscaler want = ChooseUpscaler(g);
    const profile::Upscaler up = profile::FrameGenGuard(in, want);
    const bool first = !g_header_done;
    g_header_done = true;

    if (first) {
        char line[320];
        char vram[24];
        if (g.vramMb) _snprintf(vram, sizeof(vram), "%u MB", g.vramMb);
        else _snprintf(vram, sizeof(vram), "unknown");
        vram[sizeof(vram) - 1] = 0;
        _snprintf(line, sizeof(line),
                  "header: GPU %s | vendor %s | driver %s | integrated %s | VRAM %s | low-VRAM guard %s | "
                  "4 GB texture guard %s",
                  g.vendor != gpu::VendorUnknown ? g.name : "not detected",
                  gpu::VendorName(g.vendor), g.driver[0] ? g.driver : "unknown",
                  g.integrated ? "yes" : "no", vram, LowVram() ? "on" : "off", TinyVram() ? "on" : "off");
        line[sizeof(line) - 1] = 0;
        Note(line);
        _snprintf(line, sizeof(line), "header: CPU %s | %d cores / %d threads | weak-CPU guard %s%s%s",
                  g_cpu.name[0] ? g_cpu.name : "unknown", g_cpu.cores, g_cpu.threads,
                  g_weak_cpu ? "on" : "off", g_weak_cpu && g_weak_why ? " - " : "",
                  g_weak_cpu && g_weak_why ? g_weak_why : "");
        line[sizeof(line) - 1] = 0;
        Note(line);
        {
            char adapters[600];
            _snprintf(adapters, sizeof(adapters), "header: adapters found: %s",
                      g_gpu_seen[0] ? g_gpu_seen : "none listed");
            adapters[sizeof(adapters) - 1] = 0;
            Note(adapters);
        }
        _snprintf(line, sizeof(line),
                  "header: preset %s -> %s | enforce %s (raise %s) | upscaler %s -> %s | "
                  "crash safe mode %s",
                  g_preset_recommended ? "recommended" : profile::PresetName(g_preset),
                  profile::PresetName(g_preset),
                  g_enf_mode == kEnfOff ? "off" : g_enf_mode == kEnfCheaper ? "cheaper" : "target",
                  g_enf_mode == kEnfTarget ? "allowed" : "never", g_upscaler_mode, UpName(up),
                  g_crash_guard ? "on" : "off");
        line[sizeof(line) - 1] = 0;
        Note(line);

        char w[16] = "?", hgt[16] = "?", dm[16] = "?", vs[16] = "?", capOn[16] = "?", cap[16] = "?",
             fg[16] = "?", mfg[16] = "?", aq[16] = "?", dyn[16] = "?", dl[16] = "?", fs[16] = "?",
             xe[16] = "?", ta[16] = "?";
        profile::ReadAttr(in, "resolution", "width", w, sizeof(w));
        profile::ReadAttr(in, "resolution", "height", hgt, sizeof(hgt));
        profile::ReadAttr(in, "resolution", "displayMode", dm, sizeof(dm));
        profile::ReadAttr(in, "resolution", "vsync", vs, sizeof(vs));
        profile::ReadAttr(in, "resolution", "frameRateLimiterEnabled", capOn, sizeof(capOn));
        profile::ReadAttr(in, "resolution", "frameRateLimiterValue", cap, sizeof(cap));
        profile::ReadAttr(in, "frame_gen", "mode", fg, sizeof(fg));
        profile::ReadAttr(in, "multi_frame_gen", "value", mfg, sizeof(mfg));
        profile::ReadAttr(in, "aa_quality", "value", aq, sizeof(aq));
        profile::ReadAttr(in, "dynamicresolution_enabled", "value", dyn, sizeof(dyn));
        profile::ReadAttr(in, "antialiasing", "dlss", dl, sizeof(dl));
        profile::ReadAttr(in, "antialiasing", "fsr3", fs, sizeof(fs));
        profile::ReadAttr(in, "antialiasing", "xess", xe, sizeof(xe));
        profile::ReadAttr(in, "antialiasing", "taa", ta, sizeof(ta));
        _snprintf(line, sizeof(line),
                  "header: display %sx%s mode %s | vsync %s | frame cap %s (%s fps) | frame gen %s | "
                  "multi frame gen %s | upscaler quality %s | dynamic resolution %s | "
                  "in-game AA dlss=%s fsr3=%s xess=%s taa=%s",
                  w, hgt, dm, vs, capOn, cap, fg, mfg, aq, dyn, dl, fs, xe, ta);
        line[sizeof(line) - 1] = 0;
        Note(line);
        if (!strcmp(vs, "true"))
            Note("note: vsync is on, so fps cannot go above your refresh rate; the mod can't show "
                 "a gain past it (it still lowers GPU load)");
        if (!strcmp(capOn, "true")) {
            _snprintf(line, sizeof(line),
                      "note: the in-game frame cap is on (%s fps); fps cannot go above it", cap);
            line[sizeof(line) - 1] = 0;
            Note(line);
        }
        if (g.integrated && up == profile::Upscaler::Keep && want == profile::Upscaler::Keep)
            Note("note: integrated GPU, so upscaler auto leaves your in-game choice alone");
        if (want != up) {
            _snprintf(line, sizeof(line),
                      "note: frame generation is on with your %s, so the upscaler stays as you set it "
                      "instead of %s (switching it can turn frame generation off and cost more fps "
                      "than it gains)",
                      UpName(profile::FileUpscaler(in)), UpName(want));
            line[sizeof(line) - 1] = 0;
            Note(line);
        }
    }

    char upLine[160];
    _snprintf(upLine, sizeof(upLine), "upscaler: mode=%s -> %s", g_upscaler_mode,
              UpName(up));
    upLine[sizeof(upLine) - 1] = 0;
    Note(upLine);

    if (up == profile::Upscaler::Dlss && !g.supportsDlss) {
        Note("WARN: DLSS forced on a GPU without DLSS - the game may fall back");
    }
    if (up == profile::Upscaler::Keep && g.vendor == gpu::VendorUnknown) {
        Note("upscaler: unknown GPU - leaving your in-game upscaler unchanged");
    }

    {
        char line[260];
        char why[200] = "";
        if (g_preset_recommended) {
            const profile::Preset tier = gpu::Recommend(g);
            const profile::Preset strong = gpu::RecommendAt(g, g_res_w, g_res_h, false);
            if (strong != g_preset)
                _snprintf(why, sizeof(why),
                          " (recommended: GPU tier %s at %dx%d; not stepped down: the CPU is the limit here, %s)",
                          profile::PresetName(tier), g_res_w, g_res_h, g_weak_why ? g_weak_why : "weak CPU");
            else if (tier != g_preset)
                _snprintf(why, sizeof(why), " (recommended: GPU tier %s, one step down for %dx%d)",
                          profile::PresetName(tier), g_res_w, g_res_h);
            else
                _snprintf(why, sizeof(why), " (recommended: GPU tier %s at %dx%d)",
                          profile::PresetName(tier), g_res_w, g_res_h);
            why[sizeof(why) - 1] = 0;
        }
        _snprintf(line, sizeof(line), "preset: %s%s", profile::PresetName(g_preset), why);
        line[sizeof(line) - 1] = 0;
        Note(line);
    }
    // When the preset changed since the last launch, or this is the first launch
    // of 0.3.2 (older versions could write values above the player's own), put the
    // player's own values back for the mod's attributes before applying the delta.
    {
        wchar_t last[MAX_PATH], bak[MAX_PATH];
        _snwprintf(last, MAX_PATH, L"%s\\f125_last_preset.txt", g_exe_dir);
        _snwprintf(bak, MAX_PATH, L"%s.untuned", dst);
        char prev[48] = {0};
        ReadFileAll(last, prev, sizeof(prev));
        char cur[48];
        _snprintf(cur, sizeof(cur), "%s;v032", profile::PresetName(g_preset));
        cur[sizeof(cur) - 1] = 0;
        if (strcmp(prev, cur) != 0) {
            char* unt = (char*)malloc(kCap);
            if (prev[0] && unt && ReadFileAll(bak, unt, kCap) &&
                profile::RestoreOwned(in, unt, out, kCap) > 0) {
                memcpy(in, out, strlen(out) + 1);
                Note(strstr(prev, ";v032")
                         ? "preset: changed since last launch - your own values restored before the new preset"
                         : "profile: first 0.3.2 launch - your own values restored, the delta now only lowers them");
            }
            free(unt);
            WriteFileAtomic(last, cur, strlen(cur));
        }
    }
    struct Counts {
        int set, kept;
        bool log;
    } counts = {0, 0, first};
    profile::Options opt;
    opt.up = up;
    opt.preset = g_preset;
    opt.allowRaise = g_enf_mode == kEnfTarget;
    opt.lowVram = LowVram();
    opt.weakCpu = g_weak_cpu;
    opt.tinyVram = TinyVram();
    opt.user = &counts;
    opt.onChange = [](const char* tag, const char* attr, const char* from, const char* to,
                      bool kept, void* user) {
        Counts* k = (Counts*)user;
        (kept ? k->kept : k->set)++;
        if (!k->log) return;
        char line[224];
        if (kept)
            _snprintf(line, sizeof(line), "kept %s.%s = %s (tuned %s would cost more than your setting)",
                      tag, attr, from, to);
        else
            _snprintf(line, sizeof(line), "set %s.%s: %s -> %s", tag, attr, from, to);
        line[sizeof(line) - 1] = 0;
        Note(line);
    };
    size_t n = profile::Transform(in, out, kCap, opt);
    free(in);
    if (n == 0) {
        free(out);
        Note("profile: transform failed");
        return false;
    }

    if (!WriteFileAtomic(dst, out, n)) {
        free(out);
        Note("profile: write failed");
        return false;
    }
    free(out);

    {
        char line[128];
        _snprintf(line, sizeof(line), "profile: %d value(s) lowered, %d kept as yours", counts.set,
                  counts.kept);
        line[sizeof(line) - 1] = 0;
        Note(line);
    }
    Note("profile applied (tuned delta on hardware_settings_config.xml)");
    return true;
}

bool PresetIsRecommended() { return g_preset_recommended; }

const gpu::Info& Gpu() {
    if (!g_gpu_ready) {
        g_gpu = gpu::Detect();
        g_gpu_ready = true;
    }
    return g_gpu;
}

bool LowVram() {
    const gpu::Info& g = Gpu();
    return g.vendor != gpu::VendorUnknown && gpu::LowVram(g);
}

bool TinyVram() {
    const gpu::Info& g = Gpu();
    return g.vendor != gpu::VendorUnknown && gpu::TinyVram(g);
}

bool RestoreStockSettings(const char* why) {
    wchar_t docs[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs))) return false;
    wchar_t dst[MAX_PATH], bak[MAX_PATH];
    _snwprintf(dst, MAX_PATH,
               L"%s\\My Games\\F1 25\\hardwaresettings\\hardware_settings_config.xml", docs);
    _snwprintf(bak, MAX_PATH, L"%s.untuned", dst);
    const size_t kCap = 1 << 20;
    char* cur = (char*)malloc(kCap);
    char* unt = (char*)malloc(kCap);
    char* out = (char*)malloc(kCap);
    bool ok = false;
    char line[200];
    if (cur && unt && out && ReadFileAll(dst, cur, kCap) && ReadFileAll(bak, unt, kCap)) {
        const size_t n = profile::RestoreOwned(cur, unt, out, kCap);
        ok = n > 0 && WriteFileAtomic(dst, out, n);
    }
    _snprintf(line, sizeof(line), ok ? "%s - your own settings restored from .untuned"
                                     : "%s - no .untuned backup found, settings left as they are",
              why);
    line[sizeof(line) - 1] = 0;
    Note(line);
    free(cur);
    free(unt);
    free(out);
    return ok;
}

// --- crash safe mode --------------------------------------------------------
namespace {
testing::SafeState g_safe = {0, 0, 0, 0};
volatile long      g_safe_pending = 0;
bool               g_safe_active = false;
bool               g_safe_fallback = false;  // 0.3.6: state in %LOCALAPPDATA% instead

// 0.3.6: when the game folder can't be written (Program Files without rights, a
// read-only library), the state goes to %LOCALAPPDATA%\F1 25 RE-Optimization.
bool FallbackPath(wchar_t* path) {
    wchar_t base[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base))) return false;
    wchar_t dir[MAX_PATH];
    _snwprintf(dir, MAX_PATH, L"%s\\F1 25 RE-Optimization", base);
    dir[MAX_PATH - 1] = 0;
    CreateDirectoryW(dir, nullptr);
    _snwprintf(path, MAX_PATH, L"%s\\f125_launch_state.txt", dir);
    path[MAX_PATH - 1] = 0;
    return true;
}

bool SafePath(wchar_t* path) {
    if (g_safe_fallback) return FallbackPath(path);
    if (!g_exe_dir[0]) return false;
    _snwprintf(path, MAX_PATH, L"%s\\f125_launch_state.txt", g_exe_dir);
    path[MAX_PATH - 1] = 0;
    return true;
}

bool SafeWrite(const testing::SafeState& s) {
    wchar_t path[MAX_PATH];
    if (!SafePath(path)) return false;
    char text[96];
    testing::SafeFormat(s, F125_MOD_VERSION, text, sizeof(text));
    return WriteFileAtomic(path, text, strlen(text));
}
}  // namespace

bool CrashGuardStart() {
    if (!g_crash_guard) {
        Note("crash safe mode: off");
        return false;
    }
    wchar_t path[MAX_PATH];
    if (!SafePath(path)) return false;
    char text[128] = {0};
    testing::SafeState prev = {0, 0, 0, 0};
    bool have = ReadFileAll(path, text, sizeof(text)) != 0;
    if (!have) {
        // A state left in the fallback folder by an earlier launch.
        wchar_t fb[MAX_PATH];
        if (FallbackPath(fb) && ReadFileAll(fb, text, sizeof(text))) {
            have = true;
            g_safe_fallback = true;
        }
    }
    if (have) testing::SafeParse(text, F125_MOD_VERSION, &prev);
    if (const char* last = testing::SafeLastLaunch(prev)) {
        char line[300];
        _snprintf(line, sizeof(line), "crash safe mode: %s", last);
        line[sizeof(line) - 1] = 0;
        Note(line);
    }
    const char* why = nullptr;
    const bool safe = testing::SafeStart(prev, &g_safe, &why);
    if (!SafeWrite(g_safe)) {
        if (!g_safe_fallback) {
            g_safe_fallback = true;
            if (!SafeWrite(g_safe)) g_safe_fallback = false;
        }
        if (!g_safe_fallback) {
            Note("crash safe mode: could not write f125_launch_state.txt in the game folder or in "
                 "%LOCALAPPDATA%\\F1 25 RE-Optimization - off for this launch");
            return false;
        }
    }
    if (g_safe_fallback)
        Note("crash safe mode: the game folder is not writable, so the launch state is kept in "
             "%LOCALAPPDATA%\\F1 25 RE-Optimization");
    g_safe_active = true;
    InterlockedExchange(&g_safe_pending, 1);
    if (why) {
        char line[300];
        _snprintf(line, sizeof(line), "crash safe mode: %s", why);
        line[sizeof(line) - 1] = 0;
        Note(line);
    }
    return safe;
}

void MarkCleanExit() {
    if (!g_safe_active) return;
    ConfirmLaunch(nullptr);
    g_safe.end = 2;
    SafeWrite(g_safe);
}

void ConfirmLaunch(const char* how) {
    if (!InterlockedExchange(&g_safe_pending, 0)) return;
    g_safe = testing::SafeConfirm(g_safe);
    SafeWrite(g_safe);
    if (how) {
        char line[160];
        _snprintf(line, sizeof(line), "crash safe mode: launch confirmed (%s)", how);
        line[sizeof(line) - 1] = 0;
        Note(line);
    }
}

namespace testing {
profile::Upscaler ChooseUpscaler(const gpu::Info& g, const char* mode) {
    return ChooseUpscalerFor(g, mode);
}

bool SafeStart(const SafeState& prev, SafeState* write, const char** why) {
    *why = nullptr;
    if (prev.kind == 1) {
        const int fails = prev.fails + 1;
        const int hold = fails >= 2 ? 3 : 1;
        *write = {2, fails, hold - 1, 0};
        *why = fails >= 2
                   ? "the last two launches with the mod on never reached a stable menu; running "
                     "stock for 3 launches, then the mod is tried again"
                   : "the last launch with the mod on never reached a stable menu (crash or forced "
                     "quit); this launch runs stock, the next one uses the mod again";
        return true;
    }
    if (prev.kind == 2) {
        *write = {1, 0, 0, 0};
        *why = "the last launch crashed with the mod OFF too, so the crash is likely not this mod "
               "(check other mods, drivers or the game); the "
               "mod is on again";
        return false;
    }
    if (prev.hold > 0) {
        *write = {2, prev.fails, prev.hold - 1, 0};
        *why = "still running stock after repeated crashes with the mod on";
        return true;
    }
    *write = {1, prev.fails, 0, 0};
    return false;
}

SafeState SafeConfirm(const SafeState& cur) {
    if (cur.kind == 1) return {0, 0, 0, 1};
    return {0, cur.fails, cur.hold, 1};
}

const char* SafeLastLaunch(const SafeState& prev) {
    if (prev.kind == 1)
        return "the last launch never got past 90 s (crash or forced quit early on)";
    if (prev.kind == 2) return "the last launch ran stock (crash safe mode) and did not finish cleanly";
    if (prev.end == 2) return "the last launch quit cleanly";
    if (prev.end == 1)
        return "the last launch passed the 90 s check but did not quit cleanly (a crash after 90 s or a "
               "forced quit); crash safe mode only acts on crashes in the first 90 s";
    return nullptr;
}

bool SafeParse(const char* text, const char* version, SafeState* out) {
    *out = {0, 0, 0, 0};
    char kind[16] = {0}, ver[48] = {0};
    int fails = 0, hold = 0, end = 0;
    const int got = sscanf(text, "%15s %d %d %47s %d", kind, &fails, &hold, ver, &end);
    if (got < 4) return false;
    if (strcmp(ver, version) != 0 || fails < 0 || fails > 99 || hold < 0 || hold > 9) return false;
    if (got < 5 || end < 0 || end > 2) end = 0;
    if (!strcmp(kind, "pending")) *out = {1, fails, hold, end};
    else if (!strcmp(kind, "safe")) *out = {2, fails, hold, end};
    else if (!strcmp(kind, "ok")) *out = {0, fails, hold, end};
    else return false;
    return true;
}

void SafeFormat(const SafeState& s, const char* version, char* out, size_t cap) {
    const char* k = s.kind == 1 ? "pending" : s.kind == 2 ? "safe" : "ok";
    _snprintf(out, cap, "%s %d %d %s %d\r\n", k, s.fails, s.hold, version, s.end);
    out[cap - 1] = 0;
}
}  // namespace testing

}  // namespace tune
