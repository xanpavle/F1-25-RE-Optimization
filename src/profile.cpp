// The tuned delta. Each entry rewrites one attribute of one XML element, so the
// result is "your preset, minus the expensive-but-barely-visible parts".
//
// Deliberately NOT touched:
//   * mirrors.*        - user asked to leave mirror resolution/cadence alone
//   * rt_reflections / rt_shadows - the visible part of RT stays on
//   * weather rain*    - only matters when it rains, and it is visible then
//   * aa_quality       - the quality level stays whatever you picked in-game;
//                        only the backend selectors in <antialiasing> are
//                        switched, and only when [upscaler] asks for it.
//
// rt_ao / rt_ddgi are switched off: both are full-screen ray-traced passes that
// cost far more than they show (measured +10% on the RX 7800 XT at 1440p, with
// no visible change once the reflections/shadows stay on).
#include "profile.h"
#include <string.h>

namespace profile {
namespace {

struct Ov {
    const char* tag;
    const char* attr;
    const char* val;
};

const Ov kOv[] = {
    // --- ambient occlusion / reflections quality ---
    {"ssao", "algorithm", "1"},
    {"ssrt", "quality", "2"},

    // --- volumetric fog: 14x fewer fog pixels (fog is soft, invisible) ---
    {"lighting", "volumetricFogMapWidth", "512"},
    {"lighting", "volumetricFogMapHeight", "288"},

    // --- post: niche screen-space effects off ---
    {"postprocess", "godRays", "false"},
    {"postprocess", "lensStreak", "false"},
    {"postprocess", "motionBlurV2", "false"},

    // --- shadows: half the map pixels, cheaper filtering ---
    {"shadows", "worldShadowMapSize", "2048"},
    {"shadows", "nightShadowMapSize", "2048"},
    {"shadows", "skyShadowMap32Bits", "false"},
    {"shadows", "sampling", "2"},
    {"shadows", "spotShadowSampling", "2"},
    {"shadows", "crowdInShadows", "false"},

    // --- particles / crowd ---
    {"particles", "high", "false"},
    {"crowd", "billboardDistance", "2500"},

    // --- car reflections: scale + update one cube face per frame ---
    {"vehicle_reflections", "envMapScale", "1.0"},
    {"vehicle_reflections", "cubeMapScale", "1.0"},
    {"vehicle_reflections", "cubeMapUpdateMode", "oneFacePerFrame"},

    // --- clouds (planar reflections left alone per request) ---
    {"weather_effects", "proceduralCloudRTScale", "2.0"},

    // --- ray-traced AO / DDGI: expensive full-screen passes, low visual impact ---
    {"rt_ao", "enabled", "false"},
    {"rt_ddgi", "enabled", "false"},

    // --- ground cover / misc ---
    {"ground_cover", "drawDistance", "80.0"},
    {"hair", "enabled", "false"},
    {"skidmarks", "parallax", "false"},

    // --- generic GPU culling (engine ships it off) ---
    {"cs_culling", "enabled", "true"},

    // --- texture streaming: drop to lower-res mips a bit sooner ---
    {"texture_streaming", "texelDensityBias", "0.25"},
};

const int kOvCount = (int)(sizeof(kOv) / sizeof(kOv[0]));

// Replace attr="..." inside a single element line (first match only).
void ReplaceAttr(char* line, size_t cap, const char* attr, const char* val) {
    const size_t alen = strlen(attr);
    char* p = line;
    while ((p = strstr(p, attr)) != nullptr) {
        if (p != line && p[-1] != ' ' && p[-1] != '\t' && p[-1] != '<') {
            p += alen;
            continue;
        }
        if (p[alen] != '=' || p[alen + 1] != '"') {
            p += alen;
            continue;
        }
        char* vs = p + alen + 2;
        char* ve = strchr(vs, '"');
        if (!ve) return;

        const size_t head = (size_t)(vs - line);
        const size_t nlen = strlen(val);
        const size_t tail = strlen(ve);  // includes the closing quote

        char tmp[4096];
        if (head + nlen + tail + 1 > sizeof(tmp) || head + nlen + tail + 1 > cap) return;

        memcpy(tmp, line, head);
        memcpy(tmp + head, val, nlen);
        memcpy(tmp + head + nlen, ve, tail + 1);
        memcpy(line, tmp, head + nlen + tail + 1);
        return;
    }
}

// Switch the <antialiasing .../> element to the requested backend.
// NOTE: taa/dlss/xess are booleans in the config, but fsr3 is written as a
// number ("0"/"1"). Writing "true" for fsr3 makes the engine reject the whole
// element and silently fall back to TAA, which is why the auto-selected FSR 3
// never used to take effect.
void ApplyUpscaler(char* line, size_t cap, Upscaler up) {
    const char* taa  = "false";
    const char* dlss = "false";
    const char* fsr  = "0";
    const char* xess = "false";
    const char* cas  = nullptr;  // only written when the backend needs it

    switch (up) {
        case Upscaler::Taa:  taa  = "true"; break;
        case Upscaler::Dlss: dlss = "true"; break;
        case Upscaler::Fsr3: fsr  = "1"; cas = "1"; break;
        case Upscaler::Xess: xess = "true"; break;
        case Upscaler::Off:  break;          // all false
        case Upscaler::Keep: return;         // caller filtered this already
    }

    ReplaceAttr(line, cap, "taa",  taa);
    ReplaceAttr(line, cap, "dlss", dlss);
    ReplaceAttr(line, cap, "fsr3", fsr);
    ReplaceAttr(line, cap, "xess", xess);
    if (cas) ReplaceAttr(line, cap, "cas", cas);
}

// Read the element name from an element-start line. Returns 0 if not one.
size_t ElemTag(const char* line, char* tag, size_t tagCap) {
    const char* p = line;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != '<' || p[1] == '/' || p[1] == '?' || p[1] == '!') return 0;
    ++p;
    size_t n = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '>' && *p != '/' && n < tagCap - 1) {
        tag[n++] = *p++;
    }
    tag[n] = 0;
    return n;
}

}  // namespace

size_t Transform(const char* in, char* out, size_t cap, Upscaler up) {
    size_t o = 0;
    const char* p = in;
    while (*p) {
        const char* nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) + 1 : strlen(p);

        char line[4096];
        size_t copy = (len < sizeof(line) - 1) ? len : sizeof(line) - 1;
        memcpy(line, p, copy);
        line[copy] = 0;

        char tag[64];
        if (ElemTag(line, tag, sizeof(tag)) > 0) {
            if (up != Upscaler::Keep && strcmp(tag, "antialiasing") == 0) {
                ApplyUpscaler(line, sizeof(line), up);
            }
            for (int i = 0; i < kOvCount; ++i) {
                if (strcmp(kOv[i].tag, tag) == 0) {
                    ReplaceAttr(line, sizeof(line), kOv[i].attr, kOv[i].val);
                }
            }
        }

        size_t llen = strlen(line);
        if (o + llen + 1 > cap) return 0;
        memcpy(out + o, line, llen);
        o += llen;

        p += len;
    }
    out[o] = 0;
    return o;
}

}  // namespace profile
