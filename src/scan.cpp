#include "scan.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace scan {
namespace {

#pragma pack(push, 1)
struct DosHeader {
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;
};
#pragma pack(pop)

const size_t kMaxPattern = 128;

struct Section {
    uintptr_t      va;
    const uint8_t* ptr;
    size_t         size;
    uint32_t       chars;
};

struct Pat {
    uint8_t bytes[kMaxPattern];
    uint8_t mask[kMaxPattern];   // 1 = compare, 0 = wildcard
    int     len;
};

int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool ParsePattern(const char* s, Pat* p) {
    p->len = 0;
    if (!s) return false;
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == '\n') ++s;
        if (!*s) break;
        if (p->len >= (int)kMaxPattern) return false;
        if (*s == '?') {
            ++s;
            if (*s == '?') ++s;
            p->mask[p->len] = 0;
            p->bytes[p->len] = 0;
            ++p->len;
        } else {
            const int hi = HexNibble(s[0]);
            const int lo = HexNibble(s[1]);
            if (hi < 0 || lo < 0) return false;
            p->bytes[p->len] = (uint8_t)((hi << 4) | lo);
            p->mask[p->len] = 1;
            ++p->len;
            s += 2;
        }
    }
    return p->len > 0;
}

bool ImageHeaders(const PeImage& img, uint32_t* num_sections,
                  uint32_t* size_of_headers, uint32_t* size_of_image) {
    if (!img.data || img.size < sizeof(DosHeader)) return false;
    const DosHeader* dos = (const DosHeader*)img.data;
    if (dos->e_magic != 0x5A4D) return false;  // MZ
    const size_t nt = (size_t)dos->e_lfanew;
    if (nt + 4 + 20 + 2 > img.size) return false;
    if (memcmp(img.data + nt, "PE\0\0", 4) != 0) return false;
    const uint8_t* fh = img.data + nt + 4;
    const uint16_t opt_size = *(const uint16_t*)(fh + 16);
    if (nt + 24 + opt_size > img.size) return false;
    const uint8_t* oh = fh + 20;
    const uint16_t magic = *(const uint16_t*)oh;
    if (magic != 0x20B && magic != 0x10B) return false;  // PE32+ / PE32
    if (num_sections)  *num_sections = *(const uint16_t*)(fh + 2);
    if (size_of_image) *size_of_image = *(const uint32_t*)(oh + 56);
    if (size_of_headers) *size_of_headers = *(const uint32_t*)(oh + 60);
    return true;
}

int EnumSections(const PeImage& img, Section* out, int max, bool exec_only) {
    uint32_t nsec = 0, soh = 0, soi = 0;
    if (!ImageHeaders(img, &nsec, &soh, &soi)) return 0;
    const DosHeader* dos = (const DosHeader*)img.data;
    const uint16_t opt_size = *(const uint16_t*)(img.data + dos->e_lfanew + 4 + 16);
    const size_t sec_off = (size_t)dos->e_lfanew + 4 + 20 + opt_size;
    int n = 0;
    for (uint32_t i = 0; i < nsec && n < max; ++i) {
        const uint8_t* sh = img.data + sec_off + (size_t)i * 40;
        if ((const uint8_t*)sh + 40 > img.data + img.size) break;
        const uint32_t vsize = *(const uint32_t*)(sh + 8);
        const uint32_t vaddr = *(const uint32_t*)(sh + 12);
        const uint32_t chars = *(const uint32_t*)(sh + 36);
        if (vsize == 0) continue;
        if (exec_only && !(chars & 0x20000000u)) continue;  // MEM_EXECUTE
        if (vaddr + vsize > img.size) {
            // Clamp to the image (raw PE files can have VirtualSize > file).
            if (vaddr >= (uint32_t)img.size) continue;
        }
        out[n].va = img.base + vaddr;
        out[n].ptr = img.data + vaddr;
        out[n].size = vsize;
        out[n].chars = chars;
        ++n;
    }
    return n;
}

bool InImage(const PeImage& img, uintptr_t va) {
    return va >= img.base && va < img.base + img.size;
}

