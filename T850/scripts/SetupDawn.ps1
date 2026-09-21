[CmdletBinding()]
param(
    [ValidateSet('Install', 'Check', 'Plan', 'Bootstrap')][string]$Mode = 'Install',
    [string]$SourceRoot,
    [ValidateSet('x64', 'ARM64')][string]$Architecture = 'x64'
)

$ErrorActionPreference = 'Stop'
if (!$SourceRoot) { $SourceRoot = Split-Path -Parent $PSScriptRoot }
$SourceRoot = [IO.Path]::GetFullPath($SourceRoot)
function Get-Sha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '') }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}
$vcpkgRoot = Join-Path $SourceRoot 'Librerias\vcpkg'
$vcpkg = Join-Path $vcpkgRoot 'vcpkg.exe'
$overlays = Join-Path $SourceRoot 'cmake\vcpkg-overlays'
$pinFile = Join-Path $overlays 'RequirePinnedVcpkg.cmake'
$pinText = Get-Content -LiteralPath $pinFile -Raw
if ($pinText -notmatch 'set\(T850_VCPKG_COMMIT "([0-9a-f]{40})"\)') { throw 'Invalid Dawn vcpkg revision pin.' }
$pin = $Matches[1]
if (!(Test-Path (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat')) -and $Mode -in @('Bootstrap','Install')) {
    if ((Test-Path $vcpkgRoot) -and @(Get-ChildItem $vcpkgRoot -Force).Count -gt 0) { throw "Refusing to replace the nonempty directory $vcpkgRoot." }
    & git clone --no-checkout https://github.com/microsoft/vcpkg.git $vcpkgRoot
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg clone failed.' }
    & git -C $vcpkgRoot checkout --detach $pin
    if ($LASTEXITCODE -ne 0) { throw 'Cannot check out the pinned vcpkg revision.' }
}
$actualPin = (& git -C $vcpkgRoot rev-parse HEAD)
if ($LASTEXITCODE -ne 0 -or $actualPin -ne $pin) { throw "Dawn requires vcpkg $pin at $vcpkgRoot; found $actualPin. Provision the pinned revision without updating unrelated dependencies." }
& git -C $vcpkgRoot diff --exit-code HEAD -- ports/dawn ports/imgui
if ($LASTEXITCODE -ne 0) { throw 'Dawn/ImGui upstream ports have local changes. Use tracked overlays instead.' }
if (!(Test-Path -LiteralPath $vcpkg) -and $Mode -in @('Bootstrap','Install')) {
    & (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed.' }
}
if (!(Test-Path -LiteralPath $vcpkg)) { throw "Bootstrap the pinned vcpkg checkout first: $vcpkgRoot\bootstrap-vcpkg.bat" }
if ($Mode -eq 'Bootstrap') { Write-Host "Pinned vcpkg ready: $pin"; return }

$triplet = "$($Architecture.ToLowerInvariant())-windows-static"
$packageRoot = Join-Path $vcpkgRoot "installed\$triplet"
$exportRoot = Join-Path $SourceRoot 'build\dawn-package'
if ($Architecture -eq 'ARM64') { $exportRoot += '-arm64' }
$auditPath = Join-Path $exportRoot 'package-audit.json'
$dawnManifest = Get-Content (Join-Path $overlays 'dawn\vcpkg.json') -Raw | ConvertFrom-Json
$imguiManifest = Get-Content (Join-Path $overlays 'imgui\vcpkg.json') -Raw | ConvertFrom-Json
$imguiFeatures = @('docking-experimental','dx11-binding','dx12-binding','vulkan-binding','opengl3-binding','sdl3-binding','win32-binding','webgpu-binding')
$translatorFiles = @('Framework/include/video/webgpu/WebGPUShaderCompiler.h', 'Framework/src/video/webgpu/WebGPUShaderCompiler.cpp',
    'Framework/include/utils/ShaderPreprocessor.h', 'Framework/src/utils/ShaderPreprocessor.cpp',
    'Librerias/simplecpp/simplecpp.h', 'Librerias/simplecpp/simplecpp.cpp')
$recipeFiles = @(Get-ChildItem $overlays -Recurse -File) + @(Get-ChildItem (Join-Path $SourceRoot 'cmake\dawn-package') -File) + @(Get-Item $PSCommandPath) + @($translatorFiles | ForEach-Object { Get-Item (Join-Path $SourceRoot $_) })
$recipeHashes = [ordered]@{}
foreach ($file in ($recipeFiles | Sort-Object FullName)) {
    $recipeHashes[$file.FullName.Substring($SourceRoot.Length + 1).Replace('\','/')] = Get-Sha256 $file.FullName
}

if ($Mode -ne 'Check') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path $vswhere)) { throw 'VS2022 discovery tool is missing.' }
    $compilerComponent = if ($Architecture -eq 'ARM64') { 'Microsoft.VisualStudio.Component.VC.Tools.ARM64' } else { 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' }
    $installations = @(& $vswhere -products '*' -version '[17.0,18.0)' -requires $compilerComponent -property installationPath)
    $visualStudio = $installations | Where-Object {
        (Test-Path (Join-Path $_ 'VC\Auxiliary\Build\vcvarsall.bat')) -and
        @(Get-ChildItem (Join-Path $_ "VC\Tools\MSVC\*\bin\Host*\$Architecture\cl.exe") -ErrorAction SilentlyContinue).Count -gt 0
    } | Select-Object -First 1
    if (!$visualStudio) { throw "A usable VS2022/v143 $Architecture compiler is required." }
    $env:VCPKG_VISUAL_STUDIO_PATH = $visualStudio
    if (!$env:VCPKG_MAX_CONCURRENCY) { $env:VCPKG_MAX_CONCURRENCY = '4' }
    $beforeText = & $vcpkg list --x-json
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect installed package versions.' }
    $before = ($beforeText -join "`n") | ConvertFrom-Json
    $rebuild = @()
    foreach ($manifest in @($dawnManifest, $imguiManifest)) {
        $previous = $before.PSObject.Properties["$($manifest.name):$triplet"].Value
        if ($previous -and ($previous.version -ne $manifest.version -or $previous.port_version -ne $manifest.'port-version')) {
            $rebuild += "$($manifest.name):$triplet"
        }
    }
    if ($rebuild.Count -gt 0) {
        $rebuild = @($rebuild + @("imgui:$triplet", "imguizmo:$triplet") | Sort-Object -Unique |
            Where-Object { $before.PSObject.Properties[$_] })
        Write-Host "Pinned overlay changed; rebuilding only these packages: $($rebuild -join ', ')"
        $removeArguments = @('remove') + $rebuild
        if ($Mode -eq 'Plan') { $removeArguments += '--dry-run' }
        & $vcpkg @removeArguments
        if ($LASTEXITCODE -ne 0) { throw 'Cannot replace pinned packages without affecting other dependents. Review the vcpkg removal plan.' }
    }
    $specs = @("dawn[core,d3d12]:$triplet", "imgui[$($imguiFeatures -join ',')]:$triplet", "imguizmo:$triplet", "glslang:$triplet")
    $arguments = @('install') + $specs + @("--overlay-ports=$overlays", '--recurse', '--no-print-usage')
    if ($Mode -eq 'Plan') { $arguments += '--dry-run' }
    & $vcpkg @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Dawn package installation/resolution failed.' }
    if ($Mode -eq 'Plan') { return }
}

$installedText = & $vcpkg list --x-json
if ($LASTEXITCODE -ne 0) { throw 'Cannot read installed package metadata.' }
$installed = ($installedText -join "`n") | ConvertFrom-Json
$dawn = $installed.PSObject.Properties["dawn:$triplet"].Value
$imgui = $installed.PSObject.Properties["imgui:$triplet"].Value
if (!$dawn -or $dawn.version -ne $dawnManifest.version -or $dawn.port_version -ne $dawnManifest.'port-version' -or
    @($dawn.features).Count -ne 1 -or $dawn.features[0] -ne 'd3d12') { throw 'Dawn package/version/features do not match the required D3D12-only overlay. Run SetupDawn.ps1.' }
if (!$imgui -or $imgui.version -ne $imguiManifest.version -or $imgui.port_version -ne $imguiManifest.'port-version' -or
    @($imguiFeatures | Where-Object { $_ -notin $imgui.features }).Count -gt 0) { throw 'The required ImGui backend package is missing or mismatched. Run SetupDawn.ps1.' }
$requiredFiles = @('include\webgpu\webgpu_cpp.h','include\imgui_impl_wgpu.h','lib\webgpu_dawn.lib','debug\lib\webgpu_dawn.lib',
    'include\src\tint\api\tint.h','include\src\utils\compiler.h','include\glslang\Public\ShaderLang.h',
    'bin\dxcompiler.dll','bin\dxil.dll','debug\bin\dxcompiler.dll','debug\bin\dxil.dll',
    'share\dawn\copyright','share\directx-dxc\copyright','share\dawn\vcpkg_abi_info.txt','share\imgui\vcpkg_abi_info.txt')
foreach ($relative in $requiredFiles) {
    if (!(Test-Path (Join-Path $packageRoot $relative))) { throw "Required Dawn package file is missing: $relative" }
}
$packageHashes = [ordered]@{}
foreach ($relative in @('share\dawn\vcpkg_abi_info.txt','share\imgui\vcpkg_abi_info.txt','share\directx-dxc\vcpkg_abi_info.txt','share\abseil\vcpkg_abi_info.txt','share\glslang\vcpkg_abi_info.txt')) {
    $packageHashes[$relative] = Get-Sha256 (Join-Path $packageRoot $relative)
}
$shaderIdentity = @($pin, $packageHashes['share\dawn\vcpkg_abi_info.txt'], $packageHashes['share\glslang\vcpkg_abi_info.txt'])
$shaderIdentity += @($translatorFiles | ForEach-Object { $recipeHashes[$_] })
$shaderConfigPath = Join-Path $exportRoot 'T850DawnShaderConfig.h'
$shaderConfig = "#pragma once`n#define T850_DAWN_SHADER_ABI `"$($shaderIdentity -join ';')`"`n"
if ($Mode -eq 'Install') {
    foreach ($configuration in @('dbg','rel')) {
        $cache = Get-Content (Join-Path $vcpkgRoot "buildtrees\dawn\$triplet-$configuration\CMakeCache.txt") -Raw -ErrorAction SilentlyContinue
        if ($cache) {
            if ($cache -notmatch '(?m)^DAWN_ENABLE_D3D12:BOOL=ON\r?$' -or $cache -match '(?m)^DAWN_ENABLE_(D3D11|VULKAN|DESKTOP_GL|OPENGLES|METAL|WEBGPU_ON_WEBGPU):BOOL=ON\r?$') { throw 'Built Dawn native backend configuration does not match D3D12-only policy.' }
            foreach ($flag in @('TINT_BUILD_SPV_READER','TINT_BUILD_WGSL_READER','TINT_BUILD_WGSL_WRITER','TINT_ENABLE_INSTALL')) {
                if ($cache -notmatch "(?m)^${flag}:BOOL=ON\r?`$") { throw "Required Tint translator setting is disabled: $flag" }
            }
        }
    }
    $queryRoot = Join-Path $exportRoot '.cmake\api\v1\query'
    [void][IO.Directory]::CreateDirectory($queryRoot)
    if (!(Test-Path $shaderConfigPath) -or [IO.File]::ReadAllText($shaderConfigPath) -cne $shaderConfig) {
        [IO.File]::WriteAllText($shaderConfigPath, $shaderConfig)
    }
    [IO.File]::WriteAllText((Join-Path $queryRoot 'codemodel-v2'), '')
    $cmakeCommand = Get-Command cmake.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    $cmake = if ($cmakeCommand) { $cmakeCommand.Source } else {
        Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    }
    if (!(Test-Path -LiteralPath $cmake)) { throw 'CMake is required to generate the Dawn exported-link contract.' }
    & $cmake -S (Join-Path $SourceRoot 'cmake\dawn-package') -B $exportRoot -G 'Visual Studio 17 2022' -A $Architecture "-DCMAKE_GENERATOR_INSTANCE=$visualStudio" "-DT850_DAWN_PACKAGE_ROOT=$packageRoot"
    if ($LASTEXITCODE -ne 0) { throw 'Dawn exported-link-contract generation failed.' }
    $replyRoot = Join-Path $exportRoot '.cmake\api\v1\reply'
    $indexFile = Get-ChildItem $replyRoot -Filter 'index-*.json' | Sort-Object Name -Descending | Select-Object -First 1
    $index = Get-Content $indexFile.FullName -Raw | ConvertFrom-Json
    $codemodel = Get-Content (Join-Path $replyRoot $index.reply.'codemodel-v2'.jsonFile) -Raw | ConvertFrom-Json
    $linkHashes = [ordered]@{}
    foreach ($configuration in @('Debug','Release')) {
        $model = $codemodel.configurations | Where-Object name -EQ $configuration
        $target = $model.targets | Where-Object name -EQ 'DawnPackageProbe'
        $details = Get-Content (Join-Path $replyRoot $target.jsonFile) -Raw | ConvertFrom-Json
        $fragments = @($details.link.commandFragments | Where-Object { $_.role -in @('libraries','libraryPath') } | ForEach-Object fragment)
        if ($fragments.Count -eq 0 -or ($fragments -join ' ') -notmatch 'webgpu_dawn') { throw "CMake did not resolve Dawn's $configuration link dependencies." }
        $document = [xml]'<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemDefinitionGroup><Link><AdditionalOptions /></Link></ItemDefinitionGroup></Project>'
        $document.Project.ItemDefinitionGroup.Link.AdditionalOptions = ($fragments -join ' ') + ' %(AdditionalOptions)'
        $document.Save((Join-Path $exportRoot "Dawn.$configuration.props"))
        $linkHashes[$configuration] = Get-Sha256 (Join-Path $exportRoot "Dawn.$configuration.props")
    }
    $audit = [ordered]@{ vcpkgCommit=$pin; sourceRoot=$SourceRoot; triplet=$triplet; visualStudio=$visualStudio; dawn=$dawn; imgui=$imgui; recipeHashes=$recipeHashes; packageHashes=$packageHashes; linkHashes=$linkHashes }
    $audit | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $auditPath -Encoding UTF8
} else {
    if (!(Test-Path $auditPath)) { throw 'Dawn link metadata is missing. Run scripts\SetupDawn.ps1.' }
    $audit = Get-Content $auditPath -Raw | ConvertFrom-Json
    if ($audit.sourceRoot -ne $SourceRoot -or $audit.vcpkgCommit -ne $pin -or $audit.triplet -ne $triplet) { throw 'Dawn package location or pin changed. Rerun scripts\SetupDawn.ps1.' }
    foreach ($name in $recipeHashes.Keys) {
        if ($audit.recipeHashes.PSObject.Properties[$name].Value -ne $recipeHashes[$name]) { throw "Dawn setup changed ($name). Rerun scripts\SetupDawn.ps1." }
    }
    foreach ($name in $packageHashes.Keys) {
        if ($audit.packageHashes.PSObject.Properties[$name].Value -ne $packageHashes[$name]) { throw 'Dawn package ABI changed. Rerun scripts\SetupDawn.ps1.' }
    }
}
foreach ($configuration in @('Debug','Release')) {
    $linkPath = Join-Path $exportRoot "Dawn.$configuration.props"
    if (!(Test-Path $linkPath)) { throw 'Generated Dawn linker properties are missing. Run scripts\SetupDawn.ps1.' }
    if ($Mode -eq 'Check' -and $audit.linkHashes.PSObject.Properties[$configuration].Value -ne (Get-Sha256 $linkPath)) { throw 'Generated Dawn link properties changed. Rerun scripts\SetupDawn.ps1.' }
}
if (!(Test-Path $shaderConfigPath) -or [IO.File]::ReadAllText($shaderConfigPath) -cne $shaderConfig) {
    throw 'Generated Dawn shader compiler identity is missing or stale. Run scripts\SetupDawn.ps1.'
}
Write-Host "Dawn package PASS: $($dawn.version)#$($dawn.port_version), $triplet, native backend D3D12 only."