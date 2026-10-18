// Runtime setting-override table + discovery logging for the F1 25 renderer mod.
#pragma once
#include <stdint.h>

namespace tune {

// Sets the DLL's own module handle (used for the ini fallback path).
void SetSelf(void* module);

// Reads "<exe dir>\f125_tune.ini" (falls back to the DLL's own directory) and
// prepares the override table and log path.
void Init();

// Appends a free-form status line to the discovery log.
void Note(const char* msg);

// If [profile] apply=1, rewrites the tuned attributes of the game's
// hardware_settings_config.xml in place (in memory -> disk) before the engine
// reads it, and applies the [upscaler] backend for the detected GPU.
// Returns true on success.
bool ApplyProfile();

// True if the setting should be overridden; writes the replacement value.
bool LookupBool(const char* name, uint8_t* out);
bool LookupScalar(const char* name, uint32_t* out);

// Discovery logging (only active when [log] all=1 or the name matches a prefix).
// Logs the first read of each (node, name) pair and every subsequent value
// change, so a settings reset/apply is captured as a value transition.
void LogRead(const void* node, const char* name, uint32_t raw, int is_bool);

}  // namespace tune