const uint8_t* PtrFor(const PeImage& img, uintptr_t va) {
    if (!InImage(img, va)) return nullptr;
    return img.data + (va - img.base);
}

// The end of the executable section containing `va` (for bounded forward scans).
uintptr_t SectionEnd(const PeImage& img, uintptr_t va) {
    Section secs[64];
    const int n = EnumSections(img, secs, 64, true);
    for (int i = 0; i < n; ++i) {
        if (va >= secs[i].va && va < secs[i].va + secs[i].size)
            return secs[i].va + secs[i].size;
    }
    return InImage(img, va) ? img.base + img.size : va;
}

int CmpLea(const void* a, const void* b) {
    const LeaRef* x = (const LeaRef*)a;
    const LeaRef* y = (const LeaRef*)b;
    if (x->target < y->target) return -1;
    if (x->target > y->target) return 1;
    if (x->insn < y->insn) return -1;
    if (x->insn > y->insn) return 1;
    return 0;
}

int LowerBound(const LeaIndex& idx, uintptr_t key) {
    int lo = 0, hi = idx.count;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (idx.refs[mid].target < key) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

}  // namespace

bool AttachLive(PeImage* out) {
    HMODULE mod = GetModuleHandleW(nullptr);
    if (!mod) return false;
    out->base = (uintptr_t)mod;
    out->data = (const uint8_t*)mod;
    out->owned = false;

    const DosHeader* dos = (const DosHeader*)out->data;
    if (dos->e_magic != 0x5A4D) return false;
    const size_t nt = (size_t)dos->e_lfanew;
    out->timestamp = *(const uint32_t*)(out->data + nt + 4 + 8);
    uint32_t nsec = 0, soh = 0, soi = 0;
    if (!ImageHeaders(*out, &nsec, &soh, &soi) || soi == 0) return false;
    out->size = soi;
    return true;
}

bool LoadFile(const wchar_t* path, PeImage* out) {
    *out = PeImage();
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz = {};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (1LL << 32)) {
        CloseHandle(h);
        return false;
    }
    uint8_t* file = (uint8_t*)malloc((size_t)sz.QuadPart);
    if (!file) {
        CloseHandle(h);
        return false;
    }
    DWORD got = 0;
    const BOOL ok = ReadFile(h, file, (DWORD)sz.QuadPart, &got, nullptr);
    CloseHandle(h);
    if (!ok || got != sz.QuadPart) {
        free(file);
        return false;
    }

    PeImage probe;
    probe.data = file;
    probe.base = 0;
    probe.size = (size_t)sz.QuadPart;
    uint32_t nsec = 0, soh = 0, soi = 0;
    if (!ImageHeaders(probe, &nsec, &soh, &soi) || soi == 0) {
        free(file);
        return false;
    }
    const DosHeader* dos = (const DosHeader*)file;
    const size_t nt = (size_t)dos->e_lfanew;
    const uint16_t opt_size = *(const uint16_t*)(file + nt + 4 + 16);
    const size_t sec_off = nt + 4 + 20 + opt_size;
    uint64_t image_base = 0;
    memcpy(&image_base, file + nt + 4 + 20 + 24, 8);

    uint8_t* img = (uint8_t*)calloc(1, soi);
    if (!img) {
        free(file);
        return false;
    }
    memcpy(img, file, soh < soi ? soh : soi);
    for (uint32_t i = 0; i < nsec; ++i) {
        const uint8_t* sh = file + sec_off + (size_t)i * 40;
        if ((size_t)(sh - file) + 40 > (size_t)sz.QuadPart) break;
        const uint32_t vsize = *(const uint32_t*)(sh + 8);
        const uint32_t vaddr = *(const uint32_t*)(sh + 12);
        const uint32_t rawsize = *(const uint32_t*)(sh + 16);
        const uint32_t rawptr = *(const uint32_t*)(sh + 20);
        if (vaddr >= soi || rawptr >= sz.QuadPart) continue;
        size_t n = rawsize;
        if (vsize && n > vsize) n = vsize;
        if ((size_t)vaddr + n > soi) n = soi - vaddr;
        if ((size_t)rawptr + n > (size_t)sz.QuadPart) n = (size_t)sz.QuadPart - rawptr;
        memcpy(img + vaddr, file + rawptr, n);
    }
    free(file);

    out->data = img;
    out->base = (uintptr_t)image_base;
    out->size = soi;
    out->timestamp = *(const uint32_t*)(img + nt + 4 + 8);
    out->owned = true;
    return true;
}

