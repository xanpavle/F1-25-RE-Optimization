// Signature scanning + call-site discovery for the F1 25 renderer mod.
//
// The enforcement tables in src/enforce.cpp are keyed on the *return address*
// of a call into one of the engine's shared settings parsers, so they are only
// valid for the exact build they were derived from. This module makes those
// addresses self-healing:
//
//   1. The parser entry points are found by signature (the prologue bytes),
//      and disambiguated by scoring: a candidate is only accepted if the known
//      attribute names actually resolve into calls through it.
//   2. Each enforcement site is validated against the resolved parser. If the
//      hard-coded return address no longer points at a `call <parser>`, the
//      site is re-derived by name: find the string constant, find the `lea
//      reg,[rip+str]` that loads it, and take the first `call <parser>` after
//      it.
//
// Everything here is read-only over the target image. It works on the running
// process's main module (AttachLive) or on a PE file on disk (LoadFile), so the
// derivation can be unit-tested without launching the game.
//
// No game code depends on this succeeding: on any failure the caller falls back
// to the hard-coded addresses.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace scan {

struct PeImage {
    const uint8_t* data = nullptr;   // mapped image, `base` corresponds to data[0]
    uintptr_t      base = 0;         // preferred/actual load address
    size_t         size = 0;         // SizeOfImage
    uint32_t       timestamp = 0;    // PE TimeDateStamp
    bool           owned = false;    // data was allocated by LoadFile
};

// The main module of the current process.
bool AttachLive(PeImage* out);

// A PE file on disk, mapped into a private buffer.
bool LoadFile(const wchar_t* path, PeImage* out);

void Free(PeImage* img);

// `pattern` is whitespace-separated hex bytes; "?" or "??" is a wildcard
// byte. Returns the first match's VA, or 0. `after` (optional) starts the
// search at that VA.
uintptr_t FindPattern(const PeImage& img, const char* pattern, uintptr_t after = 0);

// All matches, up to `max`. Returns the number found (capped at max).
int FindPatternAll(const PeImage& img, const char* pattern, uintptr_t* out, int max);

// VAs of the NUL-terminated ASCII string `s`. Returns the count (capped).
int FindStringVas(const PeImage& img, const char* s, uintptr_t* out, int max);

// A single `lea reg,[rip+disp]` occurrence: `insn` is the instruction VA,
// `target` the VA it materialises.
struct LeaRef {
    uintptr_t insn;
    uintptr_t target;
};

struct LeaIndex {
    LeaRef* refs = nullptr;
    int     count = 0;
};

// Collects every RIP-relative LEA in the image's executable sections.
bool LeaBuild(const PeImage& img, LeaIndex* out);
void LeaFree(LeaIndex* idx);

// Unique return addresses of `call <callee>` instructions that immediately
// follow a load of the string `name`. `window` bounds the search after the LEA.
// `require_sete` additionally demands `test eax,eax` (85 C0) right after the
// call, which identifies the boolean-parser idiom. Returns the count.
int FindSites(const PeImage& img, const LeaIndex& idx, const char* name,
              uintptr_t callee, size_t window, bool require_sete,
              uintptr_t* out, int max);

// Picks the parser among the matches of `signature` that the known attribute
// names resolve through the most. Returns its VA, or 0 when nothing matches.
// `best_score`/`cand_count` report the decision for diagnostics.
uintptr_t ResolveParser(const PeImage& img, const LeaIndex& idx,
                        const char* signature, const char* const* names,
                        int name_count, uintptr_t callee_hint, int* best_score,
                        int* cand_count);

// Convenience: true when `va` is inside the image.
bool Contains(const PeImage& img, uintptr_t va);

}  // namespace scan
