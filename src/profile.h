// Applies a tuned attribute delta on top of an F1 25 hardware_settings_config.xml.
// Pure code (no OS dependency) so it can be unit-tested standalone.
#pragma once
#include <stddef.h>

namespace profile {

// Which anti-aliasing / upscaling backend to write into <antialiasing .../>.
// Keep leaves the user's in-game choice untouched.
enum class Upscaler {
    Keep = 0,
    Taa,
    Dlss,
    Fsr3,
    Xess,
    Off,
};

// Which tuning preset to apply on top of the user's in-game settings.
//   Balanced : the original v0.2 delta (same look, much less work).
//   Quality  : Balanced, but ray-traced AO / DDGI, grass and the sharper
//              shadow / fog / AO settings stay on (for Ultra High / Ultra Max).
//   Potato   : everything that costs real GPU time is cut, ray tracing
//              included (for Low / Ultra Low and weak GPUs).
enum class Preset {
    Balanced = 0,
    Quality,
    Potato,
};

const char* PresetName(Preset p);

// Called once per tuned attribute: kept = true when the tuned value was NOT
// written because it would cost more than the player's own value.
typedef void (*ChangeFn)(const char* tag, const char* attr, const char* from,
                         const char* to, bool kept, void* user);

struct Options {
    Upscaler up = Upscaler::Keep;
    Preset   preset = Preset::Balanced;
    // false (default, 0.3.2): a tuned value is written only when it costs the same
    // or less than the value already in the file. true (enforce_mode = target):
    // tuned values may raise a setting.
    bool     allowRaise = false;
    // 6 GB or less, or integrated: memory-heavy settings never raise, even in target.
    bool     lowVram = false;
    // 0.3.6: a weak CPU also gets the cuts that save CPU time (crowd, particles,
    // trees, ground cover, night shadow casters) on any preset; lower only.
    bool     weakCpu = false;
    // 0.3.6: a 4 GB card streams textures at Potato's level on any preset.
    bool     tinyVram = false;
    ChangeFn onChange = nullptr;
    void*    user = nullptr;
};

// Rewrites known attributes in `in` (NUL-terminated XML) to tuned values,
// writing the result to `out` (cap bytes, NUL-terminated).
// When `up` is not Keep, the <antialiasing> element is switched to that backend.
// Returns the output length, or 0 on error.
size_t Transform(const char* in, char* out, size_t cap, const Options& o);

// Old signature (never raises).
size_t Transform(const char* in, char* out, size_t cap,
                 Upscaler up = Upscaler::Keep, Preset preset = Preset::Balanced);

// True when `tuned` costs the same or less GPU time than `original`. Numbers:
// lower is cheaper, except texelDensityBias. Booleans: false is cheaper, except
// skyShadowLowLod and cs_culling.enabled. Anything else counts as cheaper.
bool CheaperOrEqual(const char* tag, const char* attr, const char* original,
                    const char* tuned);

// Shadow / fog / reflection maps and texture streaming: they grow VRAM use.
bool MemoryHeavy(const char* tag, const char* attr);

// Value of attr on the first <tag ...> element. Returns false when missing.
bool ReadAttr(const char* xml, const char* tag, const char* attr, char* out, size_t cap);

// The upscaler the settings file has switched on (Keep when the file does not say).
Upscaler FileUpscaler(const char* xml);
// Frame generation on in the settings file (frame_gen mode not 0, or multi frame gen 2x+).
bool FrameGenOn(const char* xml);
// 0.3.5: in F1 25 frame generation belongs to the upscaler, so switching the
// upscaler can switch frame generation off and cost far more fps than it gains.
// With frame generation on, an upscaler different from the file's own becomes Keep.
Upscaler FrameGenGuard(const char* xml, Upscaler up);

// Every attribute the mod may change (any preset) is copied from `untuned` into
// `cur`, so the next Transform starts from the player's own values. Everything
// else (resolution, display, audio ...) stays as in `cur`. Returns the length or 0.
size_t RestoreOwned(const char* cur, const char* untuned, char* out, size_t cap);

// The merged tuned values of a preset (for tests).
struct Entry {
    const char* tag;
    const char* attr;
    const char* val;
};
int Entries(Preset p, Entry* out, int cap, bool weakCpu = false, bool tinyVram = false);

}  // namespace profile
