# The link lights (the small LINK lights in a corner of the screen showing the network activity) on or off for EVERY game in this kit:
# lights= in link.cfg (Tokyo Wars, Rave Racer) and in "Time Crisis 2\tc2_link.cfg". The menu launcher runs this.
#   link-lights.ps1 status   prints ON or OFF        link-lights.ps1 on | off | toggle
param([string]$what = "status")
$kit = Split-Path -Parent $PSScriptRoot
$files = @((Join-Path $kit "link.cfg"), (Join-Path $kit "Time Crisis 2\tc2_link.cfg"))

function Get-Lights($f) {
    if (-not (Test-Path $f)) { return $null }
    foreach ($l in Get-Content $f) { if ($l -match '^\s*lights\s*=\s*(\S+)') { return $matches[1] } }
    return "bottom-left"                                   # not set: the games' default
}
function Get-Was($f) {
    if (-not (Test-Path $f)) { return $null }
    foreach ($l in Get-Content $f) { if ($l -match '^\s*lights_was\s*=\s*(\S+)') { return $matches[1] } }
    return $null
}
function Set-Lights($f, $value) {
    if (-not (Test-Path $f)) { return }
    $old = Get-Lights $f
    $lines = @(Get-Content $f | Where-Object { $_ -notmatch '^\s*lights(_was)?\s*=' })
    $lines += "lights=$value"
    if ($value -eq "off" -and $old -and $old -ne "off") { $lines += "lights_was=$old" }   # where they were, for turning them back on
    Set-Content -Path $f -Value $lines -Encoding ASCII
}

$now = "OFF"
foreach ($f in $files) { $v = Get-Lights $f; if ($v -and $v -ne "off") { $now = "ON" } }
if ($what -eq "toggle") { $what = if ($now -eq "ON") { "off" } else { "on" } }
if ($what -eq "status") { Write-Output $now; exit 0 }
foreach ($f in $files) {
    if ($what -eq "off") { Set-Lights $f "off" }
    else { $was = Get-Was $f; if (-not $was -or $was -eq "off") { $was = "bottom-left" }; Set-Lights $f $was }
}
Write-Output ("Link lights are now " + $what.ToUpper() + " for every game (Tokyo Wars, Rave Racer, Time Crisis 2).")
Write-Output "Games already running pick it up when they are restarted."
