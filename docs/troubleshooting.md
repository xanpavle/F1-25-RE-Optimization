# Troubleshooting

The mod writes one log file next to the game:

```
<game folder>\f125_tune.log
```

It is **rewritten on every launch**, so it always describes the current session.
A healthy launch is short and looks like this:

```
[mod] F1 25 RE-Optimization v0.2.7
[mod] session: exe=...\F1_25.exe (408063872 bytes)
[mod] session: ini=...\f125_tune.ini
[mod] profile: saved stock backup hardware_settings_config.xml.untuned
[mod] GPU: AMD Radeon RX 7800 XT (AMD 7000-series)
[mod] upscaler: mode=auto -> FSR 3
[mod] profile applied (tuned delta on hardware_settings_config.xml)
[mod] enforce: using known addresses (prologue verified)
[mod] enforce: int parser at the known RVA (prologue verified)
[mod] enforce: float parser at the known RVA (prologue verified)
[mod] enforce: settings parser hooked (tuned values applied in-engine)
[mod] enforce: float parser hooked (cloud RT scale / grass distance)
[mod] enforce: 17 bool attributes pinned to the tuned profile (0 disabled, 0 unavailable)
[mod] guard: preset hooks installed (apply=yes, applyNode=yes) - delta auto-restored after menu changes
[mod] hooks installed OK
```

Find your line below.

---

## Log lines

