param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Out,
    [string]$Apis  = 'vulkan,d3d12,d3d11,gl',
    [int]$VulkanRepeats = 2,
    [switch]$SkipGate,
    [switch]$SkipCells,
    [switch]$SkipSelfTest,
    [switch]$Skip4k,
    [switch]$GateOnly
)
# Backyard 8 Windows parity matrix (WO-W1).
# For each API: the gate, the 8 parity cells, the game self-test, one 3840x2160 run, and a log
# scan. Every capture is copied under $Out\<api>\ with its log. A progress file
# $Out\progress.txt gets one line per finished step so a partial run can be read while it runs.
# -Exe is <project>\bin\x64\Release\DayScene.exe and -Out a scratch folder; both are mandatory
# because no host path belongs in the repository. `windows_parity.py run` passes them.
$ErrorActionPreference = 'Continue'
$exeDir = Split-Path -Parent $Exe
$project = Split-Path -Parent (Split-Path -Parent $exeDir)     # <project>\bin\x64\Release
$assets = Join-Path $project 'Assets\Scenes\Backyard8Fluid.json'
$gate = Join-Path $project 'scripts\validation\backyard8_regression.py'
$ppm2png = Join-Path $project 'scripts\validation\ppm2png.py'
$progress = Join-Path $Out 'progress.txt'

New-Item -ItemType Directory -Force $Out | Out-Null
"=== parity start $(Get-Date -Format o) exe=$Exe" | Set-Content $progress
"session=$((Get-Process -Id $PID).SessionId) exe_time=$((Get-Item $Exe).LastWriteTime)"

function Note([string]$line) {
    $line | Tee-Object -FilePath $progress -Append | Write-Host
}

# The spheres style: same scene config, renderMode 0 and style 3.
$spheres = Join-Path $Out 'spheres.json'
$config = Get-Content $assets -Raw | ConvertFrom-Json
$config.particles.renderMode = 0
$config.particles.style = 3
[IO.File]::WriteAllText($spheres, ($config | ConvertTo-Json -Depth 20),
    (New-Object System.Text.UTF8Encoding($false)))

function Scan-Log([string]$tag, [string]$log) {
    if (!(Test-Path $log)) { Note "LOG_MISSING $tag"; return }
    $bad = @(
        'Shader compile failed',
        'using graphics implementation',
        'compute dispatch failed',
        'DispatchTooLarge',
        'cannot build constants',
        "does not declare the kernel's complete binding layout",
        'device removed',
        'device lost',
        'DEVICE_REMOVED',
        'DXGI_ERROR_DEVICE_REMOVED',
        'D3D12: Removing Device'
    )
    foreach ($pattern in $bad) {
        $hits = @(Select-String -Path $log -SimpleMatch -Pattern $pattern)
        if ($hits.Count -gt 0) {
            Note "BAD $tag pattern='$pattern' count=$($hits.Count)"
            $hits | Select-Object -First 2 | ForEach-Object { Note "  $($_.Line.Trim())" }
        }
    }
    $errors = @(Select-String -Path $log -Pattern '\[ERROR\]')
    if ($errors.Count -gt 0) {
        Note "ERRORS $tag count=$($errors.Count)"
        $errors | Select-Object -First 4 | ForEach-Object { Note "  $($_.Line.Trim())" }
    }
    $enabled = @(Select-String -Path $log -SimpleMatch -Pattern 'enabled compute kernel').Count
    $dispatched = @(Select-String -Path $log -SimpleMatch -Pattern 'dispatched compute').Count
    Note "COUNTS $tag enabled=$enabled dispatched=$dispatched"
    $adapter = @(Select-String -Path $log -Pattern 'adapter|Adapter') |
        ForEach-Object { $_.Line.Trim() } | Select-Object -First 3
    foreach ($a in $adapter) { Note "ADAPTER $tag $a" }
}

