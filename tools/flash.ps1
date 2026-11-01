# Flashe une image sur un module E32 via ST-Link V2 (SWIM).
# Exemple : .\tools\flash.ps1 e32-868t20
param(
    [Parameter(Mandatory)] [ValidateSet('e32-868t30', 'e32-868t20', 'e32-433t20', 'uartprobe')] [string]$Image,
    [string]$OpenOcd = 'C:\Users\bsdouille\openocd'
)
$root = Split-Path $PSScriptRoot -Parent
$ihx = (Join-Path $root "firmware\$Image.ihx") -replace '\', '/'
& "$OpenOcd\bin\openocd.exe" -s "$OpenOcd\openocd\scripts" -f interface/stlink.cfg -c "adapter speed 363" `
    -f target/stm8l.cfg -c "reset_config srst_only connect_assert_srst" `
    -c "init; halt; load_image $ihx; reset run; shutdown"
