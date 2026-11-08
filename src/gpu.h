// GPU detection (vendor + series + upscaler support) for the mod.
//
// Detection is deliberately dependency-free and safe at DLL load: the video device
// map (HKLM\HARDWARE\DEVICEMAP\VIDEO, rebuilt every boot, 0.3.5: lists the
// render-only GPU of a hybrid laptop too), EnumDisplayDevicesW, and the
// display-adapter registry class as a fallback. No DXGI, no WMI, no SetupAPI.
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
// vendor == VendorUnknown when nothing usable was found. seen (optional) gets
// every adapter found, for the log.
Info Detect(char* seen = nullptr, size_t seenCap = 0);

// The adapter to tune for: highest score, then the most VRAM.
Info Pick(const Info* c, int n);

// Name-only classification (no OS calls); Detect() adds the VRAM.
Info Classify(Vendor v, const wchar_t* name);

// Name-only guess at integrated graphics (a card with >= 6 GB VRAM never is).
bool IntegratedByName(Vendor v, const wchar_t* name);

// 6 GB or less (when known), or integrated.
bool LowVram(const Info& g);

// The preset "recommended" resolves to for this GPU.
profile::Preset Recommend(const Info& g);

// 0.3.5: from the GPU and the game's resolution. A Balanced-tier GPU at 1440p or
// more (3.5 megapixels and up) steps down to Potato; 0 x 0 means unknown.
profile::Preset RecommendAt(const Info& g, int width, int height);

const char* VendorName(Vendor v);

}  // namespace gpu
