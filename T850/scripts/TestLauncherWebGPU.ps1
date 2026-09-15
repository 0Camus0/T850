[CmdletBinding()]
param([switch]$Ui)

$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Rejected([scriptblock]$Action, [string]$Message) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Assert-True $rejected $Message
}

function Test-Launcher([string]$Name) {
    $parseErrors = $null
    $tokens = $null
    $path = Join-Path $PSScriptRoot $Name
    $ast = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$parseErrors)
    Assert-True ($parseErrors.Count -eq 0) "$Name has syntax errors: $parseErrors"
    Assert-True ($ast.Extent.Text -notmatch 'fixture|cmbShaderFlow|webgpuShaderFlow') "$Name retains fixture-specific UI or dispatch"
    Assert-True ($ast.Extent.Text -match 'forward and deferred runtime scenes' -and $ast.Extent.Text -notmatch 'forward scene support only|Full deferred scenes remain unsupported') "$Name has stale WebGPU runtime capability text"
    foreach ($functionName in @('Test-WebGpuSelected', 'Test-WebGpuSupported', 'Get-LaunchCommand', 'Get-EditorLaunchCommand')) {
        $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $functionName }, $false)
        Assert-True ($null -ne $definition) "Missing function $functionName in $Name"
        . ([scriptblock]::Create($definition.Extent.Text))
    }
    $rootDir = Join-Path ([IO.Path]::GetTempPath()) ("T850 launcher test " + [guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($rootDir)
    $cmbApi = [pscustomobject]@{ SelectedItem = [pscustomobject]@{ Tag = 'webgpu' } }
    $cmbArch = [pscustomobject]@{ SelectedItem = [pscustomobject]@{ Content = 'x64' } }
    $cmbConfig = [pscustomobject]@{ SelectedItem = [pscustomobject]@{ Content = 'Release' } }
    $cmbScene = [pscustomobject]@{ SelectedItem = [pscustomobject]@{ Tag = '4' } }
    $cmbLogLevel = [pscustomobject]@{ SelectedItem = [pscustomobject]@{ Tag = 'info' } }
    $txtWidth = [pscustomobject]@{ Text = '960' }
    $txtHeight = [pscustomobject]@{ Text = '540' }
    $android = $false
    function Test-AndroidTarget { return $android }
    function Get-ArchFolder { return $cmbArch.SelectedItem.Content }
    function Get-CullingMode { return 'frustum' }
    function Get-SelectedSceneFilePath { return 'Scenes/Test.t8scene' }
    function Get-SceneFileResourcePath([string]$Path) { return $Path }
    function Write-TestExecutable([uint16]$Machine) {
        $bytes = New-Object byte[] 128
        $bytes[0] = 0x4D; $bytes[1] = 0x5A; $bytes[0x3C] = 64
        $bytes[64] = 0x50; $bytes[65] = 0x45
        [BitConverter]::GetBytes($Machine).CopyTo($bytes, 68)
        [IO.File]::WriteAllBytes((Join-Path $rootDir 'DayScene.exe'), $bytes)
    }
    try {
        if ($Name -eq 'Launcher_Release.ps1') {
            Assert-True (-not (Test-WebGpuSupported)) 'Missing release executable accepted'
            foreach ($machine in @(0xAA64, 0x014C)) {
                Write-TestExecutable $machine
                Assert-True (-not (Test-WebGpuSupported)) 'Non-x64 release binary accepted'
            }
            Write-TestExecutable 0x8664
        }
        Assert-True (Test-WebGpuSupported) "$Name rejected x64"
        foreach ($api in @('webgpu', 'd3d12', 'd3d11', 'vulkan', 'gl')) {
            $cmbApi.SelectedItem.Tag = $api
            $command = Get-LaunchCommand
            Assert-True ($command.Args -notcontains '--graphics-fixture') 'Launcher must not substitute a fixture for normal engine startup'
            Assert-True ($command.Args -notcontains '--shaderFlow' -and $command.Args -notcontains '--output') 'Fixture-only flags leaked into normal startup'
            Assert-True (($command.Args -join ' ') -eq "--api $api --scene 4 --culling frustum --sceneFile `"Scenes/Test.t8scene`" --width 960 --height 540 --logLevel info") 'Normal scene arguments were not preserved'
            Assert-True ($command.Display.StartsWith('"' + $command.ExePath + '" ')) 'Executable path with spaces is not quoted'
            Assert-True ($command.ExePath.StartsWith($rootDir)) 'Unexpected executable location'
            if ($api -eq 'webgpu') {
                Assert-True ((Get-EditorLaunchCommand).Args[1] -eq 'webgpu') 'WebGPU editor silently remapped'
            }
        }
        $cmbApi.SelectedItem.Tag = 'webgpu'
        if ($Name -eq 'Launcher.ps1') {
            foreach ($architecture in @('x86', 'ARM64')) {
                $cmbArch.SelectedItem.Content = $architecture
                Assert-Rejected { Get-LaunchCommand } 'Unsupported Windows architecture accepted'
                Assert-Rejected { Get-EditorLaunchCommand } 'Unsupported editor architecture accepted'
            }
            $cmbArch.SelectedItem.Content = 'x64'
            $android = $true
            Assert-Rejected { Get-LaunchCommand } 'Android WebGPU accepted'
        }
        Write-Output "$Name WebGPU command tests PASS"
    } finally {
        Remove-Item $rootDir -Recurse -Force
    }
}

Test-Launcher 'Launcher.ps1'
Test-Launcher 'Launcher_Release.ps1'

function Test-DawnPreflight {
    $parseErrors = $null
    $tokens = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'Launcher.ps1'), [ref]$tokens, [ref]$parseErrors)
    $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-DawnSetupStatus' }, $false)
    . ([scriptblock]::Create($definition.Extent.Text))
    $rootDir = Join-Path ([IO.Path]::GetTempPath()) ('T850 dawn preflight ' + [guid]::NewGuid().ToString('N'))
    $cmakeAvailable = $false
    $checkExitCode = 1
    $script:TestDawnCheckCalls = 0
    function Test-CommandExists { param([string]$Command) return $cmakeAvailable }
    function Invoke-DawnPackageCheck {
        $script:TestDawnCheckCalls++
        return [pscustomobject]@{ ExitCode = $checkExitCode; Output = 'stale ABI fixture' }
    }
    try {
        foreach ($platform in @('x86', 'ARM64')) { Assert-True ((Get-DawnSetupStatus $platform).Missing.Count -eq 0) 'Non-x64 requires Dawn' }
        Assert-True ((Get-DawnSetupStatus 'x64').Missing[0] -like 'CMake*') 'Missing CMake not detected'
        Assert-True ($script:TestDawnCheckCalls -eq 0) 'Dawn audit ran without prerequisites'
        $cmakeAvailable = $true
        Assert-True ((Get-DawnSetupStatus 'x64').Missing[0] -eq 'Dawn setup script') 'Missing setup script not detected'
        [void][IO.Directory]::CreateDirectory((Join-Path $rootDir 'scripts'))
        [IO.File]::WriteAllText((Join-Path $rootDir 'scripts/SetupDawn.ps1'), '')
        $status = Get-DawnSetupStatus 'x64'
        Assert-True ($status.Missing.Count -eq 1 -and $status.Diagnostic -eq 'stale ABI fixture') 'Dawn audit failure was discarded'
        $checkExitCode = 0
        Assert-True ((Get-DawnSetupStatus 'x64').Missing.Count -eq 0) 'Valid Dawn audit rejected'
        Write-Output 'Launcher Dawn preflight tests PASS'
    } finally {
        if (Test-Path $rootDir) { Remove-Item $rootDir -Recurse -Force }
    }
}

Test-DawnPreflight

function Test-LauncherUi([string]$Name) {
    $parseErrors = $null
    $tokens = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $Name), [ref]$tokens, [ref]$parseErrors)
    foreach ($definition in $ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] }, $false)) {
        . ([scriptblock]::Create($definition.Extent.Text))
    }
    $assignment = $ast.Find({ param($node) $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -eq '$xaml' }, $false)
    $literal = $assignment.Right.Find({ param($node) $node -is [Management.Automation.Language.StringConstantExpressionAst] -or $node -is [Management.Automation.Language.ExpandableStringExpressionAst] }, $true)
    Assert-True ($null -ne $literal -and $literal.NestedExpressions.Count -eq 0) 'Launcher XAML must be literal for UI testing'
    $xaml = $literal.Value
    [xml]$xml = $xaml
    $window = [Windows.Markup.XamlReader]::Parse($xaml)
    foreach ($node in $xml.SelectNodes('//*[@Name]')) {
        $control = $window.FindName($node.GetAttribute('Name'))
        if ($control) { Set-Variable -Name $node.GetAttribute('Name') -Value $control }
    }
    $temporary = Join-Path ([IO.Path]::GetTempPath()) ('T850 launcher UI ' + [guid]::NewGuid().ToString('N'))
    $rootDir = $temporary
    $configPath = Join-Path $temporary 'config.json'
    $script:LauncherBusy = $false
    $script:CloudAssetStatus = [pscustomobject]@{ Configured = $true; Ok = $true; Missing = 99; Total = 99; Message = 'Missing cloud assets' }
    $sceneDependencies = [pscustomobject]@{ Ok = $false; Missing = @('missing scene') }
    function Update-DownloadAssetsButton {}
    function Get-SelectedAndroidDeviceSerial { return '' }
    function Get-CachedSceneDependencyResult { return $sceneDependencies }
    function Set-Api([string]$Tag) { $cmbApi.SelectedItem = @($cmbApi.Items | Where-Object Tag -eq $Tag)[0] }
    try {
        [void][IO.Directory]::CreateDirectory($temporary)
        $bytes = New-Object byte[] 128
        $bytes[0] = 0x4D; $bytes[1] = 0x5A; $bytes[0x3C] = 64; $bytes[64] = 0x50; $bytes[65] = 0x45; $bytes[68] = 0x64; $bytes[69] = 0x86
        $runtime = if ($Name -eq 'Launcher.ps1') { Join-Path $rootDir 'bin/x64/Release' } else { $rootDir }
        [void][IO.Directory]::CreateDirectory($runtime)
        [IO.File]::WriteAllBytes((Join-Path $runtime 'DayScene.exe'), $bytes)
        [IO.File]::WriteAllBytes((Join-Path $runtime 'T8ditor.exe'), $bytes)
        Set-Api 'webgpu'
        $chkDump.IsChecked = $true
        $chkReplaySnapshot.IsChecked = $true
        $txtReplaySnapshotPath.Text = 'dumps/test.snapshot'
        $chkTelemetry.IsChecked = $true
        Update-Preview
        Assert-True (-not $btnRun.IsEnabled -and $txtStatus.Text -match 'Scene missing') 'Normal scene dependency check was bypassed'
        foreach ($controlName in @('cmbScene', 'txtWidth', 'chkDump', 'chkTelemetry')) {
            Assert-True $window.FindName($controlName).IsEnabled "Normal control $controlName was disabled"
        }
        Assert-True ($null -eq $window.FindName('cmbShaderFlow')) 'Fixture-only shader selector remains visible'
        Assert-True ($btnRun.Content -match 'RUN' -and $btnRun.Content -notmatch 'FIXTURE') 'Normal RUN caption was replaced'
        foreach ($flag in @('--api webgpu', '--dumpSnapshot-', '--replaySnapshot', '--telemetry')) {
            Assert-True ($txtCmdPreview.Text.Contains($flag)) "Normal preview dropped $flag"
        }
        $sceneDependencies.Ok = $true
        Update-Preview
        Assert-True (-not $btnRun.IsEnabled -and $txtStatus.Text -match 'Cloud assets missing') 'Normal cloud dependency check was bypassed'
        $script:CloudAssetStatus.Missing = 0
        Update-Preview
        Assert-True $btnRun.IsEnabled 'Normal RUN was disabled despite available executable and assets'
        Assert-True (-not $btnEditor.IsEnabled) 'Unimplemented editor API could silently fall through to native D3D12'
        Assert-True ($txtStatus.Text -eq 'WebGPU: runtime forward/deferred rendering; editor unavailable.') 'WebGPU runtime/editor capabilities were reported incorrectly'
        Assert-True ((Get-EditorLaunchCommand).Args[1] -eq 'webgpu') 'WebGPU editor selection was remapped'
        Save-Config
        Set-Api 'd3d12'
        Load-Config
        Assert-True (Test-WebGpuSelected) 'WebGPU API config round trip failed'
        Set-Api 'd3d12'
        Update-Preview
        Assert-True ($btnRun.IsEnabled -and $btnEditor.IsEnabled -and $txtStatus.Text -notmatch 'WebGPU') 'Native readiness behavior changed'
        Assert-True ((Get-LaunchCommand).Args -notcontains '--graphics-fixture') 'Native API became a fixture'
        Assert-True ((Get-EditorLaunchCommand).Args -contains 'd3d12') 'Native editor mapping changed'
        Set-Api 'webgpu'
        if ($Name -eq 'Launcher.ps1') {
            foreach ($architecture in @('ARM64', 'x86')) {
                $cmbArch.SelectedItem = @($cmbArch.Items | Where-Object Content -eq $architecture)[0]
                Update-Preview
                Assert-True (-not $btnRun.IsEnabled -and -not $btnEditor.IsEnabled -and $txtStatus.Text -match 'requires Windows x64') 'Unsupported architecture was not blocked'
            }
            $cmbArch.SelectedItem = @($cmbArch.Items | Where-Object Content -eq 'x64')[0]
            Update-Preview
            Assert-True $btnRun.IsEnabled 'Returning to x64 did not restore RUN'
        }
        Remove-Item (Join-Path $runtime 'DayScene.exe')
        Update-Preview
        Assert-True (-not $btnRun.IsEnabled) 'Missing executable did not disable RUN'
        Write-Output "$Name WPF/config/normal-routing tests PASS"
    } finally {
        $window.Close()
        Remove-Item $temporary -Recurse -Force
        $script:LauncherBusy = $false
    }
}

if ($Ui) {
    Assert-True ([Threading.Thread]::CurrentThread.GetApartmentState() -eq 'STA') 'WPF tests require an STA PowerShell host'
    Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase
    . Test-LauncherUi 'Launcher.ps1'
    . Test-LauncherUi 'Launcher_Release.ps1'
}