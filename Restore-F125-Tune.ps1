param([string]$Game = '')

# Removes the mod AND restores the stock hardware_settings_config.xml from the
# .untuned backup the mod creates on first launch.
$p = @{ Action = 'Restore' }
if ($Game) { $p.Game = $Game }
& "$PSScriptRoot\Install-F125-Tune.ps1" @p