function Convert-Dumps([string]$dir, [string]$tag) {
    $src = Join-Path $dir "${tag}.dumps"
    if (!(Test-Path $src)) { return }
    Get-ChildItem -Path $src -Filter '*.ppm' -Recurse | ForEach-Object {
        Copy-Item $_.FullName (Join-Path $dir ($_.BaseName + '.ppm')) -Force
    }
    $ppm = @(Get-ChildItem -Path $dir -Filter '*.ppm')
    foreach ($f in $ppm) {
        & python $ppm2png $f.FullName 2>&1 | Out-Null
    }
    Remove-Item -Recurse -Force $src -ErrorAction SilentlyContinue
    $keep = @('RT_Dump_HDR_Final', 'RT_Dump_FluidParticles')
    Get-ChildItem -Path $dir -Filter '*.ppm' | Where-Object { $keep -notcontains $_.BaseName } |
        Remove-Item -Force -ErrorAction SilentlyContinue
    # PNG for the fluid particles too, whatever the API's dump format was.
    foreach ($name in $keep) {
        $p = Join-Path $dir "$name.ppm"
        if (Test-Path $p) { & python $ppm2png $p 2>&1 | Out-Null }
    }
}

function Run-Cell([string]$api, [string]$cell, [string[]]$viewArgs, [string[]]$startArgs) {
    $dir = Join-Path $Out "$api\cells\$cell"
    New-Item -ItemType Directory -Force $dir | Out-Null
    $log = Join-Path $dir 'run.log'
    Remove-Item $log -ErrorAction SilentlyContinue
    Get-ChildItem -Directory -Path $exeDir -Filter "dumps_${api}_f*" -ErrorAction SilentlyContinue |
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    $frame = if ($startArgs -match '--dump-frame') { $startArgs[$startArgs.IndexOf('--dump-frame') + 1] } else { '300' }
    $arguments = @(
        '--unattended', '--api', $api, '--scene', '8', '--backyard-no-hud',
        '--width', '1920', '--height', '1080',
        '--regressionFixedDt', '0.0166667', '--postProcessMode', 'compute',
        '--logLevel', 'info', '--logFile', $log
    ) + $viewArgs + $startArgs
    $sw = [Diagnostics.Stopwatch]::StartNew()
    Push-Location $exeDir
    try {
        $outText = & $Exe @arguments 2>&1 | Out-String
    } finally { Pop-Location }
    $sw.Stop()
    $code = $LASTEXITCODE
    $outText | Set-Content (Join-Path $dir 'stdout.txt')
    Note "EXIT $api $cell $code $([int]$sw.Elapsed.TotalSeconds)s"
    if ($code -ne 0) { Note '  ' + ($outText.Trim().Split("`n") | Select-Object -Last 4 | ForEach-Object { $_.Trim() }) -join ' / ' }
    $dump = Get-ChildItem -Directory -Path $exeDir -Filter "dumps_${api}_f${frame}_*" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($dump) {
        Copy-Item -Recurse $dump.FullName (Join-Path $dir "$cell.dumps")
        Remove-Item -Recurse -Force $dump.FullName
        Convert-Dumps $dir $cell
        Note "DUMP $api $cell $($dump.Name)"
    } else {
        Note "DUMP_MISSING $api $cell"
    }
    Scan-Log "$api/$cell" $log
    if ($dump) { Note "TARGETS $api $cell " + ((Get-ChildItem -Path $dir -Filter '*.ppm' | ForEach-Object { $_.BaseName }) -join ',') }
}

function Run-Gate([string]$api) {
    $dir = Join-Path $Out "$api\gate"
    New-Item -ItemType Directory -Force $dir | Out-Null
    $log = Join-Path $dir 'gate.log'
    $sw = [Diagnostics.Stopwatch]::StartNew()
    Push-Location $exeDir
    try {
        $text = & python $gate --api $api --day-scene $Exe 2>&1 | Out-String
    } finally { Pop-Location }
    $sw.Stop()
    $code = $LASTEXITCODE
    $text | Set-Content $log
    Note "GATE $api exit=$code $([int]$sw.Elapsed.TotalSeconds)s"
    $text.Trim().Split("`n") | Where-Object { $_ -match '^(PASS|FAIL|RESULT)' } | ForEach-Object { Note "  $($_.Trim())" }
    Scan-Log "$api/gate" $log
}

