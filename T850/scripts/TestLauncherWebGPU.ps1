[CmdletBinding()]
param(
    [switch]$Ui,
    [switch]$BuildWeb,
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [ValidateSet('Build', 'Rebuild')][string]$BuildTarget = 'Rebuild',
    [string]$BuildLog = (Join-Path $env:LOCALAPPDATA 'T850Profiles\launcher-web-build.log')
)

$ErrorActionPreference = 'Stop'
if ($BuildWeb -and -not $Ui) { throw '-BuildWeb requires -Ui to exercise the launcher build handler.' }

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
    Assert-True ($ast.Extent.Text.Contains('Tag="webgpu-browser"') -and $ast.Extent.Text.Contains('Tag="webgpu"')) "$Name must offer separate browser and native WebGPU options"
    Assert-True ($ast.Extent.Text -notmatch 'fixture') "$Name retains fixture-specific UI or dispatch"
    Assert-True ($ast.Extent.Text -match 'forward and deferred runtime scenes' -and $ast.Extent.Text -notmatch 'forward scene support only|Full deferred scenes remain unsupported') "$Name has stale WebGPU runtime capability text"
    foreach ($functionName in @('Test-WebGpuSelected', 'Test-BrowserSelected', 'Get-InstalledBrowsers', 'Get-BrowserLaunchCommand', 'Test-WebGpuSupported', 'Get-LaunchCommand', 'Get-EditorLaunchCommand', 'Get-ShaderCompileCommands')) {
        $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $functionName }, $false)
        Assert-True ($null -ne $definition) "Missing function $functionName in $Name"
        . ([scriptblock]::Create($definition.Extent.Text))
    }
    if ($Name -eq 'Launcher.ps1') {
        foreach ($functionName in @('Get-BrowserBuildCommand', 'Format-LauncherCommandLine')) {
            $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $functionName }, $false)
            . ([scriptblock]::Create($definition.Extent.Text))
        }
    }
    $rootDir = Join-Path ([IO.Path]::GetTempPath()) ("T850 launcher test " + [guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($rootDir)
    $cmbApi = [pscustomobject]@{ SelectedItem = [pscustomobject]@{ Tag = 'webgpu' } }
    $cmbPostProcessMode = [pscustomobject]@{ SelectedItem = [pscustomobject]@{ Tag = 'raster' } }
    $cmbShaderFlow = [pscustomobject]@{ SelectedItem = [pscustomobject]@{ Tag = 'auto' } }
    $cmbBrowser = [pscustomobject]@{ SelectedItem = [pscustomobject]@{ Tag = '' } }
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
        $compileJobs = @(Get-ShaderCompileCommands)
        Assert-True (($compileJobs.Name -join ',') -eq 'd3d11-native,d3d12-native,vulkan-native,gl-native,webgpu-auto,webgpu-spirv') 'Shader compilation must include all APIs and both working WebGPU flows'
        foreach ($job in $compileJobs) {
            Assert-True ($job.Args[0] -eq '--compileShaders' -and $job.Args -notcontains '--scene' -and $job.Args -notcontains '--graphics-fixture') 'Shader precompile must not load a scene or fixture'
            Assert-True ($job.WorkingDirectory -eq (Split-Path $job.ExePath)) 'Shader precompile working directory differs from normal runtime'
        }
        foreach ($api in @('webgpu', 'd3d12', 'd3d11', 'vulkan', 'gl')) {
            $cmbApi.SelectedItem.Tag = $api
            foreach ($flow in @('auto', 'spirv')) {
                $cmbShaderFlow.SelectedItem.Tag = $flow
                foreach ($postProcessMode in @('raster', 'compute')) {
                    $cmbPostProcessMode.SelectedItem.Tag = $postProcessMode
                    $command = Get-LaunchCommand
                    Assert-True ($command.Args -notcontains '--graphics-fixture' -and $command.Args -notcontains '--output') 'Launcher must not substitute a fixture for normal engine startup'
                    $flowArgs = if ($api -eq 'webgpu') { " --shaderFlow $flow" } else { '' }
                    Assert-True (($command.Args -join ' ') -eq "--api $api --postProcessMode $postProcessMode$flowArgs --scene 4 --culling frustum --sceneFile `"Scenes/Test.t8scene`" --width 960 --height 540 --logLevel info") "$Name did not preserve normal arguments, post-process mode, and WebGPU-only shader flow $flow for $api"
                    Assert-True ($command.Display.StartsWith('"' + $command.ExePath + '" ')) 'Executable path with spaces is not quoted'
                    Assert-True ($command.ExePath.StartsWith($rootDir)) 'Unexpected executable location'
                    Assert-True ((Get-EditorLaunchCommand).Args -notcontains '--shaderFlow') 'Runtime shader flow leaked into editor startup'
                }
            }
            if ($api -eq 'webgpu') {
                Assert-True ((Get-EditorLaunchCommand).Args[1] -eq 'webgpu') 'WebGPU editor silently remapped'
            }
        }
        $cmbApi.SelectedItem.Tag = 'webgpu-browser'
        Assert-Rejected { Get-LaunchCommand } 'Missing browser build accepted'
        foreach ($directory in @('web', 'build/web/site', 'build/web/WebShaders', 'Assets')) { [void][IO.Directory]::CreateDirectory((Join-Path $rootDir $directory)) }
        foreach ($file in @('web/server.mjs', 'build/web/site/DayScene.html', 'build/web/site/DayScene.js', 'build/web/site/DayScene.wasm', 'build/web/site/scenes.json')) { [IO.File]::WriteAllText((Join-Path $rootDir $file), '') }
        $browserCommand = Get-LaunchCommand
        Assert-True ($browserCommand.ExePath -match 'node(\.exe)?$' -and $browserCommand.Args -contains '--open') 'Browser mode did not launch the HTTP server with Node'
        Assert-True ($browserCommand.Display -match 'scene=4' -and $browserCommand.Display -match 'sceneFile=Scenes%2FTest.t8scene') 'Browser mode lost the selected scene/document'
        Assert-True ($browserCommand.Args -notcontains '--api' -and $browserCommand.Args -notcontains '--shaderFlow') 'Native API/shader flow leaked into browser startup'
        Assert-True ($browserCommand.Args -notcontains '--browser') 'Default browser unexpectedly specified an executable'
        foreach ($mode in @('raster', 'compute')) {
            $cmbPostProcessMode.SelectedItem.Tag = $mode
            Assert-True ((Get-LaunchCommand).Display.Contains("postProcessMode=$mode")) 'Browser launch lost the selected post-process mode'
        }
        $cmbPostProcessMode.SelectedItem.Tag = 'raster'
        foreach ($relativePath in @('Google/Chrome/Application/chrome.exe', 'Mozilla Firefox/firefox.exe')) {
            $executable = Join-Path $rootDir $relativePath
            [void][IO.Directory]::CreateDirectory((Split-Path $executable))
            [IO.File]::WriteAllText($executable, '')
        }
        $installed = @(Get-InstalledBrowsers -RegistryRoots @() -InstallRoots @($rootDir, $rootDir))
        Assert-True ($installed.Count -eq 2) 'Browser discovery included missing installs or duplicates'
        foreach ($browser in $installed) {
            $cmbBrowser.SelectedItem.Tag = $browser.Path
            $browserCommand = Get-LaunchCommand
            Assert-True ($browserCommand.Args -contains '--browser' -and $browserCommand.Args[-1] -eq ('"{0}"' -f $browser.Path)) 'Selected browser executable was lost or not quoted'
        }
        Remove-Item -LiteralPath $cmbBrowser.SelectedItem.Tag
        Assert-Rejected { Get-LaunchCommand } 'Uninstalled selected browser accepted'
        $cmbBrowser.SelectedItem.Tag = ''
        Assert-Rejected { Get-EditorLaunchCommand } 'Browser editor was accepted'
        $cmbArch.SelectedItem.Content = 'ARM64'
        Assert-True ((Get-LaunchCommand).Args -contains '--open') 'Browser inherited the native x64 architecture restriction'
        $cmbArch.SelectedItem.Content = 'x64'
        if ($Name -eq 'Launcher_Release.ps1') {
            Remove-Item (Join-Path $rootDir 'DayScene.exe')
            Assert-True ((Get-LaunchCommand).Args -contains '--open') 'Browser incorrectly required the native executable'
            Write-TestExecutable 0x8664
        } else {
            Assert-Rejected { Get-BrowserBuildCommand } 'Missing browser build script accepted'
            [void][IO.Directory]::CreateDirectory((Join-Path $rootDir 'scripts'))
            [IO.File]::WriteAllText((Join-Path $rootDir 'scripts/BuildWeb.ps1'), '')
            foreach ($configuration in @('Release', 'Debug')) {
                $cmbConfig.SelectedItem.Content = $configuration
                foreach ($target in @('Build', 'Rebuild')) {
                    $buildCommand = Get-BrowserBuildCommand -BuildTarget $target
                    Assert-True ($buildCommand.ExePath -match 'powershell\.exe$' -and $buildCommand.Args -contains '-NonInteractive') 'Browser build must use a noninteractive PowerShell child, not the compiled launcher'
                    $configIndex = [array]::IndexOf($buildCommand.Args, '-Configuration')
                    Assert-True ($configIndex -ge 0 -and $buildCommand.Args[$configIndex + 1] -eq $configuration) 'Browser build ignored the selected configuration'
                    Assert-True (($buildCommand.Args -contains '-Clean') -eq ($target -eq 'Rebuild')) 'Browser Build/Rebuild clean routing is incorrect'
                    Assert-True ($buildCommand.WorkingDirectory -eq $rootDir -and $buildCommand.Display.Contains('"' + (Join-Path $rootDir 'scripts/BuildWeb.ps1') + '"')) 'Browser build path with spaces is not preserved'
                }
            }
            $cmbConfig.SelectedItem.Content = 'Release'
            $android = $true
            Assert-Rejected { Get-LaunchCommand } 'Desktop browser mode accepted the Android install target'
            Assert-Rejected { Get-BrowserBuildCommand } 'Browser build accepted the Android target'
            $android = $false
        }
        $cmbApi.SelectedItem.Tag = 'webgpu'
        if ($Name -eq 'Launcher.ps1') {
            foreach ($architecture in @('x86', 'ARM64')) {
                $cmbArch.SelectedItem.Content = $architecture
                Assert-Rejected { Get-LaunchCommand } 'Unsupported Windows architecture accepted'
                Assert-Rejected { Get-EditorLaunchCommand } 'Unsupported editor architecture accepted'
                Assert-True (@(Get-ShaderCompileCommands).Count -eq 4) 'Non-x64 shader precompile must omit WebGPU only'
            }
            $cmbArch.SelectedItem.Content = 'x64'
            $android = $true
            Assert-Rejected { Get-LaunchCommand } 'Android WebGPU accepted'
            Assert-Rejected { Get-ShaderCompileCommands } 'Android target accepted desktop precompile jobs'
        } else {
            Write-TestExecutable 0xAA64
            Assert-True (@(Get-ShaderCompileCommands).Count -eq 4) 'Portable non-x64 shader precompile must omit WebGPU'
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

function Test-WebBuildLogging {
    $parseErrors = $null
    $tokens = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'BuildWeb.ps1'), [ref]$tokens, [ref]$parseErrors)
    Assert-True ($parseErrors.Count -eq 0) 'Browser build script has syntax errors'
    foreach ($functionName in @('Assert-CommandSucceeded', 'Invoke-LoggedNativeCommand')) {
        $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $functionName }, $false)
        . ([scriptblock]::Create($definition.Extent.Text))
    }
    $temporary = Join-Path ([IO.Path]::GetTempPath()) ('T850 build logging ' + [guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($temporary)
    try {
        $log = Join-Path $temporary 'native.log'
        $command = Join-Path $temporary 'native command.cmd'
        [IO.File]::WriteAllText($command, "@echo off`r`necho informational stderr 1>&2`r`necho %~1`r`nexit /b 0`r`n")
        Invoke-LoggedNativeCommand -FilePath $command -Arguments @('path with spaces') -LogPath $log -Operation 'Native stderr check'
        Assert-True ((Get-Content $log -Raw) -match 'informational stderr' -and (Get-Content $log -Raw) -match 'path with spaces') 'Native stderr or quoted arguments were lost'
        [IO.File]::WriteAllText($command, "@echo off`r`necho build failed 1>&2`r`nexit /b 7`r`n")
        Assert-Rejected { Invoke-LoggedNativeCommand -FilePath $command -Arguments @() -LogPath $log -Operation 'Expected failure' } 'Nonzero native command exit was ignored'
        Write-Output 'Browser native-command logging tests PASS'
    } finally { Remove-Item $temporary -Recurse -Force }
}

Test-WebBuildLogging

function Test-ShaderCompileQueue([string]$Name) {
    $tokens = $null
    $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $Name), [ref]$tokens, [ref]$parseErrors)
    $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Update-ShaderCompileQueue' }, $false)
    . ([scriptblock]::Create($definition.Extent.Text))
    $temporary = Join-Path ([IO.Path]::GetTempPath()) ('T850 shader queue ' + [guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($temporary)
    try {
        foreach ($scenario in @('success', 'compile-failure', 'old-executable', 'cancelled', 'late-output')) {
            $process = [pscustomobject]@{ HasExited = $true; ExitCode = $(if ($scenario -eq 'compile-failure') { 1 } else { 0 }); Disposed = $false; FinalLog = $(if ($scenario -eq 'late-output') { Join-Path $temporary 'stdout.log' } else { '' }) }
            $process | Add-Member ScriptMethod WaitForExit {
                if ($this.FinalLog) { '[ShaderPrecompile] complete: 281 succeeded, 0 failed' | Set-Content $this.FinalLog }
            }
            $process | Add-Member ScriptMethod Dispose { $this.Disposed = $true }
            $timer = [pscustomobject]@{ Stopped = $false }
            $timer | Add-Member ScriptMethod Stop { $this.Stopped = $true }
            $output = [pscustomobject]@{ Text = '' }
            $output | Add-Member ScriptMethod ScrollToEnd {}
            $stdout = Join-Path $temporary 'stdout.log'
            $text = if ($scenario -eq 'success') { '[ShaderPrecompile] complete: 281 succeeded, 0 failed' } elseif ($scenario -eq 'compile-failure') { '[ShaderPrecompile] complete: 280 succeeded, 1 failed' } else { 'Normal engine startup' }
            $text | Set-Content $stdout
            $state = [pscustomobject]@{ Process = $process; Stdout = $stdout; Output = $output; Jobs = @([pscustomobject]@{ Name = 'd3d12-native' }); Index = 1; CancelRequested = ($scenario -eq 'cancelled'); Failures = 0; Results = [Collections.ArrayList]::new(); Progress = [pscustomobject]@{ Value = 0 }; Timer = $timer; Status = [pscustomobject]@{ Text = '' }; Cancel = [pscustomobject]@{ Content = 'Cancel'; IsEnabled = $true }; Directory = $temporary }
            Update-ShaderCompileQueue $state
            Assert-True ($state.Results.Count -eq 1 -and $state.Results[0].Passed -eq ($scenario -in @('success', 'late-output'))) "Shader queue misreported $scenario"
            Assert-True ($timer.Stopped -and $process.Disposed -and $state.Cancel.Content -eq 'Close') 'Shader queue did not clean up on completion'
            Assert-True (Test-Path (Join-Path $temporary 'summary.json')) 'Shader queue omitted the durable summary'
            if ($scenario -eq 'cancelled') { Assert-True ($state.Status.Text -like 'Cancelled.*' -and $state.Cancel.IsEnabled) 'Cancelled shader queue did not return control to the user' }
        }
        Write-Output "$Name shader compile queue tests PASS"
    } finally { Remove-Item $temporary -Recurse -Force }
}

Test-ShaderCompileQueue 'Launcher.ps1'
Test-ShaderCompileQueue 'Launcher_Release.ps1'

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
    function Get-InstalledBrowsers { return [pscustomobject]@{ Name = 'Test Chrome'; Path = Join-Path $temporary 'Chrome & Test.exe' } }
    function Set-Api([string]$Tag) { $cmbApi.SelectedItem = @($cmbApi.Items | Where-Object Tag -eq $Tag)[0] }
    try {
        [void][IO.Directory]::CreateDirectory($temporary)
        [IO.File]::WriteAllText((Join-Path $temporary 'Chrome & Test.exe'), '')
        Update-InstalledBrowsers
        Assert-True ($cmbBrowser.Items.Count -eq 2 -and $cmbBrowser.SelectedIndex -eq 0 -and $pnlBrowser.Visibility -eq 'Collapsed') 'Browser selector did not initialize hidden with default and installed entries'
        $bytes = New-Object byte[] 128
        $bytes[0] = 0x4D; $bytes[1] = 0x5A; $bytes[0x3C] = 64; $bytes[64] = 0x50; $bytes[65] = 0x45; $bytes[68] = 0x64; $bytes[69] = 0x86
        $runtime = if ($Name -eq 'Launcher.ps1') { Join-Path $rootDir 'bin/x64/Release' } else { $rootDir }
        [void][IO.Directory]::CreateDirectory($runtime)
        [IO.File]::WriteAllBytes((Join-Path $runtime 'DayScene.exe'), $bytes)
        [IO.File]::WriteAllBytes((Join-Path $runtime 'T8ditor.exe'), $bytes)
        Assert-True ($cmbPostProcessMode.Items.Count -eq 2 -and $cmbPostProcessMode.SelectedItem.Tag -eq 'raster') 'Post-process selector must default to raster and offer both execution modes'
        Assert-True ($cmbShaderFlow.Items.Count -eq 2 -and $cmbShaderFlow.SelectedItem.Tag -eq 'auto') 'Shader selector must default to WGSL-first and offer the two working runtime flows'
        Assert-True ($pnlShaderFlow.Visibility -eq 'Collapsed') 'Shader selector must initially be hidden for native APIs'
        $flowEvent = $ast.Find({ param($node) $node -is [Management.Automation.Language.InvokeMemberExpressionAst] -and $node.Extent.Text -eq '$cmbShaderFlow.Add_SelectionChanged({ Update-Preview })' }, $true)
        Assert-True ($null -ne $flowEvent) 'Shader selector is not wired to refresh the command preview'
        . ([scriptblock]::Create($flowEvent.Extent.Text))
        $browserEvent = $ast.Find({ param($node) $node -is [Management.Automation.Language.InvokeMemberExpressionAst] -and $node.Extent.Text -eq '$cmbBrowser.Add_SelectionChanged({ Update-Preview })' }, $true)
        Assert-True ($null -ne $browserEvent) 'Browser selector is not wired to refresh the preview'
        . ([scriptblock]::Create($browserEvent.Extent.Text))
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
        Assert-True ($pnlShaderFlow.Visibility -eq 'Visible' -and $cmbShaderFlow.IsEnabled) 'WebGPU runtime shader selector is unavailable'
        Assert-True ($btnRun.Content -match 'RUN' -and $btnRun.Content -notmatch 'FIXTURE') 'Normal RUN caption was replaced'
        foreach ($flag in @('--api webgpu', '--postProcessMode raster', '--shaderFlow auto', '--dumpSnapshot-', '--replaySnapshot', '--telemetry')) {
            Assert-True ($txtCmdPreview.Text.Contains($flag)) "Normal preview dropped $flag"
        }
        $sceneDependencies.Ok = $true
        Update-Preview
        Assert-True (-not $btnRun.IsEnabled -and $txtStatus.Text -match 'Cloud assets missing') 'Normal cloud dependency check was bypassed'
        $script:CloudAssetStatus.Missing = 0
        Update-Preview
        Assert-True $btnRun.IsEnabled 'Normal RUN was disabled despite available executable and assets'
        Assert-True $btnCompileShaders.IsEnabled 'Shader compile button unavailable with an executable'
        Assert-True (-not $btnEditor.IsEnabled) 'Unimplemented editor API could silently fall through to native D3D12'
        Assert-True ($txtStatus.Text -eq 'WebGPU: runtime forward/deferred rendering; editor unavailable.') 'WebGPU runtime/editor capabilities were reported incorrectly'
        Assert-True ((Get-EditorLaunchCommand).Args[1] -eq 'webgpu') 'WebGPU editor selection was remapped'
        foreach ($flow in @('spirv', 'auto')) {
            $cmbShaderFlow.SelectedItem = @($cmbShaderFlow.Items | Where-Object Tag -eq $flow)[0]
            Assert-True ($txtCmdPreview.Text.Contains("--shaderFlow $flow")) 'Changing shader flow did not immediately refresh the preview'
            Save-Config
            $savedConfig = Get-Content $configPath -Raw | ConvertFrom-Json
            Assert-True ($savedConfig.webgpuShaderFlow -eq $flow) 'Shader flow was not persisted'
            $cmbShaderFlow.SelectedIndex = 1 - $cmbShaderFlow.SelectedIndex
            Set-Api 'd3d12'
            Load-Config
            Assert-True ((Test-WebGpuSelected) -and $cmbShaderFlow.SelectedItem.Tag -eq $flow) 'WebGPU API/shader flow config round trip failed'
        }
        foreach ($postProcessMode in @('compute', 'raster')) {
            $cmbPostProcessMode.SelectedItem = @($cmbPostProcessMode.Items | Where-Object Tag -eq $postProcessMode)[0]
            Update-Preview
            Assert-True ($txtCmdPreview.Text.Contains("--postProcessMode $postProcessMode")) 'Changing post-process mode did not refresh the preview'
            Save-Config
            $savedConfig = Get-Content $configPath -Raw | ConvertFrom-Json
            Assert-True ($savedConfig.postProcessMode -eq $postProcessMode) 'Post-process mode was not persisted'
            $cmbPostProcessMode.SelectedIndex = 1 - $cmbPostProcessMode.SelectedIndex
            Load-Config
            Assert-True ($cmbPostProcessMode.SelectedItem.Tag -eq $postProcessMode) 'Post-process mode config round trip failed'
        }
        foreach ($configuredFlow in @($null, 'unknown', 'SPIRV')) {
            $savedConfig = @{ api = 'webgpu' }
            if ($null -ne $configuredFlow) { $savedConfig.webgpuShaderFlow = $configuredFlow }
            $savedConfig | ConvertTo-Json | Set-Content $configPath -Encoding UTF8
            $cmbShaderFlow.SelectedIndex = 1
            Load-Config
            $expectedFlow = if ($configuredFlow -eq 'SPIRV') { 'spirv' } else { 'auto' }
            Assert-True ($cmbShaderFlow.SelectedItem.Tag -eq $expectedFlow) 'Legacy/default or case-insensitive shader-flow loading failed'
        }
        Set-Api 'd3d12'
        Update-Preview
        Assert-True ($pnlShaderFlow.Visibility -eq 'Collapsed' -and -not $cmbShaderFlow.IsEnabled) 'Shader selector remained active for a native API'
        Assert-True ((Get-LaunchCommand).Args -notcontains '--shaderFlow') 'Native API received WebGPU shader-flow arguments'
        Assert-True ($btnRun.IsEnabled -and $btnEditor.IsEnabled -and $txtStatus.Text -notmatch 'WebGPU') 'Native readiness behavior changed'
        Assert-True ((Get-LaunchCommand).Args -notcontains '--graphics-fixture') 'Native API became a fixture'
        Assert-True ((Get-EditorLaunchCommand).Args -contains 'd3d12') 'Native editor mapping changed'
        Set-Api 'webgpu-browser'
        Update-Preview
        Assert-True (-not $btnRun.IsEnabled -and $txtStatus.Text -match 'Browser (build|bundle) missing') 'Missing browser runtime did not disable RUN'
        foreach ($directory in @('web', 'web/site', 'web/WebShaders', 'web/assets', 'build/web/site', 'build/web/WebShaders', 'Assets')) { [void][IO.Directory]::CreateDirectory((Join-Path $rootDir $directory)) }
        foreach ($file in @('web/server.mjs', 'web/site/DayScene.html', 'web/site/DayScene.js', 'web/site/DayScene.wasm', 'web/site/scenes.json', 'build/web/site/DayScene.html', 'build/web/site/DayScene.js', 'build/web/site/DayScene.wasm', 'build/web/site/scenes.json')) { [IO.File]::WriteAllText((Join-Path $rootDir $file), '') }
        Update-Preview
        Assert-True ($btnRun.IsEnabled -and $btnRun.Content -eq 'OPEN BROWSER') 'Browser RUN is unavailable with a prepared build'
        Assert-True ($pnlBrowser.Visibility -eq 'Visible' -and $cmbBrowser.IsEnabled) 'Browser selector is unavailable in Emscripten mode'
        if ($Name -eq 'Launcher.ps1') {
            Assert-True ($cmbConfig.IsEnabled -and -not $cmbArch.IsEnabled) 'Browser configuration should be selectable independently of native architecture'
            Assert-True ($btnBuild.Content -eq 'BUILD WEB' -and $btnRebuild.IsEnabled -and $btnRebuild.Content -eq 'REBUILD WEB') 'Browser Build/Rebuild controls are unavailable'
        }
        $cmbBrowser.SelectedIndex = 1
        Assert-True ($txtCmdPreview.Text -match '--browser "' -and $txtCmdPreview.Text.Contains('Chrome & Test.exe')) 'Browser selection did not update the preview'
        Assert-True (-not $btnEditor.IsEnabled -and -not $btnCompileShaders.IsEnabled) 'Browser mode enabled native editor or shader compilation'
        Assert-True ($pnlShaderFlow.Visibility -eq 'Collapsed' -and -not $cmbShaderFlow.IsEnabled) 'Native shader flow is exposed for the browser'
        foreach ($controlName in @('chkDump', 'chkTelemetry', 'chkReplaySnapshot', 'chkFullscreen', 'chkBenchmark')) { Assert-True (-not $window.FindName($controlName).IsEnabled) "Browser mode left $controlName enabled" }
        Assert-True ($txtCmdPreview.Text -match '--open' -and $txtCmdPreview.Text -notmatch 'DayScene.exe|--telemetry|--profile') 'Browser command invoked the native runtime or profiling'
        Save-Config
        Assert-True ((Get-Content $configPath -Raw | ConvertFrom-Json).webBrowser -eq (Join-Path $temporary 'Chrome & Test.exe')) 'Selected browser was not persisted'
        $cmbBrowser.SelectedIndex = 0
        Set-Api 'd3d12'
        Load-Config
        Assert-True (Test-BrowserSelected) 'Browser API config round trip failed'
        Assert-True ($cmbBrowser.SelectedIndex -eq 1) 'Browser selection config round trip failed'
        Set-Api 'd3d12'
        Update-Preview
        Assert-True ($pnlBrowser.Visibility -eq 'Collapsed' -and -not $cmbBrowser.IsEnabled -and (Get-LaunchCommand).Args -notcontains '--browser') 'Browser selector or executable leaked into native mode'
        Assert-True ($btnRun.IsEnabled -and $btnEditor.IsEnabled -and $chkTelemetry.IsEnabled -and $btnRun.Content -match 'RUN') 'Returning from browser changed native controls'
        Set-Api 'webgpu'
        Update-Preview
        Assert-True ($cmbShaderFlow.SelectedItem.Tag -eq 'spirv' -and $txtCmdPreview.Text.Contains('--shaderFlow spirv')) 'Switching APIs lost the selected shader flow'
        if ($Name -eq 'Launcher.ps1') {
            foreach ($architecture in @('ARM64', 'x86')) {
                $cmbArch.SelectedItem = @($cmbArch.Items | Where-Object Content -eq $architecture)[0]
                Update-Preview
                Assert-True (-not $btnRun.IsEnabled -and -not $btnEditor.IsEnabled -and $txtStatus.Text -match 'requires Windows x64') 'Unsupported architecture was not blocked'
                Assert-True (-not $cmbShaderFlow.IsEnabled) 'Unsupported architecture enabled the shader selector'
            }
            $cmbArch.SelectedItem = @($cmbArch.Items | Where-Object Content -eq 'x64')[0]
            Update-Preview
            Assert-True $btnRun.IsEnabled 'Returning to x64 did not restore RUN'
        }
        Remove-Item (Join-Path $runtime 'DayScene.exe')
        Update-Preview
        Assert-True (-not $btnRun.IsEnabled) 'Missing executable did not disable RUN'
        Assert-True (-not $btnCompileShaders.IsEnabled) 'Missing executable did not disable shader compilation'
        if ($Name -eq 'Launcher_Release.ps1') {
            Assert-True (-not $cmbShaderFlow.IsEnabled) 'Missing portable executable enabled the shader selector'
        }
        if ($BuildWeb -and $Name -eq 'Launcher.ps1') {
            $rootDir = Split-Path -Parent $PSScriptRoot
            $cmbConfig.SelectedItem = @($cmbConfig.Items | Where-Object Content -eq $Configuration)[0]
            $cmbBrowser.SelectedIndex = 0
            Set-Api 'webgpu-browser'
            Update-Preview
            [void][IO.Directory]::CreateDirectory((Split-Path -Parent ([IO.Path]::GetFullPath($BuildLog))))
            [IO.File]::WriteAllText($BuildLog, '')
            function Append-BuildOutput([string]$Line) {
                $txtBuildOutput.Text += $Line + [Environment]::NewLine
                [IO.File]::AppendAllText($BuildLog, $Line + [Environment]::NewLine)
                Write-Host $Line
                $window.Dispatcher.Invoke([Action]{}, [System.Windows.Threading.DispatcherPriority]::Background)
            }
            Invoke-Build -buildTarget $BuildTarget
            Assert-True ($txtBuildOutput.Text.Contains("Browser $Configuration $BuildTarget succeeded.")) "Launcher browser build failed. See $BuildLog"
            Assert-True (-not $script:LauncherBusy -and $btnBuild.IsEnabled -and $btnRebuild.IsEnabled -and $cmbConfig.IsEnabled) 'Launcher did not recover its controls after building'
            Assert-True $btnRun.IsEnabled 'Successful browser build did not enable OPEN BROWSER'
            Write-Output "Launcher $Configuration $BuildTarget integration PASS"
        }
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