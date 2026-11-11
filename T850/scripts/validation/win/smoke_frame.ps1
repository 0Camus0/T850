# One DayScene frame on the Windows host, to prove the tooling end to end.
$ErrorActionPreference = 'Continue'
$root = 'D:\sandbox\T850\T850\bin\x64\Release'
$out = 'D:\sandbox\__parity\smoke'
Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $out | Out-Null
Set-Location $root
Get-ChildItem -Directory -Filter 'dumps_*_f10_*' -ErrorAction SilentlyContinue |
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
$arguments = @(
    '--unattended', '--api', 'd3d12', '--scene', '8', '--backyard-no-hud',
    '--width', '640', '--height', '360', '--regressionFixedDt', '0.0166667',
    '--postProcessMode', 'compute', '--dump-frame', '10',
    '--logLevel', 'info', '--logFile', "$out\smoke.log"
)
& .\DayScene.exe @arguments 2>&1 | Out-File "$out\stdout.txt" -Encoding utf8
$code = $LASTEXITCODE
$dump = Get-ChildItem -Directory -Filter 'dumps_d3d12_f10_*' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
"session=$((Get-Process -Id $PID).SessionId) exit=$code dump=$($dump.Name)" | Set-Content "$out\result.txt"
if ($dump) {
    Get-ChildItem $dump.FullName -Filter '*.ppm' |
        ForEach-Object { Copy-Item $_.FullName (Join-Path $out ($_.BaseName + '.ppm')) }
    Remove-Item -Recurse -Force $dump.FullName
}
Get-Content "$out\result.txt"