function Run-SelfTest([string]$api) {
    $dir = Join-Path $Out "$api\selftest"
    New-Item -ItemType Directory -Force $dir | Out-Null
    Push-Location $exeDir
    try {
        $text = & $Exe --unattended --api $api --game-selftest 2>&1 | Out-String
    } finally { Pop-Location }
    $code = $LASTEXITCODE
    $text | Set-Content (Join-Path $dir 'selftest.txt')
    $pass = @($text -split "`n" | Where-Object { $_ -match '^PASS ' }).Count
    $fail = @($text -split "`n" | Where-Object { $_ -match '^FAIL ' }).Count
    Note "SELFTEST $api exit=$code PASS=$pass FAIL=$fail"
    if ($fail -gt 0) {
        $text -split "`n" | Where-Object { $_ -match '^FAIL ' } | Select-Object -First 5 |
            ForEach-Object { Note "  $($_.Trim())" }
    }
}

function Run-4K([string]$api) {
    $dir = Join-Path $Out "$api\cells\4k_default_water150"
    New-Item -ItemType Directory -Force $dir | Out-Null
    $log = Join-Path $dir 'run.log'
    Remove-Item $log -ErrorAction SilentlyContinue
    Get-ChildItem -Directory -Path $exeDir -Filter "dumps_${api}_f*" -ErrorAction SilentlyContinue |
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    $arguments = @(
        '--unattended', '--api', $api, '--scene', '8', '--backyard-no-hud',
        '--backyard-start', 'water', '--width', '3840', '--height', '2160',
        '--regressionFixedDt', '0.0166667', '--postProcessMode', 'compute',
        '--fluid-render', 'screen-space', '--dump-frame', '150',
        '--logLevel', 'info', '--logFile', $log
    )
    $sw = [Diagnostics.Stopwatch]::StartNew()
    Push-Location $exeDir
    try {
        $text = & $Exe @arguments 2>&1 | Out-String
    } finally { Pop-Location }
    $sw.Stop()
    $code = $LASTEXITCODE
    $text | Set-Content (Join-Path $dir 'stdout.txt')
    Note "EXIT $api 4k $code $([int]$sw.Elapsed.TotalSeconds)s"
    $dump = Get-ChildItem -Directory -Path $exeDir -Filter "dumps_${api}_f150_*" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($dump) {
        Copy-Item -Recurse $dump.FullName (Join-Path $dir 'd')
        Remove-Item -Recurse -Force $dump.FullName
        Convert-Dumps $dir 'd'
        Note "DUMP $api 4k $($dump.Name)"
    } else { Note "DUMP_MISSING $api 4k" }
    Scan-Log "$api/4k" $log
}

# The 8 parity cells (documentation/scenes/backyard8-windows-port-work-orders.md).
$views = @{
    'default' = @()
    'close'   = @('--backyard-camera', '0', '1.3', '-10.0', '0.32', '0')
}
$starts = @{
    'water150' = @('--backyard-start', 'water', '--dump-frame', '150')
    'lake300'  = @('--dump-frame', '300')
}
$renders = @{
    'ss'     = @('--fluid-render', 'screen-space')
    'surfel' = @('--fluid-render', 'splat')
}

foreach ($api in ($Apis -split ',')) {
    Note "=== API $api start $(Get-Date -Format HH:mm:ss) ==="
    if (!$SkipGate) { Run-Gate $api }
    if ($GateOnly) { Note "=== API $api done (gate only) ==="; continue }
    if (!$SkipCells) {
        $repeats = if ($api -eq 'vulkan') { $VulkanRepeats } else { 1 }
        foreach ($r in $renders.Keys) {
            foreach ($v in $views.Keys) {
                foreach ($s in $starts.Keys) {
                    $cell = "${r}_${v}_${s}"
                    for ($n = 1; $n -le $repeats; $n++) {
                        $name = if ($repeats -gt 1) { "${cell}_r$n" } else { $cell }
                        Run-Cell $api $name ($renders[$r] + $views[$v]) $starts[$s]
                    }
                }
            }
        }
    }
    if (!$Skip4k) { Run-4K $api }
    if (!$SkipSelfTest) { Run-SelfTest $api }
    Note "=== API $api done $(Get-Date -Format HH:mm:ss) ==="
}
Note 'ALL_DONE'
Set-Content -Path (Join-Path $Out 'DONE.txt') -Value 'DONE'
