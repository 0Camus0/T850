# SyncShadowLayout.ps1
# Copies positions & sizes from the "Shadows" group to all other groups
# (except "Global") for the shared controls listed below.
# Run from any directory — uses relative path from script location.

param(
    [switch]$DryRun  # Pass -DryRun to preview without writing
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$layoutPath = Join-Path (Split-Path $scriptDir -Parent) "Assets\Layouts\gui_layout.json"

if (-not (Test-Path $layoutPath)) {
    Write-Host "ERROR: Layout file not found: $layoutPath" -ForegroundColor Red
    exit 1
}

# Element IDs to synchronize from the Shadows group
$elementIds = @(
    "label_debug_render_target",
    "selector_debug_render_target",
    "label_active_camera",
    "selector_active_camera",
    "label_active_gauss_kernel",
    "selector_active_gauss_kernel",
    "label_gauss_kernel_sample_count",
    "selector_gauss_kernel_sample_count",
    "label_gauss_kernel_radius",
    "slider_gauss_kernel_radius",
    "label_gauss_kernel_deviation",
    "slider_gauss_kernel_deviation"
)

$json = Get-Content $layoutPath -Raw -Encoding UTF8 | ConvertFrom-Json

# Find the Shadows group
$shadowGroup = $json.groups | Where-Object { $_.name -eq "Shadows" }
if (-not $shadowGroup) {
    Write-Host "ERROR: 'Shadows' group not found in layout." -ForegroundColor Red
    exit 1
}

# Build reference lookup from Shadows
$refValues = @{}
foreach ($id in $elementIds) {
    $elem = $shadowGroup.elements | Where-Object { $_.id -eq $id }
    if ($elem) { $refValues[$id] = $elem }
}

Write-Host "Source: Shadows group ($($refValues.Count) reference elements)" -ForegroundColor Cyan
Write-Host ""

$updateCount = 0
foreach ($group in $json.groups) {
    if ($group.name -eq "Global" -or $group.name -eq "Shadows") { continue }

    foreach ($id in $elementIds) {
        $elem = $group.elements | Where-Object { $_.id -eq $id }
        $src  = $refValues[$id]
        if ($elem -and $src) {
            if ($DryRun) {
                Write-Host "  [DRY RUN] $($group.name)/$id  ($($elem.x),$($elem.y),$($elem.w),$($elem.h)) -> ($($src.x),$($src.y),$($src.w),$($src.h))"
            } else {
                $elem.x = $src.x
                $elem.y = $src.y
                $elem.w = $src.w
                $elem.h = $src.h
                Write-Host "  Updated $($group.name)/$id"
            }
            $updateCount++
        }
    }
}

if ($DryRun) {
    Write-Host "`n$updateCount elements would be updated (dry run, no file written)." -ForegroundColor Yellow
} else {
    $jsonOut = $json | ConvertTo-Json -Depth 10
    [System.IO.File]::WriteAllText($layoutPath, $jsonOut, (New-Object System.Text.UTF8Encoding $false))
    Write-Host "`n$updateCount elements updated. Saved to $layoutPath (UTF-8 no BOM)." -ForegroundColor Green
}
