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
    gpu::Info amd{};      amd.vendor = gpu::VendorAmd; amd.series = 7000;  // RDNA (0.3.8: pre-RDNA keeps the upscaler)
    gpu::Info arc{};      arc.vendor = gpu::VendorIntel; arc.supportsXess = true;
    gpu::Info igpu{};     igpu.vendor = gpu::VendorIntel; igpu.supportsXess = false;
    igpu.integrated = true;
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
    // 0.3.2: integrated GPUs keep the player's own upscaler.
    map &= Check("auto Intel iGPU -> keep", tune::testing::ChooseUpscaler(igpu, "auto"),
                 profile::Upscaler::Keep);
    map &= Check("forced fsr3 on iGPU", tune::testing::ChooseUpscaler(igpu, "fsr3"),
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

    // 0.3.2: integrated detection, VRAM guard and the Recommended preset.
    int g032 = 1;
    auto T = [&](const char* label, bool cond) {
        printf("%s: %s\n", label, cond ? "ok" : "FAIL");
        g032 &= cond ? 1 : 0;
    };
    using gpu::Classify;
    T("APU Radeon(TM) Graphics integrated", Classify(gpu::VendorAmd, L"AMD Radeon(TM) Graphics").integrated);
    T("Radeon 780M integrated", Classify(gpu::VendorAmd, L"AMD Radeon 780M Graphics").integrated);
    T("RX Vega 11 Graphics integrated", Classify(gpu::VendorAmd, L"AMD Radeon RX Vega 11 Graphics").integrated);
    T("RX Vega 64 discrete", !Classify(gpu::VendorAmd, L"AMD Radeon RX Vega 64").integrated);
    T("RX 6650 XT discrete", !Classify(gpu::VendorAmd, L"AMD Radeon RX 6650 XT").integrated);
    T("Iris Xe integrated", Classify(gpu::VendorIntel, L"Intel(R) Iris(R) Xe Graphics").integrated);
    T("Arc Graphics (MTL) integrated", Classify(gpu::VendorIntel, L"Intel(R) Arc(TM) Graphics").integrated);
    T("Arc B580 discrete", !Classify(gpu::VendorIntel, L"Intel(R) Arc(TM) B580 Graphics").integrated);
    T("APU auto -> keep",
      tune::testing::ChooseUpscaler(Classify(gpu::VendorAmd, L"AMD Radeon(TM) Graphics"), "auto") ==
          profile::Upscaler::Keep);
    gpu::Info v8 = Classify(gpu::VendorNvidia, L"NVIDIA GeForce RTX 3070");
    T("unknown VRAM not low", !gpu::LowVram(v8));
    v8.vramMb = 8192;
    T("8 GB not low (0.3.3)", !gpu::LowVram(v8));
    v8.vramMb = 6144;
    T("6 GB low", gpu::LowVram(v8));
    v8.vramMb = 12288;
    T("12 GB not low", !gpu::LowVram(v8));
    auto R = [](gpu::Vendor v, const wchar_t* n, unsigned vram) {
        gpu::Info i = gpu::Classify(v, n);
        i.vramMb = vram;
        return gpu::Recommend(i);
    };
    T("4090 quality", R(gpu::VendorNvidia, L"NVIDIA GeForce RTX 4090", 0) == profile::Preset::Quality);
    T("5070 Ti quality", R(gpu::VendorNvidia, L"NVIDIA GeForce RTX 5070 Ti", 0) == profile::Preset::Quality);
    T("5070 balanced", R(gpu::VendorNvidia, L"NVIDIA GeForce RTX 5070", 0) == profile::Preset::Balanced);
    T("GTX 1660 potato", R(gpu::VendorNvidia, L"NVIDIA GeForce GTX 1660 SUPER", 0) == profile::Preset::Potato);
    T("4 GB potato", R(gpu::VendorNvidia, L"NVIDIA GeForce RTX 3050", 4096) == profile::Preset::Potato);
    T("7900 XTX quality", R(gpu::VendorAmd, L"AMD Radeon RX 7900 XTX", 0) == profile::Preset::Quality);
    T("9070 XT quality", R(gpu::VendorAmd, L"AMD Radeon RX 9070 XT", 0) == profile::Preset::Quality);
    T("7800 XT balanced", R(gpu::VendorAmd, L"AMD Radeon RX 7800 XT", 0) == profile::Preset::Balanced);
    T("5700 XT potato", R(gpu::VendorAmd, L"AMD Radeon RX 5700 XT", 0) == profile::Preset::Potato);
    T("APU potato", R(gpu::VendorAmd, L"AMD Radeon(TM) Graphics", 0) == profile::Preset::Potato);
    T("Arc A770 balanced", R(gpu::VendorIntel, L"Intel(R) Arc(TM) A770 Graphics", 0) == profile::Preset::Balanced);

    // 0.3.2: crash safe mode steps.
    using tune::testing::SafeState;
    SafeState w;
    const char* why = nullptr;
    T("fresh: mod runs", !tune::testing::SafeStart({0, 0, 0}, &w, &why) && w.kind == 1 && !why);
    T("confirm -> ok", tune::testing::SafeConfirm(w).kind == 0);
    SafeState crash1;
    T("crash -> stock once", tune::testing::SafeStart({1, 0, 0}, &crash1, &why) && crash1.fails == 1 &&
                                 crash1.hold == 0 && why);
    SafeState back;
    T("after stock -> mod again", !tune::testing::SafeStart(tune::testing::SafeConfirm(crash1), &back, &why));
    SafeState hold;
    T("second crash -> hold 3", tune::testing::SafeStart(back, &hold, &why) && hold.fails == 2 && hold.hold == 2);
    SafeState h2, h3, h4;
    T("hold 2", tune::testing::SafeStart(tune::testing::SafeConfirm(hold), &h2, &why));
    T("hold 3", tune::testing::SafeStart(tune::testing::SafeConfirm(h2), &h3, &why));
    T("hold over", !tune::testing::SafeStart(tune::testing::SafeConfirm(h3), &h4, &why));
    SafeState s2;
    T("stock crashed too -> mod", !tune::testing::SafeStart(crash1, &s2, &why) && s2.kind == 1 && s2.fails == 0);
    char text[96];
    tune::testing::SafeFormat(hold, "0.3.2", text, sizeof(text));
    SafeState parsed;
    T("parse round trip", tune::testing::SafeParse(text, "0.3.2", &parsed) && parsed.kind == 2 &&
                              parsed.fails == 2 && parsed.hold == 2);
    T("new version starts clean", !tune::testing::SafeParse(text, "0.3.3", &parsed) && parsed.kind == 0);

    // 0.3.6: how the last launch ended survives the state file.
    SafeState clean = {0, 0, 0, 2};
    tune::testing::SafeFormat(clean, "0.3.6", text, sizeof(text));
    T("clean exit round trip", tune::testing::SafeParse(text, "0.3.6", &parsed) && parsed.end == 2 &&
                                   tune::testing::SafeLastLaunch(parsed) != nullptr);
    T("old 4-field state parses", tune::testing::SafeParse("ok 0 0 0.3.6\r\n", "0.3.6", &parsed) &&
                                      parsed.end == 0 && !tune::testing::SafeLastLaunch(parsed));
    T("confirm marks 90 s", tune::testing::SafeConfirm(w).end == 1 &&
                                tune::testing::SafeLastLaunch(tune::testing::SafeConfirm(w)) != nullptr);
    T("pending -> early crash line", tune::testing::SafeLastLaunch(w) != nullptr);

    const int all = ok && map && g032;
    printf(all ? "PASS\n" : "FAIL\n");
    return all ? 0 : 1;
}
