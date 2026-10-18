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
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
