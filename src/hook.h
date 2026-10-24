// Minimal x64 inline-detour engine for the F1 25 renderer mod.
// Writes a 14-byte absolute jump (FF 25 + 8-byte target) at the target and
// relocates the overwritten prologue into a trampoline. The jump is indirect but
// not compiler-emitted, so Control Flow Guard does not check it.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace hook {

// Checks that the first `len` bytes at `code` are whole instructions that are
// safe to copy verbatim: no RIP-relative operand, no branch/call/ret, and the
// last instruction ends exactly at `len`. Only the small instruction subset
// used by compiler prologues is understood; anything else is rejected.
// On failure, `why` (optional, >= 64 bytes) receives a short reason.
bool SafeToRelocate(const void* code, int len, char* why);

// Installs a detour at `target`. `patch_len` (>= 14) bytes are copied to a
// trampoline; the target is overwritten with `jmp detour`. Refuses (returns
// nullptr) when SafeToRelocate() rejects the prologue, so a wrong patch_len or
// a changed prologue can never corrupt the game's code.
// Returns a pointer to the trampoline (callable with the original ABI) or
// nullptr on failure.
void* Install(void* target, void* detour, int patch_len);

}  // namespace hook
