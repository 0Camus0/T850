# T850 Engine Launcher (Release)
# WPF GUI for launching DayScene — reads/writes config.json

Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase, System.Windows.Forms

$xaml = @"
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="T850 Engine Launcher" SizeToContent="Height" Width="920" MinWidth="760"
        WindowStartupLocation="CenterScreen" ResizeMode="CanResize"
        Background="#1B1B2F" Foreground="#E0E0E0">
    <Window.Resources>
        <SolidColorBrush x:Key="AccentBrush" Color="#6C63FF"/>
        <SolidColorBrush x:Key="AccentHoverBrush" Color="#8B85FF"/>
        <SolidColorBrush x:Key="SurfaceBrush" Color="#162447"/>
        <SolidColorBrush x:Key="Surface2Brush" Color="#1F4068"/>
        <SolidColorBrush x:Key="TextBrush" Color="#E0E0E0"/>
        <SolidColorBrush x:Key="SubtextBrush" Color="#A0A0B8"/>
        <SolidColorBrush x:Key="GreenBrush" Color="#A6E3A1"/>
        <SolidColorBrush x:Key="RedBrush" Color="#F38BA8"/>

        <!-- ComboBox: force light text via a custom ControlTemplate -->
        <ControlTemplate x:Key="ComboBoxToggleButton" TargetType="ToggleButton">
            <Grid>
                <Grid.ColumnDefinitions>
                    <ColumnDefinition/>
                    <ColumnDefinition Width="28"/>
                </Grid.ColumnDefinitions>
                <Border x:Name="Border" Grid.ColumnSpan="2" CornerRadius="4"
                        Background="{StaticResource Surface2Brush}"
                        BorderBrush="{StaticResource Surface2Brush}" BorderThickness="1"/>
                <Path x:Name="Arrow" Grid.Column="1" HorizontalAlignment="Center" VerticalAlignment="Center"
                      Data="M 0 0 L 4 4 L 8 0 Z" Fill="{StaticResource TextBrush}"/>
            </Grid>
        </ControlTemplate>
        <Style TargetType="ComboBox">
            <Setter Property="Foreground" Value="{StaticResource TextBrush}"/>
            <Setter Property="FontSize" Value="14"/>
            <Setter Property="Height" Value="36"/>
            <Setter Property="SnapsToDevicePixels" Value="True"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="ComboBox">
                        <Grid>
                            <ToggleButton Name="ToggleButton" Template="{StaticResource ComboBoxToggleButton}"
                                          Focusable="False" IsChecked="{Binding Path=IsDropDownOpen, Mode=TwoWay, RelativeSource={RelativeSource TemplatedParent}}"
                                          ClickMode="Press"/>
                            <ContentPresenter Name="ContentSite" IsHitTestVisible="False"
                                              Content="{TemplateBinding SelectionBoxItem}"
                                              ContentTemplate="{TemplateBinding SelectionBoxItemTemplate}"
                                              ContentTemplateSelector="{TemplateBinding ItemTemplateSelector}"
                                              Margin="10,3,28,3" VerticalAlignment="Center" HorizontalAlignment="Left">
                                <ContentPresenter.Resources>
                                    <Style TargetType="TextBlock">
                                        <Setter Property="Foreground" Value="{StaticResource TextBrush}"/>
                                    </Style>
                                </ContentPresenter.Resources>
                            </ContentPresenter>
                            <Popup Name="Popup" Placement="Bottom" IsOpen="{TemplateBinding IsDropDownOpen}"
                                   AllowsTransparency="True" Focusable="False" PopupAnimation="Slide">
                                <Grid Name="DropDown" SnapsToDevicePixels="True"
                                      MinWidth="{TemplateBinding ActualWidth}" MaxHeight="{TemplateBinding MaxDropDownHeight}">
                                    <Border x:Name="DropDownBorder" Background="{StaticResource SurfaceBrush}"
                                            BorderBrush="{StaticResource Surface2Brush}" BorderThickness="1" CornerRadius="4"/>
                                    <ScrollViewer Margin="2" SnapsToDevicePixels="True">
                                        <StackPanel IsItemsHost="True" KeyboardNavigation.DirectionalNavigation="Contained"/>
                                    </ScrollViewer>
                                </Grid>
                            </Popup>
                        </Grid>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
        <Style TargetType="ComboBoxItem">
            <Setter Property="Foreground" Value="{StaticResource TextBrush}"/>
            <Setter Property="Background" Value="{StaticResource SurfaceBrush}"/>
            <Setter Property="Padding" Value="8,5"/>
            <Setter Property="FontSize" Value="14"/>
            <Style.Triggers>
                <Trigger Property="IsHighlighted" Value="True">
                    <Setter Property="Background" Value="{StaticResource Surface2Brush}"/>
                </Trigger>
            </Style.Triggers>
        </Style>

        <Style TargetType="TextBox">
            <Setter Property="Background" Value="{StaticResource SurfaceBrush}"/>
            <Setter Property="Foreground" Value="{StaticResource TextBrush}"/>
            <Setter Property="BorderBrush" Value="{StaticResource Surface2Brush}"/>
            <Setter Property="BorderThickness" Value="1"/>
            <Setter Property="Padding" Value="8,6"/>
            <Setter Property="FontSize" Value="14"/>
            <Setter Property="Height" Value="36"/>
            <Setter Property="VerticalContentAlignment" Value="Center"/>
            <Setter Property="CaretBrush" Value="{StaticResource TextBrush}"/>
        </Style>

        <Style TargetType="CheckBox">
            <Setter Property="Foreground" Value="{StaticResource TextBrush}"/>
            <Setter Property="FontSize" Value="14"/>
            <Setter Property="VerticalContentAlignment" Value="Center"/>
        </Style>

        <!-- Label style (keyed — does NOT bleed into ComboBox/CheckBox/Button) -->
        <Style x:Key="LabelStyle" TargetType="TextBlock">
            <Setter Property="Foreground" Value="{StaticResource SubtextBrush}"/>
            <Setter Property="FontSize" Value="13"/>
            <Setter Property="Margin" Value="0,0,0,4"/>
        </Style>

        <Style TargetType="RadioButton">
            <Setter Property="Foreground" Value="{StaticResource TextBrush}"/>
            <Setter Property="FontSize" Value="13"/>
            <Setter Property="Margin" Value="0,0,12,0"/>
            <Setter Property="VerticalContentAlignment" Value="Center"/>
        </Style>
    </Window.Resources>

    <Grid Margin="24,16,24,20">
        <Grid.RowDefinitions>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="Auto"/>
        </Grid.RowDefinitions>
        <Grid.ColumnDefinitions>
            <ColumnDefinition Width="*"/>
            <ColumnDefinition Width="16"/>
            <ColumnDefinition Width="*"/>
        </Grid.ColumnDefinitions>

        <!-- Header -->
        <StackPanel Grid.Row="0" Grid.ColumnSpan="3" Margin="0,0,0,20">
            <StackPanel Orientation="Horizontal">
                <TextBlock Text="T850 ENGINE" FontSize="28" FontWeight="Bold"
                           Foreground="{StaticResource AccentBrush}" Margin="0"/>
                <Border Background="{StaticResource GreenBrush}" CornerRadius="4" Padding="8,2" Margin="12,4,0,0"
                        VerticalAlignment="Center">
                    <TextBlock Text="RELEASE" FontSize="11" FontWeight="Bold" Foreground="#1E1E2E"/>
                </Border>
            </StackPanel>
            <TextBlock Text="Deferred Rendering Demo Launcher" FontSize="13"
                       Foreground="#A6ADC8" Margin="0,2,0,0"/>
        </StackPanel>

        <!-- Graphics API -->
        <Border Grid.Row="1" Grid.Column="0" Background="{StaticResource SurfaceBrush}"
                CornerRadius="8" Padding="16,12" Margin="0,0,0,12">
            <StackPanel>
                <TextBlock Text="GRAPHICS API" FontSize="12" FontWeight="SemiBold"
                           Foreground="{StaticResource AccentBrush}" Margin="0,0,0,10"/>
                <ComboBox Name="cmbApi">
                    <ComboBoxItem Content="D3D11 (Direct3D 11)" Tag="d3d11"/>
                    <ComboBoxItem Content="D3D12 (Direct3D 12)" Tag="d3d12"/>
                    <ComboBoxItem Content="Vulkan" IsSelected="True" Tag="vulkan"/>
                    <ComboBoxItem Content="OpenGL (Desktop GL 3.3)" Tag="gl"/>
                </ComboBox>
            </StackPanel>
        </Border>

        <!-- Display -->
        <Border Grid.Row="2" Grid.Column="0" Background="{StaticResource SurfaceBrush}"
                CornerRadius="8" Padding="16,12" Margin="0,0,0,12">
            <StackPanel>
                <TextBlock Text="DISPLAY" FontSize="12" FontWeight="SemiBold"
                           Foreground="{StaticResource AccentBrush}" Margin="0,0,0,10"/>
                <Grid Margin="0,0,0,10">
                    <Grid.ColumnDefinitions>
                        <ColumnDefinition Width="*"/>
                        <ColumnDefinition Width="12"/>
                        <ColumnDefinition Width="*"/>
                        <ColumnDefinition Width="12"/>
                        <ColumnDefinition Width="*"/>
                    </Grid.ColumnDefinitions>
                    <StackPanel Grid.Column="0">
                        <TextBlock Text="Scene" Style="{StaticResource LabelStyle}"/>
                        <ComboBox Name="cmbScene">
                            <ComboBoxItem Content="Sandbox" Tag="0" IsSelected="True"/>
                            <ComboBoxItem Content="Day" Tag="1"/>
                            <ComboBoxItem Content="Quake3 Mock" Tag="2"/>
                            <ComboBoxItem Content="Ragdoll Editor" Tag="3"/>
                            <ComboBoxItem Content="Scene Template" Tag="4"/>
                        </ComboBox>
                    </StackPanel>
                    <StackPanel Grid.Column="2" VerticalAlignment="Bottom">
                        <CheckBox Name="chkFullscreen" Content="Fullscreen" Margin="0,0,0,8"/>
                    </StackPanel>
                </Grid>
                <StackPanel Margin="0,0,0,8">
                    <TextBlock Text="Culling" Style="{StaticResource LabelStyle}"/>
                    <StackPanel Orientation="Horizontal">
                        <RadioButton Name="rbCullingFull" Content="Enabled (Full on Load)" GroupName="CullingMode" IsChecked="True"/>
                        <RadioButton Name="rbCullingLazy" Content="Lazy" GroupName="CullingMode"/>
                        <RadioButton Name="rbCullingDisabled" Content="Disabled" GroupName="CullingMode"/>
                    </StackPanel>
                </StackPanel>
                <CheckBox Name="chkBenchmark" Content="Benchmark mode" Margin="0,0,0,6" Visibility="Collapsed"/>
                <StackPanel Name="pnlBenchmarkMode" Margin="20,0,0,8" Visibility="Collapsed">
                    <TextBlock Text="Benchmark type" Style="{StaticResource LabelStyle}"/>
                    <StackPanel Orientation="Horizontal">
                        <RadioButton Name="rbBenchmarkSingle" Content="Single run at selected resolution" GroupName="BenchmarkMode" IsChecked="True"/>
                        <RadioButton Name="rbBenchmarkMatrix" Content="Matrix: all APIs/resolutions + onscreen/offscreen" GroupName="BenchmarkMode"/>
                    </StackPanel>
                </StackPanel>
                <StackPanel Name="pnlSandboxInput" Margin="0,6,0,8">
                    <TextBlock Text="Sandbox input" Style="{StaticResource LabelStyle}"/>
                    <ComboBox Name="cmbSandboxInput">
                        <ComboBoxItem Content="Single GLB model" Tag="model" IsSelected="True"/>
                        <ComboBoxItem Content="Editor scene (.t8scene)" Tag="scene"/>
                    </ComboBox>
                </StackPanel>
                <StackPanel Name="pnlModelSelect" Margin="0,0,0,8">
                    <TextBlock Text="Model (Sandbox)" Style="{StaticResource LabelStyle}"/>
                    <ComboBox Name="cmbModel"/>
                </StackPanel>
                <StackPanel Name="pnlSceneFileSelect" Margin="0,0,0,8" Visibility="Collapsed">
                    <TextBlock Text="Scene file (Sandbox)" Style="{StaticResource LabelStyle}"/>
                    <ComboBox Name="cmbSceneFile"/>
                </StackPanel>
                <Grid>
                    <Grid.ColumnDefinitions>
                        <ColumnDefinition Width="*"/>
                        <ColumnDefinition Width="12"/>
                        <ColumnDefinition Width="*"/>
                    </Grid.ColumnDefinitions>
                    <StackPanel Grid.Column="0">
                        <TextBlock Text="Width" Style="{StaticResource LabelStyle}"/>
                        <TextBox Name="txtWidth" Text="2560"/>
                    </StackPanel>
                    <StackPanel Grid.Column="2">
                        <TextBlock Text="Height" Style="{StaticResource LabelStyle}"/>
                        <TextBox Name="txtHeight" Text="1440"/>
                    </StackPanel>
                </Grid>
            </StackPanel>
        </Border>

        <!-- Snapshot Settings -->
        <Border Grid.Row="1" Grid.Column="2" Grid.RowSpan="2" Background="{StaticResource SurfaceBrush}"
                CornerRadius="8" Padding="16,12" Margin="0,0,0,12">
            <StackPanel>
                <TextBlock Text="SNAPSHOT" FontSize="12" FontWeight="SemiBold"
                           Foreground="{StaticResource AccentBrush}" Margin="0,0,0,10"/>
                <CheckBox Name="chkDump" Content="Enable snapshot dump on run" Margin="0,0,0,10"/>
                <CheckBox Name="chkDebugFrames" Content="Debug Frames (spacebar dumps + exits)" Margin="0,0,0,10"/>
                <CheckBox Name="chkReplaySnapshot" Content="Replay Snapshot (restore full scene state)" Margin="0,0,0,6"/>
                <CheckBox Name="chkKeepRunning" Content="Keep running after dump" Margin="0,0,0,10"/>
                <Grid Name="pnlReplaySnapshot" IsEnabled="False" Margin="20,0,0,10">
                    <Grid.ColumnDefinitions>
                        <ColumnDefinition Width="*"/>
                        <ColumnDefinition Width="8"/>
                        <ColumnDefinition Width="Auto"/>
                    </Grid.ColumnDefinitions>
                    <TextBox Grid.Column="0" Name="txtReplaySnapshotPath" IsReadOnly="True"
                             FontSize="11" VerticalContentAlignment="Center"/>
                    <Button Grid.Column="2" Name="btnBrowseSnapshot" Content="Browse..."
                            Padding="10,4" FontSize="12" Cursor="Hand"
                            Background="{StaticResource Surface2Brush}" Foreground="{StaticResource TextBrush}"
                            BorderThickness="0">
                        <Button.Resources>
                            <Style TargetType="Border">
                                <Setter Property="CornerRadius" Value="4"/>
                            </Style>
                        </Button.Resources>
                    </Button>
                </Grid>
                <StackPanel Name="pnlDumpOptions" IsEnabled="False">
                    <StackPanel Orientation="Horizontal" Margin="0,0,0,8">
                        <RadioButton Name="rbSeconds" Content="Dump at second"
                                     IsChecked="True" GroupName="DumpTrigger"/>
                        <RadioButton Name="rbFrame" Content="Dump at frame"
                                     GroupName="DumpTrigger"/>
                    </StackPanel>
                    <Grid>
                        <Grid.ColumnDefinitions>
                            <ColumnDefinition Width="*"/>
                            <ColumnDefinition Width="12"/>
                            <ColumnDefinition Width="*"/>
                        </Grid.ColumnDefinitions>
                        <StackPanel Grid.Column="0" Name="pnlSeconds">
                            <TextBlock Text="Seconds" Style="{StaticResource LabelStyle}"/>
                            <TextBox Name="txtSeconds" Text="5"/>
                        </StackPanel>
                        <StackPanel Grid.Column="2" Name="pnlFrame" IsEnabled="False">
                            <TextBlock Text="Frame Number" Style="{StaticResource LabelStyle}"/>
                            <TextBox Name="txtFrame" Text="300"/>
                        </StackPanel>
                    </Grid>
                </StackPanel>
            </StackPanel>
        </Border>

        <!-- Logging -->
        <Border Grid.Row="3" Grid.Column="0" Background="{StaticResource SurfaceBrush}"
                CornerRadius="8" Padding="16,12" Margin="0,0,0,12">
            <StackPanel>
                <TextBlock Text="LOGGING" FontSize="12" FontWeight="SemiBold"
                           Foreground="{StaticResource AccentBrush}" Margin="0,0,0,10"/>
                <StackPanel Margin="0,0,0,10">
                    <TextBlock Text="Log Level" Style="{StaticResource LabelStyle}"/>
                    <ComboBox Name="cmbLogLevel">
                        <ComboBoxItem Content="Error" Tag="error" IsSelected="True"/>
                        <ComboBoxItem Content="Info" Tag="info"/>
                        <ComboBoxItem Content="Debug" Tag="debug"/>
                        <ComboBoxItem Content="Verbose" Tag="verbose"/>
                        <ComboBoxItem Content="Trace" Tag="trace"/>
                    </ComboBox>
                </StackPanel>
                <CheckBox Name="chkLogToFile" Content="Save log to file" Margin="0,0,0,6"/>
                <CheckBox Name="chkTelemetry" Content="Runtime telemetry JSON" Margin="0,0,0,6"/>
                <StackPanel Name="pnlTelemetryOptions" Margin="20,0,0,0">
                    <TextBlock Text="Telemetry frequency frames (0 = every frame)" Style="{StaticResource LabelStyle}"/>
                    <TextBox Name="txtTelemetryFrequency" Text="60"/>
                </StackPanel>
            </StackPanel>
        </Border>

        <!-- Status + Command Preview -->
        <StackPanel Grid.Row="4" Grid.ColumnSpan="3" VerticalAlignment="Bottom" Margin="0,0,0,12">
            <TextBlock Name="txtStatus" Text="" FontSize="12"
                       Foreground="#A6ADC8" Margin="0,0,0,4"
                       TextWrapping="Wrap"/>
            <TextBox Name="txtCmdPreview" Text="" FontSize="11"
                     Foreground="#A6ADC8" Margin="0" Padding="0"
                     TextWrapping="Wrap" FontFamily="Consolas"
                     IsReadOnly="True" IsReadOnlyCaretVisible="True"
                     Background="Transparent" BorderThickness="0"
                     Height="Auto" MinHeight="24" VerticalContentAlignment="Top"
                     Cursor="IBeam"/>
        </StackPanel>

        <!-- Buttons -->
        <Grid Grid.Row="5" Grid.ColumnSpan="3">
            <Grid.ColumnDefinitions>
                <ColumnDefinition Width="*"/>
                <ColumnDefinition Width="12"/>
                <ColumnDefinition Width="*"/>
                <ColumnDefinition Width="12"/>
                <ColumnDefinition Width="*"/>
            </Grid.ColumnDefinitions>
            <Button Grid.Column="0" Name="btnRun" Content="&#x25B6;  RUN" Height="48"
                    FontSize="18" FontWeight="Bold" Cursor="Hand"
                    Background="{StaticResource GreenBrush}" Foreground="#1E1E2E"
                    BorderThickness="0">
                <Button.Resources>
                    <Style TargetType="Border">
                        <Setter Property="CornerRadius" Value="6"/>
                    </Style>
                </Button.Resources>
            </Button>
            <Button Grid.Column="2" Name="btnDownloadAssets" Content="Download Assets" Height="48"
                    FontSize="16" FontWeight="Bold" Cursor="Hand"
                    Background="{StaticResource GreenBrush}" Foreground="#1E1E2E"
                    BorderThickness="0" IsEnabled="False">
                <Button.Resources>
                    <Style TargetType="Border">
                        <Setter Property="CornerRadius" Value="6"/>
                    </Style>
                </Button.Resources>
            </Button>
            <Button Grid.Column="4" Name="btnBenchmarkMatrix" Content="Benchmark Matrix" Height="48"
                    FontSize="15" FontWeight="Bold" Cursor="Hand"
                    Background="{StaticResource Surface2Brush}" Foreground="#E0E0E0"
                    BorderThickness="0">
                <Button.Resources>
                    <Style TargetType="Border">
                        <Setter Property="CornerRadius" Value="6"/>
                    </Style>
                </Button.Resources>
            </Button>
            <Button Grid.Column="6" Name="btnEditor" Content="&#x270E;  EDITOR" Height="48"
                    FontSize="18" FontWeight="Bold" Cursor="Hand"
                    Background="{StaticResource AccentBrush}" Foreground="#E0E0E0"
                    BorderThickness="0">
                <Button.Resources>
                    <Style TargetType="Border">
                        <Setter Property="CornerRadius" Value="6"/>
                    </Style>
                </Button.Resources>
            </Button>
        </Grid>
    </Grid>
