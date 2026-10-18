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
    return 0;
}
