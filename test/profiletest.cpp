// Standalone check for profile::Transform.
//   profiletest <in.xml> <out.xml>
#include "../src/profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: profiletest <in.xml> <out.xml>\n");
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
    size_t on = profile::Transform(in, out, cap);
    if (on == 0) {
        fprintf(stderr, "Transform failed\n");
        return 1;
    }
    FILE* g = fopen(argv[2], "wb");
    fwrite(out, 1, on, g);
    fclose(g);
    printf("OK: %zu bytes -> %zu bytes\n", got, on);
    return 0;
}
