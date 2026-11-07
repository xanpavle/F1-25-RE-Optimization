// Prints what the mod detects on this machine. No game needed.
//   gputest.exe
#include <stdio.h>
#include "gpu.h"

int main(void) {
    const gpu::Info g = gpu::Detect();
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
    return 0;
}
