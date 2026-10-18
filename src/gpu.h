// GPU detection (vendor + series + upscaler support) for the mod.
//
// Detection is deliberately dependency-free: EnumDisplayDevicesW for the normal
// case, the display-adapter registry class as a fallback. No DXGI, no WMI.
#pragma once

namespace gpu {

enum Vendor {
    VendorUnknown = 0,
    VendorNvidia,
    VendorAmd,
    VendorIntel,
};

struct Info {
    Vendor vendor;
    int    series;        // 40 = RTX 40-series, 7000 = RX 7000, 0 = n/a
    char   name[128];     // e.g. "NVIDIA GeForce RTX 4070"
    bool   supportsDlss;  // NVIDIA RTX 20-series and newer
    bool   supportsXess;  // Intel Arc
};

// Picks the "best" adapter present (discrete over integrated). Returns
// vendor == VendorUnknown when nothing usable was found.
Info Detect();

const char* VendorName(Vendor v);

}  // namespace gpu
