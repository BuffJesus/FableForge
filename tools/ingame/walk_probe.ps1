# Walk the hero toward a world Y (or X) during a harness --follow window: taps each movement key,
# reads the hero position the follow sampler logs, keeps the key that moves the right way and holds
# it. Prints the hero positions before / after. Used for the placed-prop collision checks.
param(
    [double]$TargetY = 0,
    [int]$HoldMs = 3500,
    [string]$Log = "C:\Programs\Steam\steamapps\common\Fable The Lost Chapters\FSE\FableScriptExtender.log"
)
$gw = Join-Path $PSScriptRoot "gamewin.ps1"
function HeroPos {
    $l = Get-Content $Log | Select-String "followpos" | Select-Object -Last 1
    if (-not $l) { return $null }
    $f = ("$l" -split "\|")
    return @([double]$f[7], [double]$f[8])
}
Start-Sleep -Milliseconds 1200
$p0 = HeroPos
Write-Output ("start {0:F2} {1:F2}" -f $p0[0], $p0[1])
$best = $null; $bestGain = 0
foreach ($k in @("W", "S", "A", "D")) {
    $a = HeroPos
    & $gw -Action hold -Keys $k -X 600 | Out-Null
    Start-Sleep -Milliseconds 1300
    $b = HeroPos
    $gain = ($b[1] - $a[1]) * [Math]::Sign($TargetY - $a[1])
    Write-Output ("{0}: dy {1:F2} dx {2:F2}" -f $k, ($b[1] - $a[1]), ($b[0] - $a[0]))
    if ($gain -gt $bestGain) { $bestGain = $gain; $best = $k }
}
if ($best) {
    Write-Output "holding $best"
    & $gw -Action hold -Keys $best -X $HoldMs | Out-Null
    Start-Sleep -Milliseconds 1500
}
$p1 = HeroPos
Write-Output ("end {0:F2} {1:F2}" -f $p1[0], $p1[1])
