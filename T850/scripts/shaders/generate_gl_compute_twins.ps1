# Generate the OpenGL (W-28) GLSL twins for the compute kernels.
#
# The GL backend has no runtime SPIRV-Cross: `GLCompute.cpp` loads `<kernel>.glsl` next to the
# `.hlsl`, inserts the pipeline defines after `#version`, reflects it through glslang for the
# binding layout and then compiles the GLSL text with the driver. So every kernel that GL can
# dispatch needs a committed GLSL twin, generated here from the HLSL that D3D12/Vulkan compile.
#
#   hlsl --glslangValidator -D -S comp--> SPIR-V --spirv-cross --version 430--> GLSL 430
#
# Only desktop GL 4.3 is targeted (`--no-es`); GLES keeps `SupportsComputeTextures3D()` false.
#
# Files written (next to the HLSL, in Assets/Shaders):
#   <stem>.glsl                 the `base` permutation
#   <stem>.<permutation>.glsl   every other permutation; `GLCompute::LoadComputeSource` prefers
#                               that name when `desc.permutationName` is not `base`
#
# Line 1 stays `#version 430` (the loader checks it) and line 2 is the GENERATED banner carrying
# `hlsl-sha256`. `scripts/validation/check_gl_twins.py` fails CI when the hash no longer matches,
# which is the protection the hand-maintained `.wgsl` twins never had.
#
# Never overwrites a hand-written twin: a `.glsl` without the GENERATED banner belongs to someone
# else (CS_Arithmetic, CS_Blur, CS_Bright, CS_GodRays, CS_HDRComposite, CS_ImagePattern*,
# CS_TorchParticles) and is left alone.
#
#   pwsh scripts/shaders/generate_gl_compute_twins.ps1            # write missing/stale twins
#   pwsh scripts/shaders/generate_gl_compute_twins.ps1 -Force     # rewrite all generated twins
#   pwsh scripts/shaders/generate_gl_compute_twins.ps1 -Check     # dry run, non-zero when stale
#
# Exit code 0 only when every kernel in the list below has an up-to-date twin.

[CmdletBinding()]
param(
    [string]$RootDir,
    [string]$SdkRoot = $env:VULKAN_SDK,
    # Generate only these kernels (stems, e.g. CS_FluidPressure3D). Default: the whole list.
    [string[]]$Kernel = @(),
    [switch]$Force,
    [switch]$Check,
    # Keep the intermediate SPIR-V and raw SPIRV-Cross output for inspection.
    [string]$KeepIntermediateDir = ''
)

$ErrorActionPreference = 'Stop'
if (-not $RootDir) { $RootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot) }
$shaderDir = Join-Path $RootDir 'Assets\Shaders'
if (-not (Test-Path $shaderDir)) { throw "No Assets/Shaders directory under $RootDir" }

if (-not $SdkRoot) { throw 'VULKAN_SDK is not set; pass -SdkRoot C:\VulkanSDK\<version>' }
$glslang = Join-Path $SdkRoot 'Bin\glslangValidator.exe'
$spirvCross = Join-Path $SdkRoot 'Bin\spirv-cross.exe'
foreach ($tool in @($glslang, $spirvCross)) {
    if (-not (Test-Path $tool)) { throw "Missing $tool" }
}

$bannerTag = 'GENERATED from'
$commonDefines = @('T850_SPIRV=1', 'T850_VULKAN=1', 'T850_GL=1')

# Kernels the GL backend can dispatch that are NOT in compute_kernels.json. The volume-pattern
# pairs are the 3D case of `--compute-selftest` (`ComputeSelfTest.cpp` names the .hlsl directly);
# CS_SeparableBlur is deliberately absent, it only feeds the Dawn shader probe.
$unregistered = @(
    @{ source = 'CS_VolumePatternWrite.hlsl';      entry = 'CS'; permutations = @{} },
    @{ source = 'CS_VolumePatternRead.hlsl';       entry = 'CS'; permutations = @{} },
    @{ source = 'CS_VolumeFloatPatternWrite.hlsl'; entry = 'CS'; permutations = @{} },
    @{ source = 'CS_VolumeFloatPatternRead.hlsl';  entry = 'CS'; permutations = @{} }
)

# Extra macros per registered permutation. A *generated* permutation missing here is a hard error,
# so a new permutation can never silently compile the base branch. Kernels whose twin is
# hand-written (CS_Blur, CS_Arithmetic, ...) express their permutations with `#ifdef` in the file
# and never reach the generator.
$permutationDefines = @{
    'fluid_seed' = @{ 'divergent' = @('SEED_DIVERGENT') }
    # particle_splat's stamp/resolve are runtime branches selected by the `Iteration` constant,
    # not preprocessor permutations. Declare them with no extra defines so the generator emits
    # the twin instead of treating the missing entry as an error.
    'particle_splat' = @{ 'stamp' = @(); 'resolve' = @() }
}

$registry = Get-Content -LiteralPath (Join-Path $shaderDir 'compute_kernels.json') -Raw | ConvertFrom-Json
$work = New-Object System.Collections.Generic.List[object]
foreach ($k in $registry.kernels) {
    $permutations = @{}
    foreach ($name in @($k.permutations)) {
        if ($name -eq 'base') { continue }
        $table = $permutationDefines[$k.id]
        if (-not $table -or -not $table.ContainsKey($name)) {
            $permutations[$name] = $null
        } else {
            # Assign directly. An empty @() returned from an if-expression unrolls to $null,
            # which made a declared no-define permutation look undeclared.
            $permutations[$name] = @($table[$name])
        }
    }
    $work.Add(@{ source = $k.source; entry = $k.entry; permutations = $permutations })
}
foreach ($extra in $unregistered) { $work.Add($extra) }

