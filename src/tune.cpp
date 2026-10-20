#include "tune.h"
#include "profile.h"
#include "gpu.h"
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
};
const int kMaxEnfOv = 256;
EnfOv     g_enfov[kMaxEnfOv];
int       g_enfov_count = 0;

enum EnfMode { kEnfOff = 0, kEnfCheaper, kEnfTarget };

EnfMode   g_enf_mode        = kEnfTarget;
bool      g_enforce_enabled = true;
bool      g_enforce_bools   = true;
bool      g_log_all         = false;
bool      g_log_report      = false;
char      g_log_prefix[64]  = {0};
bool      g_profile_apply   = false;
bool      g_want_guard      = true;
char      g_upscaler_mode[16] = "auto";

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

profile::Upscaler ChooseUpscaler(const gpu::Info& g) {
    const char* m = g_upscaler_mode;

    if (!_stricmp(m, "keep") || !_stricmp(m, "none")) return profile::Upscaler::Keep;
    if (!_stricmp(m, "off"))  return profile::Upscaler::Off;
    if (!_stricmp(m, "taa"))  return profile::Upscaler::Taa;
    if (!_stricmp(m, "dlss")) return profile::Upscaler::Dlss;
    if (!_stricmp(m, "fsr3") || !_stricmp(m, "fsr")) return profile::Upscaler::Fsr3;
    if (!_stricmp(m, "xess")) return profile::Upscaler::Xess;

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

    _snwprintf(g_log_path, MAX_PATH, L"%s\\f125_tune.log", exe);
    ResetLog();
    Note(F125_MOD_NAME " v" F125_MOD_VERSION);
    SessionInfo();
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

bool LookupEnforce(const char* name, int* value, int* disable) {
    if (!name) return false;
    for (int i = 0; i < g_enfov_count; ++i) {
        if (strcmp(g_enfov[i].name, name) == 0) {
            if (value) *value = g_enfov[i].value;
            if (disable) *disable = g_enfov[i].disable;
            return true;
        }
    }
    return false;
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

    const gpu::Info g = gpu::Detect();
    const profile::Upscaler up = ChooseUpscaler(g);

    char gpuLine[224];
    if (g.vendor != gpu::VendorUnknown) {
        char series[32] = "";
        if (g.series > 0) {
            _snprintf(series, sizeof(series), " %d-series", g.series);
        }
        _snprintf(gpuLine, sizeof(gpuLine), "GPU: %s (%s%s)", g.name,
                  gpu::VendorName(g.vendor), series);
    } else {
        _snprintf(gpuLine, sizeof(gpuLine), "GPU: not detected");
    }
    gpuLine[sizeof(gpuLine) - 1] = 0;
    Note(gpuLine);

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

    size_t n = profile::Transform(in, out, kCap, up);
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

    Note("profile applied (tuned delta on hardware_settings_config.xml)");
    return true;
}

}  // namespace tune
