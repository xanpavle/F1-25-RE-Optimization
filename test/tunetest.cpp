#include <stdio.h>
#include <windows.h>
#include "tune.h"

int main(void) {
    tune::SetSelf(GetModuleHandleW(nullptr));
    tune::Init();

    uint8_t b;
    uint32_t s;

    int vrs = tune::LookupBool("Renderer.Gfx.enableVRS", &b) ? b : -1;
    int async = tune::LookupBool("Renderer.Gfx.enableAsyncCompute", &b) ? b : -1;
    int farclip = tune::LookupScalar("Renderer.Gfx.reflectionsFarClip", &s) ? (int)s : -1;
    float ff;
    memcpy(&ff, &s, 4);
    int missing = tune::LookupBool("Nope.Nope", &b) ? 1 : 0;
    int vrs_scalar = tune::LookupScalar("Renderer.Gfx.enableVRS", &s) ? (int)s : -1;

    printf("VRS(bool)=%d (expect 1)\n", vrs);
    printf("async(bool)=%d (expect 1)\n", async);
    printf("farClip(raw=%d float=%g) (expect 1120403456 / 100)\n", farclip, ff);
    printf("missing=%d (expect 0)\n", missing);
    printf("VRS(scalar)=%d (expect 1)\n", vrs_scalar);

    int ok = (vrs == 1 && async == 1 && farclip == 1120403456 && missing == 0 &&
              vrs_scalar == 1);
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
