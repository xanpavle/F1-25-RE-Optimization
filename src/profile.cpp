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
#include <stdio.h>
#include <stdlib.h>
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

// Quality: applied after kOv. Keeps the parts of Ultra that are visible.
// Measured on the RX 7800 XT (Austria wet, 1440p): 154 FPS vs 171 for
// Balanced, 302 for Potato and 142 for stock Ultra.
const Ov kQuality[] = {
    {"ssao", "algorithm", "3"},
    {"ssrt", "quality", "4"},
    {"lighting", "volumetricFogMapWidth", "960"},
    {"lighting", "volumetricFogMapHeight", "540"},
    {"shadows", "worldShadowMapSize", "4096"},
    {"shadows", "sampling", "3"},
    {"crowd", "billboardDistance", "4000"},
    {"rt_ao", "enabled", "true"},
    {"rt_ddgi", "enabled", "true"},
    {"ground_cover", "enabled", "true"},
    {"ground_cover", "drawDistance", "120.0"},
    {"hair", "enabled", "true"},
    {"texture_streaming", "texelDensityBias", "0.0"},
};
const int kQualityCount = (int)(sizeof(kQuality) / sizeof(kQuality[0]));

// Potato: applied after kOv. Lowest cost the engine accepts; ray tracing off.
// Mirrors stay untouched (they are needed to race).
const Ov kPotato[] = {
    {"ssao", "enabled", "false"},
    {"ssrt", "enabled", "false"},
    {"lighting", "quality", "0"},
    {"lighting", "volumetricFog", "false"},
    {"lighting", "volumetricFogMapWidth", "256"},
    {"lighting", "volumetricFogMapHeight", "128"},
    {"postprocess", "depthOfField", "false"},
    {"postprocess", "lensFlare", "false"},
    {"postprocess", "motionBlur", "false"},
    {"shadows", "skyShadowMapSize", "1024"},
    // skyShadowCascadeCount is not changed (0.3.3): below 4 it is a known crash
    // risk in-engine and saves almost nothing.
    {"shadows", "skyShadowLowLod", "true"},
    {"shadows", "worldShadowMapSize", "1024"},
    {"shadows", "nightShadowMapSize", "1024"},
    {"shadows", "nightShadowCharacterCast", "false"},
    {"shadows", "nightShadowSourceCount", "1"},
    {"shadows", "sampling", "0"},
    {"shadows", "spotShadowMapSize", "512"},
    {"shadows", "spotShadowSampling", "0"},
    {"particles", "distanceScale", "0.5"},
    {"particles", "rate", "0.5"},
    {"crowd", "distanceScale", "0.2"},
    {"crowd", "billboardDistance", "1000"},
    {"vehicle_reflections", "envMapScale", "0.5"},
    {"vehicle_reflections", "cubeMapEnabled", "false"},
    {"weather_effects", "rainHigh", "false"},
    {"weather_effects", "trackHeatHaze", "false"},
    {"weather_effects", "planarReflectionsHQEnabled", "false"},
    {"weather_effects", "planarReflectionsRTScale", "0.5"},
    // 0.3.2: was 4.0, the Ultra value (the scale is the cloud render resolution,
    // lower is cheaper; Balanced uses 2.0), which made Potato heavier than Balanced.
    {"weather_effects", "proceduralCloudRTScale", "1.0"},
    {"weather_effects", "proceduralCloudQuality", "0"},
    {"ground_cover", "enabled", "false"},
    {"ground_cover", "meshes", "false"},
    {"trees", "useLOD0", "false"},
    {"texture_streaming", "texelDensityBias", "1.0"},
    {"anisotropic_filter", "level", "4"},
    {"rt_shadows", "enabled", "false"},
    {"rt_reflections", "enabled", "false"},
    {"rt_transparent_reflections", "enabled", "false"},
    {"rt_ao", "enabled", "false"},
    {"rt_ddgi", "enabled", "false"},
};
const int kPotatoCount = (int)(sizeof(kPotato) / sizeof(kPotato[0]));

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

