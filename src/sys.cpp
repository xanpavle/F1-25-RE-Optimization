#include "sys.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_4.h>
#include <cpuid.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace sys {
namespace {

const char* FindNoCase(const char* hay, const char* needle) {
    const size_t n = strlen(needle);
    for (const char* p = hay; *p; ++p) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) ++i;
        if (i == n) return p;
    }
    return nullptr;
}

// The model number after "i3-" / "i5-" / "i7-" / "i9-" (4770 in "i7-4770K"), 0 when none.
int IntelCoreModel(const char* name) {
    for (const char* p = name; *p; ++p) {
        if ((p[0] == 'i' || p[0] == 'I') && (p[1] == '3' || p[1] == '5' || p[1] == '7' || p[1] == '9') &&
            p[2] == '-' && isdigit((unsigned char)p[3]) && (p == name || !isalnum((unsigned char)p[-1]))) {
            int n = 0, digits = 0;
            for (const char* q = p + 3; isdigit((unsigned char)*q) && digits < 6; ++q, ++digits)
                n = n * 10 + (*q - '0');
            return digits >= 3 ? n : 0;
        }
    }
    return 0;
}

}  // namespace

bool WeakCpu(const char* name, int cores, int threads, const char** why) {
    const char* r = nullptr;
    (void)cores;
    if (threads > 0 && threads <= 4) {
        r = "4 threads or fewer";
    } else if (name && *name) {
        const int m = IntelCoreModel(name);
        // 4 digits: 2nd to 9th generation (2xxx..9xxx); 5 digits: 10th and later.
        if (FindNoCase(name, "intel") && !FindNoCase(name, "ultra") && m >= 2000 && m <= 7999)
            r = "Intel Core 7th generation or older";
        else if (FindNoCase(name, "core(tm)2") || FindNoCase(name, "core 2") || FindNoCase(name, "pentium") ||
                 FindNoCase(name, "celeron"))
            r = "older or entry-level Intel CPU";
        else if (FindNoCase(name, "amd") &&
                 (FindNoCase(name, " fx") || FindNoCase(name, "phenom") || FindNoCase(name, "athlon") ||
                  FindNoCase(name, " a4-") || FindNoCase(name, " a6-") || FindNoCase(name, " a8-") ||
                  FindNoCase(name, " a10-") || FindNoCase(name, " a12-")))
            r = "older AMD CPU (FX, Phenom, Athlon or A-series)";
    }
    if (why) *why = r;
    return r != nullptr;
}

void PowerText(const Power& p, char* out, size_t cap) {
    if (!cap) return;
    if (!p.known) {
        _snprintf(out, cap, "unknown");
    } else if (!p.hasBattery) {
        _snprintf(out, cap, "no battery (desktop)");
    } else {
        char pct[16] = "";
        if (p.onBattery && p.percent >= 0) _snprintf(pct, sizeof(pct), " (%d%%)", p.percent);
        _snprintf(out, cap, "%s%s%s", p.onBattery ? "on battery" : "plugged in", pct,
                  p.saver ? ", battery saver on" : "");
    }
    out[cap - 1] = 0;
}

const char* PowerNote(const Power& p) {
    if (!p.known || !p.hasBattery) return nullptr;
    if (p.onBattery)
        return "this laptop is running on battery: Windows and the GPU driver cut GPU and CPU power, "
               "which can halve fps; plug it in for full speed";
    if (p.saver) return "battery saver is on: it limits CPU and GPU power and can cost a lot of fps";
    return nullptr;
}

