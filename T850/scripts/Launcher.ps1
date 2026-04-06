# T850 Engine Launcher
# WPF GUI for launching DayScene — reads/writes config.json

Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase

$xaml = @"
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="T850 Engine Launcher" Height="520" Width="480"
        WindowStartupLocation="CenterScreen" ResizeMode="NoResize"
        Background="#1E1E2E" Foreground="#CDD6F4">
    <Window.Resources>
        <SolidColorBrush x:Key="AccentBrush" Color="#89B4FA"/>
        <SolidColorBrush x:Key="AccentHoverBrush" Color="#B4D0FB"/>
        <SolidColorBrush x:Key="SurfaceBrush" Color="#313244"/>
        <SolidColorBrush x:Key="Surface2Brush" Color="#45475A"/>
        <SolidColorBrush x:Key="TextBrush" Color="#CDD6F4"/>
        <SolidColorBrush x:Key="SubtextBrush" Color="#A6ADC8"/>
        <SolidColorBrush x:Key="GreenBrush" Color="#A6E3A1"/>
        <SolidColorBrush x:Key="RedBrush" Color="#F38BA8"/>

        <Style TargetType="ComboBox">
            <Setter Property="Background" Value="{StaticResource SurfaceBrush}"/>
            <Setter Property="Foreground" Value="{StaticResource TextBrush}"/>
            <Setter Property="BorderBrush" Value="{StaticResource Surface2Brush}"/>
            <Setter Property="BorderThickness" Value="1"/>
            <Setter Property="Padding" Value="8,6"/>
            <Setter Property="FontSize" Value="14"/>
            <Setter Property="Height" Value="36"/>
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

        <Style TargetType="TextBlock">
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
            <RowDefinition Height="*"/>
            <RowDefinition Height="Auto"/>
        </Grid.RowDefinitions>

        <!-- Header -->
        <StackPanel Grid.Row="0" Margin="0,0,0,20">
            <TextBlock Text="T850 ENGINE" FontSize="28" FontWeight="Bold"
                       Foreground="{StaticResource AccentBrush}" Margin="0"/>
            <TextBlock Text="Deferred Rendering Demo Launcher" FontSize="13"
                       Foreground="{StaticResource SubtextBrush}" Margin="0,2,0,0"/>
        </StackPanel>

        <!-- Build Configuration -->
        <Border Grid.Row="1" Background="{StaticResource SurfaceBrush}"
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
                        <TextBlock Text="Architecture"/>
                        <ComboBox Name="cmbArch">
                            <ComboBoxItem Content="x64" IsSelected="True"/>
                            <ComboBoxItem Content="x86"/>
                            <ComboBoxItem Content="ARM64"/>
                        </ComboBox>
                    </StackPanel>
                    <StackPanel Grid.Column="2">
                        <TextBlock Text="Configuration"/>
                        <ComboBox Name="cmbConfig">
                            <ComboBoxItem Content="Release" IsSelected="True"/>
                            <ComboBoxItem Content="Debug"/>
                        </ComboBox>
                    </StackPanel>
                </Grid>
            </StackPanel>
        </Border>

        <!-- Graphics API -->
        <Border Grid.Row="2" Background="{StaticResource SurfaceBrush}"
                CornerRadius="8" Padding="16,12" Margin="0,0,0,12">
            <StackPanel>
                <TextBlock Text="GRAPHICS API" FontSize="12" FontWeight="SemiBold"
                           Foreground="{StaticResource AccentBrush}" Margin="0,0,0,10"/>
                <ComboBox Name="cmbApi">
                    <ComboBoxItem Content="D3D11 (Direct3D 11)" IsSelected="True" Tag="d3d11"/>
                    <ComboBoxItem Content="OpenGL (Desktop GL 3.3)" Tag="gl"/>
                </ComboBox>
            </StackPanel>
        </Border>

        <!-- RT Dump Settings -->
        <Border Grid.Row="3" Background="{StaticResource SurfaceBrush}"
                CornerRadius="8" Padding="16,12" Margin="0,0,0,12">
            <StackPanel>
                <TextBlock Text="RENDER TARGET DUMP" FontSize="12" FontWeight="SemiBold"
                           Foreground="{StaticResource AccentBrush}" Margin="0,0,0,10"/>
                <CheckBox Name="chkDump" Content="Enable RT dump on run" Margin="0,0,0,10"/>
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
                            <TextBlock Text="Seconds"/>
                            <TextBox Name="txtSeconds" Text="5"/>
                        </StackPanel>
                        <StackPanel Grid.Column="2" Name="pnlFrame" IsEnabled="False">
                            <TextBlock Text="Frame Number"/>
                            <TextBox Name="txtFrame" Text="300"/>
                        </StackPanel>
                    </Grid>
                </StackPanel>
            </StackPanel>
        </Border>

        <!-- Resolution -->
        <Border Grid.Row="4" Background="{StaticResource SurfaceBrush}"
                CornerRadius="8" Padding="16,12" Margin="0,0,0,12">
            <StackPanel>
                <TextBlock Text="DISPLAY" FontSize="12" FontWeight="SemiBold"
                           Foreground="{StaticResource AccentBrush}" Margin="0,0,0,10"/>
                <Grid>
                    <Grid.ColumnDefinitions>
                        <ColumnDefinition Width="*"/>
                        <ColumnDefinition Width="12"/>
                        <ColumnDefinition Width="*"/>
                    </Grid.ColumnDefinitions>
                    <StackPanel Grid.Column="0">
                        <TextBlock Text="Width"/>
                        <TextBox Name="txtWidth" Text="1280"/>
                    </StackPanel>
                    <StackPanel Grid.Column="2">
                        <TextBlock Text="Height"/>
                        <TextBox Name="txtHeight" Text="720"/>
                    </StackPanel>
                </Grid>
            </StackPanel>
        </Border>

        <!-- Status + Command Preview -->
        <StackPanel Grid.Row="5" VerticalAlignment="Bottom" Margin="0,0,0,12">
            <TextBlock Name="txtStatus" Text="" FontSize="12"
                       Foreground="{StaticResource SubtextBrush}" Margin="0,0,0,4"
                       TextWrapping="Wrap"/>
            <TextBlock Name="txtCmdPreview" Text="" FontSize="11"
                       Foreground="{StaticResource Surface2Brush}" Margin="0"
                       TextWrapping="Wrap" FontFamily="Consolas"/>
        </StackPanel>

        <!-- Launch Button -->
        <Button Grid.Row="6" Name="btnLaunch" Content="LAUNCH" Height="44"
                FontSize="16" FontWeight="Bold" Cursor="Hand"
                Background="{StaticResource AccentBrush}" Foreground="#1E1E2E"
                BorderThickness="0">
            <Button.Resources>
                <Style TargetType="Border">
                    <Setter Property="CornerRadius" Value="6"/>
                </Style>
            </Button.Resources>
        </Button>
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
$pnlDumpOptions = $window.FindName("pnlDumpOptions")
$rbSeconds      = $window.FindName("rbSeconds")
$rbFrame        = $window.FindName("rbFrame")
$pnlSeconds     = $window.FindName("pnlSeconds")
$pnlFrame       = $window.FindName("pnlFrame")
$txtSeconds     = $window.FindName("txtSeconds")
$txtFrame       = $window.FindName("txtFrame")
$txtWidth       = $window.FindName("txtWidth")
$txtHeight      = $window.FindName("txtHeight")
$txtStatus      = $window.FindName("txtStatus")
$txtCmdPreview  = $window.FindName("txtCmdPreview")
$btnLaunch      = $window.FindName("btnLaunch")

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
    } catch {
        # Silently ignore corrupt config — defaults will be used
    }
}

