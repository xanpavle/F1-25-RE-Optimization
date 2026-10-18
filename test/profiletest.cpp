// Standalone check for profile::Transform.
//   profiletest <in.xml> <out.xml> [keep|taa|dlss|fsr3|xess|off]
#include "../src/profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static profile::Upscaler ParseUp(const char* s) {
    if (!s) return profile::Upscaler::Keep;
    if (!strcmp(s, "taa"))  return profile::Upscaler::Taa;
    if (!strcmp(s, "dlss")) return profile::Upscaler::Dlss;
    if (!strcmp(s, "fsr3")) return profile::Upscaler::Fsr3;
    if (!strcmp(s, "xess")) return profile::Upscaler::Xess;
    if (!strcmp(s, "off"))  return profile::Upscaler::Off;
    return profile::Upscaler::Keep;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: profiletest <in.xml> <out.xml> [keep|taa|dlss|fsr3|xess|off]\n");
        return 2;
    }
    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* in = (char*)malloc((size_t)n + 1);
    size_t got = fread(in, 1, (size_t)n, f);
    in[got] = 0;
    fclose(f);

    const size_t cap = 1 << 20;
    char* out = (char*)malloc(cap);
    size_t on = profile::Transform(in, out, cap, ParseUp(argc >= 4 ? argv[3] : nullptr));
    if (on == 0) {
        fprintf(stderr, "Transform failed\n");
        return 1;
    }
    FILE* g = fopen(argv[2], "wb");
    fwrite(out, 1, on, g);
    fclose(g);
    printf("OK: %zu bytes -> %zu bytes (upscaler=%s)\n", got, on,
           argc >= 4 ? argv[3] : "keep");
    return 0;
}
