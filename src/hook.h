// Minimal x64 inline-detour engine for the F1 25 renderer mod.
// Uses a relative jmp (E9) at the target plus a relocated trampoline.
// Direct relative jumps are not subject to Control Flow Guard, and this DLL is
// not CFG-instrumented, so the indirect call to the trampoline is also fine.
#pragma once
#include <stddef.h>

namespace hook {

// Installs a detour at `target`. `patch_len` whole, RIP-relative-free bytes are
// copied to a trampoline; the target is overwritten with `jmp detour`.
// Returns a pointer to the trampoline (callable with the original ABI) or
// nullptr on failure.
void* Install(void* target, void* detour, int patch_len);

}  // namespace hook