void Free(PeImage* img) {
    if (!img) return;
    if (img->owned && img->data) free((void*)img->data);
    *img = PeImage();
}

uintptr_t FindPattern(const PeImage& img, const char* pattern, uintptr_t after) {
    Pat p;
    if (!ParsePattern(pattern, &p)) return 0;
    int anchor = -1;
    for (int i = 0; i < p.len; ++i) {
        if (p.mask[i]) { anchor = i; break; }
    }
    if (anchor < 0) return 0;

    Section secs[64];
    const int n = EnumSections(img, secs, 64, true);
    for (int s = 0; s < n; ++s) {
        const uint8_t* base = secs[s].ptr;
        const size_t size = secs[s].size;
        if (size < (size_t)p.len) continue;
        const size_t limit = size - p.len;
        for (size_t i = 0; i <= limit; ++i) {
            if (base[i + anchor] != p.bytes[anchor]) continue;
            bool ok = true;
            for (int j = 0; j < p.len; ++j) {
                if (p.mask[j] && base[i + j] != p.bytes[j]) { ok = false; break; }
            }
            if (!ok) continue;
            const uintptr_t va = secs[s].va + i;
            if (after && va <= after) continue;
            return va;
        }
    }
    return 0;
}

int FindPatternAll(const PeImage& img, const char* pattern, uintptr_t* out, int max) {
    Pat p;
    if (!ParsePattern(pattern, &p) || max <= 0) return 0;
    int anchor = -1;
    for (int i = 0; i < p.len; ++i) {
        if (p.mask[i]) { anchor = i; break; }
    }
    if (anchor < 0) return 0;

    Section secs[64];
    const int n = EnumSections(img, secs, 64, true);
    int found = 0;
    for (int s = 0; s < n && found < max; ++s) {
        const uint8_t* base = secs[s].ptr;
        const size_t size = secs[s].size;
        if (size < (size_t)p.len) continue;
        const size_t limit = size - p.len;
        for (size_t i = 0; i <= limit && found < max; ++i) {
            if (base[i + anchor] != p.bytes[anchor]) continue;
            bool ok = true;
            for (int j = 0; j < p.len; ++j) {
                if (p.mask[j] && base[i + j] != p.bytes[j]) { ok = false; break; }
            }
            if (ok) out[found++] = secs[s].va + i;
        }
    }
    return found;
}

int FindStringVas(const PeImage& img, const char* s, uintptr_t* out, int max) {
    if (!s || !*s || max <= 0) return 0;
    const size_t len = strlen(s);
    Section secs[64];
    const int n = EnumSections(img, secs, 64, false);
    int found = 0;
    for (int si = 0; si < n && found < max; ++si) {
        const uint8_t* base = secs[si].ptr;
        const size_t size = secs[si].size;
        if (size < len + 1) continue;
        const size_t limit = size - len - 1;
        size_t i = 0;
        while (i <= limit && found < max) {
            const uint8_t* q = (const uint8_t*)memchr(base + i, s[0], limit - i + 1);
            if (!q) break;
            i = (size_t)(q - base);
            if (memcmp(q, s, len) == 0 && q[len] == 0) {
                out[found++] = secs[si].va + i;
            }
            ++i;
        }
    }
    return found;
}

