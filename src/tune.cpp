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

struct Entry {
    char     name[96];
    uint32_t u32;
    int      is_float;
};

const int kMaxEntries = 512;
Entry     g_entries[kMaxEntries];
int       g_entry_count = 0;

bool      g_log_all = false;
char      g_log_prefix[64] = {0};
bool      g_profile_apply = false;
char      g_upscaler_mode[16] = "auto";

HMODULE   g_self = nullptr;
wchar_t   g_exe_dir[MAX_PATH] = {0};
CRITICAL_SECTION g_cs;
bool      g_cs_ready = false;

// (node, name) pairs we have seen, with the last value written to the log.
struct Seen {
    const void* node;
    char        name[96];
    uint32_t    last;
};
const int kMaxSeen = 16384;
Seen      g_seen[kMaxSeen];
int       g_seen_count = 0;

wchar_t   g_log_path[MAX_PATH] = {0};

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
    CloseHandle(h);
    return ok && wrote == n;
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

void AddEntry(const char* name, const char* value) {
    if (g_entry_count >= kMaxEntries) return;
    Entry& e = g_entries[g_entry_count];
    strncpy(e.name, name, sizeof(e.name) - 1);
    e.name[sizeof(e.name) - 1] = 0;

    char v[64];
    strncpy(v, value, sizeof(v) - 1);
    v[sizeof(v) - 1] = 0;
    Trim(v);

    if (!_stricmp(v, "true") || !_stricmp(v, "on") || !_stricmp(v, "yes")) {
        e.u32 = 1; e.is_float = 0;
    } else if (!_stricmp(v, "false") || !_stricmp(v, "off") || !_stricmp(v, "no")) {
        e.u32 = 0; e.is_float = 0;
    } else if (strchr(v, '.') || strchr(v, 'e') || strchr(v, 'E')) {
        if (!ParseFloat(v, &e.u32)) return;
        e.is_float = 1;
    } else {
        if (!ParseInt(v, &e.u32)) return;
        e.is_float = 0;
    }
    ++g_entry_count;
}

const Entry* Find(const char* name) {
    if (!name) return nullptr;
    for (int i = 0; i < g_entry_count; ++i) {
        if (strcmp(g_entries[i].name, name) == 0) return &g_entries[i];
    }
    return nullptr;
}

void ParseIni(const char* text) {
    int   section = 0;  // 0 none, 1 log, 2 override
    char  line[256];
    const char* p = text;

    while (*p) {
        size_t n = 0;
        while (*p && *p != '\n' && n < sizeof(line) - 1) line[n++] = *p++;
        if (*p == '\n') ++p;
        line[n] = 0;
        Trim(line);
        if (line[0] == 0 || line[0] == '#' || line[0] == ';') continue;

        if (line[0] == '[') {
            if (strstr(line, "log")) section = 1;
            else if (strstr(line, "override")) section = 2;
            else if (strstr(line, "profile")) section = 3;
            else if (strstr(line, "upscaler")) section = 4;
            else section = 0;
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = line;
        char* val = eq + 1;
        Trim(key);
        Trim(val);

        if (section == 1) {
            if (!_stricmp(key, "all")) g_log_all = (atoi(val) != 0);
            else if (!_stricmp(key, "prefix")) {
                strncpy(g_log_prefix, val, sizeof(g_log_prefix) - 1);
                g_log_prefix[sizeof(g_log_prefix) - 1] = 0;
            }
        } else if (section == 2) {
            AddEntry(key, val);
        } else if (section == 3) {
            if (!_stricmp(key, "apply")) g_profile_apply = (atoi(val) != 0);
        } else if (section == 4) {
            if (!_stricmp(key, "mode")) {
                strncpy(g_upscaler_mode, val, sizeof(g_upscaler_mode) - 1);
                g_upscaler_mode[sizeof(g_upscaler_mode) - 1] = 0;
            }
        }
    }
}

void AppendLog(const char* line) {
    if (!g_log_path[0]) return;
    EnterCriticalSection(&g_cs);
    HANDLE h = CreateFileW(g_log_path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0;
        WriteFile(h, line, (DWORD)strlen(line), &wrote, nullptr);
        CloseHandle(h);
    }
    LeaveCriticalSection(&g_cs);
}

// Start every launch with a fresh log, so the file always describes the
// current session instead of accumulating forever.
void ResetLog() {
    if (!g_log_path[0]) return;
    HANDLE h = CreateFileW(g_log_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
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

// [upscaler] mode -> concrete backend. "auto" maps vendor/series to the best
// backend the GPU actually supports.
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

}  // namespace

void Init() {
    if (!g_cs_ready) {
        InitializeCriticalSection(&g_cs);
        g_cs_ready = true;
    }

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
        // Fall back to the DLL's own directory.
        wchar_t dll[MAX_PATH] = {0};
        if (g_self) GetModuleFileNameW(g_self, dll, MAX_PATH);
        wchar_t* ds = wcsrchr(dll, L'\\');
        if (ds) *ds = 0;
        _snwprintf(ini, MAX_PATH, L"%s\\f125_tune.ini", dll);
        if (!ReadFileAll(ini, buf, sizeof(buf))) {
            // No ini: run with an empty override table, no logging.
            g_log_all = false;
            return;
        }
    }

    ParseIni(buf);

    _snwprintf(g_log_path, MAX_PATH, L"%s\\f125_tune.log", exe);
    ResetLog();
    Note(F125_MOD_NAME " v" F125_MOD_VERSION);
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
    int i = 0;
    for (; i < g_seen_count; ++i) {
        if (g_seen[i].node == node && strcmp(g_seen[i].name, name) == 0) break;
    }
    if (i == g_seen_count) {
        if (g_seen_count < kMaxSeen) {
            g_seen[i].node = node;
            strncpy(g_seen[i].name, name, sizeof(g_seen[i].name) - 1);
            g_seen[i].name[sizeof(g_seen[i].name) - 1] = 0;
            g_seen[i].last = raw;
            ++g_seen_count;
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

// Called from DllMain so the DLL-directory fallback works.
void SetSelf(void* m) { g_self = (HMODULE)m; }

void Note(const char* msg) {
    if (!msg) return;
    char line[256];
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

    HANDLE h = CreateFileW(dst, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        free(out);
        Note("profile: write open failed");
        return false;
    }
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, out, (DWORD)n, &wrote, nullptr);
    CloseHandle(h);
    free(out);
    if (!ok || wrote != n) {
        Note("profile: write failed");
        return false;
    }

    Note("profile applied (tuned delta on hardware_settings_config.xml)");
    return true;
}

}  // namespace tune
