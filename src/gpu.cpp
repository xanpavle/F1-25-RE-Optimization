#include "gpu.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

namespace gpu {
namespace {

bool WContainsNoCase(const wchar_t* hay, const wchar_t* needle) {
    if (!hay || !needle || !*needle) return false;
    const size_t n = wcslen(needle);
    for (const wchar_t* p = hay; *p; ++p) {
        size_t i = 0;
        while (i < n && p[i] && towlower(p[i]) == towlower(needle[i])) ++i;
        if (i == n) return true;
    }
    return false;
}

Vendor FromId(const wchar_t* id) {
    if (!id) return VendorUnknown;
    if (WContainsNoCase(id, L"VEN_10DE")) return VendorNvidia;
    if (WContainsNoCase(id, L"VEN_1002")) return VendorAmd;
    if (WContainsNoCase(id, L"VEN_8086")) return VendorIntel;
    return VendorUnknown;
}

Vendor FromName(const wchar_t* name) {
    if (!name) return VendorUnknown;
    if (WContainsNoCase(name, L"nvidia") || WContainsNoCase(name, L"geforce"))
        return VendorNvidia;
    if (WContainsNoCase(name, L"amd") || WContainsNoCase(name, L"radeon"))
        return VendorAmd;
    if (WContainsNoCase(name, L"intel")) return VendorIntel;
    return VendorUnknown;
}

// First run of >= 3 digits: "RTX 4070 Ti" -> 4070, "RX 7900 XTX" -> 7900,
// "Arc A770" -> 770. A 2-digit suffix (e.g. "RTX 4090 D") is ignored on purpose.
bool NContainsNoCase(const char* hay, const char* needle) {
    const size_t n = strlen(needle);
    for (const char* p = hay; *p; ++p) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) ++i;
        if (i == n) return true;
    }
    return false;
}

// "A770", "B580": Intel Arc desktop cards.
bool HasArcModel(const wchar_t* s) {
    for (const wchar_t* p = s; *p; ++p)
        if ((*p == L'A' || *p == L'B') && iswdigit(p[1]) && iswdigit(p[2]) && iswdigit(p[3]))
            return true;
    return false;
}

// "780M", "680M": Radeon APU graphics.
bool HasMobileIgpuModel(const wchar_t* s) {
    for (const wchar_t* p = s; p[0] && p[1] && p[2] && p[3]; ++p)
        if (iswdigit(p[0]) && iswdigit(p[1]) && iswdigit(p[2]) && (p[3] == L'M' || p[3] == L'm') &&
            !iswalnum(p[4]))
            return true;
    return false;
}

int FirstModel(const char* s) {
    if (!s) return 0;
    for (const char* p = s; *p;) {
        if (*p >= '0' && *p <= '9') {
            char* end = nullptr;
            const long v = strtol(p, &end, 10);
            if (end - p >= 3) return (int)v;
            p = end;
        } else {
            ++p;
        }
    }
    return 0;
}

int NvSeries(int n) {
    if (n >= 5000) return 50;
    if (n >= 4000) return 40;
    if (n >= 3000) return 30;
    if (n >= 2000) return 20;
    if (n >= 1600) return 16;
    if (n >= 1000) return 10;
    return 0;
}

int AmdSeries(int n) {
    if (n >= 9000) return 9000;
    if (n >= 7000) return 7000;
    if (n >= 6000) return 6000;
    if (n >= 5000) return 5000;
    if (n >= 4000) return 4000;
    if (n >= 300) return 300;
    return 0;
}

void ToNarrow(const wchar_t* w, char* out, size_t cap) {
    out[0] = 0;
    if (!w || cap == 0) return;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, nullptr, nullptr);
    out[cap - 1] = 0;
}

int ScoreOf(const Info& g) {
    int s;
    switch (g.vendor) {
        case VendorNvidia: s = 30; break;
        case VendorAmd:    s = 28; break;
        case VendorIntel:  s = g.supportsXess ? 20 : 5; break;
        default:           return 0;
    }
    // A discrete card always wins over the CPU's graphics, whatever the vendor.
    return g.integrated ? s - 25 : s;
}

Info Make(Vendor v, const wchar_t* name) {
    return Classify(v, name);
}

