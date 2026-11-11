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
    char     driver[48];    // 0.3.6: driver version as the vendor names it, "" when unknown
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

// 0.3.6: a discrete card with 4 GB or less (when known): textures stream at
// Potato's level on every preset.
bool TinyVram(const Info& g);

// 0.3.6: the driver version as the vendor shows it. NVIDIA "32.0.15.6614" ->
// "566.14"; AMD prefers the Radeon Software version; else the raw version.
void DriverLabel(Vendor v, const char* version, const char* radeon, char* out, size_t cap);

// The preset "recommended" resolves to for this GPU.
profile::Preset Recommend(const Info& g);

// 0.3.5: from the GPU and the game's resolution. A Balanced-tier GPU at 1440p or
// more (3.5 megapixels and up) steps down to Potato; 0 x 0 means unknown.
// 0.3.6: not on a weak CPU, where the CPU is the limit and Potato only looks worse.
profile::Preset RecommendAt(const Info& g, int width, int height, bool weakCpu = false);

const char* VendorName(Vendor v);

// 0.3.8: switching to FSR 3 is a risk here: an AMD card from before RDNA (RX 400 /
// 500, Vega, R9) or an AMD driver older than 24.1 ("pipeline state doesn't exist" on
// an RX 570 with driver 23.11.1). Upscaler auto then keeps the player's choice.
bool Fsr3Risky(const Info& g, const char** why = nullptr);

}  // namespace gpu
