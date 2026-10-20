// In-engine setting enforcement for the F1 25 renderer mod.
//
// The game loads the graphics profile out of hardware_settings_config.xml, but
// then *clamps every restricted attribute back to the values allowed by the
// preset you picked in the graphics menu*. That is why a tuned file delta
// (fog map size, shadow map size, crowd distance, ...) silently reverts: the
// engine never uses the file's value for a restricted setting.
//
// The engine reads each restricted value by walking its own settings tree and
// calling one shared integer parser:
//
//     int FUN_14534dcd8(const char* valueString, int def, int flags)
//
// Every call site belongs to exactly one setting, so the *return address* tells
// us which setting is being read. We detour that parser and return the tuned
// value for the call sites we care about. This is pure value substitution: no
// engine memory is written, so it is safe even if the game changes slightly
// (an unknown return address simply falls through to the original parser).
//
// We also watch the settings-file lifecycle: whenever the engine (re)applies a
// settings preset - `hardwareSettings::Options::apply` / `applyNode` - we
// re-write the tuned delta to disk so the file never drifts from the mod.
#pragma once

namespace enforce {

// Installs the enforcement detour(s) and (optionally) the preset watchdog.
// Safe to call once, after tune::Init().
void Init();

// True when the in-engine value enforcement is live.
bool Active();

}  // namespace enforce
