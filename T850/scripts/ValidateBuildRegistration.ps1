[CmdletBinding()]
param(
    [string]$SourceRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
$SourceRoot = [IO.Path]::GetFullPath($SourceRoot)

function Get-RelativeBuildPath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Path
    )

    return $Path.Substring($Root.Length + 1).Replace('\', '/')
}

$frameworkRoot = Join-Path $SourceRoot "Framework"
$frameworkProject = Get-Content (Join-Path $frameworkRoot "Framework.vcxproj") -Raw
$frameworkFilters = Get-Content (Join-Path $frameworkRoot "Framework.vcxproj.filters") -Raw
$frameworkCmake = Get-Content (Join-Path $frameworkRoot "CMakeLists.txt") -Raw
$androidCmake = Get-Content (Join-Path $SourceRoot "cmake\AndroidBuild.cmake") -Raw

$dayRoot = Join-Path $SourceRoot "DayScene"
$dayProject = Get-Content (Join-Path $dayRoot "DayScene.vcxproj") -Raw
$dayFilters = Get-Content (Join-Path $dayRoot "App.vcxproj.filters") -Raw
$dayCmake = Get-Content (Join-Path $dayRoot "CMakeLists.txt") -Raw

$imguiRoot = Join-Path $SourceRoot "FrameworkImGui"
$imguiProject = Get-Content (Join-Path $imguiRoot "FrameworkImGui.vcxproj") -Raw
$imguiFilters = Get-Content (Join-Path $imguiRoot "FrameworkImGui.vcxproj.filters") -Raw
$imguiCmake = Get-Content (Join-Path $imguiRoot "CMakeLists.txt") -Raw
$editorRoot = Join-Path $SourceRoot "T8ditor"
$editorProject = Get-Content (Join-Path $editorRoot "T8ditor.vcxproj") -Raw
$editorCmake = Get-Content (Join-Path $editorRoot "CMakeLists.txt") -Raw

$errors = New-Object System.Collections.Generic.List[string]
function Require-Entry {
    param(
        [Parameter(Mandatory = $true)][string]$Content,
        [Parameter(Mandatory = $true)][string]$Entry,
        [Parameter(Mandatory = $true)][string]$Owner
    )

    if (-not $Content.Contains($Entry)) {
        $errors.Add("$Owner is missing $Entry")
    }
}

$frameworkSources = @(
    Get-ChildItem (Join-Path $frameworkRoot "src\game") -Recurse -Filter "*.cpp"
    Get-ChildItem (Join-Path $frameworkRoot "src\terrain") -Recurse -Filter "*.cpp"
    Get-Item (Join-Path $frameworkRoot "src\debug\CrashDiagnostics.cpp")
    Get-Item (Join-Path $frameworkRoot "src\physics\GameplayLayers.cpp")
    Get-Item (Join-Path $frameworkRoot "src\scene\MutableMesh.cpp")
    Get-Item (Join-Path $frameworkRoot "src\scene\MutableMeshData.cpp")
    Get-Item (Join-Path $frameworkRoot "src\scene\ShadowSystem.cpp")
) | Sort-Object FullName -Unique

$frameworkHeaders = @(
    Get-ChildItem (Join-Path $frameworkRoot "include\game") -Recurse -Filter "*.h"
    Get-ChildItem (Join-Path $frameworkRoot "include\terrain") -Recurse -Filter "*.h"
    Get-Item (Join-Path $frameworkRoot "include\debug\CrashDiagnostics.h")
    Get-Item (Join-Path $frameworkRoot "include\physics\GameplayLayers.h")
    Get-Item (Join-Path $frameworkRoot "include\scene\MutableMesh.h")
    Get-Item (Join-Path $frameworkRoot "include\scene\MutableMeshData.h")
) | Sort-Object FullName -Unique

foreach ($source in $frameworkSources) {
    $relative = Get-RelativeBuildPath -Root $frameworkRoot -Path $source.FullName
    $msbuildPath = $relative.Replace('/', '\')
    Require-Entry $frameworkProject $msbuildPath "Framework.vcxproj"
    Require-Entry $frameworkFilters $msbuildPath "Framework.vcxproj.filters"
    Require-Entry $frameworkCmake $relative "Framework/CMakeLists.txt"
    Require-Entry $androidCmake "Framework/$relative" "cmake/AndroidBuild.cmake"
}

foreach ($header in $frameworkHeaders) {
    $relative = (Get-RelativeBuildPath -Root $frameworkRoot -Path $header.FullName).Replace('/', '\')
    Require-Entry $frameworkProject $relative "Framework.vcxproj"
    Require-Entry $frameworkFilters $relative "Framework.vcxproj.filters"
}

foreach ($scene in @("VoxelScene", "MinecraftScene")) {
    foreach ($name in @("$scene.cpp", "$scene.h")) {
        Require-Entry $dayProject $name "DayScene.vcxproj"
        Require-Entry $dayFilters $name "DayScene/App.vcxproj.filters"
        if ($name.EndsWith(".cpp")) {
            Require-Entry $dayCmake $name "DayScene/CMakeLists.txt"
            Require-Entry $androidCmake "DayScene/$name" "cmake/AndroidBuild.cmake"
        }
    }
}

$ragdollGui = "src/RagdollEditorGui.cpp"
Require-Entry $imguiProject ($ragdollGui.Replace('/', '\')) "FrameworkImGui.vcxproj"
Require-Entry $imguiFilters ($ragdollGui.Replace('/', '\')) "FrameworkImGui.vcxproj.filters"
Require-Entry $imguiCmake $ragdollGui "FrameworkImGui/CMakeLists.txt"

foreach ($sharedScene in @("RagdollEditor.cpp", "Quake3Mock.cpp", "SceneTemplate.cpp")) {
    Require-Entry $editorProject "..\DayScene\$sharedScene" "T8ditor.vcxproj"
    Require-Entry $editorCmake "../DayScene/$sharedScene" "T8ditor/CMakeLists.txt"
}

if ($errors.Count -gt 0) {
    $errors | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host "Build registration PASS" -ForegroundColor Green
Write-Host "  Framework sources: $($frameworkSources.Count)" -ForegroundColor Green
Write-Host "  Framework headers: $($frameworkHeaders.Count)" -ForegroundColor Green
Write-Host "  DayScene: VoxelScene.cpp/.h, MinecraftScene.cpp/.h" -ForegroundColor Green
Write-Host "  FrameworkImGui: RagdollEditorGui.cpp" -ForegroundColor Green
Write-Host "  T8ditor shared scenes: RagdollEditor, Quake3Mock, SceneTemplate" -ForegroundColor Green