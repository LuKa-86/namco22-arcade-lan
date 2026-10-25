# Session recordings (each game keeps the last played session's inputs as <game>_last.rec, so a problem can be replayed exactly)
# on or off for EVERY game in this kit: record= in link.cfg (Tokyo Wars, Rave Racer) and in "Time Crisis 2\tc2_link.cfg".
# The menu launcher runs this.
#   recordings.ps1 status   prints ON or OFF        recordings.ps1 on | off | toggle
param([string]$what = "status")
$kit = Split-Path -Parent $PSScriptRoot
$files = @((Join-Path $kit "link.cfg"), (Join-Path $kit "Time Crisis 2\tc2_link.cfg"))

function Get-Record($f) {
    if (-not (Test-Path $f)) { return $null }
    foreach ($l in Get-Content $f) { if ($l -match '^\s*record\s*=\s*(\S+)') { return $matches[1] } }
    return "on"                                            # not set: the games' default
}
function Set-Record($f, $value) {
    if (-not (Test-Path $f)) { return }
    $lines = @(Get-Content $f | Where-Object { $_ -notmatch '^\s*record\s*=' })
    $lines += "record=$value"
    Set-Content -Path $f -Value $lines -Encoding ASCII
}

$now = "OFF"
foreach ($f in $files) { $v = Get-Record $f; if ($v -and $v -ne "off") { $now = "ON" } }
if ($what -eq "toggle") { $what = if ($now -eq "ON") { "off" } else { "on" } }
if ($what -eq "status") { Write-Output $now; exit 0 }
foreach ($f in $files) { Set-Record $f $what }
Write-Output ("Session recordings are now " + $what.ToUpper() + " for every game (Tokyo Wars, Rave Racer, Time Crisis 2).")
Write-Output "Games already running pick it up when they are restarted."
