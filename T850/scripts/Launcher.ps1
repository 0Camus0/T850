# T850 Engine Launcher
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
                <Border Background="#F9E2AF" CornerRadius="4" Padding="8,2" Margin="12,4,0,0"
                        VerticalAlignment="Center">
                    <TextBlock Text="DEV" FontSize="11" FontWeight="Bold" Foreground="#1E1E2E"/>
                </Border>
            </StackPanel>
            <TextBlock Text="Deferred Rendering Demo Launcher" FontSize="13"
                       Foreground="#A6ADC8" Margin="0,2,0,0"/>
        </StackPanel>

        <!-- Build Configuration -->
        <Border Grid.Row="1" Grid.Column="0" Background="{StaticResource SurfaceBrush}"
                CornerRadius="8" Padding="16,12" Margin="0,0,0,12">
            <StackPanel>
                <TextBlock Text="BUILD CONFIGURATION" FontSize="12" FontWeight="SemiBold"
                           Foreground="{StaticResource AccentBrush}" Margin="0,0,0,10"/>
                <Grid>
                    <Grid.ColumnDefinitions>
                        <ColumnDefinition Width="*"/>
                        <ColumnDefinition Width="12"/>
                        <ColumnDefinition Width="*"/>
                    </Grid.ColumnDefinitions>
                    <StackPanel Grid.Column="0">
                        <TextBlock Text="Architecture" Style="{StaticResource LabelStyle}"/>
                        <ComboBox Name="cmbArch">
                            <ComboBoxItem Content="x64" IsSelected="True"/>
                            <ComboBoxItem Content="x86"/>
                            <ComboBoxItem Content="ARM64"/>
                        </ComboBox>
                    </StackPanel>
                    <StackPanel Grid.Column="2">
                        <TextBlock Text="Configuration" Style="{StaticResource LabelStyle}"/>
                        <ComboBox Name="cmbConfig">
                            <ComboBoxItem Content="Release" IsSelected="True"/>
                            <ComboBoxItem Content="Debug"/>
                        </ComboBox>
                    </StackPanel>
                </Grid>
            </StackPanel>
        </Border>

        <!-- Graphics API -->
        <Border Grid.Row="2" Grid.Column="0" Background="{StaticResource SurfaceBrush}"
                CornerRadius="8" Padding="16,12" Margin="0,0,0,12">
            <StackPanel>
                <TextBlock Text="GRAPHICS API" FontSize="12" FontWeight="SemiBold"
                           Foreground="{StaticResource AccentBrush}" Margin="0,0,0,10"/>
                <ComboBox Name="cmbApi">
                    <ComboBoxItem Content="D3D11 (Direct3D 11)" IsSelected="True" Tag="d3d11"/>
                    <ComboBoxItem Content="D3D12 (Direct3D 12)" Tag="d3d12"/>
                    <ComboBoxItem Content="Vulkan" Tag="vulkan"/>
                    <ComboBoxItem Content="OpenGL (Desktop GL 3.3)" Tag="gl"/>
                </ComboBox>
            </StackPanel>
        </Border>

        <!-- RT Dump Settings -->
        <Border Grid.Row="1" Grid.Column="2" Background="{StaticResource SurfaceBrush}"
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

        <!-- Display -->
        <Border Grid.Row="3" Grid.Column="0" Background="{StaticResource SurfaceBrush}"
                CornerRadius="8" Padding="16,12" Margin="0,0,0,12">
            <StackPanel>
                <TextBlock Text="DISPLAY" FontSize="12" FontWeight="SemiBold"
                           Foreground="{StaticResource AccentBrush}" Margin="0,0,0,10"/>
                <Grid Margin="0,0,0,10">
                    <Grid.ColumnDefinitions>
                        <ColumnDefinition Width="*"/>
                        <ColumnDefinition Width="12"/>
                        <ColumnDefinition Width="*"/>
                    </Grid.ColumnDefinitions>
                    <StackPanel Grid.Column="0">
                        <TextBlock Text="Scene" Style="{StaticResource LabelStyle}"/>
                        <ComboBox Name="cmbScene">
                            <ComboBoxItem Content="Sandbox" Tag="0" IsSelected="True"/>
                            <ComboBoxItem Content="Day" Tag="1"/>
                        </ComboBox>
                    </StackPanel>
                    <StackPanel Grid.Column="2" VerticalAlignment="Bottom">
                        <CheckBox Name="chkFullscreen" Content="Fullscreen" Margin="0,0,0,8"/>
                    </StackPanel>
                </Grid>
                <CheckBox Name="chkCullDisabled" Content="Disable culling" Margin="0,0,0,6"/>
                <CheckBox Name="chkBenchmark" Content="Benchmark mode" Margin="0,0,0,6" Visibility="Collapsed"/>
                <StackPanel Name="pnlModelSelect" Margin="0,6,0,0">
                    <TextBlock Text="Model (Sandbox)" Style="{StaticResource LabelStyle}"/>
                    <ComboBox Name="cmbModel"/>
                </StackPanel>
                <Grid>
                    <Grid.ColumnDefinitions>
                        <ColumnDefinition Width="*"/>
                        <ColumnDefinition Width="12"/>
                        <ColumnDefinition Width="*"/>
                    </Grid.ColumnDefinitions>
                    <StackPanel Grid.Column="0">
                        <TextBlock Text="Width" Style="{StaticResource LabelStyle}"/>
                        <TextBox Name="txtWidth" Text="1280"/>
                    </StackPanel>
                    <StackPanel Grid.Column="2">
                        <TextBlock Text="Height" Style="{StaticResource LabelStyle}"/>
                        <TextBox Name="txtHeight" Text="720"/>
                    </StackPanel>
                </Grid>
            </StackPanel>
        </Border>

        <!-- Dev Tools -->
        <Border Grid.Row="2" Grid.Column="2" Background="{StaticResource SurfaceBrush}"
                CornerRadius="8" Padding="16,12" Margin="0,0,0,12">
            <StackPanel>
                <TextBlock Text="DEV TOOLS" FontSize="12" FontWeight="SemiBold"
                           Foreground="{StaticResource AccentBrush}" Margin="0,0,0,10"/>
                <StackPanel Margin="0,0,0,10">
                    <TextBlock Text="Log Level" Style="{StaticResource LabelStyle}"/>
                    <ComboBox Name="cmbLogLevel">
                        <ComboBoxItem Content="Error" Tag="error"/>
                        <ComboBoxItem Content="Info" Tag="info"/>
                        <ComboBoxItem Content="Debug" Tag="debug"/>
                        <ComboBoxItem Content="Verbose" Tag="verbose" IsSelected="True"/>
                        <ComboBoxItem Content="Trace" Tag="trace"/>
                    </ComboBox>
                </StackPanel>
                <CheckBox Name="chkLogToFile" Content="Save log to file" Margin="0,0,0,6"/>
                <CheckBox Name="chkD3D12Debug" Content="D3D12 Debug Layer (validates API usage)" Margin="0,0,0,6"/>
            </StackPanel>
        </Border>

        <!-- Status + Command Preview -->
        <StackPanel Grid.Row="4" Grid.ColumnSpan="3" VerticalAlignment="Bottom" Margin="0,0,0,12">
            <TextBlock Name="txtStatus" Text="" FontSize="12"
                       Foreground="#A6ADC8" Margin="0,0,0,4"
                       TextWrapping="Wrap"/>
            <TextBlock Name="txtCmdPreview" Text="" FontSize="11"
                       Foreground="#45475A" Margin="0"
                       TextWrapping="Wrap" FontFamily="Consolas"/>
            <!-- Build output log -->
            <Border Name="pnlBuildOutput" Background="#0D1117" CornerRadius="4"
                    Margin="0,8,0,0" Padding="2" MaxHeight="200"
                    Visibility="Collapsed">
                <ScrollViewer Name="svBuildOutput" VerticalScrollBarVisibility="Auto">
                    <TextBlock Name="txtBuildOutput" FontFamily="Consolas" FontSize="11"
                               Foreground="#C9D1D9" TextWrapping="Wrap" Padding="6"/>
                </ScrollViewer>
            </Border>
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
            <Grid Grid.Column="0">
                <Grid.RowDefinitions>
                    <RowDefinition Height="*"/>
                    <RowDefinition Height="2"/>
                    <RowDefinition Height="*"/>
                </Grid.RowDefinitions>
                <Button Grid.Row="0" Name="btnRebuild" Content="REBUILD" Height="23"
                        FontSize="11" FontWeight="Bold" Cursor="Hand"
                        Background="#E8D9A0" Foreground="#1E1E2E"
                        BorderThickness="0">
                    <Button.Resources>
                        <Style TargetType="Border">
                            <Setter Property="CornerRadius" Value="6,6,0,0"/>
                        </Style>
                    </Button.Resources>
                </Button>
                <Button Grid.Row="2" Name="btnBuild" Content="BUILD" Height="23"
                        FontSize="13" FontWeight="Bold" Cursor="Hand"
                        Background="#F9E2AF" Foreground="#1E1E2E"
                        BorderThickness="0">
                    <Button.Resources>
                        <Style TargetType="Border">
                            <Setter Property="CornerRadius" Value="0,0,6,6"/>
                        </Style>
                    </Button.Resources>
                </Button>
            </Grid>
            <Button Grid.Column="2" Name="btnRun" Content="&#x25B6;  RUN" Height="48"
                    FontSize="18" FontWeight="Bold" Cursor="Hand"
                    Background="{StaticResource GreenBrush}" Foreground="#1E1E2E"
                    BorderThickness="0">
                <Button.Resources>
                    <Style TargetType="Border">
                        <Setter Property="CornerRadius" Value="6"/>
                    </Style>
                </Button.Resources>
            </Button>
            <Button Grid.Column="4" Name="btnEditor" Content="&#x270E;  EDITOR" Height="48"
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
$cmbArch        = $window.FindName("cmbArch")
$cmbConfig      = $window.FindName("cmbConfig")
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
$chkCullDisabled = $window.FindName("chkCullDisabled")
$chkBenchmark   = $window.FindName("chkBenchmark")
$cmbModel       = $window.FindName("cmbModel")
$pnlModelSelect = $window.FindName("pnlModelSelect")
$txtWidth       = $window.FindName("txtWidth")
$txtHeight      = $window.FindName("txtHeight")
$txtStatus      = $window.FindName("txtStatus")
$txtCmdPreview  = $window.FindName("txtCmdPreview")
$pnlBuildOutput = $window.FindName("pnlBuildOutput")
$svBuildOutput  = $window.FindName("svBuildOutput")
$txtBuildOutput = $window.FindName("txtBuildOutput")
$btnBuild       = $window.FindName("btnBuild")
$btnRebuild     = $window.FindName("btnRebuild")
$btnRun         = $window.FindName("btnRun")
$btnEditor      = $window.FindName("btnEditor")
$cmbLogLevel    = $window.FindName("cmbLogLevel")
$chkLogToFile   = $window.FindName("chkLogToFile")
$chkD3D12Debug  = $window.FindName("chkD3D12Debug")

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