bool LeaBuild(const PeImage& img, LeaIndex* out) {
    const int kMax = 4000000;
    int cap = 1 << 16;
    LeaRef* refs = (LeaRef*)malloc(sizeof(LeaRef) * (size_t)cap);
    if (!refs) return false;
    int count = 0;

    Section secs[64];
    const int n = EnumSections(img, secs, 64, true);
    for (int s = 0; s < n; ++s) {
        const uint8_t* b = secs[s].ptr;
        const size_t size = secs[s].size;
        if (size < 8) continue;
        const size_t limit = size - 7;
        for (size_t i = 0; i < limit; ++i) {
            const uint8_t rex = b[i];
            if (rex < 0x48 || rex > 0x4F) continue;
            if (b[i + 1] != 0x8D) continue;
            if ((b[i + 2] & 0xC7) != 0x05) continue;
            if (count == cap) {
                if (cap >= kMax) break;
                int ncap = cap * 2;
                if (ncap > kMax) ncap = kMax;
                LeaRef* grown = (LeaRef*)realloc(refs, sizeof(LeaRef) * (size_t)ncap);
                if (!grown) { free(refs); return false; }
                refs = grown;
                cap = ncap;
            }
            int32_t disp = 0;
            memcpy(&disp, b + i + 3, 4);
            const uintptr_t insn = secs[s].va + i;
            refs[count].insn = insn;
            refs[count].target = insn + 7 + (intptr_t)disp;
            ++count;
        }
    }
    qsort(refs, (size_t)count, sizeof(LeaRef), CmpLea);
    out->refs = refs;
    out->count = count;
    return true;
}

void LeaFree(LeaIndex* idx) {
    if (!idx) return;
    free(idx->refs);
    idx->refs = nullptr;
    idx->count = 0;
}

static uintptr_t FindCallAfter(const PeImage& img, uintptr_t from, uintptr_t callee,
                               size_t window, bool require_sete) {
    const uintptr_t end = SectionEnd(img, from);
    const uintptr_t stop = (from + window < end) ? from + window : end;
    for (uintptr_t va = from; va + 5 <= stop; ++va) {
        const uint8_t* p = PtrFor(img, va);
        if (!p || p[0] != 0xE8) continue;
        int32_t rel = 0;
        memcpy(&rel, p + 1, 4);
        if ((uintptr_t)(va + 5 + (intptr_t)rel) != callee) continue;
        const uintptr_t ret = va + 5;
        if (require_sete) {
            const uint8_t* q = PtrFor(img, ret);
            if (!q || q[0] != 0x85 || q[1] != 0xC0) continue;
        }
        return ret;
    }
    return 0;
}

int FindSites(const PeImage& img, const LeaIndex& idx, const char* name,
              uintptr_t callee, size_t window, bool require_sete,
              uintptr_t* out, int max) {
    if (!name || !idx.refs || max <= 0) return 0;
    uintptr_t strs[256];
    const int ns = FindStringVas(img, name, strs, 256);
    int found = 0;
    for (int si = 0; si < ns; ++si) {
        int i = LowerBound(idx, strs[si]);
        for (; i < idx.count && idx.refs[i].target == strs[si]; ++i) {
            const uintptr_t ret =
                FindCallAfter(img, idx.refs[i].insn, callee, window, require_sete);
            if (!ret) continue;
            bool dup = false;
            for (int k = 0; k < found; ++k) {
                if (out[k] == ret) { dup = true; break; }
            }
            if (!dup && found < max) out[found++] = ret;
        }
    }
    return found;
}

uintptr_t ResolveParser(const PeImage& img, const LeaIndex& idx,
                        const char* signature, const char* const* names,
                        int name_count, uintptr_t callee_hint, int* best_score,
                        int* cand_count) {
    uintptr_t cands[64];
    const int nc = FindPatternAll(img, signature, cands, 64);
    if (cand_count) *cand_count = nc;
    if (nc == 0) {
        if (best_score) *best_score = -1;
        return 0;
    }
    int best = -1;
    uintptr_t bestVa = 0;
    for (int c = 0; c < nc; ++c) {
        int score = 0;
        uintptr_t tmp[4];
        for (int i = 0; i < name_count; ++i) {
            if (FindSites(img, idx, names[i], cands[c], 0x100, false, tmp, 4) > 0)
                ++score;
        }
        if (score > best || (score == best && cands[c] == callee_hint)) {
            best = score;
            bestVa = cands[c];
        }
    }
    if (best_score) *best_score = best;
    return best > 0 ? bestVa : 0;
}

}  // namespace scan
