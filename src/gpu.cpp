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
    return g.integrated || (g.vramMb > 0 && g.vramMb <= 8448);
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

const char* VendorName(Vendor v) {
    switch (v) {
        case VendorNvidia: return "NVIDIA";
        case VendorAmd:    return "AMD";
        case VendorIntel:  return "Intel";
        default:           return "unknown";
    }
}

Info Detect() {
    Info best;
    memset(&best, 0, sizeof(best));
    int bestScore = -1;

    DISPLAY_DEVICEW dd;
    for (DWORD i = 0;; ++i) {
        memset(&dd, 0, sizeof(dd));
        dd.cb = sizeof(dd);
        if (!EnumDisplayDevicesW(nullptr, i, &dd, 0)) break;

        if (WContainsNoCase(dd.DeviceString, L"microsoft basic")) continue;
        if (WContainsNoCase(dd.DeviceString, L"remote")) continue;

        Vendor v = FromId(dd.DeviceID);
        if (v == VendorUnknown) v = FromName(dd.DeviceString);
        if (v == VendorUnknown) continue;

        Info cand = Make(v, dd.DeviceString);
        Finish(cand, VramFor(dd.DeviceString));
        const int score = ScoreOf(cand);
        if (score > bestScore) {
            bestScore = score;
            best = cand;
        }
    }

    if (bestScore < 0) {
        // Fallback: the display-adapter class key. Covers sessions where the
        // display devices are not enumerable (remote/VM, some laptops).
        HKEY root;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SYSTEM\\CurrentControlSet\\Control\\Class\\"
                          L"{4d36e968-e325-11ce-bfc1-08002be10318}",
                          0, KEY_READ, &root) == ERROR_SUCCESS) {
            wchar_t sub[16];
            for (int i = 0; i < 16; ++i) {
                _snwprintf(sub, 16, L"%04d", i);
                HKEY k;
                if (RegOpenKeyExW(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) continue;

                wchar_t desc[256];
                memset(desc, 0, sizeof(desc));
                DWORD cb = sizeof(desc);
                DWORD type = 0;
                if (RegQueryValueExW(k, L"DriverDesc", nullptr, &type,
                                     (LPBYTE)desc, &cb) == ERROR_SUCCESS) {
                    const Vendor v = FromName(desc);
                    if (v != VendorUnknown) {
                        Info cand = Make(v, desc);
                        Finish(cand, VramOfKey(k));
                        const int score = ScoreOf(cand);
                        if (score > bestScore) {
                            bestScore = score;
                            best = cand;
                        }
                    }
                }
                RegCloseKey(k);
            }
            RegCloseKey(root);
        }
    }

    return best;
}

}  // namespace gpu