# ── Config load/save ──

function Load-Config {
    if (-not (Test-Path $configPath)) { return }
    try {
        $cfg = Get-Content $configPath -Raw | ConvertFrom-Json

        # Architecture
        foreach ($item in $cmbArch.Items) {
            if ($item.Content -ieq $cfg.architecture) {
                $cmbArch.SelectedItem = $item; break
            }
        }
        # Configuration
        foreach ($item in $cmbConfig.Items) {
            if ($item.Content -ieq $cfg.configuration) {
                $cmbConfig.SelectedItem = $item; break
            }
        }
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
        if ($cfg.PSObject.Properties['cullDisabled']) {
            $chkCullDisabled.IsChecked = [bool]$cfg.cullDisabled
        } elseif ($cfg.devTools -and $cfg.devTools.PSObject.Properties['cullDisabled']) {
            $chkCullDisabled.IsChecked = [bool]$cfg.devTools.cullDisabled
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
        # Dev Tools
        if ($cfg.devTools) {
            if ($cfg.devTools.PSObject.Properties['logLevel']) {
                foreach ($item in $cmbLogLevel.Items) {
                    if ($item.Tag -ieq $cfg.devTools.logLevel) {
                        $cmbLogLevel.SelectedItem = $item; break
                    }
                }
            }
            if ($cfg.devTools.PSObject.Properties['logToFile']) {
                $chkLogToFile.IsChecked = [bool]$cfg.devTools.logToFile
            }
            if ($cfg.devTools.PSObject.Properties['d3d12Debug']) {
                $chkD3D12Debug.IsChecked = [bool]$cfg.devTools.d3d12Debug
            }
        }
    } catch {
        # Silently ignore corrupt config — defaults will be used
    }
}

function Save-Config {
    $sceneTag = ($cmbScene.SelectedItem).Tag.ToString()
    $cfg = @{
        architecture  = ($cmbArch.SelectedItem).Content.ToString().ToLower()
        configuration = ($cmbConfig.SelectedItem).Content.ToString()
        api           = ($cmbApi.SelectedItem).Tag.ToString()
        display = @{
            width      = [int]$txtWidth.Text
            height     = [int]$txtHeight.Text
            fullscreen = [bool]$chkFullscreen.IsChecked
            scene      = [int]$sceneTag
            model      = if ($cmbModel.SelectedItem) { ($cmbModel.SelectedItem).Tag.ToString() } else { "Models/DamagedHelmet.glb" }
        }
        debugFrames = [bool]$chkDebugFrames.IsChecked
        benchmark = ($sceneTag -eq "1" -and [bool]$chkBenchmark.IsChecked)
        cullDisabled = [bool]$chkCullDisabled.IsChecked
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
        devTools = @{
            logLevel  = ($cmbLogLevel.SelectedItem).Tag.ToString()
            logToFile = [bool]$chkLogToFile.IsChecked
            d3d12Debug = [bool]$chkD3D12Debug.IsChecked
        }
    }
    $cfg | ConvertTo-Json -Depth 3 | Set-Content $configPath -Encoding UTF8
}

# ── Helpers ──

function Patch-GLDriverConfig {
    param([string]$ApiTag)
    $configH = Join-Path $rootDir "Framework\Config.h"
    if (-not (Test-Path $configH)) {
        return "Config.h not found at $configH"
    }
    $content = Get-Content $configH -Raw
    $desired = switch ($ApiTag) {
        "gl"   { "OGL" }
        default { $null }
    }
    if (-not $desired) { return $null }  # D3D11 — no GL config change needed
    if ($content -match '#define\s+GL_DRIVER_SELECTED\s+(\w+)') {
        $current = $Matches[1]
        if ($current -ne $desired) {
            $content = $content -replace '#define\s+GL_DRIVER_SELECTED\s+\w+', "#define GL_DRIVER_SELECTED $desired"
            Set-Content $configH $content -NoNewline -Encoding UTF8
            return "Patched Config.h: GL_DRIVER_SELECTED $current -> $desired"
        } else {
            return "Config.h already set to GL_DRIVER_SELECTED $desired"
        }
    }
    return "Could not find GL_DRIVER_SELECTED in Config.h"
}

function Test-MSBuildSupportsPlatform {
    param(
        [Parameter(Mandatory = $true)] [string]$MSBuildPath,
        [Parameter(Mandatory = $true)] [string]$TargetPlatform
    )

    if (-not (Test-Path $MSBuildPath)) { return $false }
    if ($TargetPlatform -ne "ARM64") { return $true }

    $installRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MSBuildPath)))
    $vcRoot = Join-Path $installRoot "VC\Tools\MSVC"
    if (-not (Test-Path $vcRoot)) { return $false }

    $arm64Compiler = Get-ChildItem -Path $vcRoot -Recurse -Filter cl.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\bin\\Hostx64\\arm64\\cl\.exe$' } |
        Select-Object -First 1
    return [bool]$arm64Compiler
}

