param([int]$Lines = 40, [int]$GapMs = 450)
# Lignes numérotées de 40 octets : "A0001 xxxxxxxxx...\n"
$a = New-Object System.IO.Ports.SerialPort COM22,9600,None,8,One
$b = New-Object System.IO.Ports.SerialPort COM18,9600,None,8,One
$a.Open(); $b.Open(); Start-Sleep -Milliseconds 300
$a.DiscardInBuffer(); $b.DiscardInBuffer()

function MakeLine($tag, $i) { ("{0}{1:D4} " -f $tag, $i) + ('x' * 33) + "`n" }

function Check($name, $tag, $rx, $n) {
    $ok = @($rx -split "`n" | Where-Object { $_ -match "^$tag(\d{4}) x{33}$" } | ForEach-Object { [int]$matches[1] } | Sort-Object -Unique)
    $lost = $n - $ok.Count
    "{0,-26} envoyées {1,4}  reçues intactes {2,4}  perdues {3,4}  ({4:P0})" -f $name, $n, $ok.Count, $lost, ($ok.Count / $n)
}

function Run($title, $sendA, $sendB, $n, $gap) {
    $a.DiscardInBuffer(); $b.DiscardInBuffer()
    $rxA = ''; $rxB = ''
    $t = [Diagnostics.Stopwatch]::StartNew()
    for ($i = 1; $i -le $n; $i++) {
        if ($sendA) { $a.Write((MakeLine 'A' $i)) }
        if ($sendB) { $b.Write((MakeLine 'B' $i)) }
        if ($gap -gt 0) { Start-Sleep -Milliseconds $gap }
        $rxA += $a.ReadExisting(); $rxB += $b.ReadExisting()
    }
    $end = $t.Elapsed.TotalSeconds + 8
    while ($t.Elapsed.TotalSeconds -lt $end) { Start-Sleep -Milliseconds 100; $rxA += $a.ReadExisting(); $rxB += $b.ReadExisting() }
    "--- $title ($([int]$t.Elapsed.TotalSeconds) s)"
    if ($sendA) { Check 'COM22 -> COM18' 'A' $rxB $n }
    if ($sendB) { Check 'COM18 -> COM22' 'B' $rxA $n }
}

Run "1. Flux continu A->B, rythme radio" $true $false $Lines $GapMs
Run "2. Flux continu B->A, rythme radio" $false $true $Lines $GapMs
Run "3. Les deux sens EN MEME TEMPS, rythme radio" $true $true $Lines $GapMs
Run "4. Rafale A->B sans pause (2 Ko d'un coup)" $true $false 50 0

$a.Close(); $b.Close()