const wchar_t kClassKey[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";

// Dedicated memory in MB from a display-adapter driver key (0 when not reported).
unsigned VramOfKey(HKEY k) {
    unsigned long long q = 0;
    DWORD type = 0, cb = sizeof(q);
    if (RegQueryValueExW(k, L"HardwareInformation.qwMemorySize", nullptr, &type, (LPBYTE)&q, &cb) ==
            ERROR_SUCCESS &&
        (type == REG_QWORD || type == REG_BINARY) && cb >= 4)
        return (unsigned)(q >> 20);
    DWORD d = 0;
    cb = sizeof(d);
    if (RegQueryValueExW(k, L"HardwareInformation.MemorySize", nullptr, &type, (LPBYTE)&d, &cb) ==
            ERROR_SUCCESS &&
        (type == REG_DWORD || type == REG_BINARY) && cb == 4)
        return (unsigned)(d >> 20);
    return 0;
}

bool ReadDesc(HKEY k, wchar_t* desc, DWORD capBytes) {
    memset(desc, 0, capBytes);
    DWORD cb = capBytes - sizeof(wchar_t);
    DWORD type = 0;
    return RegQueryValueExW(k, L"DriverDesc", nullptr, &type, (LPBYTE)desc, &cb) == ERROR_SUCCESS &&
           type == REG_SZ;
}

unsigned VramFor(const wchar_t* name) {
    unsigned mb = 0;
    HKEY root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kClassKey, 0, KEY_READ, &root) != ERROR_SUCCESS) return 0;
    wchar_t sub[16], desc[256];
    for (int i = 0; i < 32 && !mb; ++i) {
        _snwprintf(sub, 16, L"%04d", i);
        HKEY k;
        if (RegOpenKeyExW(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) continue;
        if (ReadDesc(k, desc, sizeof(desc)) && _wcsicmp(desc, name) == 0) mb = VramOfKey(k);
        RegCloseKey(k);
    }
    RegCloseKey(root);
    return mb;
}

void Finish(Info& g, unsigned vramMb) {
    g.vramMb = vramMb;
    // An APU reports a small carve-out; a card with real VRAM is never integrated.
    if (vramMb >= 6144) g.integrated = false;
}

}  // namespace

Info Classify(Vendor v, const wchar_t* name) {
    Info g;
    memset(&g, 0, sizeof(g));
    g.vendor = v;
    ToNarrow(name, g.name, sizeof(g.name));

    const bool arc = (v == VendorIntel) && WContainsNoCase(name, L"arc");
    const int n = FirstModel(g.name);
    if (v == VendorNvidia)      g.series = NvSeries(n);
    else if (v == VendorAmd)    g.series = AmdSeries(n);

    g.supportsDlss = (v == VendorNvidia) && (g.series >= 20);
    g.supportsXess = (v == VendorIntel) && arc && HasArcModel(name ? name : L"");
    g.model = n;
    g.integrated = IntegratedByName(v, name);
    return g;
}

bool IntegratedByName(Vendor v, const wchar_t* name) {
    if (!name) return false;
    switch (v) {
        case VendorIntel:
            return !(WContainsNoCase(name, L"arc") && HasArcModel(name));
        case VendorAmd:
            return WContainsNoCase(name, L"graphics") ||
                   (!WContainsNoCase(name, L"rx") &&
                    (WContainsNoCase(name, L"vega") || HasMobileIgpuModel(name)));
        default:
            return false;
    }
}

bool LowVram(const Info& g) {
    // 6 GB cards report 6144 MB; 8 GB cards handle the tuned values fine (0.3.3).
    return g.integrated || (g.vramMb > 0 && g.vramMb <= 6400);
}

profile::Preset Recommend(const Info& g) {
    if (g.vendor == VendorUnknown) return profile::Preset::Balanced;
    if (g.integrated || (g.vramMb > 0 && g.vramMb <= 4352)) return profile::Preset::Potato;
    const int m = g.model;
    if (g.vendor == VendorNvidia) {
        if (g.series == 10 || g.series == 16 || NContainsNoCase(g.name, "mx"))
            return profile::Preset::Potato;
        if ((g.series == 40 && m >= 4080) ||
            (g.series == 50 && (m >= 5080 || (m == 5070 && NContainsNoCase(g.name, " ti")))))
            return profile::Preset::Quality;
        return profile::Preset::Balanced;
    }
    if (g.vendor == VendorAmd) {
        if ((m >= 7900 && m < 8000) || m >= 9070) return profile::Preset::Quality;
        if (g.series <= 5000 || m == 6400 || m == 6500) return profile::Preset::Potato;
        return profile::Preset::Balanced;
    }
    return profile::Preset::Balanced;
}

