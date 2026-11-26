# flysim regression check (Windows PowerShell 5.1).
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File check.ps1
# Requires out\baseline\metrics_seed42_20s_threads16.csv (created once by hand).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root
$fail = 0

function Pass($m) { Write-Host "[PASS] $m" -ForegroundColor Green }
function Fail($m) { Write-Host "[FAIL] $m" -ForegroundColor Red; $script:fail++ }
function Check($cond, $m) { if ($cond) { Pass $m } else { Fail $m } }

$exe = Join-Path $root 'flysim.exe'
if (-not (Test-Path $exe)) { Fail 'flysim.exe not found'; exit 1 }

# ---- 1. connectome ----
$v = (& $exe validate 2>&1 | Out-String)
Check ($v -match 'neurons\s+:\s+166700') 'validate: 166700 neurons'
Check ($v -match 'raw connection rows:\s+9039608') 'validate: 9039608 rows'
Check ($v -match 'unique \(pre,post\)\s+:\s+6242118') 'validate: 6242118 pairs'
Check ($v -match 'total synapses\s+:\s+89860280') 'validate: 89860280 synapses'

# ---- 2. eye tests ----
$e = (& $exe eyetest 2>&1 | Out-String)
Check ($e -match 'ESCAPE events \(DNp01 bursts\): total=2') 'eyetest: 2 escapes'
Check ($e -match 'looming=0\.056') 'eyetest: GF peak 0.056 on looming'
Check ($e -match 'rest=0\.00[0-9]') 'eyetest: rest GF near zero'

$l = (& $exe eyetest lc 2>&1 | Out-String)
Check ($l -match 'mode=debug-lc') 'eyetest lc: runs'
Check ($l -match 'ESCAPE events \(DNp01 bursts\): total=3') 'eyetest lc: 3 escapes'

$s = (& $exe --shuffle eyetest 2>&1 | Out-String)
Check ($s -match 'ESCAPE events \(DNp01 bursts\): total=0') 'shuffle: 0 escapes'
Check ($s -match 'looming=0\.01[89]|looming=0\.02[0-3]') 'shuffle: GF peak 0.018-0.023'

# ---- 3. CSV identity and speed ----
$base = Join-Path $root 'out\baseline\metrics_seed42_20s_threads16.csv'
$video = Join-Path $root 'out\test_video.mp4'
if (-not (Test-Path $base)) {
    Fail "baseline missing: $base"
} else {
    $t16 = Measure-Command {
        & $exe run --video $video --max-sec 20 --seed 42 --threads 16 2>&1 | Out-Null
    }
    $h16 = (Get-FileHash (Join-Path $root 'out\metrics_run.csv') -Algorithm SHA256).Hash
    $b16 = (Get-FileHash $base -Algorithm SHA256).Hash
    Check ($h16 -eq $b16) 'CSV 20s seed42 threads16 == baseline'
    $speed = 20.0 / $t16.TotalSeconds
    Write-Host ("[INFO] speed: {0:N2}x realtime (20s / {1:N1}s)" -f $speed, $t16.TotalSeconds)
    Check ($speed -ge 0.90) 'speed >= 0.90x'

    $t4 = Measure-Command {
        & $exe run --video $video --max-sec 20 --seed 42 --threads 4 2>&1 | Out-Null
    }
    $h4 = (Get-FileHash (Join-Path $root 'out\metrics_run.csv') -Algorithm SHA256).Hash
    Check ($h4 -eq $b16) 'CSV 20s seed42 threads4 == baseline'

    $header = Get-Content (Join-Path $root 'out\metrics_run.csv') -TotalCount 1
    Check (((($header -split ',').Count) -eq 24)) 'CSV has 24 columns'
}

if ($fail -eq 0) {
    Write-Host 'CHECK OK' -ForegroundColor Green
} else {
    Write-Host ("CHECK FAILED: {0} problem(s)" -f $fail) -ForegroundColor Red
}
exit $fail
