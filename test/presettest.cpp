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

    // --- 0.3.2: never raise a setting above the player's own ---
    static char low[1 << 16];
    strcpy(low, g_in);
    auto set = [&](const char* from, const char* to) {
        char* p = strstr(low, from);
        if (!p) { printf("  [FAIL] missing %s\n", from); ++fails; return; }
        const size_t fl = strlen(from), tl = strlen(to);
        memmove(p + tl, p + fl, strlen(p + fl) + 1);
        memcpy(p, to, tl);
    };
    set("worldShadowMapSize=\"4096\"", "worldShadowMapSize=\"1024\"");
    set("volumetricFogMapWidth=\"1920\"", "volumetricFogMapWidth=\"256\"");
    set("billboardDistance=\"4000\"", "billboardDistance=\"1000\"");
    set("texelDensityBias=\"0.0\"", "texelDensityBias=\"1.0\"");
    set("<rt_ao enabled=\"true\"", "<rt_ao enabled=\"false\"");
    set("<hair enabled=\"true\"", "<hair enabled=\"false\"");

    struct Cnt { int set, kept; } cnt = {0, 0};
    profile::Options o;
    o.user = &cnt;
    o.onChange = [](const char*, const char*, const char*, const char*, bool kept, void* u) {
        Cnt* c = (Cnt*)u;
        (kept ? c->kept : c->set)++;
    };
    profile::Transform(low, g_out, sizeof(g_out), o);
    fails += Expect("low+bal", "worldShadowMapSize=\"1024\"", true);
    fails += Expect("low+bal", "nightShadowMapSize=\"2048\"", true);
    fails += Expect("low+bal", "volumetricFogMapWidth=\"256\"", true);
    fails += Expect("low+bal", "billboardDistance=\"1000\"", true);
    fails += Expect("low+bal", "texelDensityBias=\"1.0\"", true);
    printf("  [%s] kept count %d (expect 4)\n", cnt.kept == 4 ? "ok  " : "FAIL", cnt.kept);
    fails += cnt.kept == 4 ? 0 : 1;

    o.preset = profile::Preset::Quality;
    o.onChange = nullptr;
    profile::Transform(low, g_out, sizeof(g_out), o);
    fails += Expect("low+qual", "<rt_ao enabled=\"false\"", true);
    fails += Expect("low+qual", "<hair enabled=\"false\"", true);
    fails += Expect("low+qual", "worldShadowMapSize=\"1024\"", true);

    o.preset = profile::Preset::Balanced;
    o.allowRaise = true;
    profile::Transform(low, g_out, sizeof(g_out), o);
    fails += Expect("target", "worldShadowMapSize=\"2048\"", true);
    fails += Expect("target", "billboardDistance=\"2500\"", true);
    o.lowVram = true;
    profile::Transform(low, g_out, sizeof(g_out), o);
    fails += Expect("target+8GB", "worldShadowMapSize=\"1024\"", true);
    fails += Expect("target+8GB", "volumetricFogMapWidth=\"256\"", true);
    fails += Expect("target+8GB", "billboardDistance=\"2500\"", true);

    // Idempotent: the delta applied to its own output changes nothing.
    static char once[1 << 16];
    profile::Transform(g_in, once, sizeof(once), profile::Upscaler::Keep, profile::Preset::Balanced);
    profile::Transform(once, g_out, sizeof(g_out), profile::Upscaler::Keep, profile::Preset::Balanced);
    printf("  [%s] idempotent\n", strcmp(once, g_out) == 0 ? "ok  " : "FAIL");
    fails += strcmp(once, g_out) == 0 ? 0 : 1;

    // RestoreOwned puts the mod's attributes back from the stock copy, nothing else.
    static char raised[1 << 16], restored[1 << 16];
    strcpy(raised, once);
    {
        char* p = strstr(raised, "width=\"2560\"");
        if (p) memcpy(p, "width=\"1920\"", 12);
    }
    profile::RestoreOwned(raised, g_in, restored, sizeof(restored));
    strcpy(g_out, restored);
    fails += Expect("restore", "worldShadowMapSize=\"4096\"", true);
    fails += Expect("restore", "volumetricFogMapWidth=\"1920\"", true);
    fails += Expect("restore", "width=\"1920\"", true);  // not the mod's: stays

    // Potato never costs more than Balanced.
    profile::Entry bal[128], pot[128];
    const int nb = profile::Entries(profile::Preset::Balanced, bal, 128);
    const int np = profile::Entries(profile::Preset::Potato, pot, 128);
    int compared = 0, above = 0;
    for (int i = 0; i < np; ++i)
        for (int j = 0; j < nb; ++j)
            if (!strcmp(pot[i].tag, bal[j].tag) && !strcmp(pot[i].attr, bal[j].attr)) {
                ++compared;
                if (!profile::CheaperOrEqual(pot[i].tag, pot[i].attr, bal[j].val, pot[i].val)) {
                    printf("  [FAIL] potato above balanced: %s.%s %s > %s\n", pot[i].tag,
                           pot[i].attr, pot[i].val, bal[j].val);
                    ++above;
                }
            }
    printf("  [%s] potato <= balanced (%d compared)\n", above == 0 && compared >= 20 ? "ok  " : "FAIL",
           compared);
    fails += above == 0 && compared >= 20 ? 0 : 1;

    printf(fails ? "PRESETTEST FAILED\n" : "PRESETTEST PASSED\n");
    return fails ? 1 : 0;
}
