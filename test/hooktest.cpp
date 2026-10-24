#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hook.h"

typedef int (*fn_t)(void);
static int g_calls = 0;
static fn_t g_orig = nullptr;

static int Detour(void) {
    g_calls++;
    return g_orig() + 100;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    BYTE code[] = {
        0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x38,  // pushes + sub
        0x44, 0x89, 0xCB, 0x4C, 0x89, 0xC7,                       // mov ebx,r9d; mov rdi,r8
        0xB8, 0x2A, 0x00, 0x00, 0x00,                             // mov eax,42
        0x48, 0x83, 0xC4, 0x38,                                   // add rsp,0x38
        0x5B, 0x5F, 0x5E, 0x41, 0x5E,                             // pop rbx,rdi,rsi,r14
        0xC3                                                      // ret
    };
    void* mem = VirtualAlloc(nullptr, sizeof(code), MEM_COMMIT | MEM_RESERVE,
                             PAGE_EXECUTE_READWRITE);
    printf("mem=%p\n", mem);
    if (!mem) return 3;
    memcpy(mem, code, sizeof(code));
    fn_t f = (fn_t)mem;

    printf("calling raw...\n");
    int before = f();
    printf("before=%d\n", before);

    g_orig = (fn_t)hook::Install(mem, (void*)Detour, 15);
    printf("orig=%p\n", (void*)g_orig);
    if (!g_orig) return 2;

    int r1 = f();
    printf("r1=%d\n", r1);
    int r2 = f();
    printf("r2=%d calls=%d\n", r2, g_calls);
    int ok = (before == 42 && r1 == 142 && r2 == 142 && g_calls == 2);

    // Relocation safety: the real game prologues must pass; bad lengths,
    // RIP-relative operands and branches must be refused.
    struct Case { const char* name; BYTE b[24]; int len; bool want; };
    static const Case cases[] = {
        {"getter 15", {0x41,0x56,0x56,0x57,0x53,0x48,0x83,0xEC,0x38,0x44,0x89,0xCB,0x4C,0x89,0xC7}, 15, true},
        {"parseInt 14", {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x7C,0x24,0x10,0x55,0x48,0x8B,0xEC}, 14, true},
        {"apply 19", {0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53,0x48,0x81,0xEC,0x00,0x01,0x00,0x00}, 19, true},
        {"parseFloat 16", {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xFA,0x48,0x8B,0xD9}, 16, true},
        {"split insn", {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x7C,0x24,0x10,0x55,0x48,0x8B,0xEC}, 13, false},
        {"rip-relative", {0x48,0x83,0xEC,0x28,0x83,0x3D,0,0,0,0,0x00,0x75,0x2D,0x48,0x85,0xC9}, 16, false},
        {"jmp thunk", {0x45,0x33,0xC0,0xE9,0,0,0,0,0x48,0x89,0x5C,0x24,0x08,0x57}, 14, false},
    };
    for (const Case& c : cases) {
        char why[64] = {0};
        const bool got = hook::SafeToRelocate(c.b, c.len, why);
        printf("relocate %-14s -> %s %s\n", c.name, got ? "ok" : "refused", why);
        if (got != c.want) ok = 0;
    }
    // Install itself must refuse a split instruction.
    BYTE bad[32];
    memcpy(bad, cases[4].b, 14);
    bad[14] = 0x48; bad[15] = 0x8B; bad[16] = 0xEC;  // 15 cuts this mov in half
    if (hook::Install(bad, (void*)Detour, 15)) ok = 0;

    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
