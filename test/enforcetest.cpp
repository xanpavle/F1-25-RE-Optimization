// Fail-safe test for the in-engine enforcement module.
//
// The real enforcement targets live inside F1_25.exe. This test runs in a
// process where they do not exist, so every detour must refuse to install and
// the module must stay inactive instead of patching something random.
#include <stdio.h>
#include "enforce.h"
#include "tune.h"

int main() {
    tune::SetSelf(nullptr);
    tune::Init();
    enforce::Init();

    if (enforce::Active()) {
        printf("FAIL: enforcement reported active with no F1_25.exe present\n");
        return 1;
    }
    printf("enforce: correctly inactive without the game (fail-safe)\n");

    // Calling Init() twice must not crash or double-patch.
    enforce::Init();
    printf("enforce: second Init() is harmless\n");

    printf("enforce test OK\n");
    return 0;
}