function Find-MSBuild {
    param([string]$TargetPlatform = "x64")

    $progX86 = [System.Environment]::GetFolderPath("ProgramFilesX86")
    $progFiles = [System.Environment]::GetFolderPath("ProgramFiles")

    $preferred = if ($TargetPlatform -eq "ARM64") {
        @(
            (Join-Path $progFiles "Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path $progFiles "Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path $progFiles "Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path $progX86  "Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe")
        )
    } else {
        @(
            (Join-Path $progX86  "Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path $progFiles "Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path $progFiles "Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path $progFiles "Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe")
        )
    }

    $candidates = $preferred + @(
        (Join-Path $progX86  "Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe"),
        (Join-Path $progX86  "Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe")
    )
    foreach ($c in $candidates) {
        if (Test-MSBuildSupportsPlatform -MSBuildPath $c -TargetPlatform $TargetPlatform) { return $c }
    }
    # Fallback: vswhere
    $vswhere = Join-Path $progX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPaths = & $vswhere -all -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        foreach ($vsPath in $vsPaths) {
            $candidate = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-MSBuildSupportsPlatform -MSBuildPath $candidate -TargetPlatform $TargetPlatform) { return $candidate }
        }
    }
    return $null
}

function Get-LaunchCommand {
    $arch   = ($cmbArch.SelectedItem).Content.ToString().ToLower()
    $config = ($cmbConfig.SelectedItem).Content.ToString()
    $apiTag = ($cmbApi.SelectedItem).Tag.ToString()

    $archFolder = switch ($arch) {
        "arm64" { "arm64" }
        "x86"   { "x86"   }
        default { "x64"   }
    }

    $exePath = Join-Path $rootDir "bin\$archFolder\$config\DayScene.exe"
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

    if ($sceneTag -eq "1" -and $chkBenchmark.IsChecked) {
        $argList += "--benchmark"
    }

    if ($chkCullDisabled.IsChecked) {
        $argList += "--cullDisabled"
    }

    # Model path (for Sandbox scene)
    if ($cmbModel.SelectedItem) {
        $argList += @("--model", ($cmbModel.SelectedItem).Tag.ToString())
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
        $apiTag = ($cmbApi.SelectedItem).Tag.ToString()
        $ts = Get-Date -Format "yyyyMMdd_HHmmss"
        $logFilename = "logs\T850_${ts}_${apiTag}.log"
        $argList += @("--logFile", $logFilename)
    }

    if ($chkD3D12Debug.IsChecked) {
        $argList += "--d3d12debug"
        # Force log to file when debug layer is active
        if (-not $chkLogToFile.IsChecked) {
            $apiTag = ($cmbApi.SelectedItem).Tag.ToString()
            $ts = Get-Date -Format "yyyyMMdd_HHmmss"
            $logFilename = "logs\T850_${ts}_${apiTag}_debug.log"
            $argList += @("--logFile", $logFilename)
        }
    }

    return @{
        ExePath = $exePath
        Args    = $argList
        Display = ('"' + $exePath + '" ' + ($argList -join ' '))
    }
}

