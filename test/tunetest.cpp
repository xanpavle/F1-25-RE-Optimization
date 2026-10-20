// Self-test for the ini parser + override table.
// Expects f125_tune.ini next to this exe (test.bat copies it there).
#include <stdio.h>
#include <windows.h>
#include "tune.h"

static int Check(const char* label, profile::Upscaler got, profile::Upscaler want) {
    printf("%s: %d (expect %d)\n", label, (int)got, (int)want);
    return got == want;
}

int main(void) {
    tune::SetSelf(GetModuleHandleW(nullptr));
    tune::Init();

    uint8_t b;
    uint32_t s;

    const int vrs = tune::LookupBool("Renderer.Gfx.enableVRS", &b) ? b : -1;
    const int async_ = tune::LookupBool("Renderer.Gfx.enableAsyncCompute", &b) ? b : -1;
    const int maxInst =
        tune::LookupScalar("Renderer.Gfx.groundCoverMaxInstances", &s) ? (int)s : -1;
    const int missing = tune::LookupBool("Nope.Nope", &b) ? 1 : 0;
    const int vrsScalar = tune::LookupScalar("Renderer.Gfx.enableVRS", &s) ? (int)s : -1;

    printf("VRS(bool)=%d (expect 1)\n", vrs);
    printf("async(bool)=%d (expect 1)\n", async_);
    printf("groundCoverMaxInstances=%d (expect 40000)\n", maxInst);
    printf("missing=%d (expect 0)\n", missing);
    printf("VRS(scalar)=%d (expect 1)\n", vrsScalar);

    const int ok = (vrs == 1 && async_ == 1 && maxInst == 40000 && missing == 0 &&
                    vrsScalar == 1);

    // GPU -> upscaler mapping (auto and forced modes).
    gpu::Info nv{};       nv.vendor = gpu::VendorNvidia; nv.supportsDlss = true;
    gpu::Info gtx{};      gtx.vendor = gpu::VendorNvidia; gtx.supportsDlss = false;
    gpu::Info amd{};      amd.vendor = gpu::VendorAmd;
    gpu::Info arc{};      arc.vendor = gpu::VendorIntel; arc.supportsXess = true;
    gpu::Info igpu{};     igpu.vendor = gpu::VendorIntel; igpu.supportsXess = false;
    gpu::Info unknown{};

    int map = 1;
    map &= Check("auto NV RTX -> DLSS", tune::testing::ChooseUpscaler(nv, "auto"),
                 profile::Upscaler::Dlss);
    map &= Check("auto NV GTX -> FSR3", tune::testing::ChooseUpscaler(gtx, "auto"),
                 profile::Upscaler::Fsr3);
    map &= Check("auto AMD -> FSR3", tune::testing::ChooseUpscaler(amd, "auto"),
                 profile::Upscaler::Fsr3);
    map &= Check("auto Intel Arc -> XeSS", tune::testing::ChooseUpscaler(arc, "auto"),
                 profile::Upscaler::Xess);
    map &= Check("auto Intel iGPU -> FSR3", tune::testing::ChooseUpscaler(igpu, "auto"),
                 profile::Upscaler::Fsr3);
    map &= Check("auto unknown -> keep", tune::testing::ChooseUpscaler(unknown, "auto"),
                 profile::Upscaler::Keep);
    map &= Check("forced taa", tune::testing::ChooseUpscaler(unknown, "taa"),
                 profile::Upscaler::Taa);
    map &= Check("forced dlss", tune::testing::ChooseUpscaler(amd, "dlss"),
                 profile::Upscaler::Dlss);
    map &= Check("forced fsr3", tune::testing::ChooseUpscaler(nv, "fsr3"),
                 profile::Upscaler::Fsr3);
    map &= Check("forced xess", tune::testing::ChooseUpscaler(amd, "xess"),
                 profile::Upscaler::Xess);
    map &= Check("forced off", tune::testing::ChooseUpscaler(nv, "off"),
                 profile::Upscaler::Off);
    map &= Check("forced keep", tune::testing::ChooseUpscaler(nv, "keep"),
                 profile::Upscaler::Keep);

    const int all = ok && map;
    printf(all ? "PASS\n" : "FAIL\n");
    return all ? 0 : 1;
}
