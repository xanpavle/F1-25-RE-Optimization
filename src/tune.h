// Runtime setting-override table + discovery logging for the F1 25 renderer mod.
#pragma once
#include <stdint.h>
#include "gpu.h"
#include "profile.h"

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

// [profile] enforce = apply the tuned delta in-engine (past the preset clamp).
// [profile] guard   = re-write the delta after the graphics menu re-applies a preset.
// [profile] enforce_mode = target | cheaper | off
//   target  : always use the tuned value (the mod defines the quality level)
//   cheaper : use the cheaper of the preset value and the tuned value
//   off     : disable all in-engine enforcement (file delta only)
bool WantEnforce();
bool WantGuard();
bool EnforceTarget();
// [enforce] bools = 0 disables the boolean pinning (only meaningful in target mode).
bool WantEnforceBools();
// [log] all = 1 (discovery mode). Used for diagnostics.
bool LogAll();
// [log] report = 1: write the hook-resolution report to the log.
bool LogReport();
// [profile] unknown_build = scan | off. On a game build other than the one the
// addresses were taken from: `scan` re-derives every hook by signature (each one
// is skipped if its prologue does not verify); `off` disables the whole mod.
bool UnknownBuildOff();
// [profile] status_popup = 1: show a short "mod loaded / error" banner at launch.
bool StatusPopup();
// [profile] preset = quality | balanced | potato.
profile::Preset GetPreset();
// A/B switch: while f125_ab_stock.txt exists next to the game, the next launch
// runs fully stock (untouched settings restored, no in-engine hooks).
bool StockMode();
bool SetStockMode(bool on);

// F125_FORCE_SCAN=1 ignores the known addresses and resolves every hook by
// signature/name. Used to test the self-healing path against the current build.
bool ForceScan();
// Set at startup when F1_25.exe is not the build the addresses came from;
// makes ForceScan() true so no hard-coded address is trusted.
void SetUnknownBuild(bool unknown);

// Test seam: the GPU -> backend mapping without touching the ini or the OS.
namespace testing {
profile::Upscaler ChooseUpscaler(const gpu::Info& g, const char* mode);
}

// [enforce] per-attribute override of the in-engine table.
// Returns true when the attribute is listed; *value is the replacement and
// *disable is set for "off"/"disable"/"none"/"-" (skip that attribute).
bool LookupEnforce(const char* name, int* value, int* disable);

// Float variant: [enforce] accepts decimal literals ("2.0", "80.0").
bool LookupEnforceFloat(const char* name, float* value, int* disable);

// Game / ini paths (for diagnostics).
const wchar_t* ExeDir();
const wchar_t* IniPath();

// True if the setting should be overridden; writes the replacement value.
bool LookupBool(const char* name, uint8_t* out);
bool LookupScalar(const char* name, uint32_t* out);

// Discovery logging (only active when [log] all=1 or the name matches a prefix).
// Logs the first read of each (node, name) pair and every subsequent value
// change, so a settings reset/apply is captured as a value transition.
void LogRead(const void* node, const char* name, uint32_t raw, int is_bool);

}  // namespace tune