</Window>
"@

# Parse XAML
$reader = [System.Xml.XmlReader]::Create([System.IO.StringReader]::new($xaml))
$window = [System.Windows.Markup.XamlReader]::Load($reader)

# Get controls
$cmbApi         = $window.FindName("cmbApi")
$chkDump        = $window.FindName("chkDump")
$chkDebugFrames = $window.FindName("chkDebugFrames")
$chkKeepRunning = $window.FindName("chkKeepRunning")
$chkReplaySnapshot   = $window.FindName("chkReplaySnapshot")
$pnlReplaySnapshot   = $window.FindName("pnlReplaySnapshot")
$txtReplaySnapshotPath = $window.FindName("txtReplaySnapshotPath")
$btnBrowseSnapshot     = $window.FindName("btnBrowseSnapshot")
$pnlDumpOptions = $window.FindName("pnlDumpOptions")
$rbSeconds      = $window.FindName("rbSeconds")
$rbFrame        = $window.FindName("rbFrame")
$pnlSeconds     = $window.FindName("pnlSeconds")
$pnlFrame       = $window.FindName("pnlFrame")
$txtSeconds     = $window.FindName("txtSeconds")
$txtFrame       = $window.FindName("txtFrame")
$cmbScene       = $window.FindName("cmbScene")
$chkFullscreen  = $window.FindName("chkFullscreen")
$rbCullingFull     = $window.FindName("rbCullingFull")
$rbCullingLazy     = $window.FindName("rbCullingLazy")
$rbCullingDisabled = $window.FindName("rbCullingDisabled")
$chkBenchmark   = $window.FindName("chkBenchmark")
$pnlBenchmarkMode = $window.FindName("pnlBenchmarkMode")
$rbBenchmarkSingle = $window.FindName("rbBenchmarkSingle")
$rbBenchmarkMatrix = $window.FindName("rbBenchmarkMatrix")
$cmbSandboxInput = $window.FindName("cmbSandboxInput")
$cmbModel       = $window.FindName("cmbModel")
$cmbSceneFile   = $window.FindName("cmbSceneFile")
$pnlSandboxInput = $window.FindName("pnlSandboxInput")
$pnlModelSelect = $window.FindName("pnlModelSelect")
$pnlSceneFileSelect = $window.FindName("pnlSceneFileSelect")
$txtWidth       = $window.FindName("txtWidth")
$txtHeight      = $window.FindName("txtHeight")
$cmbLogLevel    = $window.FindName("cmbLogLevel")
$chkLogToFile   = $window.FindName("chkLogToFile")
$chkTelemetry   = $window.FindName("chkTelemetry")
$txtTelemetryFrequency = $window.FindName("txtTelemetryFrequency")
$txtStatus      = $window.FindName("txtStatus")
$txtCmdPreview  = $window.FindName("txtCmdPreview")
$btnRun         = $window.FindName("btnRun")
$btnDownloadAssets = $window.FindName("btnDownloadAssets")
$btnBenchmarkMatrix = $window.FindName("btnBenchmarkMatrix")
$btnEditor      = $window.FindName("btnEditor")

