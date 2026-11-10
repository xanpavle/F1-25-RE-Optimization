// Prints what the mod detects on this machine. No game needed.
//   gputest.exe
#include <stdio.h>
#include <string.h>
#include "gpu.h"
#include "sys.h"

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

static const char* EntryVal(const profile::Entry* e, int n, const char* tag, const char* attr) {
    for (int i = 0; i < n; ++i)
        if (!strcmp(e[i].tag, tag) && !strcmp(e[i].attr, attr)) return e[i].val;
    return "";
}

// 0.3.6: weak CPU, 4 GB cards, driver versions, power.
static void Test036() {
    Check(sys::WeakCpu("Intel(R) Core(TM) i7-4770 CPU @ 3.40GHz", 4, 8), "i7-4770: weak");
    Check(sys::WeakCpu("Intel(R) Core(TM) i3-9100F CPU @ 3.60GHz", 4, 4), "4 threads: weak");
    Check(sys::WeakCpu("AMD FX(tm)-8350 Eight-Core Processor", 4, 8), "FX-8350: weak");
    Check(!sys::WeakCpu("Intel(R) Core(TM) i5-8400 CPU @ 2.80GHz", 6, 6), "i5-8400: not weak");
    Check(!sys::WeakCpu("13th Gen Intel(R) Core(TM) i5-13400F", 10, 16), "i5-13400F: not weak");
    Check(!sys::WeakCpu("Intel(R) Core(TM) Ultra 7 155H", 16, 22), "Core Ultra: not weak");
    Check(!sys::WeakCpu("AMD Ryzen 5 3600 6-Core Processor", 6, 12), "Ryzen 5 3600: not weak");
    gpu::Info mid = gpu::Classify(gpu::VendorNvidia, L"NVIDIA GeForce RTX 4060");
    mid.vramMb = 8188;
    Check(gpu::RecommendAt(mid, 2560, 1377) == profile::Preset::Potato, "4060 at 2560x1377: potato");
    Check(gpu::RecommendAt(mid, 2560, 1377, true) == profile::Preset::Balanced, "...weak CPU: balanced");
    gpu::Info small = gpu::Classify(gpu::VendorNvidia, L"NVIDIA GeForce GTX 1650 Ti");
    small.vramMb = 4096;
    Check(gpu::TinyVram(small) && !gpu::TinyVram(mid), "4 GB texture guard: 1650 Ti yes, 4060 no");
    char d[48];
    gpu::DriverLabel(gpu::VendorNvidia, "32.0.15.6614", "", d, sizeof(d));
    Check(!strcmp(d, "566.14"), "NVIDIA driver 566.14");
    gpu::DriverLabel(gpu::VendorAmd, "31.0.24027.1012", "24.10.1", d, sizeof(d));
    Check(!strcmp(d, "24.10.1"), "AMD driver 24.10.1");
    gpu::DriverLabel(gpu::VendorIntel, "32.0.101.6078", nullptr, d, sizeof(d));
    Check(!strcmp(d, "32.0.101.6078"), "Intel driver raw");
    profile::Entry e[128];
    int n = profile::Entries(profile::Preset::Balanced, e, 128, true, false);
    Check(!strcmp(EntryVal(e, n, "crowd", "distanceScale"), "0.5"), "weak CPU: crowd 0.5 on balanced");
    Check(!strcmp(EntryVal(e, n, "crowd", "billboardDistance"), "1000"), "weak CPU: billboards 1000");
    n = profile::Entries(profile::Preset::Potato, e, 128, true, false);
    Check(!strcmp(EntryVal(e, n, "crowd", "distanceScale"), "0.2"), "weak CPU never raises potato's crowd");
    n = profile::Entries(profile::Preset::Quality, e, 128, false, true);
    Check(!strcmp(EntryVal(e, n, "texture_streaming", "texelDensityBias"), "1.0"), "4 GB: textures at potato");
    n = profile::Entries(profile::Preset::Balanced, e, 128);
    Check(!strcmp(EntryVal(e, n, "crowd", "billboardDistance"), "2500"), "normal CPU: balanced unchanged");
    sys::Power bat = {true, true, true, false, 40};
    char pt[96];
    sys::PowerText(bat, pt, sizeof(pt));
    Check(!strcmp(pt, "on battery (40%)") && sys::PowerNote(bat), "power: on battery note");
    sys::Power desk = {true, false, false, false, -1};
    sys::PowerText(desk, pt, sizeof(pt));
    Check(!strcmp(pt, "no battery (desktop)") && !sys::PowerNote(desk), "power: desktop, no note");
}

int main(void) {
    Test035();
    Test036();
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
    printf("driver : %s\n", g.driver[0] ? g.driver : "unknown");
    const sys::Cpu c = sys::ReadCpu();
    const char* why = nullptr;
    printf("cpu    : %s, %d cores / %d threads, weak %s\n", c.name, c.cores, c.threads,
           sys::WeakCpu(c.name, c.cores, c.threads, &why) ? why : "no");
    char pt[96];
    sys::PowerText(sys::ReadPower(), pt, sizeof(pt));
    printf("power  : %s\n", pt);
    printf(g_fails ? "GPUTEST FAILED\n" : "GPUTEST PASSED\n");
    return g_fails ? 1 : 0;
}
