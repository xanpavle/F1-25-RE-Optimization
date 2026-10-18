// Applies a tuned attribute delta on top of an F1 25 hardware_settings_config.xml.
// Pure code (no OS dependency) so it can be unit-tested standalone.
#pragma once
#include <stddef.h>

namespace profile {

// Rewrites known attributes in `in` (NUL-terminated XML) to tuned values,
// writing the result to `out` (cap bytes, NUL-terminated).
// Returns the output length, or 0 on error.
size_t Transform(const char* in, char* out, size_t cap);

}  // namespace profile
