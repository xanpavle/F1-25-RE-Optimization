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

// Rewrites known attributes in `in` (NUL-terminated XML) to tuned values,
// writing the result to `out` (cap bytes, NUL-terminated).
// When `up` is not Keep, the <antialiasing> element is switched to that backend.
// Returns the output length, or 0 on error.
size_t Transform(const char* in, char* out, size_t cap,
                 Upscaler up = Upscaler::Keep, Preset preset = Preset::Balanced);

}  // namespace profile
