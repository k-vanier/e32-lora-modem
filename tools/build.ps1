# Compile les firmwares avec SDCC : intermédiaires dans build/, images dans firmware/.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$sdccBin = 'C:\Program Files\SDCC\bin'
if (Test-Path $sdccBin) { $env:PATH = "$sdccBin;$env:PATH" }

$targets = @(
    @{ Name = 'e32-868t30'; Src = 'modem.c';     Flags = @() },
    @{ Name = 'e32-868t20'; Src = 'modem.c';     Flags = @('-DMODEL_T20') },
    @{ Name = 'e32-433t20'; Src = 'modem.c';     Flags = @('-DMODEL_T20', '-DBAND_433') },
    @{ Name = 'uartprobe';  Src = 'uartprobe.c'; Flags = @() }
)

foreach ($t in $targets) {
    $out = Join-Path $root "build\$($t.Name)"
    New-Item -ItemType Directory -Force $out | Out-Null
    & sdcc -mstm8 --opt-code-size --out-fmt-ihx @($t.Flags) -o "$out\$($t.Name).ihx" (Join-Path $root "src\$($t.Src)")
    if ($LASTEXITCODE -ne 0) { throw "échec de compilation : $($t.Name)" }
    Copy-Item "$out\$($t.Name).ihx" (Join-Path $root 'firmware') -Force
    "{0,-12} {1,6} octets" -f $t.Name, (Get-Item "$out\$($t.Name).ihx").Length
}
