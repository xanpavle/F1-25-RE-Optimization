#include "hook.h"
#include <windows.h>
#include <stdint.h>
#include <string.h>

namespace hook {
namespace {

// Emits a 14-byte absolute jump: `jmp qword ptr [rip+0]` + 8-byte target.
// An indirect jump without the compiler's __guard_check_icall is not subject to
// Control Flow Guard, and it reaches an arbitrary 64-bit address (a plain
// E9 rel32 only spans +/-2 GB, which is not guaranteed for a loaded DLL).
void WriteAbsJmp(BYTE* p, void* dest) {
    p[0] = 0xFF;
    p[1] = 0x25;
    p[2] = 0x00;
    p[3] = 0x00;
    p[4] = 0x00;
    p[5] = 0x00;
    memcpy(p + 6, &dest, 8);
}

}  // namespace

void* Install(void* target, void* detour, int patch_len) {
    if (!target || !detour || patch_len < 14) return nullptr;

    const int kJmpLen = 14;

    BYTE* tramp = (BYTE*)VirtualAlloc(nullptr, 128, MEM_COMMIT | MEM_RESERVE,
                                      PAGE_EXECUTE_READWRITE);
    if (!tramp) return nullptr;

    // Trampoline: relocated prologue, then jump back to (target + patch_len).
    memcpy(tramp, target, (size_t)patch_len);
    WriteAbsJmp(tramp + patch_len, (BYTE*)target + patch_len);

    // Patch the target: absolute jump to detour, NOP-pad to patch_len.
    DWORD old = 0;
    if (!VirtualProtect(target, (SIZE_T)patch_len, PAGE_EXECUTE_READWRITE, &old)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        return nullptr;
    }
    BYTE* p = (BYTE*)target;
    WriteAbsJmp(p, detour);
    for (int i = kJmpLen; i < patch_len; ++i) p[i] = 0x90;
    VirtualProtect(target, (SIZE_T)patch_len, old, &old);

    FlushInstructionCache(GetCurrentProcess(), target, (SIZE_T)patch_len);
    return tramp;
}

}  // namespace hook
