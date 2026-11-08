// Prints what the mod detects on this machine. No game needed.
//   gputest.exe
#include <stdio.h>
#include "gpu.h"

static int g_fails = 0;
static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_fails;
}

// 0.3.5: hybrid laptops pick the discrete GPU; Recommended steps down at 1440p+.
static void Test035() {
    gpu::Info igpu = gpu::Classify(gpu::VendorAmd, L"AMD Radeon(TM) Graphics");
    igpu.vramMb = 512;
    gpu::Info dgpu = gpu::Classify(gpu::VendorNvidia, L"NVIDIA GeForce RTX 3050 Laptop GPU");
    dgpu.vramMb = 4096;
    gpu::Info a[2] = {igpu, dgpu}, b[2] = {dgpu, igpu};
    Check(gpu::Pick(a, 2).vendor == gpu::VendorNvidia, "laptop: RTX 3050 over Radeon iGPU");
    Check(gpu::Pick(b, 2).vendor == gpu::VendorNvidia, "laptop: order does not matter");
    Check(gpu::Pick(a, 0).vendor == gpu::VendorUnknown, "no adapters: unknown");
    gpu::Info mid = gpu::Classify(gpu::VendorNvidia, L"NVIDIA GeForce RTX 4070");
    mid.vramMb = 12282;
    Check(gpu::RecommendAt(mid, 1920, 1080) == profile::Preset::Balanced, "4070 at 1080p: balanced");
    Check(gpu::RecommendAt(mid, 2560, 1080) == profile::Preset::Balanced, "4070 at 2560x1080: balanced");
    Check(gpu::RecommendAt(mid, 2560, 1440) == profile::Preset::Potato, "4070 at 1440p: potato");
    Check(gpu::RecommendAt(mid, 3840, 2160) == profile::Preset::Potato, "4070 at 4K: potato");
    Check(gpu::RecommendAt(mid, 0, 0) == profile::Preset::Balanced, "unknown resolution: no step");
    gpu::Info top = gpu::Classify(gpu::VendorNvidia, L"NVIDIA GeForce RTX 5080");
    top.vramMb = 16303;
    Check(gpu::RecommendAt(top, 3840, 2160) == profile::Preset::Quality, "5080 at 4K: quality stays");
}

int main(void) {
    Test035();
    char seen[512];
    const gpu::Info g = gpu::Detect(seen, sizeof(seen));
    printf("found  : %s\n", seen);
    printf("vendor : %s\n", gpu::VendorName(g.vendor));
    printf("name   : %s\n", g.name[0] ? g.name : "(not detected)");
    printf("series : %d\n", g.series);
    printf("dlss   : %s\n", g.supportsDlss ? "yes" : "no");
    printf("xess   : %s\n", g.supportsXess ? "yes" : "no");
    printf("model  : %d\n", g.model);
    printf("integr.: %s\n", g.integrated ? "yes" : "no");
    printf("vram   : %u MB%s\n", g.vramMb, g.vramMb ? "" : " (unknown)");
    printf("lowvram: %s\n", gpu::LowVram(g) ? "yes" : "no");
    printf("recomm.: %s\n", profile::PresetName(gpu::Recommend(g)));
    printf("at1440 : %s\n", profile::PresetName(gpu::RecommendAt(g, 2560, 1440)));
    printf(g_fails ? "GPUTEST FAILED\n" : "GPUTEST PASSED\n");
    return g_fails ? 1 : 0;
}
