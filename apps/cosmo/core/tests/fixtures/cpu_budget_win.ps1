<#
    D-41 fixture — is a decode INSIDE the CPU budget, or outside it? (R-CPU-1/2c/4)

    The budget is enforced as a worker count, so the only honest check is to measure the
    CPU a real run actually consumed and compare it against what the user asked for. This
    script does that for the three paths a photo can be decoded through, at two budgets, and
    prints cores-busy for each. Windows-only on purpose: MSYS2's LibRaw is built -fopenmp,
    so the nested team R-CPU-2(c) exists to pin is only live here (D-12's platform note).

        pwsh apps/cosmo/core/tests/fixtures/cpu_budget_win.ps1 `
             -Cc  build-mingw64/apps/cosmo/cli/cosmo-cc.exe `
             -Raw C:/photos/DSC00729.ARW

    Expected, with the budget honoured on every path: meanCores tracks the budget on all
    three rows -- roughly total() at 25%, roughly 4x that at 100%.

    Actual on 2026-08-22 (16 logical cores, MSYS2 MINGW64):

        project  25%  meanCores 3.31   100%  meanCores 7.16     <- budget honoured
        render   25%  meanCores 2.89   100%  meanCores 7.01     <- budget honoured
        info     25%  meanCores 4.32   100%  meanCores 4.03     <- IDENTICAL: outside the budget

    `info` decodes with a bare NativeImageDecoder and no per-thread OpenMP pin, so LibRaw
    opens a machine-sized team the budget knows nothing about. The control is the last row:
    with OMP_NUM_THREADS=1 set BEFORE exec (the only way libgomp reads it -- D-12) the same
    decode falls to ~1.5 cores and 9 OS threads, which is what the pin would have achieved.
    That is the whole of D-41: the pin reaches ProjectLoader's pool workers and nothing else.
#>
param(
    [Parameter(Mandatory = $true)][string]$Cc,     # path to cosmo-cc.exe
    [Parameter(Mandatory = $true)][string]$Raw,    # path to one RAW file
    [string]$Sandbox = "$env:TEMP\cosmo-cpu-fixture",
    [string]$Mingw   = "C:\msys64\mingw64\bin"
)

$ErrorActionPreference = "Stop"
$cores = [Environment]::ProcessorCount

# A sandbox, always: configDir() honours XDG_CONFIG_HOME on Windows too, and without one
# this run would read and REWRITE the user's real settings.txt and recent.tsv.
$cfg = Join-Path $Sandbox "cosmo_v2"
New-Item -ItemType Directory -Force -Path $cfg | Out-Null
$imgs = Join-Path $Sandbox "imgs"
New-Item -ItemType Directory -Force -Path $imgs | Out-Null

# Eight copies of one RAW: enough entries that the decode pool is the thing being measured
# rather than one file's decode time.
0..7 | ForEach-Object { Copy-Item -Force $Raw (Join-Path $imgs "img$_.ARW") }
$cmp = Join-Path $Sandbox "raws.cmp"
$lines = @("cosmoworkspace=1")
$imgsFwd = $imgs.Replace([char]92, [char]47)   # .cmp paths are forward-slashed, whatever Join-Path gave us
0..7 | ForEach-Object { $lines += @("#image", "parent=-1", "path=$imgsFwd/img$_.ARW") }
Set-Content -Encoding ascii -Path $cmp -Value $lines

function Measure-Run {
    param([string]$Label, [string]$CcArgs, [int]$Pct, [string]$Omp = "")

    Set-Content -Encoding ascii -Path (Join-Path $cfg "settings.txt") `
        -Value "cosmosettings=1`npreviewEdge=1600`nthreads=0`nuseGpu=0`ncpuPercent=$Pct"

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = (Resolve-Path $Cc).Path
    $psi.Arguments = $CcArgs
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.EnvironmentVariables["XDG_CONFIG_HOME"] = $Sandbox.Replace([char]92, [char]47)
    $psi.EnvironmentVariables["PATH"] = "$Mingw;" + $env:PATH
    # Set before exec, never from main(): libgomp parses the environment in a load-time
    # constructor, which is the entirety of D-12.
    if ($Omp -ne "") { $psi.EnvironmentVariables["OMP_NUM_THREADS"] = $Omp }

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $p = [System.Diagnostics.Process]::Start($psi)
    $peak = 0
    while (-not $p.HasExited) {
        try { $p.Refresh(); if ($p.Threads.Count -gt $peak) { $peak = $p.Threads.Count } } catch {}
        Start-Sleep -Milliseconds 25
    }
    $null = $p.StandardOutput.ReadToEnd()
    $p.WaitForExit(); $sw.Stop()

    $cpu = $p.TotalProcessorTime.TotalSeconds
    $wall = $sw.Elapsed.TotalSeconds
    $mean = if ($wall -gt 0) { $cpu / $wall } else { 0 }
    "{0,-9} budget={1,3}%{2}  wall={3,6:N2}s  meanCores={4,5:N2} of {5}  ({6,4:N1}% of machine)  peakOSThreads={7}" -f `
        $Label, $Pct, $(if ($Omp -ne "") { "  OMP_NUM_THREADS=$Omp" } else { "" }), $wall, $mean, $cores, (100 * $mean / $cores), $peak
}

$q = '"'
Write-Output "machine: $cores logical cores"
foreach ($pct in 25, 100) {
    Measure-Run -Label "project" -Pct $pct -CcArgs "project $q$cmp$q --print"
    Measure-Run -Label "render"  -Pct $pct -CcArgs "render $q$Raw$q -o $q$Sandbox\r$pct.jpg$q --cpu"
    Measure-Run -Label "info"    -Pct $pct -CcArgs "info $q$Raw$q"
}
Write-Output "-- control: the same bare decode with the nested team pinned before exec --"
Measure-Run -Label "info" -Pct 25 -Omp "1" -CcArgs "info $q$Raw$q"