function Get-EditorLaunchCommand {
    $arch   = ($cmbArch.SelectedItem).Content.ToString().ToLower()
    $config = ($cmbConfig.SelectedItem).Content.ToString()
    $apiTag = ($cmbApi.SelectedItem).Tag.ToString()

    $archFolder = switch ($arch) {
        "arm64" { "arm64" }
        "x86"   { "x86"   }
        default { "x64"   }
    }

    $exePath = Join-Path $rootDir "bin\$archFolder\$config\T8ditor.exe"
    $argList = @()

    # Editor supports D3D12 and Vulkan only: D3D11/D3D12 -> D3D12, GL/Vulkan -> Vulkan
    $editorApi = if ($apiTag -eq "vulkan" -or $apiTag -eq "gl") { "vulkan" } else { "d3d12" }
    if ($editorApi -eq "vulkan") {
        $argList += @("--api", "vulkan")
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
        $logFilename = "logs\T8ditor_${ts}_${editorApi}.log"
        $argList += @("--logFile", $logFilename)
    }

    if ($chkD3D12Debug.IsChecked) {
        $argList += "--d3d12debug"
        if (-not $chkLogToFile.IsChecked) {
            $ts = Get-Date -Format "yyyyMMdd_HHmmss"
            $logFilename = "logs\T8ditor_${ts}_${editorApi}_debug.log"
            $argList += @("--logFile", $logFilename)
        }
    }

    return @{
        ExePath = $exePath
        Args    = $argList
        Display = ('"' + $exePath + '" ' + ($argList -join ' '))
    }
}