bool TinyVram(const Info& g) {
    return !g.integrated && g.vramMb > 0 && g.vramMb <= 4352;
}

void DriverLabel(Vendor v, const char* ver, const char* radeon, char* out, size_t cap) {
    if (!cap) return;
    out[0] = 0;
    if (v == VendorAmd && radeon && *radeon) {
        _snprintf(out, cap, "%s", radeon);
        out[cap - 1] = 0;
        return;
    }
    if (!ver || !*ver) return;
    if (v == VendorNvidia) {
        // 32.0.15.6614: the last five digits of the last two fields are the release.
        const char* dot = strchr(ver, '.');
        dot = dot ? strchr(dot + 1, '.') : nullptr;
        char digits[32];
        int nd = 0;
        if (dot)
            for (const char* c = dot + 1; *c && nd < 31; ++c)
                if (*c >= '0' && *c <= '9') digits[nd++] = *c;
        digits[nd] = 0;
        if (nd >= 5) {
            const char* d = digits + nd - 5;
            _snprintf(out, cap, "%d.%c%c", (d[0] - '0') * 100 + (d[1] - '0') * 10 + (d[2] - '0'), d[3], d[4]);
            out[cap - 1] = 0;
            return;
        }
    }
    _snprintf(out, cap, "%s", ver);
    out[cap - 1] = 0;
}

profile::Preset RecommendAt(const Info& g, int width, int height, bool weakCpu) {
    const profile::Preset p = Recommend(g);
    if (p == profile::Preset::Balanced && !weakCpu && g.vendor != VendorUnknown && width > 0 && height > 0 &&
        (long long)width * height >= 3500000)
        return profile::Preset::Potato;
    return p;
}

Info Pick(const Info* c, int n) {
    Info best;
    memset(&best, 0, sizeof(best));
    int bestScore = 0;
    for (int i = 0; i < n; ++i) {
        const int s = ScoreOf(c[i]);
        if (s > bestScore || (s == bestScore && s > 0 && c[i].vramMb > best.vramMb)) {
            bestScore = s;
            best = c[i];
        }
    }
    return best;
}

bool Fsr3Risky(const Info& g, const char** why) {
    const char* r = nullptr;
    if (g.vendor == VendorAmd && !g.integrated) {
        if (g.series < 5000) {
            r = "AMD card from before RDNA (RX 400 / 500, Vega)";
        } else {
            int year = 0, month = 0, patch = 0, dots = 0;
            for (const char* c = g.driver; *c; ++c) dots += *c == '.';
            if (dots == 2 && sscanf(g.driver, "%d.%d.%d", &year, &month, &patch) == 3 && year >= 15 &&
                year <= 40 && year < 24)
                r = "AMD driver older than 24.1";
        }
    }
    if (why) *why = r;
    return r != nullptr;
}

const char* VendorName(Vendor v) {
    switch (v) {
        case VendorNvidia: return "NVIDIA";
        case VendorAmd:    return "AMD";
        case VendorIntel:  return "Intel";
        default:           return "unknown";
    }
}

namespace {
void ReadSz(HKEY k, const wchar_t* value, char* out, size_t cap) {
    wchar_t w[64] = {0};
    DWORD cb = sizeof(w) - sizeof(wchar_t), type = 0;
    out[0] = 0;
    if (RegQueryValueExW(k, value, nullptr, &type, (LPBYTE)w, &cb) == ERROR_SUCCESS && type == REG_SZ)
        ToNarrow(w, out, cap);
}

// The driver version from a display-adapter driver key, as the vendor names it.
void DriverOfKey(Vendor v, HKEY k, char* out, size_t cap) {
    char ver[64], radeon[64];
    ReadSz(k, L"DriverVersion", ver, sizeof(ver));
    ReadSz(k, L"RadeonSoftwareVersion", radeon, sizeof(radeon));
    DriverLabel(v, ver, radeon, out, cap);
}
}  // namespace

