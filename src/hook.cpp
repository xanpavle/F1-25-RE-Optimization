#include "hook.h"
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

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

void SetWhy(char* why, const char* msg, int at) {
    if (!why) return;
    _snprintf(why, 64, "%s at +%d", msg, at);
    why[63] = 0;
}

// Length of a ModRM operand (ModRM + optional SIB + displacement), or -1 for a
// RIP-relative operand, which would point somewhere else once relocated.
int ModRmLen(const BYTE* p) {
    const int mod = p[0] >> 6, rm = p[0] & 7;
    int n = 1;
    if (mod == 3) return n;
    if (rm == 4) {
        const int base = p[1] & 7;
        ++n;
        if (mod == 0 && base == 5) return n + 4;  // [index*s + disp32], absolute
    } else if (mod == 0 && rm == 5) {
        return -1;                                // [rip + disp32]
    }
    if (mod == 1) n += 1;
    if (mod == 2) n += 4;
    return n;
}

}  // namespace

bool SafeToRelocate(const void* code, int len, char* why) {
    const BYTE* p = (const BYTE*)code;
    int i = 0;
    while (i < len) {
        const int start = i;
        bool opsize = false, rexw = false;
        if (p[i] == 0x66) { opsize = true; ++i; }
        if ((p[i] & 0xF0) == 0x40) { rexw = (p[i] & 0x08) != 0; ++i; }
        const BYTE op = p[i++];
        int imm = 0;
        bool modrm = false;
        if (op >= 0x50 && op <= 0x5F) {
            // push / pop reg
        } else if (op == 0x90) {
            // nop
        } else if (op >= 0xB8 && op <= 0xBF) {
            imm = rexw ? 8 : (opsize ? 2 : 4);    // mov reg, imm
        } else if ((op < 0x40 && (op & 7) <= 3) ||
                   (op >= 0x84 && op <= 0x8B) || op == 0x8D) {
            modrm = true;                          // ALU r/m, test, xchg, mov, lea
        } else if (op == 0x80 || op == 0x83 || op == 0xC6 || op == 0x6B) {
            modrm = true; imm = 1;
        } else if (op == 0x81 || op == 0xC7 || op == 0x69) {
            modrm = true; imm = opsize ? 2 : 4;
        } else {
            SetWhy(why, "unsupported or branch opcode", start);
            return false;
        }
        if (modrm) {
            const int n = ModRmLen(p + i);
            if (n < 0) {
                SetWhy(why, "RIP-relative operand", start);
                return false;
            }
            i += n;
        }
        i += imm;
        if (i > len) {
            SetWhy(why, "patch_len splits an instruction", start);
            return false;
        }
    }
    return true;
}

void* Install(void* target, void* detour, int patch_len) {
    if (!target || !detour || patch_len < 14 || patch_len > 64) return nullptr;
    if (!SafeToRelocate(target, patch_len, nullptr)) return nullptr;

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