function Save-Config {
    $cfg = @{
        architecture  = ($cmbArch.SelectedItem).Content.ToString().ToLower()
        configuration = ($cmbConfig.SelectedItem).Content.ToString()
        api           = ($cmbApi.SelectedItem).Tag.ToString()
        display = @{
            width  = [int]$txtWidth.Text
            height = [int]$txtHeight.Text
        }
        dump = @{
            enabled = [bool]$chkDump.IsChecked
            trigger = if ($rbFrame.IsChecked) { "frame" } else { "seconds" }
            seconds = [int]$txtSeconds.Text
            frame   = [int]$txtFrame.Text
        }
    }
    $cfg | ConvertTo-Json -Depth 3 | Set-Content $configPath -Encoding UTF8
}

# ── Helpers ──

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

    if ($chkDump.IsChecked) {
        if ($rbFrame.IsChecked) {
            $argList += "--dump-frame"
            $argList += $txtFrame.Text
        } else {
            $argList += "--dump-seconds"
            $argList += $txtSeconds.Text
        }
    }

    $w = $txtWidth.Text
    $h = $txtHeight.Text
    if ($w -and $h) {
        $argList += @("--width", $w, "--height", $h)
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

    if (Test-Path $cmd.ExePath) {
        $txtStatus.Text = "Ready to launch"
        $txtStatus.Foreground = $window.FindResource("GreenBrush")
        $btnLaunch.IsEnabled = $true
    } else {
        $txtStatus.Text = "Executable not found - build this configuration first"
        $txtStatus.Foreground = $window.FindResource("RedBrush")
        $btnLaunch.IsEnabled = $false
    }
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

$cmbArch.Add_SelectionChanged({ Update-Preview })
$cmbConfig.Add_SelectionChanged({ Update-Preview })
$cmbApi.Add_SelectionChanged({ Update-Preview })
$txtSeconds.Add_TextChanged({ Update-Preview })
$txtFrame.Add_TextChanged({ Update-Preview })
$txtWidth.Add_TextChanged({ Update-Preview })
$txtHeight.Add_TextChanged({ Update-Preview })

# Launch button
$btnLaunch.Add_Click({
    $cmd = Get-LaunchCommand
    if (-not (Test-Path $cmd.ExePath)) {
        [System.Windows.MessageBox]::Show(
            ("Executable not found:" + "`n" + $cmd.ExePath + "`n`n" + "Please build this configuration first."),
            "T850 Launcher", "OK", "Error")
        return
    }

    # Save settings to config.json before launching
    Save-Config

    $txtStatus.Text = "Launching..."
    $txtStatus.Foreground = $window.FindResource("AccentBrush")
    $window.Dispatcher.Invoke([Action]{}, [System.Windows.Threading.DispatcherPriority]::Background)

    $workDir = Split-Path -Parent $cmd.ExePath
    Start-Process -FilePath $cmd.ExePath -ArgumentList $cmd.Args -WorkingDirectory $workDir

    $txtStatus.Text = "Process launched"
    $txtStatus.Foreground = $window.FindResource("GreenBrush")
})

# ── Initialize ──

Load-Config
Update-Preview

$window.ShowDialog() | Out-Null
