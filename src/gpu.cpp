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

int ScoreOf(Vendor v, bool arc) {
    switch (v) {
        case VendorNvidia: return 30;
        case VendorAmd:    return 28;
        case VendorIntel:  return arc ? 20 : 5;
        default:           return 0;
    }
}

Info Make(Vendor v, const wchar_t* name) {
    Info g;
    memset(&g, 0, sizeof(g));
    g.vendor = v;
    ToNarrow(name, g.name, sizeof(g.name));

    const bool arc = (v == VendorIntel) && WContainsNoCase(name, L"arc");
    const int n = FirstModel(g.name);
    if (v == VendorNvidia)      g.series = NvSeries(n);
    else if (v == VendorAmd)    g.series = AmdSeries(n);

    g.supportsDlss = (v == VendorNvidia) && (g.series >= 20);
    g.supportsXess = (v == VendorIntel) && arc;
    return g;
}

}  // namespace

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
        const int score = ScoreOf(v, cand.supportsXess);
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
                        const Info cand = Make(v, desc);
                        const int score = ScoreOf(v, cand.supportsXess);
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