// Finds attr="..." in one element line; returns the value span.
bool FindAttr(const char* line, const char* attr, const char** vs, const char** ve) {
    const size_t alen = strlen(attr);
    const char* p = line;
    while ((p = strstr(p, attr)) != nullptr) {
        if (p != line && p[-1] != ' ' && p[-1] != '\t' && p[-1] != '<') {
            p += alen;
            continue;
        }
        if (p[alen] != '=' || p[alen + 1] != '"') {
            p += alen;
            continue;
        }
        *vs = p + alen + 2;
        *ve = strchr(*vs, '"');
        return *ve != nullptr;
    }
    return false;
}

bool GetAttr(const char* line, const char* attr, char* out, size_t cap) {
    const char *vs, *ve;
    if (!FindAttr(line, attr, &vs, &ve)) return false;
    size_t n = (size_t)(ve - vs);
    if (n >= cap) n = cap - 1;
    memcpy(out, vs, n);
    out[n] = 0;
    return true;
}

bool ParseNum(const char* v, double* out) {
    if (!_stricmp(v, "true")) { *out = 1; return true; }
    if (!_stricmp(v, "false")) { *out = 0; return true; }
    if (!*v) return false;
    char* end = nullptr;
    *out = strtod(v, &end);
    return end && *end == 0 && end != v;
}

int Merge(Preset p, Entry* out, int cap) {
    int n = 0;
    auto put = [&](const Ov& o) {
        for (int i = 0; i < n; ++i)
            if (!strcmp(out[i].tag, o.tag) && !strcmp(out[i].attr, o.attr)) {
                out[i].val = o.val;
                return;
            }
        if (n < cap) out[n++] = {o.tag, o.attr, o.val};
    };
    for (int i = 0; i < kOvCount; ++i) put(kOv[i]);
    if (p == Preset::Quality)
        for (int i = 0; i < kQualityCount; ++i) put(kQuality[i]);
    else if (p == Preset::Potato)
        for (int i = 0; i < kPotatoCount; ++i) put(kPotato[i]);
    return n;
}