# Resolve root directory: if running from ps2exe, use exe location; otherwise script location
if ($MyInvocation.MyCommand.Path) {
    $rootDir = Split-Path -Parent $MyInvocation.MyCommand.Path
} else {
    $rootDir = (Get-Location).Path
}
# If launched from scripts/, go up one level
if ((Split-Path -Leaf $rootDir) -eq "scripts") {
    $rootDir = Split-Path -Parent $rootDir
}

$configPath = Join-Path $rootDir "config.json"
$script:SceneDependencyCacheKey = ""
$script:SceneDependencyResult = @{ Ok = $true; Missing = @() }
$script:SuppressSceneDependencyValidation = $false
$script:LauncherInitializing = $true
$script:LauncherBusy = $false
$script:AssetDownloadInProgress = $false
$script:CloudAssetStatus = $null
$modelCloudScript = Join-Path $rootDir "scripts\ModelCloud.ps1"
if (-not (Test-Path $modelCloudScript) -and $PSScriptRoot) { $modelCloudScript = Join-Path $PSScriptRoot "ModelCloud.ps1" }
if (Test-Path $modelCloudScript) { . $modelCloudScript }

function Get-SandboxInputMode {
    if ($cmbSandboxInput -and $cmbSandboxInput.SelectedItem) { return $cmbSandboxInput.SelectedItem.Tag.ToString() }
    return "model"
}