Cpu ReadCpu() {
    Cpu c;
    memset(&c, 0, sizeof(c));
    unsigned regs[12] = {0};
    unsigned a, b, cc, d;
    if (__get_cpuid(0x80000000u, &a, &b, &cc, &d) && a >= 0x80000004u) {
        for (unsigned i = 0; i < 3; ++i)
            __get_cpuid(0x80000002u + i, &regs[i * 4], &regs[i * 4 + 1], &regs[i * 4 + 2], &regs[i * 4 + 3]);
        char raw[49] = {0};
        memcpy(raw, regs, 48);
        const char* s = raw;
        while (*s == ' ') ++s;
        _snprintf(c.name, sizeof(c.name), "%s", s);
        c.name[sizeof(c.name) - 1] = 0;
        for (size_t n = strlen(c.name); n && c.name[n - 1] == ' '; --n) c.name[n - 1] = 0;
    }
    c.threads = (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (len && len < (1u << 20)) {
        char* buf = (char*)malloc(len);
        if (buf && GetLogicalProcessorInformationEx(RelationProcessorCore,
                                                    (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf, &len)) {
            for (DWORD off = 0; off < len;) {
                PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX e = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buf + off);
                if (!e->Size) break;
                if (e->Relationship == RelationProcessorCore) ++c.cores;
                off += e->Size;
            }
        }
        free(buf);
    }
    return c;
}

Power ReadPower() {
    Power p = {false, false, false, false, -1};
    SYSTEM_POWER_STATUS s;
    if (!GetSystemPowerStatus(&s)) return p;
    p.known = true;
    p.hasBattery = s.BatteryFlag != 128 && s.BatteryFlag != 255;
    p.onBattery = p.hasBattery && s.ACLineStatus == 0;
    p.saver = s.SystemStatusFlag == 1;
    p.percent = s.BatteryLifePercent <= 100 ? (int)s.BatteryLifePercent : -1;
    return p;
}

const char* WineVersion() {
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    if (!nt) return nullptr;
    typedef const char* (*Fn)();
    Fn f = (Fn)(void*)GetProcAddress(nt, "wine_get_version");
    return f ? f() : nullptr;
}

bool VramUsage(unsigned long long* used, unsigned long long* budget) {
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    if (!dxgi) return false;
    typedef HRESULT(WINAPI * CreateFn)(REFIID, void**);
    CreateFn create = (CreateFn)(void*)GetProcAddress(dxgi, "CreateDXGIFactory1");
    IDXGIFactory1* factory = nullptr;
    bool ok = false;
    if (create && SUCCEEDED(create(__uuidof(IDXGIFactory1), (void**)&factory)) && factory) {
        IDXGIAdapter1* best = nullptr;
        SIZE_T bestMem = 0;
        IDXGIAdapter1* a = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 d;
            if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                d.DedicatedVideoMemory > bestMem) {
                if (best) best->Release();
                best = a;
                bestMem = d.DedicatedVideoMemory;
            } else {
                a->Release();
            }
        }
        IDXGIAdapter3* a3 = nullptr;
        if (best && SUCCEEDED(best->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3)) && a3) {
            DXGI_QUERY_VIDEO_MEMORY_INFO info;
            if (SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)) && info.Budget) {
                *used = info.CurrentUsage;
                *budget = info.Budget;
                ok = true;
            }
            a3->Release();
        }
        if (best) best->Release();
        factory->Release();
    }
    FreeLibrary(dxgi);
    return ok;
}

VramMemory VramParse(const char* text) {
    VramMemory m = {0, 0};
    int pct = 0;
    unsigned mb = 0;
    if (text && sscanf(text, "%d %u", &pct, &mb) == 2 && pct > 0 && pct < 1000 && mb > 0 && mb < (1u << 20)) {
        m.pct = pct;
        m.vramMb = mb;
    }
    return m;
}

void VramFormat(const VramMemory& m, char* out, size_t cap) {
    if (!cap) return;
    _snprintf(out, cap, "%d %u\r\n", m.pct, m.vramMb);
    out[cap - 1] = 0;
}

bool VramTight(const VramMemory& m, unsigned currentVramMb) {
    return m.pct >= 100 && m.vramMb && currentVramMb && m.vramMb == currentVramMb;
}

VramMemory VramAfter(const VramMemory& prev, bool guardOn, int pctNow, unsigned vramMb) {
    VramMemory m = {0, vramMb};
    if (guardOn && pctNow < 75) {
        m.pct = pctNow;
        return m;
    }
    int keep = prev.vramMb == vramMb ? prev.pct : 0;
    if (guardOn && keep < 100) keep = 100;
    m.pct = pctNow > keep ? pctNow : keep;
    return m;
}

}  // namespace sys