// Writes the tuned value only when it costs no more than the current one, or when
// raising is allowed and the attribute is not memory-heavy on a low-VRAM GPU.
void GuardedAttr(char* line, size_t cap, const char* tag, const Entry& e, const Options& o) {
    char cur[64];
    if (!GetAttr(line, e.attr, cur, sizeof(cur))) return;
    if (!strcmp(cur, e.val)) return;
    const bool raiseOk = o.allowRaise && !(o.lowVram && MemoryHeavy(tag, e.attr));
    if (!raiseOk && !CheaperOrEqual(tag, e.attr, cur, e.val)) {
        if (o.onChange) o.onChange(tag, e.attr, cur, e.val, true, o.user);
        return;
    }
    ReplaceAttr(line, cap, e.attr, e.val);
    if (o.onChange) o.onChange(tag, e.attr, cur, e.val, false, o.user);
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

const char* PresetName(Preset p) {
    switch (p) {
        case Preset::Quality: return "quality";
        case Preset::Potato:  return "potato";
        default:              return "balanced";
    }
}

int Entries(Preset p, Entry* out, int cap) { return Merge(p, out, cap); }

bool CheaperOrEqual(const char* tag, const char* attr, const char* original,
                    const char* tuned) {
    double o, t;
    if (!ParseNum(original, &o) || !ParseNum(tuned, &t)) return true;
    const bool higherIsCheaper = !strcmp(attr, "texelDensityBias") ||
                                 !strcmp(attr, "skyShadowLowLod") ||
                                 (!strcmp(tag, "cs_culling") && !strcmp(attr, "enabled"));
    return higherIsCheaper ? t >= o : t <= o;
}

bool MemoryHeavy(const char* tag, const char* attr) {
    const size_t n = strlen(attr);
    if (!strcmp(tag, "shadows")) return n > 7 && !strcmp(attr + n - 7, "MapSize");
    if (!strcmp(tag, "lighting")) return !strncmp(attr, "volumetricFogMap", 16);
    if (!strcmp(tag, "vehicle_reflections"))
        return !strcmp(attr, "envMapScale") || !strcmp(attr, "cubeMapScale") ||
               !strcmp(attr, "cubeMapEnabled");
    if (!strcmp(tag, "texture_streaming")) return true;
    // Ray-traced passes are never switched on for a low-VRAM GPU either (0.3.3).
    if (!strncmp(tag, "rt_", 3)) return !strcmp(attr, "enabled");
    return false;
}

bool ReadAttr(const char* xml, const char* tag, const char* attr, char* out, size_t cap) {
    char open[80];
    _snprintf(open, sizeof(open), "<%s ", tag);
    open[sizeof(open) - 1] = 0;
    const char* p = strstr(xml, open);
    if (!p) return false;
    const char* end = strchr(p, '>');
    if (!end) return false;
    char line[4096];
    size_t n = (size_t)(end - p) + 1;
    if (n >= sizeof(line)) n = sizeof(line) - 1;
    memcpy(line, p, n);
    line[n] = 0;
    return GetAttr(line, attr, out, cap);
}

size_t Transform(const char* in, char* out, size_t cap, const Options& o) {
    Entry entries[128];
    const int ne = Merge(o.preset, entries, 128);
    size_t w = 0;
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
            if (o.up != Upscaler::Keep && strcmp(tag, "antialiasing") == 0) {
                ApplyUpscaler(line, sizeof(line), o.up);
            }
            for (int i = 0; i < ne; ++i)
                if (!strcmp(entries[i].tag, tag)) GuardedAttr(line, sizeof(line), tag, entries[i], o);
        }

        size_t llen = strlen(line);
        if (w + llen + 1 > cap) return 0;
        memcpy(out + w, line, llen);
        w += llen;

        p += len;
    }
    out[w] = 0;
    return w;
}

size_t Transform(const char* in, char* out, size_t cap, Upscaler up, Preset preset) {
    Options o;
    o.up = up;
    o.preset = preset;
    return Transform(in, out, cap, o);
}

size_t RestoreOwned(const char* cur, const char* untuned, char* out, size_t cap) {
    // The upscaler backend selectors are the mod's too.
    static const Ov kAa[] = {{"antialiasing", "taa", ""},  {"antialiasing", "cas", ""},
                             {"antialiasing", "dlss", ""}, {"antialiasing", "fsr3", ""},
                             {"antialiasing", "xess", ""}};
    const Ov* tables[4] = {kOv, kQuality, kPotato, kAa};
    const int counts[4] = {kOvCount, kQualityCount, kPotatoCount, 5};
    size_t w = 0;
    const char* p = cur;
    while (*p) {
        const char* nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) + 1 : strlen(p);
        char line[4096];
        size_t copy = (len < sizeof(line) - 1) ? len : sizeof(line) - 1;
        memcpy(line, p, copy);
        line[copy] = 0;
        char tag[64];
        if (ElemTag(line, tag, sizeof(tag)) > 0) {
            for (int t = 0; t < 4; ++t)
                for (int i = 0; i < counts[t]; ++i) {
                    if (strcmp(tables[t][i].tag, tag)) continue;
                    char v[64];
                    if (ReadAttr(untuned, tag, tables[t][i].attr, v, sizeof(v)))
                        ReplaceAttr(line, sizeof(line), tables[t][i].attr, v);
                }
        }
        size_t llen = strlen(line);
        if (w + llen + 1 > cap) return 0;
        memcpy(out + w, line, llen);
        w += llen;
        p += len;
    }
    out[w] = 0;
    return w;
}

}  // namespace profile