Info Detect(char* seen, size_t seenCap) {
    Info cands[16];
    int n = 0;
    auto add = [&](Vendor v, const wchar_t* name, unsigned vram, const char* driver) {
        if (n >= 16 || v == VendorUnknown || !name || !*name) return;
        if (WContainsNoCase(name, L"microsoft basic") || WContainsNoCase(name, L"remote")) return;
        Info c = Make(v, name);
        Finish(c, vram);
        _snprintf(c.driver, sizeof(c.driver), "%s", driver ? driver : "");
        c.driver[sizeof(c.driver) - 1] = 0;
        for (int i = 0; i < n; ++i) {
            if (!strcmp(cands[i].name, c.name)) {
                if (!cands[i].vramMb && c.vramMb) cands[i] = c;
                if (!cands[i].driver[0] && c.driver[0]) memcpy(cands[i].driver, c.driver, sizeof(c.driver));
                return;
            }
        }
        cands[n++] = c;
    };

    // 1. The video device map (0.3.5). HARDWARE is rebuilt at every boot, so it holds
    // only adapters present now, including a GPU with no screen attached (the NVIDIA
    // card of a hybrid laptop), which EnumDisplayDevicesW often leaves out.
    {
        HKEY map;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\VIDEO", 0, KEY_READ, &map) ==
            ERROR_SUCCESS) {
            for (DWORD i = 0; i < 64; ++i) {
                wchar_t name[64], val[512];
                DWORD nameLen = 64, valBytes = sizeof(val) - sizeof(wchar_t), type = 0;
                memset(val, 0, sizeof(val));
                const LONG r = RegEnumValueW(map, i, name, &nameLen, nullptr, &type, (LPBYTE)val, &valBytes);
                if (r == ERROR_NO_MORE_ITEMS) break;
                if (r != ERROR_SUCCESS || type != REG_SZ) continue;
                const wchar_t kPrefix[] = L"\\Registry\\Machine\\";
                const size_t pl = wcslen(kPrefix);
                if (_wcsnicmp(val, kPrefix, pl) != 0) continue;
                HKEY k;
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, val + pl, 0, KEY_READ, &k) != ERROR_SUCCESS) continue;
                wchar_t desc[256];
                if (ReadDesc(k, desc, sizeof(desc))) {
                    const Vendor v = FromName(desc);
                    char drv[48];
                    DriverOfKey(v, k, drv, sizeof(drv));
                    add(v, desc, VramOfKey(k), drv);
                }
                RegCloseKey(k);
            }
            RegCloseKey(map);
        }
    }

    // 2. Adapters attached to the desktop.
    DISPLAY_DEVICEW dd;
    for (DWORD i = 0;; ++i) {
        memset(&dd, 0, sizeof(dd));
        dd.cb = sizeof(dd);
        if (!EnumDisplayDevicesW(nullptr, i, &dd, 0)) break;
        Vendor v = FromId(dd.DeviceID);
        if (v == VendorUnknown) v = FromName(dd.DeviceString);
        add(v, dd.DeviceString, VramFor(dd.DeviceString), nullptr);
    }

    // 3. Nothing found: the display-adapter class key (remote / VM sessions).
    if (n == 0) {
        HKEY root;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kClassKey, 0, KEY_READ, &root) == ERROR_SUCCESS) {
            wchar_t sub[16], desc[256];
            for (int i = 0; i < 32; ++i) {
                _snwprintf(sub, 16, L"%04d", i);
                HKEY k;
                if (RegOpenKeyExW(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) continue;
                if (ReadDesc(k, desc, sizeof(desc))) {
                    const Vendor v = FromName(desc);
                    char drv[48];
                    DriverOfKey(v, k, drv, sizeof(drv));
                    add(v, desc, VramOfKey(k), drv);
                }
                RegCloseKey(k);
            }
            RegCloseKey(root);
        }
    }

    if (seen && seenCap) {
        seen[0] = 0;
        size_t used = 0;
        for (int i = 0; i < n && used + 1 < seenCap; ++i) {
            const int w = _snprintf(seen + used, seenCap - used, "%s%s (%u MB%s)", i ? ", " : "",
                                    cands[i].name, cands[i].vramMb,
                                    cands[i].integrated ? ", integrated" : "");
            if (w < 0) {
                seen[seenCap - 1] = 0;
                break;
            }
            used += (size_t)w;
        }
        seen[seenCap - 1] = 0;
    }
    return Pick(cands, n);
}

}  // namespace gpu
