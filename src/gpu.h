// GPU detection (vendor + series + upscaler support) for the mod.
//
// Detection is deliberately dependency-free: EnumDisplayDevicesW for the normal
// case, the display-adapter registry class as a fallback. No DXGI, no WMI.
// VRAM comes from the display driver's registry key (HardwareInformation.*).
#pragma once
#include "profile.h"

namespace gpu {

enum Vendor {
    VendorUnknown = 0,
    VendorNvidia,
    VendorAmd,
    VendorIntel,
};

struct Info {
    Vendor   vendor;
    int      series;        // 40 = RTX 40-series, 7000 = RX 7000, 0 = n/a
    char     name[128];     // e.g. "NVIDIA GeForce RTX 4070"
    bool     supportsDlss;  // NVIDIA RTX 20-series and newer
    bool     supportsXess;  // Intel Arc (discrete)
    int      model;         // 4070, 7800, 770 ... (0 when the name has none)
    bool     integrated;    // APU / iGPU: shares system memory
    unsigned vramMb;        // dedicated memory, 0 when the driver does not say
};

// Picks the "best" adapter present (discrete over integrated). Returns
// vendor == VendorUnknown when nothing usable was found.
Info Detect();

// Name-only classification (no OS calls); Detect() adds the VRAM.
Info Classify(Vendor v, const wchar_t* name);

// Name-only guess at integrated graphics (a card with >= 6 GB VRAM never is).
bool IntegratedByName(Vendor v, const wchar_t* name);

// 8 GB or less (when known), or integrated.
bool LowVram(const Info& g);

// The preset "recommended" resolves to for this GPU.
profile::Preset Recommend(const Info& g);

const char* VendorName(Vendor v);

}  // namespace gpu