if ($Kernel.Count -gt 0) {
    $wanted = @{}
    foreach ($stem in $Kernel) { $wanted[[System.IO.Path]::GetFileNameWithoutExtension($stem)] = $true }
    $work = @($work | Where-Object { $wanted.ContainsKey([System.IO.Path]::GetFileNameWithoutExtension($_.source)) })
    if ($work.Count -eq 0) { throw "No kernel matched -Kernel $($Kernel -join ', ')" }
}

function Get-Sha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

$tempRoot = if ($KeepIntermediateDir) { $KeepIntermediateDir } else { Join-Path $env:TEMP ('t850-gl-twins-' + [Guid]::NewGuid().ToString('n').Substring(0, 8)) }
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null

$written = 0
$current = 0
$stale = New-Object System.Collections.Generic.List[string]
$failed = New-Object System.Collections.Generic.List[string]

foreach ($job in $work) {
    $sourcePath = Join-Path $shaderDir $job.source
    if (-not (Test-Path $sourcePath)) { $failed.Add("$($job.source): missing HLSL"); continue }
    $hash = Get-Sha256 $sourcePath
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($job.source)

    # A hand-written base twin means the whole family is hand-written; leave it alone.
    $basePath = Join-Path $shaderDir "$stem.glsl"
    if (Test-Path $basePath) {
        $head = @(Get-Content -LiteralPath $basePath -TotalCount 2)
        if (-not (($head.Count -ge 2) -and $head[1].Contains($bannerTag))) {
            Write-Host "  skip  $stem.glsl (hand-written, not generated)"
            continue
        }
    }

    $variants = @(@{ name = 'base'; defines = @() })
    foreach ($name in $job.permutations.Keys) {
        $variantDefines = $job.permutations[$name]
        if ($null -eq $variantDefines) {
            $failed.Add("$stem`: no defines declared for permutation '$name'; add it to permutationDefines")
            continue
        }
        $variants += @{ name = $name; defines = $variantDefines }
    }

    foreach ($variant in $variants) {
        $current++
        $targetName = if ($variant.name -eq 'base') { "$stem.glsl" } else { "$stem.$($variant.name).glsl" }
        $targetPath = Join-Path $shaderDir $targetName
        $label = "$($job.source)#$($variant.name)"

        $existing = @()
        if (Test-Path $targetPath) {
            $existing = @(Get-Content -LiteralPath $targetPath -TotalCount 2)
            if (-not $Force -and $existing.Count -ge 2 -and $existing[1].Contains("hlsl-sha256=$hash")) {
                Write-Host "  ok    $targetName"
                continue
            }
        }

        if ($Check) {
            $stale.Add("$targetName (missing or HLSL changed)")
            Write-Host "  STALE $targetName" -ForegroundColor Yellow
            continue
        }

        $suffix = if ($variant.name -eq 'base') { '' } else { ".$($variant.name)" }
        $spv = Join-Path $tempRoot "$stem$suffix.spv"
        $raw = Join-Path $tempRoot "$stem$suffix.raw.glsl"
        $defines = @($commonDefines) + @($variant.defines)
        $defineArgs = @($defines | ForEach-Object { "-D$_" })

        $glslangArgs = @('-D', '-S', 'comp', '-e', $job.entry, '--target-env', 'vulkan1.0') + $defineArgs + @('-o', $spv, $sourcePath)
        $log = & $glslang @glslangArgs 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) {
            $failed.Add("$label`: glslangValidator failed`n$log")
            Write-Host "  FAIL  $label (glslangValidator)" -ForegroundColor Red
            continue
        }

        $crossLog = & $spirvCross $spv --version 430 --no-es --combined-samplers-inherit-bindings --output $raw 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) {
            $failed.Add("$label`: spirv-cross failed`n$crossLog")
            Write-Host "  FAIL  $label (spirv-cross)" -ForegroundColor Red
            continue
        }

        $body = @(Get-Content -LiteralPath $raw)
        if ($body.Count -lt 2 -or $body[0] -notmatch '^#version') {
            $failed.Add("$label`: spirv-cross output does not begin with #version")
            Write-Host "  FAIL  $label (no #version)" -ForegroundColor Red
            continue
        }
        $banner = "// $bannerTag $($job.source) by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=$hash; defines=$($defines -join ',')"
        $lines = @($body[0], $banner) + @($body | Select-Object -Skip 1)
        # Write UTF-8 without a BOM. The GL compute loader requires line 1 to be `#version`;
        # PowerShell 5's `Set-Content -Encoding UTF8` prepends EF BB BF, which makes the engine
        # reject an otherwise valid twin.
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllLines($targetPath, [string[]]$lines, $utf8NoBom)
        $written++
        Write-Host "  write $targetName" -ForegroundColor Green
    }
}

if (-not $KeepIntermediateDir) { Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue }

Write-Host ''
if ($failed.Count -gt 0) {
    foreach ($message in $failed) { Write-Host "FAILED $message" -ForegroundColor Red }
    Write-Host "FAIL: $($failed.Count) twin(s) could not be generated" -ForegroundColor Red
    exit 1
}
if ($stale.Count -gt 0) {
    foreach ($message in $stale) { Write-Host "STALE $message" -ForegroundColor Yellow }
    Write-Host "STALE: $($stale.Count) twin(s) need regeneration; rerun without -Check" -ForegroundColor Yellow
    exit 1
}
Write-Host "PASS: $current GLSL twin(s) current, $written written" -ForegroundColor Green
exit 0