function Normalize-ResourcePath {
    param([string]$Path)
    if (-not $Path) { return "" }
    $normalized = $Path.Replace('\', '/').Trim()
    while ($normalized.StartsWith("/")) { $normalized = $normalized.Substring(1) }
    if ($normalized.StartsWith("Assets/", [System.StringComparison]::OrdinalIgnoreCase)) {
        $normalized = $normalized.Substring(7)
    }
    return $normalized
}

function Get-SelectedSceneFilePath {
    if ($cmbSceneFile -and $cmbSceneFile.SelectedItem) { return $cmbSceneFile.SelectedItem.Tag.ToString() }
    return ""
}

function Get-PreferredSceneFileSuffix {
    $sceneTag = if ($cmbScene -and $cmbScene.SelectedItem) { $cmbScene.SelectedItem.Tag.ToString() } else { "" }
    switch ($sceneTag) {
        "2" { return "Q3\q3dm6_mod_3.t8scene" }
        "4" { return "Q3\q3dm6_mod_3_jolt.t8scene" }
        default { return "" }
    }
}

function Select-PreferredSceneFileForScene {
    $preferredSuffix = Get-PreferredSceneFileSuffix
    if (-not $preferredSuffix -or -not $cmbSceneFile) { return }
    foreach ($item in $cmbSceneFile.Items) {
        if ($item.Tag -and $item.Tag.ToString().EndsWith($preferredSuffix, [System.StringComparison]::OrdinalIgnoreCase)) {
            $cmbSceneFile.SelectedItem = $item
            return
        }
    }
}

function Get-SceneFileResourcePath {
    param([string]$ScenePath)
    if (-not $ScenePath) { return "" }
    try { $full = (Resolve-Path $ScenePath).Path } catch { $full = $ScenePath }
    if ($full.StartsWith($rootDir, [System.StringComparison]::OrdinalIgnoreCase)) {
        return (Normalize-ResourcePath $full.Substring($rootDir.Length).TrimStart('\', '/'))
    }
    return $full
}

function Resolve-SceneAssetPath {
    param([string]$ResourcePath)
    if (-not $ResourcePath) { return "" }
    if ([System.IO.Path]::IsPathRooted($ResourcePath)) {
        if (Test-Path $ResourcePath) { return $ResourcePath }
        return ""
    }
    $rel = (Normalize-ResourcePath $ResourcePath).Replace('/', '\')
    $candidates = @(
        (Join-Path $rootDir $rel),
        (Join-Path (Join-Path $rootDir "Assets") $rel)
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) { return $candidate }
    }
    return ""
}

function Get-LauncherAssetRoot {
    return $rootDir
}

function Get-LauncherCloudAssetStatus {
    if (-not (Get-Command Get-T850CloudAssetsStatus -ErrorAction SilentlyContinue)) {
        return [pscustomobject]@{ Ok = $true; Configured = $false; Total = 0; Ready = 0; Missing = 0; MissingPaths = @(); Errors = @(); Message = "Cloud asset checks are not configured." }
    }
    try {
        return (Get-T850CloudAssetsStatus -RootDir $rootDir -AssetRoot (Get-LauncherAssetRoot))
    } catch {
        return [pscustomobject]@{ Ok = $false; Configured = $true; Total = 0; Ready = 0; Missing = 0; MissingPaths = @(); Errors = @($_.Exception.Message); Message = ("Could not check cloud assets: " + $_.Exception.Message) }
    }
}

function Update-DownloadAssetsButton {
    if (-not $btnDownloadAssets) { return }
    $status = $script:CloudAssetStatus
    $missing = if ($status) { [int]$status.Missing } else { 0 }
    $btnDownloadAssets.Content = if ($script:AssetDownloadInProgress) { "Downloading..." } else { "Download Assets" }
    $btnDownloadAssets.IsEnabled = -not $script:AssetDownloadInProgress
    $btnDownloadAssets.ToolTip = if ($script:AssetDownloadInProgress) {
        "Asset download is already running."
    } elseif ($missing -gt 0) {
        "$missing cloud asset(s) missing. Click to download only missing files."
    } elseif ($status -and $status.Total -gt 0) {
        "All cloud assets are present. Click to scan again."
    } else {
        "Click to scan cloud asset manifests."
    }
}

function Update-LauncherCloudAssetStatus {
    $script:CloudAssetStatus = Get-LauncherCloudAssetStatus
    Update-DownloadAssetsButton
    return $script:CloudAssetStatus
}

function Invoke-LauncherModelDownload {
    param([bool]$Quiet = $false)
    if (-not (Get-Command Ensure-T850CloudModels -ErrorAction SilentlyContinue) -and
        -not (Get-Command Ensure-T850CloudTextures -ErrorAction SilentlyContinue)) { return $true }
    $targetRoot = Get-LauncherAssetRoot
    $statusCallback = {
        param([string]$Message)
        if (-not $Quiet -and $txtStatus) {
            $txtStatus.Text = $Message
            $txtStatus.Foreground = $window.FindResource("AccentBrush")
            $window.Dispatcher.Invoke([Action]{}, [System.Windows.Threading.DispatcherPriority]::Background)
        }
    }
    $status = Update-LauncherCloudAssetStatus
    if ($status -and $status.Missing -eq 0 -and $status.Ok) {
        if (-not $Quiet) {
            $message = if ($status.Total -gt 0) { "OK, all asset files are there." } else { $status.Message }
            $txtStatus.Text = $message
            $txtStatus.Foreground = $window.FindResource("GreenBrush")
            [System.Windows.MessageBox]::Show($message, "T850 Launcher", "OK", "Information") | Out-Null
        }
        return $true
    }
    if (Get-Command Ensure-T850CloudModels -ErrorAction SilentlyContinue) {
        $result = Ensure-T850CloudModels -RootDir $rootDir -AssetRoot $targetRoot -StatusCallback $statusCallback
        if (-not $result.Ok) {
            if (-not $Quiet) {
                [System.Windows.MessageBox]::Show(("Could not download model assets:" + "`n`n" + $result.Message), "T850 Launcher", "OK", "Error") | Out-Null
            }
            return $false
        }
    }
    if (Get-Command Ensure-T850CloudTextures -ErrorAction SilentlyContinue) {
        $result = Ensure-T850CloudTextures -RootDir $rootDir -AssetRoot $targetRoot -StatusCallback $statusCallback
        if (-not $result.Ok) {
            if (-not $Quiet) {
                [System.Windows.MessageBox]::Show(("Could not download texture assets:" + "`n`n" + $result.Message), "T850 Launcher", "OK", "Error") | Out-Null
            }
            return $false
        }
    }
    Update-LauncherCloudAssetStatus | Out-Null
    return $true
}

function Get-SceneRequiredMeshes {
    param([string]$ScenePath)
    if (-not $ScenePath -or -not (Test-Path $ScenePath)) { return @() }
    try {
        $scene = Get-Content $ScenePath -Raw | ConvertFrom-Json
        if (-not $scene.objects) { return @() }
        $meshes = @()
        foreach ($object in $scene.objects) {
            if ($object.PSObject.Properties['visible'] -and -not [bool]$object.visible) { continue }
            if ($object.PSObject.Properties['mesh'] -and $object.mesh) {
                $meshes += (Normalize-ResourcePath $object.mesh.ToString())
            }
        }
        return @($meshes | Sort-Object -Unique)
    } catch {
        return @()
    }
}

function Test-SelectedSceneDependencies {
    if (-not $cmbScene.SelectedItem) {
        return @{ Ok = $true; Missing = @() }
    }
    $sceneTag = $cmbScene.SelectedItem.Tag.ToString()
    if (($sceneTag -ne "2") -and ($sceneTag -ne "4") -and (($sceneTag -ne "0") -or ((Get-SandboxInputMode) -ne "scene"))) {
        return @{ Ok = $true; Missing = @() }
    }
    $scenePath = Get-SelectedSceneFilePath
    if (-not $scenePath -or -not (Test-Path $scenePath)) {
        return @{ Ok = $false; Missing = @("Selected .t8scene file") }
    }
    $missing = @()
    foreach ($mesh in (Get-SceneRequiredMeshes $scenePath)) {
        if (-not (Resolve-SceneAssetPath $mesh)) { $missing += $mesh }
    }
    return @{ Ok = ($missing.Count -eq 0); Missing = $missing }
}

function Show-SceneDependencyError {
    param($Result)
    if ($Result.Ok) { return $true }
    [System.Windows.MessageBox]::Show(
        ("The selected scene cannot be launched because required files are missing:" + "`n`n" + (($Result.Missing | ForEach-Object { "- $_" }) -join "`n")),
        "T850 Launcher", "OK", "Error") | Out-Null
    return $false
}

function Get-ComboBoxItemKey {
    param($Item)
    if ($null -eq $Item) { return "" }
    if ($null -ne $Item.Tag) { return $Item.Tag.ToString() }
    if ($null -ne $Item.Content) { return $Item.Content.ToString() }
    return ""
}

function Get-LauncherControl {
    param([string]$Name)
    $control = Get-Variable -Name $Name -Scope Script -ValueOnly -ErrorAction SilentlyContinue
    if ($null -eq $control -and $script:window) {
        $control = $script:window.FindName($Name)
        if ($null -ne $control) {
            Set-Variable -Name $Name -Scope Script -Value $control
        }
    }
    if ($null -eq $control) {
        throw "Launcher UI control '$Name' was not found."
    }
    return $control
}

function Get-SceneDependencyCacheKey {
    $sceneTag = if ($cmbScene) { Get-ComboBoxItemKey $cmbScene.SelectedItem } else { "" }
    return (((Get-SandboxInputMode), $sceneTag, (Get-SelectedSceneFilePath)) -join "|")
}

function Update-SceneDependencyCache {
    if ($script:LauncherInitializing -or $script:SuppressSceneDependencyValidation) {
        $script:SceneDependencyCacheKey = ""
        $script:SceneDependencyResult = @{ Ok = $true; Missing = @() }
        return
    }
    $script:SceneDependencyCacheKey = Get-SceneDependencyCacheKey
    $script:SceneDependencyResult = Test-SelectedSceneDependencies
}

function Get-CachedSceneDependencyResult {
    if ($script:SceneDependencyCacheKey -ne (Get-SceneDependencyCacheKey)) {
        return @{ Ok = $true; Missing = @() }
    }
    return $script:SceneDependencyResult
}

# ── Config load/save ──

function Normalize-CullingMode {
    param([string]$mode)
    $lower = if ($mode) { $mode.ToLowerInvariant() } else { "full" }
    switch ($lower) {
        { $_ -in @("full", "enabled", "enable", "fullonload", "on", "1") } { return "full" }
        { $_ -in @("lazy", "deferred", "2") } { return "lazy" }
        { $_ -in @("disabled", "disable", "off", "none", "0") } { return "disabled" }
        default { return "full" }
    }
}

function Get-CullingMode {
    if ($rbCullingDisabled.IsChecked) { return "disabled" }
    if ($rbCullingLazy.IsChecked) { return "lazy" }
    return "full"
}

function Set-CullingMode {
    param([string]$mode)
    $normalized = Normalize-CullingMode $mode
    $rbCullingFull.IsChecked = ($normalized -eq "full")
    $rbCullingLazy.IsChecked = ($normalized -eq "lazy")
    $rbCullingDisabled.IsChecked = ($normalized -eq "disabled")
}

function Load-Config {
    if (-not (Test-Path $configPath)) { return }
    try {
        $cfg = Get-Content $configPath -Raw | ConvertFrom-Json

        # API
        foreach ($item in $cmbApi.Items) {
            if ($item.Tag -ieq $cfg.api) {
                $cmbApi.SelectedItem = $item; break
            }
        }
        # Display
        if ($cfg.display) {
            if ($cfg.display.width)  { $txtWidth.Text  = $cfg.display.width.ToString() }
            if ($cfg.display.height) { $txtHeight.Text = $cfg.display.height.ToString() }
            if ($cfg.display.PSObject.Properties['fullscreen']) {
                $chkFullscreen.IsChecked = [bool]$cfg.display.fullscreen
            }
            if ($cfg.display.PSObject.Properties['scene']) {
                foreach ($item in $cmbScene.Items) {
                    if ($item.Tag -eq $cfg.display.scene.ToString()) {
                        $cmbScene.SelectedItem = $item; break
                    }
                }
            }
            if ($cfg.display.PSObject.Properties['model'] -and $cfg.display.model) {
                foreach ($item in $cmbModel.Items) {
                    if ($item.Tag -eq $cfg.display.model) {
                        $cmbModel.SelectedItem = $item; break
                    }
                }
            }
            if ($cfg.display.PSObject.Properties['sceneFile'] -and $cfg.display.sceneFile) {
                foreach ($item in $cmbSandboxInput.Items) {
                    if ($item.Tag -eq "scene") { $cmbSandboxInput.SelectedItem = $item; break }
                }
                if (-not ($cfg.display.PSObject.Properties['scene']) -or $cfg.display.scene.ToString() -eq "0") {
                    foreach ($item in $cmbScene.Items) {
                        if ($item.Tag -eq "0") { $cmbScene.SelectedItem = $item; break }
                    }
                }
                foreach ($item in $cmbSceneFile.Items) {
                    if ($item.Tag -eq $cfg.display.sceneFile -or (Get-SceneFileResourcePath $item.Tag) -eq $cfg.display.sceneFile) {
                        $cmbSceneFile.SelectedItem = $item; break
                    }
                }
            } elseif ($cfg.display.PSObject.Properties['model'] -and $cfg.display.model) {
                foreach ($item in $cmbSandboxInput.Items) {
                    if ($item.Tag -eq "model") { $cmbSandboxInput.SelectedItem = $item; break }
                }
            }
        }
        # Debug Frames
        if ($cfg.PSObject.Properties['debugFrames']) {
            $chkDebugFrames.IsChecked = [bool]$cfg.debugFrames
        }
        # Runtime flags
        if ($cfg.PSObject.Properties['benchmark']) {
            $chkBenchmark.IsChecked = [bool]$cfg.benchmark
        } elseif ($cfg.devTools -and $cfg.devTools.PSObject.Properties['benchmark']) {
            $chkBenchmark.IsChecked = [bool]$cfg.devTools.benchmark
        }
        if ($cfg.PSObject.Properties['cullingMode']) {
            Set-CullingMode $cfg.cullingMode
        } elseif ($cfg.PSObject.Properties['cullDisabled'] -and [bool]$cfg.cullDisabled) {
            Set-CullingMode "disabled"
        } elseif ($cfg.devTools -and $cfg.devTools.PSObject.Properties['cullingMode']) {
            Set-CullingMode $cfg.devTools.cullingMode
        } elseif ($cfg.devTools -and $cfg.devTools.PSObject.Properties['cullDisabled'] -and [bool]$cfg.devTools.cullDisabled) {
            Set-CullingMode "disabled"
        }
        # Keep Running
        if ($cfg.PSObject.Properties['keepRunning']) {
            $chkKeepRunning.IsChecked = [bool]$cfg.keepRunning
        }
        # Replay Snapshot
        if ($cfg.PSObject.Properties['replaySnapshot']) {
            $chkReplaySnapshot.IsChecked = [bool]$cfg.replaySnapshot.enabled
            if ($cfg.replaySnapshot.path) { $txtReplaySnapshotPath.Text = $cfg.replaySnapshot.path }
        }
        # Dump
        if ($cfg.dump) {
            $chkDump.IsChecked = [bool]$cfg.dump.enabled
            if ($cfg.dump.trigger -eq "frame") {
                $rbFrame.IsChecked   = $true
                $rbSeconds.IsChecked = $false
            } else {
                $rbSeconds.IsChecked = $true
                $rbFrame.IsChecked   = $false
            }
            if ($cfg.dump.seconds) { $txtSeconds.Text = $cfg.dump.seconds.ToString() }
            if ($cfg.dump.frame)   { $txtFrame.Text   = $cfg.dump.frame.ToString() }
        }
        # Logging — support both flat and nested devTools format from dev launcher
        if ($cfg.PSObject.Properties['logLevel']) {
            foreach ($item in $cmbLogLevel.Items) {
                if ($item.Tag -ieq $cfg.logLevel) {
                    $cmbLogLevel.SelectedItem = $item; break
                }
            }
        } elseif ($cfg.devTools -and $cfg.devTools.PSObject.Properties['logLevel']) {
            foreach ($item in $cmbLogLevel.Items) {
                if ($item.Tag -ieq $cfg.devTools.logLevel) {
                    $cmbLogLevel.SelectedItem = $item; break
                }
            }
        }
        if ($cfg.PSObject.Properties['logToFile']) {
            $chkLogToFile.IsChecked = [bool]$cfg.logToFile
        } elseif ($cfg.devTools -and $cfg.devTools.PSObject.Properties['logToFile']) {
            $chkLogToFile.IsChecked = [bool]$cfg.devTools.logToFile
        }
        if ($cfg.PSObject.Properties['telemetry']) {
            if ($cfg.telemetry.PSObject.Properties['enabled']) {
                $chkTelemetry.IsChecked = [bool]$cfg.telemetry.enabled
            }
            if ($cfg.telemetry.PSObject.Properties['frequencyFrames']) {
                $txtTelemetryFrequency.Text = ([int]$cfg.telemetry.frequencyFrames).ToString()
            }
        }
    } catch {
        # Silently ignore corrupt config — defaults will be used
    }
}

function Save-Config {
    $sceneTag = ($cmbScene.SelectedItem).Tag.ToString()
    $cullingMode = Get-CullingMode
    $sandboxMode = Get-SandboxInputMode
    $display = @{
        width      = [int]$txtWidth.Text
        height     = [int]$txtHeight.Text
        fullscreen = [bool]$chkFullscreen.IsChecked
        scene      = [int]$sceneTag
    }
    if (($sceneTag -eq "2") -or ($sceneTag -eq "4") -or (($sceneTag -eq "0") -and ($sandboxMode -eq "scene"))) {
        $display.sceneFile = Get-SceneFileResourcePath (Get-SelectedSceneFilePath)
    } else {
        $display.model = if ($cmbModel.SelectedItem) { ($cmbModel.SelectedItem).Tag.ToString() } else { "Models/DamagedHelmet.glb" }
    }
    $cfg = @{
        api           = ($cmbApi.SelectedItem).Tag.ToString()
        display = $display
        debugFrames = [bool]$chkDebugFrames.IsChecked
        benchmark = ($sceneTag -eq "1" -and [bool]$chkBenchmark.IsChecked)
        cullingMode = $cullingMode
        cullDisabled = ($cullingMode -eq "disabled")
        keepRunning = [bool]$chkKeepRunning.IsChecked
        replaySnapshot = @{
            enabled = [bool]$chkReplaySnapshot.IsChecked
            path    = $txtReplaySnapshotPath.Text
        }
        dump = @{
            enabled = [bool]$chkDump.IsChecked
            trigger = if ($rbFrame.IsChecked) { "frame" } else { "seconds" }
            seconds = [int]$txtSeconds.Text
            frame   = [int]$txtFrame.Text
        }
        logLevel  = ($cmbLogLevel.SelectedItem).Tag.ToString()
        logToFile = [bool]$chkLogToFile.IsChecked
        telemetry = @{
            enabled = [bool]$chkTelemetry.IsChecked
            frequencyFrames = [int]$txtTelemetryFrequency.Text
            outputPath = "logs\perf_telemetry.json"
        }
    }
    $cfg | ConvertTo-Json -Depth 3 | Set-Content $configPath -Encoding UTF8
}

# ── Helpers ──

function Format-LauncherCommandLine {
    param(
        [Parameter(Mandatory = $true)] [string]$FilePath,
        [string[]]$Arguments = @()
    )
    $quotedArgs = @()
    foreach ($arg in $Arguments) {
        if ($arg -match '[\s",]') { $quotedArgs += ('"' + ($arg -replace '"', '\"') + '"') }
        else { $quotedArgs += $arg }
    }
    return (('"' + $FilePath + '" ' + ($quotedArgs -join ' ')).Trim())
}

function Get-LaunchCommand {
    $apiTag = ($cmbApi.SelectedItem).Tag.ToString()

    $exePath = Join-Path $rootDir "DayScene.exe"
    $argList = @("--api", $apiTag)

    if ($chkDebugFrames.IsChecked) {
        $argList += "--debugFrames"
    }

    if ($chkReplaySnapshot.IsChecked -and $txtReplaySnapshotPath.Text) {
        $argList += @("--replaySnapshot", ('"{0}"' -f $txtReplaySnapshotPath.Text))
    }

    if ($chkKeepRunning.IsChecked) {
        $argList += "--keepRunning"
    }

    if ($chkDump.IsChecked) {
        if ($rbFrame.IsChecked) {
            $argList += "--dumpSnapshot-frame"
            $argList += $txtFrame.Text
        } else {
            $argList += "--dumpSnapshot-seconds"
            $argList += $txtSeconds.Text
        }
    }

    $sceneTag = ($cmbScene.SelectedItem).Tag.ToString()
    if ($sceneTag -ne "0") {
        $argList += @("--scene", $sceneTag)
    }

    $benchmarkMatrix = ($sceneTag -eq "1" -and $chkBenchmark.IsChecked -and $rbBenchmarkMatrix.IsChecked)
    if ($benchmarkMatrix) {
        $ts = Get-Date -Format "yyyyMMdd_HHmmss"
        $reportPath = Join-Path $rootDir ("benchmark_reports\dayscene_matrix_$ts\DayScene_Benchmark_Report.md")
        $argList += @("--scene", "1", "--benchmarkMatrix", "--benchmarkSeconds", "90", "--benchmarkReport", $reportPath)
    } elseif ($sceneTag -eq "1" -and $chkBenchmark.IsChecked) {
        $argList += @("--benchmark", "--benchmarkSeconds", "90")
    }

    $argList += @("--culling", (Get-CullingMode))

    if ($sceneTag -eq "0" -or $sceneTag -eq "2" -or $sceneTag -eq "3" -or $sceneTag -eq "4") {
        if (($sceneTag -eq "2") -or ($sceneTag -eq "4") -or (($sceneTag -eq "0") -and ((Get-SandboxInputMode) -eq "scene"))) {
            $sceneFile = Get-SelectedSceneFilePath
            if ($sceneFile) {
                $argList += @("--sceneFile", ('"{0}"' -f (Get-SceneFileResourcePath $sceneFile)))
            }
        } elseif (($sceneTag -eq "3" -or $sceneTag -eq "0") -and $cmbModel.SelectedItem) {
            $argList += @("--model", ($cmbModel.SelectedItem).Tag.ToString())
        }
    }

    if ($chkFullscreen.IsChecked) {
        $argList += "--fullscreen"
    }

    $w = $txtWidth.Text
    $h = $txtHeight.Text
    if ($w -and $h) {
        $argList += @("--width", $w, "--height", $h)
    }

    $logTag = ($cmbLogLevel.SelectedItem).Tag.ToString()
    $argList += @("--logLevel", $logTag)

    if ($chkLogToFile.IsChecked) {
        $ts = Get-Date -Format "yyyyMMdd_HHmmss"
        $logFilename = "logs\T850_${ts}_${apiTag}.log"
        $argList += @("--logFile", $logFilename)
    }

    if ($chkTelemetry.IsChecked) {
        $argList += @("--telemetry", "--telemetryFrequencyFrames", $txtTelemetryFrequency.Text, "--telemetryOutput", "logs\perf_telemetry.json")
    }

    return @{
        ExePath = $exePath
        Args    = $argList
        Display = ('"' + $exePath + '" ' + ($argList -join ' '))
    }
}

function Get-EditorLaunchCommand {
    $apiTag = ($cmbApi.SelectedItem).Tag.ToString()
    $exePath = Join-Path $rootDir "T8ditor.exe"
    $argList = @()

    # Editor supports D3D12 and Vulkan only: D3D11/D3D12 -> D3D12, GL/Vulkan -> Vulkan
    $editorApi = if ($apiTag -eq "vulkan" -or $apiTag -eq "gl") { "vulkan" } else { "d3d12" }
    if ($editorApi -eq "vulkan") {
        $argList += @("--api", "vulkan")
    }

    $w = $txtWidth.Text
    $h = $txtHeight.Text
    if ($w -and $h) {
        $argList += @("--width", $w, "--height", $h)
    }

    $logTag = ($cmbLogLevel.SelectedItem).Tag.ToString()
    $argList += @("--logLevel", $logTag)

    if ($chkLogToFile.IsChecked) {
        $ts = Get-Date -Format "yyyyMMdd_HHmmss"
        $logFilename = "logs\T8ditor_${ts}_${editorApi}.log"
        $argList += @("--logFile", $logFilename)
    }

    return @{
        ExePath = $exePath
        Args    = $argList
        Display = ('"' + $exePath + '" ' + ($argList -join ' '))
    }
}

function Update-Preview {
    Update-DownloadAssetsButton
    $cmd = Get-LaunchCommand
    $txtCmdPreview.Text = $cmd.Display

    $sceneOk = Test-Path $cmd.ExePath
    $editorCmd = Get-EditorLaunchCommand
    $editorOk = Test-Path $editorCmd.ExePath
    $sceneDeps = Get-CachedSceneDependencyResult
    $assetStatus = $script:CloudAssetStatus
    $assetsMissing = ($assetStatus -and $assetStatus.Configured -and ($assetStatus.Missing -gt 0 -or -not $assetStatus.Ok))

    if (-not $sceneDeps.Ok) {
        $txtStatus.Text = "Scene missing: $($sceneDeps.Missing -join ', ')"
        $txtStatus.Foreground = $window.FindResource("RedBrush")
        $btnRun.IsEnabled = $false
        $btnEditor.IsEnabled = $editorOk
    } elseif ($assetsMissing) {
        $txtStatus.Text = "Cloud assets missing: $($assetStatus.Missing)/$($assetStatus.Total). Click Download Assets before running."
        $txtStatus.Foreground = $window.FindResource("AccentBrush")
        $btnRun.IsEnabled = $false
        $btnEditor.IsEnabled = $editorOk
    } elseif ($sceneOk -and $editorOk) {
        $txtStatus.Text = "Ready to run (Scene + Editor)"
        $txtStatus.Foreground = $window.FindResource("GreenBrush")
        $btnRun.IsEnabled = $true
        $btnEditor.IsEnabled = $true
    } elseif ($sceneOk) {
        $txtStatus.Text = "Scene ready, Editor not found"
        $txtStatus.Foreground = $window.FindResource("GreenBrush")
        $btnRun.IsEnabled = $true
        $btnEditor.IsEnabled = $false
    } else {
        $txtStatus.Text = "DayScene.exe not found in current folder"
        $txtStatus.Foreground = $window.FindResource("RedBrush")
        $btnRun.IsEnabled = $false
        $btnEditor.IsEnabled = $editorOk
    }
}

function Update-SceneOptionVisibility {
    if (-not $cmbScene.SelectedItem) { return }
    $sceneTag = ($cmbScene.SelectedItem).Tag.ToString()
    $sandboxVisible = ($sceneTag -eq "0")
    $sandboxMode = Get-SandboxInputMode
    $pnlSandboxInput.Visibility = if ($sandboxVisible) { "Visible" } else { "Collapsed" }
    $pnlModelSelect.Visibility = if (($sandboxVisible -and ($sandboxMode -eq "model")) -or ($sceneTag -eq "3")) { "Visible" } else { "Collapsed" }
    $pnlSceneFileSelect.Visibility = if (($sandboxVisible -and ($sandboxMode -eq "scene")) -or ($sceneTag -eq "2") -or ($sceneTag -eq "4")) { "Visible" } else { "Collapsed" }
    $chkBenchmark.Visibility = if ($sceneTag -eq "1") { "Visible" } else { "Collapsed" }
    $pnlBenchmarkMode.Visibility = if ($sceneTag -eq "1" -and $chkBenchmark.IsChecked) { "Visible" } else { "Collapsed" }
    if ($btnBenchmarkMatrix) { $btnBenchmarkMatrix.Visibility = "Collapsed" }
    if ($sceneTag -ne "1") { $chkBenchmark.IsChecked = $false }
}

# ── Events ──

$chkDump.Add_Checked({
    $pnlDumpOptions.IsEnabled = $true
    Update-Preview
})
$chkDump.Add_Unchecked({
    $pnlDumpOptions.IsEnabled = $false
    Update-Preview
})

$rbSeconds.Add_Checked({
    $pnlSeconds.IsEnabled = $true
    $pnlFrame.IsEnabled   = $false
    Update-Preview
})
$rbFrame.Add_Checked({
    $pnlSeconds.IsEnabled = $false
    $pnlFrame.IsEnabled   = $true
    Update-Preview
})

$chkDebugFrames.Add_Checked({ Update-Preview })
$chkDebugFrames.Add_Unchecked({ Update-Preview })

$chkKeepRunning.Add_Checked({ Update-Preview })
$chkKeepRunning.Add_Unchecked({ Update-Preview })

$chkReplaySnapshot.Add_Checked({
    $pnlReplaySnapshot.IsEnabled = $true
    Update-Preview
})
$chkReplaySnapshot.Add_Unchecked({
    $pnlReplaySnapshot.IsEnabled = $false
    Update-Preview
})

$btnBrowseSnapshot.Add_Click({
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title  = "Select snapshot file"
    $dlg.Filter = "Snapshot files (*.json)|*.json|All files (*.*)|*.*"
    if ($dlg.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
        $txtReplaySnapshotPath.Text = $dlg.FileName
        Update-Preview
    }
})

$cmbModel.Add_SelectionChanged({ Update-Preview })
$cmbSceneFile.Add_SelectionChanged({
    Update-SceneDependencyCache
    Update-Preview
})
$cmbSandboxInput.Add_SelectionChanged({
    Update-SceneOptionVisibility
    Update-SceneDependencyCache
    Update-Preview
})

$cmbApi.Add_SelectionChanged({ Update-Preview })
$cmbScene.Add_SelectionChanged({
    Update-SceneOptionVisibility
    Select-PreferredSceneFileForScene
    Update-SceneDependencyCache
    Update-Preview
})
$chkFullscreen.Add_Checked({ Update-Preview })
$chkFullscreen.Add_Unchecked({ Update-Preview })
$rbCullingFull.Add_Checked({ Update-Preview })
$rbCullingLazy.Add_Checked({ Update-Preview })
$rbCullingDisabled.Add_Checked({ Update-Preview })
$chkBenchmark.Add_Checked({ Update-SceneOptionVisibility; Update-Preview })
$chkBenchmark.Add_Unchecked({ Update-SceneOptionVisibility; Update-Preview })
$rbBenchmarkSingle.Add_Checked({ Update-Preview })
$rbBenchmarkMatrix.Add_Checked({ Update-Preview })
$cmbLogLevel.Add_SelectionChanged({ Update-Preview })
$chkLogToFile.Add_Checked({ Update-Preview })
$chkLogToFile.Add_Unchecked({ Update-Preview })
$chkTelemetry.Add_Checked({ Update-Preview })
$chkTelemetry.Add_Unchecked({ Update-Preview })
$txtTelemetryFrequency.Add_TextChanged({ Update-Preview })
$txtSeconds.Add_TextChanged({ Update-Preview })
$txtFrame.Add_TextChanged({ Update-Preview })
$txtWidth.Add_TextChanged({ Update-Preview })
$txtHeight.Add_TextChanged({ Update-Preview })

# DOWNLOAD ASSETS button
$btnDownloadAssets.Add_Click({
    $script:AssetDownloadInProgress = $true
    $script:LauncherBusy = $true
    Update-DownloadAssetsButton
    try {
        if (-not (Invoke-LauncherModelDownload)) { return }
        Populate-ModelList
        Populate-SceneFileList
        Update-SceneDependencyCache
    } finally {
        $script:AssetDownloadInProgress = $false
        $script:LauncherBusy = $false
        Update-Preview
    }
})

# RUN button
$btnRun.Add_Click({
    Update-LauncherCloudAssetStatus | Out-Null
    Update-Preview
    $assetStatus = $script:CloudAssetStatus
    if ($assetStatus -and $assetStatus.Configured -and ($assetStatus.Missing -gt 0 -or -not $assetStatus.Ok)) {
        [System.Windows.MessageBox]::Show(
            ("Cloud assets are missing or invalid. Click Download Assets before running." + "`n`n" + $assetStatus.Message),
            "T850 Launcher", "OK", "Warning") | Out-Null
        return
    }
    Populate-ModelList
    Populate-SceneFileList
    Update-SceneDependencyCache
    $sceneDeps = $script:SceneDependencyResult
    if (-not (Show-SceneDependencyError $sceneDeps)) { return }

    $cmd = Get-LaunchCommand
    if (-not (Test-Path $cmd.ExePath)) {
        [System.Windows.MessageBox]::Show(
            ("DayScene.exe not found in:" + "`n" + $rootDir),
            "T850 Launcher", "OK", "Error")
        return
    }

    Save-Config

    $txtStatus.Text = "Running..."
    $txtStatus.Foreground = $window.FindResource("GreenBrush")
    $window.Dispatcher.Invoke([Action]{}, [System.Windows.Threading.DispatcherPriority]::Background)

    $workDir = $rootDir
    Start-Process -FilePath $cmd.ExePath -ArgumentList $cmd.Args -WorkingDirectory $workDir

    $txtStatus.Text = "Process running"
    $txtStatus.Foreground = $window.FindResource("GreenBrush")
})

# EDITOR button
$btnEditor.Add_Click({
    Populate-ModelList
    $cmd = Get-EditorLaunchCommand
    if (-not (Test-Path $cmd.ExePath)) {
        [System.Windows.MessageBox]::Show(
            ("T8ditor.exe not found in:" + "`n" + $rootDir),
            "T850 Launcher", "OK", "Error")
        return
    }

    Save-Config

    $txtStatus.Text = "Editor running..."
    $txtStatus.Foreground = $window.FindResource("AccentBrush")
    $window.Dispatcher.Invoke([Action]{}, [System.Windows.Threading.DispatcherPriority]::Background)

    $workDir = $rootDir
    Start-Process -FilePath $cmd.ExePath -ArgumentList $cmd.Args -WorkingDirectory $workDir

    $txtStatus.Text = "Editor running"
    $txtStatus.Foreground = $window.FindResource("AccentBrush")
})

# ── Initialize ──

# Scan Models folder for .glb/.gltf files and populate the dropdown
function Populate-ModelList {
    $previous = if ($cmbModel.SelectedItem -and $cmbModel.SelectedItem.Tag) { $cmbModel.SelectedItem.Tag.ToString() } else { "" }
    $cmbModel.Items.Clear()
    $modelsDir = Join-Path $rootDir "Models"
    if (Test-Path $modelsDir) {
        $files = @()
        $files += @(Get-ChildItem $modelsDir -Filter "*.glb" -ErrorAction SilentlyContinue | Sort-Object Name)
        $files += @(Get-ChildItem $modelsDir -Filter "*.gltf" -ErrorAction SilentlyContinue | Sort-Object Name)
        foreach ($f in $files) {
            $item = New-Object System.Windows.Controls.ComboBoxItem
            $item.Content = $f.Name
            $item.Tag = "Models/" + $f.Name
            $cmbModel.Items.Add($item) | Out-Null
        }
    }
    $selected = $false
    if ($previous) {
        foreach ($item in $cmbModel.Items) {
            if ($item.Tag -eq $previous) {
                $cmbModel.SelectedItem = $item; $selected = $true; break
            }
        }
    }
    foreach ($item in $cmbModel.Items) {
        if (-not $selected -and $item.Content -eq "DamagedHelmet.glb") {
            $cmbModel.SelectedItem = $item; $selected = $true; break
        }
    }
    if (-not $selected -and $cmbModel.Items.Count -gt 0) {
        $cmbModel.SelectedIndex = 0
    }
}

function Populate-SceneFileList {
    $script:SuppressSceneDependencyValidation = $true
    try {
        $sceneFileCombo = Get-LauncherControl "cmbSceneFile"
        $previous = Get-SelectedSceneFilePath
        $sceneFileCombo.Items.Clear()
        $scenesDir = Join-Path $rootDir "Scenes"
        if (Test-Path $scenesDir) {
            $files = Get-ChildItem $scenesDir -Filter "*.t8scene" -Recurse | Sort-Object FullName
            foreach ($f in $files) {
                $item = New-Object System.Windows.Controls.ComboBoxItem
                $relative = $f.FullName.Substring($scenesDir.Length).TrimStart('\', '/')
                $item.Content = if ($relative) { $relative } else { $f.Name }
                $item.Tag = $f.FullName
                $sceneFileCombo.Items.Add($item) | Out-Null
            }
        }
        $selected = $false
        if ($previous) {
            foreach ($item in $sceneFileCombo.Items) {
                if ($item.Tag -eq $previous -or (Get-SceneFileResourcePath $item.Tag) -eq $previous) {
                    $sceneFileCombo.SelectedItem = $item; $selected = $true; break
                }
            }
        }
        if (-not $selected -and $sceneFileCombo.Items.Count -gt 0) {
            $sceneFileCombo.SelectedIndex = 0
        }
    } finally {
        $script:SuppressSceneDependencyValidation = $false
    }
}

try {
    Populate-ModelList
    Populate-SceneFileList
    Load-Config
    Update-LauncherCloudAssetStatus | Out-Null
    Update-SceneOptionVisibility
    Update-Preview
} finally {
    $script:LauncherInitializing = $false
}

$window.ShowDialog() | Out-Null