| Line | Meaning | What to do |
|---|---|---|
| `[mod] F1 25 RE-Optimization v0.2.7` | The DLL loaded with the game. | Good - the proxy is working. |
| `[mod] session: exe=… / ini=…` | Which executable and ini the mod actually loaded. | Check both paths if you have several game copies. |
| `[mod] hooks installed OK` | Both engine accessors were verified and patched. | Good. |
| `[mod] enforce: int parser at the known RVA (prologue verified)` | Same game build as the mod was derived from; the fast path was used. | Good. |
| `[mod] enforce: force-scan enabled - known addresses ignored, resolving by signature` | You created `f125_force_scan.txt`; the scanner is re-deriving everything. | Expected during testing. Delete the marker file afterwards; takes about 4 s. |
| `[mod] enforce: int parser re-derived by signature (score N/12, …)` | A different game build; the scanner relocated the parser and sites. | Good — send the log if anything looks off. |
| `[mod] enforce: WARN <name> unresolved (N candidate sites) - skipped` | The scanner could not uniquely re-derive that site after an update. | Harmless — that one value is left to the preset. |
| `[mod] enforce: int parser not found (game build unsupported)` | The change was too large for the scanner. | The file delta still applies; open an issue with the log. |
| `[mod] enforce: [enforce] overrides applied (N set, M disabled)` | Your `[enforce]` section changed the table. | Good — remove entries if unintended. |
| `[mod] profile applied (tuned delta on hardware_settings_config.xml)` | The settings file was rewritten. | Good. |
| `[mod] GPU: ...` | Detected adapter. | Check that it names your real GPU. |
| `[mod] GPU: not detected` | Detection failed (VM, remote session, unusual driver). | The mod still applies the profile; the upscaler is left unchanged. Run `build\gputest.exe`. |
| `[mod] upscaler: mode=auto -> FSR 3` | Backend chosen for your GPU. | Good. See [FAQ](faq.md#will-it-work-on-my-gpu). |
| `[mod] upscaler: unknown GPU - leaving your in-game upscaler unchanged` | No vendor detected, `auto` did nothing. | Set `[upscaler] mode = dlss`, `fsr3`, `xess` or `taa` manually. |
| `[mod] WARN: DLSS forced on a GPU without DLSS - the game may fall back` | You forced DLSS on GTX / non-NVIDIA. | Use `fsr3` instead, or `auto`. |
| `[mod] profile: saved stock backup hardware_settings_config.xml.untuned` | First run only — the restore point was created. | Good. Keep that file. |
| `[mod] WARN: could not write the .untuned settings backup` | No permission / disk full. | The mod still works, but `Restore-F125-Tune.bat` will have nothing to restore. |
| `[mod] WARN: bool getter signature mismatch - not patching` | **Different `F1_25.exe` build.** The engine hooks were skipped. | The settings delta still applies. Engine `[override]`s do not. Wait for an updated DLL. |
| `[mod] WARN: scalar getter signature mismatch - not patching` | Same, for the 32-bit accessor. | Same. |
| `[mod] WARN: bool getter hook FAILED` | Signature matched but the memory patch failed. | Usually an anti-cheat / AV / integrity tool. Close overlays, verify files. |
| `[mod] WARN: scalar getter hook FAILED` | Same, other accessor. | Same. |
| `[mod] profile: could not resolve Documents folder - skipping` | Windows profile is redirected or broken. | Move `Documents\My Games\F1 25` back to the default location, or set `[profile] apply = 0`. |
| `[mod] profile: hardware_settings_config.xml not found - skipping` | The game has never written its settings, or OneDrive moved Documents. | Launch the game once, quit cleanly, relaunch with the mod. Check `%USERPROFILE%\Documents\My Games\F1 25\hardwaresettings`. |
| `[mod] profile: read failed` | File exists but cannot be read. | Permissions / file locked by another tool. |
| `[mod] profile: transform failed` | The XML did not look like an F1 25 settings file. | Delete `hardware_settings_config.xml` and let the game recreate it. |
| `[mod] profile: write open failed` | Cannot open the file for writing. | Read-only attribute, or another process holds it. |
| `[mod] profile: write failed` | Write started but did not complete. | Disk full / antivirus. |
| `[0x...] Name = value` | Discovery mode (`[log] all = 1`). | Normal; set `all = 0` when done. |

---

## No log file at all

The DLL never loaded. Check, in order:

1. `dinput8.dll` **and** `f125_tune.ini` are directly next to `F1_25.exe`
   (not in a subfolder).
2. You launched the game from that same installation (not a second copy on
   another drive).
3. Another `dinput8.dll` mod was already there. The installer backs it up as
   `dinput8.dll.bak-f125`; only one file can own the name.
4. Your antivirus quarantined it. Check the AV log and allow the file, or build
   it yourself from source.

## The game does not start

- Remove `dinput8.dll` and try again. If it starts, the DLL is the cause.
- On a mismatched game build the hooks fail safe, so a startup crash is *not*
  expected from the mod. If it happens, run `Uninstall-F125-Tune.bat`.
- Verify the game files in Steam/EA App; a partial update can leave the exe in a
  state the mod has never seen.

## The mod loads but FPS does not change

1. Confirm `[profile] apply = 1` in `f125_tune.ini`.
2. Confirm the log says `profile applied`, `settings parser hooked` and
   `N/N bool attributes pinned`.
3. The log must show `enforce: <name> -> <tuned> (preset gave <value>, mode=<mode>)`
   lines. If it does not, the game build differs and the hooks were skipped — the log
   says so.
4. With `enforce_mode = cheaper` and a preset already at/below the tuning, the mod is a
   deliberate no-op. Use `target` if the engine is stuck on a low preset.
5. Check the upscaler line. If it says `keep` / `unknown GPU`, set
   `[upscaler] mode = auto` (or force your vendor's backend).
6. You may be CPU-bound, or capped by refresh rate / Frame Generation. See
   [Why is my FPS not higher?](faq.md#why-is-my-fps-not-higher).

## The installer cannot find the game

Pass the folder explicitly:

```bat
Install-F125-Tune.bat "D:\Games\F1 25"
```

The folder must contain `F1_25.exe`. Auto-detection covers Steam libraries,
Epic manifests, EA App and the default paths.

If the game is in `Program Files`, run the installer **as administrator**.

## The settings file looks stock / the mod seems to have reverted

From v0.2.0 this is expected. `hardware_settings_config.xml` is an **engine export**:
the engine writes its preset values into it and reads back those same values, and it may
overwrite the tuned file at any point. The mod does not depend on the file for the main
quality levers — it substitutes the tuned values inside the running engine (the `enforce:`
log lines) and pins the boolean attributes in code, so the picture quality does not
follow the file. The `apply` / `applyNode` hooks restore the tuned delta to the file after
a menu change, but the file is cosmetic for those settings.

## I want my old settings back

```bat
Restore-F125-Tune.bat
```

It removes the mod and copies `hardware_settings_config.xml.untuned` back. If the
backup does not exist (the mod never wrote a session), delete
`hardware_settings_config.xml` and let the game rebuild it, then pick a preset.

## Still stuck

Open an issue with:

- the full `f125_tune.log`,
- your GPU / CPU / resolution / preset,
- the game version and where you bought it,
- whether it worked before a game update.
