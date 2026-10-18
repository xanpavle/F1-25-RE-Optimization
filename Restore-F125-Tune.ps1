# Undo the F1 25 Renderer Tune mod.
# Removes the proxy dinput8.dll + tune files and restores the original
# hardware_settings_config.xml.

$game = "C:\Program Files (x86)\Steam\steamapps\common\F1 25"
$cfg  = "C:\Users\Pavle\Documents\My Games\F1 25\hardwaresettings\hardware_settings_config.xml"

"Removing mod files from: $game"
foreach ($f in @("dinput8.dll","f125_tune.ini","f125_tune.log")) {
    $p = Join-Path $game $f
    if (Test-Path $p) { Remove-Item $p -Force; "  removed $f" } else { "  (not present) $f" }
}

if (Test-Path "$cfg.untuned") {
    Copy-Item "$cfg.untuned" $cfg -Force
    "Restored original settings from hardware_settings_config.xml.untuned"
} else {
    "No .untuned backup found - re-pick a preset in-game to restore defaults."
}
"Done."