function Update-Preview {
    $cmd = Get-LaunchCommand
    $txtCmdPreview.Text = $cmd.Display

    $sceneOk = Test-Path $cmd.ExePath
    $editorCmd = Get-EditorLaunchCommand
    $editorOk = Test-Path $editorCmd.ExePath

    if ($sceneOk -and $editorOk) {
        $txtStatus.Text = "Ready to run (Scene + Editor)"
        $txtStatus.Foreground = $window.FindResource("GreenBrush")
        $btnRun.IsEnabled = $true
        $btnEditor.IsEnabled = $true
    } elseif ($sceneOk) {
        $txtStatus.Text = "Scene ready, Editor not found - build required"
        $txtStatus.Foreground = $window.FindResource("GreenBrush")
        $btnRun.IsEnabled = $true
        $btnEditor.IsEnabled = $false
    } else {
        $missing = @()
        if (-not $sceneOk) { $missing += "DayScene.exe" }
        if (-not $editorOk) { $missing += "T8ditor.exe" }
        $txtStatus.Text = "Not found: $($missing -join ', ') - build this configuration first"
        $txtStatus.Foreground = $window.FindResource("RedBrush")
        $btnRun.IsEnabled = $false
        $btnEditor.IsEnabled = $false
    }
}

function Update-SceneOptionVisibility {
    if (-not $cmbScene.SelectedItem) { return }
    $sceneTag = ($cmbScene.SelectedItem).Tag.ToString()
    $pnlModelSelect.Visibility = if ($sceneTag -eq "0") { "Visible" } else { "Collapsed" }
    $chkBenchmark.Visibility = if ($sceneTag -eq "1") { "Visible" } else { "Collapsed" }
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

$cmbArch.Add_SelectionChanged({ Populate-ModelList; Update-Preview })
$cmbConfig.Add_SelectionChanged({ Populate-ModelList; Update-Preview })
$cmbApi.Add_SelectionChanged({ Update-Preview })
$cmbScene.Add_SelectionChanged({
    Update-SceneOptionVisibility
    Update-Preview
})
$chkFullscreen.Add_Checked({ Update-Preview })
$chkFullscreen.Add_Unchecked({ Update-Preview })
$chkCullDisabled.Add_Checked({ Update-Preview })
$chkCullDisabled.Add_Unchecked({ Update-Preview })
$chkBenchmark.Add_Checked({ Update-Preview })
$chkBenchmark.Add_Unchecked({ Update-Preview })
$chkLogToFile.Add_Checked({ Update-Preview })
$chkLogToFile.Add_Unchecked({ Update-Preview })
$cmbLogLevel.Add_SelectionChanged({ Update-Preview })
$txtSeconds.Add_TextChanged({ Update-Preview })
$txtFrame.Add_TextChanged({ Update-Preview })
$txtWidth.Add_TextChanged({ Update-Preview })
$txtHeight.Add_TextChanged({ Update-Preview })

# Shared build function — $buildTarget is "Build" (incremental) or "Rebuild" (clean)
function Invoke-Build {
    param([string]$buildTarget = "Build")

    $arch   = ($cmbArch.SelectedItem).Content.ToString().ToLower()
    $config = ($cmbConfig.SelectedItem).Content.ToString()

    $platform = switch ($arch) {
        "arm64" { "ARM64" }
        "x86"   { "x86" }
        default { "x64" }
    }

    # Locate the build script
    $buildScript = Join-Path $rootDir "scripts\build.ps1"
    if (-not (Test-Path $buildScript)) {
        [System.Windows.MessageBox]::Show(
            ("Build script not found:" + "`n" + $buildScript),
            "T850 Launcher", "OK", "Error")
        return
    }

    # Disable buttons during build
    $btnBuild.IsEnabled   = $false
    $btnRebuild.IsEnabled = $false
    $btnRun.IsEnabled     = $false
    $btnBuild.Content     = "BUILDING..."

    # Show build output panel
    $txtBuildOutput.Text = ""
    $pnlBuildOutput.Visibility = [System.Windows.Visibility]::Visible

    $label = if ($buildTarget -eq "Rebuild") { "Rebuilding" } else { "Building" }
    $txtStatus.Text = "$label $config|$platform ..."
    $txtStatus.Foreground = $window.FindResource("AccentBrush")
    $window.Dispatcher.Invoke([Action]{}, [System.Windows.Threading.DispatcherPriority]::Background)

    Save-Config

    # Patch Config.h for GL driver selection (GLEW vs ANGLE)
    $apiTag = ($cmbApi.SelectedItem).Tag.ToString()
    $patchResult = Patch-GLDriverConfig -ApiTag $apiTag
    if ($patchResult) {
        $txtBuildOutput.Text += $patchResult + "`r`n"
    }

    # Find MSBuild
    $msbuild = Find-MSBuild -TargetPlatform $platform
    if (-not $msbuild) {
        $txtBuildOutput.Text = "ERROR: MSBuild not found. Install Visual Studio Build Tools."
        $txtStatus.Text = "Build failed - MSBuild not found"
        $txtStatus.Foreground = $window.FindResource("RedBrush")
        $btnBuild.IsEnabled = $true
        $btnRebuild.IsEnabled = $true
        $btnBuild.Content = "BUILD"
        Update-Preview
        return
    }

    # Start MSBuild as a process and read output asynchronously
    $slnPath = Join-Path $rootDir "T850.sln"
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $msbuild
    $msbArgs = '"{0}" /p:Configuration={1} /p:Platform={2} /t:{3} /v:minimal /m' -f $slnPath, $config, $platform, $buildTarget
    $psi.Arguments = $msbArgs
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $psi.CreateNoWindow = $true
    $psi.WorkingDirectory = $rootDir

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi

    $buildLog   = New-Object System.Text.StringBuilder
    $errorLines = New-Object System.Collections.Generic.List[string]

    try {
        $proc.Start() | Out-Null

        # Read stdout + stderr synchronously on UI thread with Dispatcher pumping
        $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
        $stderrTask = $proc.StandardError.ReadToEndAsync()

        # Pump the dispatcher while waiting for process to exit
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        while (-not $proc.HasExited) {
            $window.Dispatcher.Invoke([Action]{}, [System.Windows.Threading.DispatcherPriority]::Background)
            Start-Sleep -Milliseconds 100
            $elapsed = $sw.Elapsed.ToString("mm\:ss")
            $txtStatus.Text = "Building $config|$platform ... ($elapsed)"
        }
        $proc.WaitForExit()
        $sw.Stop()

        $stdout = $stdoutTask.Result
        $stderr = $stderrTask.Result

        $nl = [System.Environment]::NewLine
        $allOutput = ($stdout + $nl + $stderr).Trim()

        # Extract errors for display
        $allLines = $allOutput -split $nl
        $allLines | ForEach-Object {
            $line = $_.Trim()
            if ($line -match ": error ") {
                $errorLines.Add($line)
            }
        }

        # Trim output to last ~80 lines max for display
        if ($allLines.Count -gt 80) {
            $allLines = @("... (truncated, showing last 80 lines) ...") + ($allLines | Select-Object -Last 80)
        }
        $txtBuildOutput.Text = ($allLines -join $nl)
        $svBuildOutput.ScrollToEnd()

        $exitCode = $proc.ExitCode
        if ($exitCode -eq 0) {
            $txtStatus.Text = "Build succeeded - $config|$platform"
            $txtStatus.Foreground = $window.FindResource("GreenBrush")
            # Refresh model list — build creates symlinks to Models/ etc.
            Populate-ModelList
        } else {
            if ($errorLines.Count -gt 0) {
                $txtStatus.Text = "Build FAILED ($($errorLines.Count) error(s))"
                $txtStatus.Foreground = $window.FindResource("RedBrush")
            } else {
                $txtStatus.Text = "Build FAILED (exit code $exitCode)"
                $txtStatus.Foreground = $window.FindResource("RedBrush")
            }
        }
    } catch {
        $txtBuildOutput.Text = "Exception: $($_.Exception.Message)"
        $txtStatus.Text = "Build error"
        $txtStatus.Foreground = $window.FindResource("RedBrush")
    } finally {
        if ($proc -and -not $proc.HasExited) {
            try { $proc.Kill() } catch {}
        }
        $btnBuild.Content = "BUILD"
        $btnBuild.IsEnabled = $true
        $btnRebuild.IsEnabled = $true
        Update-Preview
    }
}

# BUILD button — incremental build
$btnBuild.Add_Click({ Invoke-Build -buildTarget "Build" })

# REBUILD button — clean rebuild
$btnRebuild.Add_Click({ Invoke-Build -buildTarget "Rebuild" })

# RUN button — launch the app with current settings (no dump override)
$btnRun.Add_Click({
    $cmd = Get-LaunchCommand
    if (-not (Test-Path $cmd.ExePath)) {
        [System.Windows.MessageBox]::Show(
            ("Executable not found:" + "`n" + $cmd.ExePath + "`n`n" + "Please build this configuration first."),
            "T850 Launcher", "OK", "Error")
        return
    }

    Save-Config

    $txtStatus.Text = "Running..."
    $txtStatus.Foreground = $window.FindResource("GreenBrush")
    $window.Dispatcher.Invoke([Action]{}, [System.Windows.Threading.DispatcherPriority]::Background)

    $workDir = Split-Path -Parent $cmd.ExePath
    Start-Process -FilePath $cmd.ExePath -ArgumentList $cmd.Args -WorkingDirectory $workDir

    $txtStatus.Text = "Process running"
    $txtStatus.Foreground = $window.FindResource("GreenBrush")
})

# EDITOR button — launch T8ditor with current graphics/resolution/log settings
$btnEditor.Add_Click({
    $cmd = Get-EditorLaunchCommand
    if (-not (Test-Path $cmd.ExePath)) {
        [System.Windows.MessageBox]::Show(
            ("Editor not found:" + "`n" + $cmd.ExePath + "`n`n" + "Please build this configuration first."),
            "T850 Launcher", "OK", "Error")
        return
    }

    Save-Config

    $txtStatus.Text = "Editor running..."
    $txtStatus.Foreground = $window.FindResource("AccentBrush")
    $window.Dispatcher.Invoke([Action]{}, [System.Windows.Threading.DispatcherPriority]::Background)

    $workDir = Split-Path -Parent $cmd.ExePath
    Start-Process -FilePath $cmd.ExePath -ArgumentList $cmd.Args -WorkingDirectory $workDir

    $txtStatus.Text = "Editor running"
    $txtStatus.Foreground = $window.FindResource("AccentBrush")
})

# ── Initialize ──

# Scan Models folder for .glb/.gltf files and populate the dropdown
function Populate-ModelList {
    $cmbModel.Items.Clear()
    $arch = ($cmbArch.SelectedItem).Content.ToString().ToLower()
    $config = ($cmbConfig.SelectedItem).Content.ToString()
    $archFolder = switch ($arch) { "arm64" { "arm64" }; "x86" { "x86" }; default { "x64" } }
    $modelsDir = Join-Path $rootDir "bin\$archFolder\$config\Models"
    if (Test-Path $modelsDir) {
        $files = Get-ChildItem $modelsDir -Filter "*.glb" | Sort-Object Name
        $files += Get-ChildItem $modelsDir -Filter "*.gltf" | Sort-Object Name
        foreach ($f in $files) {
            $item = New-Object System.Windows.Controls.ComboBoxItem
            $item.Content = $f.Name
            $item.Tag = "Models/" + $f.Name
            $cmbModel.Items.Add($item) | Out-Null
        }
    }
    # Select DamagedHelmet.glb by default, or first item
    $selected = $false
    foreach ($item in $cmbModel.Items) {
        if ($item.Content -eq "DamagedHelmet.glb") {
            $cmbModel.SelectedItem = $item; $selected = $true; break
        }
    }
    if (-not $selected -and $cmbModel.Items.Count -gt 0) {
        $cmbModel.SelectedIndex = 0
    }
}

Populate-ModelList
Load-Config
Update-SceneOptionVisibility
Update-Preview

$window.ShowDialog() | Out-Null
