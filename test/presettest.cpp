// Checks that each preset writes its key values into a settings file.
#include <stdio.h>
#include <string.h>
#include "profile.h"

static char g_in[1 << 16];
static char g_out[1 << 16];

static int Expect(const char* name, const char* needle, bool want) {
    const bool got = strstr(g_out, needle) != nullptr;
    printf("  [%s] %-9s %s%s\n", got == want ? "ok  " : "FAIL", name, want ? "" : "no ",
           needle);
    return got == want ? 0 : 1;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "test/sample_settings.xml";
    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("cannot open %s\n", path);
        return 2;
    }
    const size_t n = fread(g_in, 1, sizeof(g_in) - 1, f);
    fclose(f);
    g_in[n] = 0;

    int fails = 0;
    profile::Transform(g_in, g_out, sizeof(g_out), profile::Upscaler::Keep,
                       profile::Preset::Balanced);
    fails += Expect("balanced", "<rt_ao enabled=\"false\"", true);
    fails += Expect("balanced", "<rt_reflections enabled=\"true\"", true);

    profile::Transform(g_in, g_out, sizeof(g_out), profile::Upscaler::Keep,
                       profile::Preset::Quality);
    fails += Expect("quality", "<rt_ao enabled=\"true\"", true);
    fails += Expect("quality", "<rt_ddgi enabled=\"true\"", true);
    fails += Expect("quality", "worldShadowMapSize=\"4096\"", true);

    profile::Transform(g_in, g_out, sizeof(g_out), profile::Upscaler::Keep,
                       profile::Preset::Potato);
    fails += Expect("potato", "<rt_reflections enabled=\"false\"", true);
    fails += Expect("potato", "<rt_shadows enabled=\"false\"", true);
    fails += Expect("potato", "worldShadowMapSize=\"1024\"", true);
    fails += Expect("potato", "volumetricFogMapWidth=\"256\"", true);
    fails += Expect("potato", "mirrorsUpdateMode=\"both\"", true);  // mirrors untouched

    printf(fails ? "PRESETTEST FAILED\n" : "PRESETTEST PASSED\n");
    return fails ? 1 : 0;
}
