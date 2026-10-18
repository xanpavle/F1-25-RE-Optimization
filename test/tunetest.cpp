// Self-test for the ini parser + override table.
// Expects f125_tune.ini next to this exe (test.bat copies it there).
#include <stdio.h>
#include <windows.h>
#include "tune.h"

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
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
