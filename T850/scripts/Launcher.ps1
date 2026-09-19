# T850 Engine Launcher
# WPF GUI for launching DayScene — reads/writes config.json

Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase, System.Windows.Forms

$xaml = @"
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
    Title="T850 Engine Launcher" SizeToContent="Height" Width="920"
        MinWidth="640" MinHeight="480"
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
            <Setter Property="ContentTemplate">
                <Setter.Value>
                    <DataTemplate><TextBlock Text="{Binding}" TextWrapping="Wrap"/></DataTemplate>
                </Setter.Value>
            </Setter>
            <Setter Property="VerticalContentAlignment" Value="Center"/>
        </Style>

        <!-- Label style (keyed — does NOT bleed into ComboBox/CheckBox/Button) -->
        <Style x:Key="LabelStyle" TargetType="TextBlock">
            <Setter Property="Foreground" Value="{StaticResource SubtextBrush}"/>
            <Setter Property="FontSize" Value="13"/>
            <Setter Property="Margin" Value="0,0,0,4"/>
            <Setter Property="TextWrapping" Value="Wrap"/>
        </Style>

        <Style TargetType="RadioButton">
            <Setter Property="Foreground" Value="{StaticResource TextBrush}"/>
            <Setter Property="FontSize" Value="13"/>
            <Setter Property="Margin" Value="0,0,12,0"/>
            <Setter Property="VerticalContentAlignment" Value="Center"/>
        </Style>
    </Window.Resources>

    <Grid Name="launcherLayout" Margin="24,16,24,20">
        <Grid.RowDefinitions>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="*"/>
            <RowDefinition Height="Auto"/>
        </Grid.RowDefinitions>
    <TextBlock Name="txtDependencyStatus" Grid.Row="0" Visibility="Collapsed"
               TextWrapping="Wrap" FontSize="13" Padding="10,8" Margin="0,0,0,8"
               Background="{StaticResource Surface2Brush}" Foreground="{StaticResource TextBrush}"/>
    <ScrollViewer Name="svSettings" Grid.Row="1" VerticalScrollBarVisibility="Auto"
                  HorizontalScrollBarVisibility="Disabled"
                  CanContentScroll="False">
    <Grid Name="pnlSettings">
        <Grid.RowDefinitions>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="Auto"/>
        </Grid.RowDefinitions>

        <!-- Header -->
        <StackPanel Name="pnlLauncherHeader" Grid.Row="0" Margin="0,0,0,20">
            <StackPanel Orientation="Horizontal">
                <TextBlock Text="T850 ENGINE" FontSize="28" FontWeight="Bold"
                           Foreground="{StaticResource AccentBrush}" Margin="0"/>
                <Border Background="#F9E2AF" CornerRadius="4" Padding="8,2" Margin="12,4,0,0"
                        VerticalAlignment="Center">
                    <TextBlock Text="DEV" FontSize="11" FontWeight="Bold" Foreground="#1E1E2E"/>
                </Border>
            </StackPanel>
            <TextBlock Name="txtLauncherSubtitle" Text="Deferred Rendering Demo Launcher" FontSize="13"
                       Foreground="#A6ADC8" Margin="0,2,0,0"/>
        </StackPanel>

        <Grid Name="pnlSections" Grid.Row="1">
            <Grid.RowDefinitions>
                <RowDefinition Height="Auto"/>
                <RowDefinition Height="Auto"/>
            </Grid.RowDefinitions>
            <Grid.ColumnDefinitions>
                <ColumnDefinition Width="*"/>
                <ColumnDefinition Width="16"/>
                <ColumnDefinition Width="*"/>
            </Grid.ColumnDefinitions>
        <StackPanel Name="pnlPrimarySettings" Grid.Column="0">
        <!-- Build Configuration -->
        <Border Background="{StaticResource SurfaceBrush}"
                CornerRadius="8" Padding="16,12" Margin="0,0,0,12">
            <StackPanel>
                <TextBlock Text="BUILD CONFIGURATION" FontSize="12" FontWeight="SemiBold"
                           Foreground="{StaticResource AccentBrush}" Margin="0,0,0,10"/>
                <Grid>
                    <Grid.ColumnDefinitions>
                        <ColumnDefinition Width="*"/>
                        <ColumnDefinition Width="12"/>
                        <ColumnDefinition Width="*"/>
                        <ColumnDefinition Width="12"/>
                        <ColumnDefinition Width="*"/>
                    </Grid.ColumnDefinitions>
                    <StackPanel Grid.Column="0">
                        <TextBlock Text="Target" Style="{StaticResource LabelStyle}"/>
                        <ComboBox Name="cmbTarget">
                            <ComboBoxItem Content="Windows" Tag="windows" IsSelected="True"/>
                            <ComboBoxItem Content="Android" Tag="android"/>
                        </ComboBox>
                    </StackPanel>
                    <StackPanel Grid.Column="2">
                        <TextBlock Text="Architecture" Style="{StaticResource LabelStyle}"/>
                        <ComboBox Name="cmbArch">
                            <ComboBoxItem Content="x64" IsSelected="True"/>
                            <ComboBoxItem Content="x86"/>
                            <ComboBoxItem Content="ARM64"/>
                        </ComboBox>
                    </StackPanel>
                    <StackPanel Grid.Column="4">
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
        <Border Background="{StaticResource SurfaceBrush}"
                CornerRadius="8" Padding="16,12" Margin="0,0,0,12">
            <StackPanel>
                <TextBlock Text="GRAPHICS API" FontSize="12" FontWeight="SemiBold"
                           Foreground="{StaticResource AccentBrush}" Margin="0,0,0,10"/>
                <Button Name="btnCompileShaders" Content="Compile Shaders" Height="30" Margin="0,0,0,10"
                    FontSize="13" Background="{StaticResource Surface2Brush}" Foreground="{StaticResource TextBrush}"
                    BorderThickness="0" Cursor="Hand"
                    ToolTip="Compile all recorded permutations for every supported desktop API, including both WebGPU flows."/>
                <ComboBox Name="cmbApi">
                    <ComboBoxItem Content="D3D11 (Direct3D 11)" IsSelected="True" Tag="d3d11"/>
                    <ComboBoxItem Content="D3D12 (Direct3D 12)" Tag="d3d12"/>
                    <ComboBoxItem Content="WebGPU (Dawn/D3D12)" Tag="webgpu"/>
                    <ComboBoxItem Content="WebGPU + Browser (Emscripten)" Tag="webgpu-browser"/>
                    <ComboBoxItem Content="Vulkan" Tag="vulkan"/>
                    <ComboBoxItem Content="OpenGL (Desktop GL 3.3)" Tag="gl"/>
                </ComboBox>
                <StackPanel Margin="0,12,0,0">
                    <TextBlock Text="Post-process Mode" Style="{StaticResource LabelStyle}"/>
                    <ComboBox Name="cmbPostProcessMode">
                        <ComboBoxItem Content="Raster" Tag="raster" IsSelected="True"
                                      ToolTip="Use the authored graphics or clear fallback for every compute-capable render-graph pass."/>
                        <ComboBoxItem Content="Compute" Tag="compute"
                                      ToolTip="Use compute shaders for every supported render-graph compute pass, including Minecraft torch particles."/>
                    </ComboBox>
                </StackPanel>
                <StackPanel Name="pnlBrowser" Margin="0,12,0,0" Visibility="Collapsed">
                    <TextBlock Text="Browser" Style="{StaticResource LabelStyle}"/>
                    <ComboBox Name="cmbBrowser" IsEnabled="False">
                        <ComboBoxItem Content="System default" Tag="" IsSelected="True"/>
                    </ComboBox>
                </StackPanel>
                <StackPanel Name="pnlShaderFlow" Margin="0,12,0,0" Visibility="Collapsed">
                    <TextBlock Text="Shader Flow" Style="{StaticResource LabelStyle}"/>
                    <ComboBox Name="cmbShaderFlow" IsEnabled="False">
                        <ComboBoxItem Content="WGSL preferred (auto)" Tag="auto" IsSelected="True"
                                      ToolTip="Prefer WGSL; allow HLSL translation for missing WGSL sources and unnamed helpers."/>
                        <ComboBoxItem Content="SPIR-V (HLSL translation)" Tag="spirv"
                                      ToolTip="Use HLSL through glslang/SPIR-V and Tint/WGSL, with no source-language fallback."/>
                    </ComboBox>
                </StackPanel>
                <StackPanel Name="pnlAndroidDevice" Margin="0,12,0,0" Visibility="Collapsed">
                    <TextBlock Text="Android Device" Style="{StaticResource LabelStyle}"/>
                    <ComboBox Name="cmbAndroidDevice"/>
                    <TextBlock Name="txtAndroidDeviceStatus" Text="No Android devices" Style="{StaticResource LabelStyle}" Margin="0,6,0,0"/>
                </StackPanel>
            </StackPanel>
        </Border>

        <!-- Display -->
        <Border Background="{StaticResource SurfaceBrush}"
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
                            <ComboBoxItem Content="Quake3 Mock" Tag="2"/>
                            <ComboBoxItem Content="Ragdoll Editor" Tag="3"/>
                            <ComboBoxItem Content="Scene Template" Tag="4"/>
                            <ComboBoxItem Content="Voxel Streaming" Tag="5"/>
                            <ComboBoxItem Content="Minecraft" Tag="6"/>
                        </ComboBox>
                    </StackPanel>
                    <StackPanel Grid.Column="2" VerticalAlignment="Bottom">
                        <CheckBox Name="chkFullscreen" Content="Fullscreen" Margin="0,0,0,8"/>
                    </StackPanel>
                </Grid>
                <StackPanel Margin="0,0,0,8">
                    <TextBlock Text="Culling" Style="{StaticResource LabelStyle}"/>
                    <WrapPanel>
                        <RadioButton Name="rbCullingFull" Content="Enabled (Full on Load)" GroupName="CullingMode" IsChecked="True"/>
                        <RadioButton Name="rbCullingLazy" Content="Lazy" GroupName="CullingMode"/>
                        <RadioButton Name="rbCullingDisabled" Content="Disabled" GroupName="CullingMode"/>
                    </WrapPanel>
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
                        <TextBox Name="txtWidth" Text="1280"/>
                    </StackPanel>
                    <StackPanel Grid.Column="2">
                        <TextBlock Text="Height" Style="{StaticResource LabelStyle}"/>
                        <TextBox Name="txtHeight" Text="720"/>
                    </StackPanel>
                </Grid>
            </StackPanel>
        </Border>
        </StackPanel>
        <StackPanel Name="pnlSecondarySettings" Grid.Column="2">

        <!-- RT Dump Settings -->
        <Border Background="{StaticResource SurfaceBrush}"
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
                    <WrapPanel Margin="0,0,0,8">
                        <RadioButton Name="rbSeconds" Content="Dump at second"
                                     IsChecked="True" GroupName="DumpTrigger"/>
                        <RadioButton Name="rbFrame" Content="Dump at frame"
                                     GroupName="DumpTrigger"/>
                    </WrapPanel>
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

        <!-- Dev Tools -->
        <Border Background="{StaticResource SurfaceBrush}"
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
                <CheckBox Name="chkTelemetry" Content="Runtime telemetry JSON" Margin="0,0,0,6"/>
                <StackPanel Name="pnlTelemetryOptions" Margin="20,0,0,0">
                    <TextBlock Text="Telemetry frequency frames (0 = every frame)" Style="{StaticResource LabelStyle}"/>
                    <TextBox Name="txtTelemetryFrequency" Text="60"/>
                </StackPanel>
            </StackPanel>
        </Border>
        </StackPanel>
        </Grid>

        <!-- Status + Command Preview -->
        <StackPanel Grid.Row="2" VerticalAlignment="Bottom" Margin="0,0,0,12">
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

    </Grid>
    </ScrollViewer>

        <!-- Buttons -->
        <UniformGrid Name="pnlActions" Grid.Row="2" Columns="5" Margin="-6,8,-6,0">
            <UniformGrid.Resources>
                <Style TargetType="Button">
                    <Setter Property="ContentTemplate">
                        <Setter.Value>
                            <DataTemplate><TextBlock Text="{Binding}" TextWrapping="Wrap" TextAlignment="Center"/></DataTemplate>
                        </Setter.Value>
                    </Setter>
                </Style>
            </UniformGrid.Resources>
            <Grid Margin="6,4">
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
            <Button Name="btnRun" Content="&#x25B6;  RUN" Height="48" Margin="6,4"
                    FontSize="18" FontWeight="Bold" Cursor="Hand"
                    Background="{StaticResource GreenBrush}" Foreground="#1E1E2E"
                    BorderThickness="0">
                <Button.Resources>
                    <Style TargetType="Border">
                        <Setter Property="CornerRadius" Value="6"/>
                    </Style>
                </Button.Resources>
            </Button>
            <Button Name="btnDownloadAssets" Content="Download Assets" Height="48" Margin="6,4"
                    FontSize="16" FontWeight="Bold" Cursor="Hand"
                    Background="{StaticResource GreenBrush}" Foreground="#1E1E2E"
                    BorderThickness="0" IsEnabled="False">
                <Button.Resources>
                    <Style TargetType="Border">
                        <Setter Property="CornerRadius" Value="6"/>
                    </Style>
                </Button.Resources>
            </Button>
            <Button Name="btnBenchmarkMatrix" Content="Benchmark Matrix" Height="48" Margin="6,4"
                    FontSize="15" FontWeight="Bold" Cursor="Hand"
                    Background="{StaticResource Surface2Brush}" Foreground="#E0E0E0"
                    BorderThickness="0">
                <Button.Resources>
                    <Style TargetType="Border">
                        <Setter Property="CornerRadius" Value="6"/>
                    </Style>
                </Button.Resources>
            </Button>
            <Button Name="btnEditor" Content="&#x270E;  EDITOR" Height="48" Margin="6,4"
                    FontSize="18" FontWeight="Bold" Cursor="Hand"
                    Background="{StaticResource AccentBrush}" Foreground="#E0E0E0"
                    BorderThickness="0">
                <Button.Resources>
                    <Style TargetType="Border">
                        <Setter Property="CornerRadius" Value="6"/>
                    </Style>
                </Button.Resources>
            </Button>
        </UniformGrid>
    </Grid>
</Window>
"@

# Parse XAML
$reader = [System.Xml.XmlReader]::Create([System.IO.StringReader]::new($xaml))
$window = [System.Windows.Markup.XamlReader]::Load($reader)

function Initialize-LauncherWindow {
    param($Window, [System.Windows.Rect]$WorkArea = [System.Windows.SystemParameters]::WorkArea)
    $availableWidth = [Math]::Max(1.0, $WorkArea.Width - 24.0)
    $availableHeight = [Math]::Max(1.0, $WorkArea.Height - 24.0)
    $Window.MinWidth = [Math]::Min(640.0, $availableWidth)
    $Window.MinHeight = [Math]::Min(480.0, $availableHeight)
    $Window.MaxWidth = $availableWidth
    $Window.MaxHeight = $availableHeight
    $Window.Width = [Math]::Min(920.0, $availableWidth)
    $Window.SizeToContent = [System.Windows.SizeToContent]::Height
    $Window.Resources['LauncherWorkArea'] = $WorkArea
}

Initialize-LauncherWindow $window

function Update-LauncherLayout {
    param($Window, [double]$Width = $Window.ActualWidth)
    $sections = $Window.FindName('pnlSections')
    $secondary = $Window.FindName('pnlSecondarySettings')
    $narrow = $Width -lt 840
    [System.Windows.Controls.Grid]::SetColumn($secondary, $(if ($narrow) { 0 } else { 2 }))
    [System.Windows.Controls.Grid]::SetRow($secondary, $(if ($narrow) { 1 } else { 0 }))
    $sections.ColumnDefinitions[1].Width = [System.Windows.GridLength]::new($(if ($narrow) { 0 } else { 16 }))
    $sections.ColumnDefinitions[2].Width = if ($narrow) { [System.Windows.GridLength]::new(0) } else { [System.Windows.GridLength]::new(1, [System.Windows.GridUnitType]::Star) }
    $Window.FindName('pnlActions').Columns = if ($Width -ge 900) { 5 } elseif ($Width -ge 480) { 3 } else { 2 }
    $short = $Window.MaxHeight -le 600 -or ($Window.ActualHeight -gt 0 -and $Window.ActualHeight -le 600)
    $Window.FindName('launcherLayout').Margin = if ($short) { [System.Windows.Thickness]::new(16, 12, 16, 12) } else { [System.Windows.Thickness]::new(24, 16, 24, 20) }
    $Window.FindName('pnlLauncherHeader').Margin = [System.Windows.Thickness]::new(0, 0, 0, $(if ($short) { 12 } else { 20 }))
    $Window.FindName('txtLauncherSubtitle').Visibility = if ($short) { [System.Windows.Visibility]::Collapsed } else { [System.Windows.Visibility]::Visible }
}

function Get-LauncherWorkArea {
    param($Window)
    $source = [System.Windows.PresentationSource]::FromVisual($Window)
    if ($source -and $source.CompositionTarget) {
        $handle = [System.Windows.Interop.WindowInteropHelper]::new($Window).Handle
        $bounds = [System.Windows.Forms.Screen]::FromHandle($handle).WorkingArea
        $pixels = [System.Windows.Rect]::new($bounds.X, $bounds.Y, $bounds.Width, $bounds.Height)
        $transform = [System.Windows.Media.MatrixTransform]::new($source.CompositionTarget.TransformFromDevice)
        return $transform.TransformBounds($pixels)
    }
    return [System.Windows.SystemParameters]::WorkArea
}

function Update-LauncherScreen {
    param($Window, [System.Windows.Rect]$WorkArea = (Get-LauncherWorkArea $Window))
    if ($Window.WindowState -ne [System.Windows.WindowState]::Normal) { return }
    if ($Window.Resources['LauncherWorkArea'] -eq $WorkArea) { return }
    Initialize-LauncherWindow $Window $WorkArea
    Update-LauncherLayout $Window $Window.Width
}

$window.Add_SourceInitialized({ param($sender, $eventArgs) Update-LauncherScreen $sender })
$window.Add_LocationChanged({ param($sender, $eventArgs) Update-LauncherScreen $sender })
$window.Add_DpiChanged({ param($sender, $eventArgs) Update-LauncherScreen $sender })
$window.Add_SizeChanged({ param($sender, $eventArgs) Update-LauncherLayout $sender $eventArgs.NewSize.Width })
Update-LauncherLayout $window $window.Width

# Get controls
$cmbTarget      = $window.FindName("cmbTarget")
$cmbArch        = $window.FindName("cmbArch")
$cmbConfig      = $window.FindName("cmbConfig")
$cmbApi         = $window.FindName("cmbApi")
$btnCompileShaders = $window.FindName("btnCompileShaders")
$cmbPostProcessMode = $window.FindName("cmbPostProcessMode")
$pnlShaderFlow  = $window.FindName("pnlShaderFlow")
$cmbShaderFlow  = $window.FindName("cmbShaderFlow")
$cmbBrowser     = $window.FindName("cmbBrowser")
$pnlBrowser     = $window.FindName("pnlBrowser")
$pnlAndroidDevice = $window.FindName("pnlAndroidDevice")
$cmbAndroidDevice = $window.FindName("cmbAndroidDevice")
$txtAndroidDeviceStatus = $window.FindName("txtAndroidDeviceStatus")
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
$txtStatus      = $window.FindName("txtStatus")
$txtCmdPreview  = $window.FindName("txtCmdPreview")
$pnlBuildOutput = $window.FindName("pnlBuildOutput")
$svBuildOutput  = $window.FindName("svBuildOutput")
$txtBuildOutput = $window.FindName("txtBuildOutput")
$btnBuild       = $window.FindName("btnBuild")
$btnRebuild     = $window.FindName("btnRebuild")
$btnRun         = $window.FindName("btnRun")
$btnDownloadAssets = $window.FindName("btnDownloadAssets")
$btnBenchmarkMatrix = $window.FindName("btnBenchmarkMatrix")
$btnEditor      = $window.FindName("btnEditor")
$cmbLogLevel    = $window.FindName("cmbLogLevel")
$chkLogToFile   = $window.FindName("chkLogToFile")
$chkD3D12Debug  = $window.FindName("chkD3D12Debug")
$chkTelemetry   = $window.FindName("chkTelemetry")
$txtTelemetryFrequency = $window.FindName("txtTelemetryFrequency")

# Resolve the source root from script, compiled-launcher, or working-directory locations.
# Walking upward also supports a developer launcher copied beside bin/<arch>/<config> outputs.
$rootDir = $null
$rootCandidates = New-Object System.Collections.Generic.List[string]
if ($PSScriptRoot) { $rootCandidates.Add($PSScriptRoot) }
if ($MyInvocation.MyCommand.Path) {
    $rootCandidates.Add((Split-Path -Parent $MyInvocation.MyCommand.Path))
}
try {
    $processPath = [System.Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
    $processName = [System.IO.Path]::GetFileName($processPath)
    if ($processPath -and $processName -notin @("powershell.exe", "pwsh.exe")) {
        $rootCandidates.Add((Split-Path -Parent $processPath))
    }
} catch {}
$rootCandidates.Add((Get-Location).Path)

foreach ($candidate in $rootCandidates) {
    if (-not $candidate) { continue }
    $probe = $candidate
    for ($depth = 0; $depth -le 4 -and $probe; ++$depth) {
        if ((Test-Path (Join-Path $probe "T850.sln")) -and
            (Test-Path (Join-Path $probe "scripts\build.ps1"))) {
            $rootDir = $probe
            break
        }
        $parent = Split-Path -Parent $probe
        if (-not $parent -or $parent -eq $probe) { break }
        $probe = $parent
    }
    if ($rootDir) { break }
}

if (-not $rootDir) {
    [System.Windows.MessageBox]::Show(
        "Could not locate the T850 source root (T850.sln and scripts\build.ps1).",
        "T850 Launcher", "OK", "Error") | Out-Null
    exit 1
}

$configPath = Join-Path $rootDir "config.json"
$script:SceneDependencyCacheKey = ""
$script:SceneDependencyResult = @{ Ok = $true; Missing = @() }
$script:SuppressSceneDependencyValidation = $false
$script:LauncherInitializing = $true
$script:LauncherBusy = $false
$script:AssetDownloadInProgress = $false
$script:AndroidDeviceRefreshInProgress = $false
$script:SelectedAndroidDeviceSerial = ""
$script:CloudAssetStatus = $null
$modelCloudScript = Join-Path $rootDir "scripts\ModelCloud.ps1"
if (-not (Test-Path $modelCloudScript) -and $PSScriptRoot) { $modelCloudScript = Join-Path $PSScriptRoot "ModelCloud.ps1" }
if (Test-Path $modelCloudScript) { . $modelCloudScript }

function Get-TargetPlatform {
    if ($cmbTarget -and $cmbTarget.SelectedItem) { return ($cmbTarget.SelectedItem).Tag.ToString() }
    return "windows"
}

function Test-AndroidTarget {
    return (Get-TargetPlatform) -eq "android"
}

function Get-SandboxInputMode {
    if ($cmbSandboxInput -and $cmbSandboxInput.SelectedItem) { return $cmbSandboxInput.SelectedItem.Tag.ToString() }
    return "model"
}

function Get-ArchFolder {
    $arch = ($cmbArch.SelectedItem).Content.ToString().ToLower()
    switch ($arch) {
        "arm64" { "ARM64" }
        "x86"   { "Win32" }
        default { "x64" }
    }
}

function Get-WindowsRuntimeRoot {
    $config = ($cmbConfig.SelectedItem).Content.ToString()
    return (Join-Path $rootDir ("bin\{0}\{1}" -f (Get-ArchFolder), $config))
}

function Get-LauncherAssetRoot {
    return (Join-Path $rootDir "Assets")
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
    $assetsRoot = Join-Path $rootDir "Assets"
    if ($full.StartsWith($assetsRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return (Normalize-ResourcePath $full.Substring($assetsRoot.Length).TrimStart('\', '/'))
    }
    if ($full.StartsWith($rootDir, [System.StringComparison]::OrdinalIgnoreCase)) {
        return (Normalize-ResourcePath $full.Substring($rootDir.Length).TrimStart('\', '/'))
    }
    return $full
}

function Resolve-SceneAssetPath {
    param([string]$ResourcePath, [string]$RuntimeRoot)
    if (-not $ResourcePath) { return "" }
    if ([System.IO.Path]::IsPathRooted($ResourcePath)) {
        if (Test-Path $ResourcePath) { return $ResourcePath }
        return ""
    }
    $rel = (Normalize-ResourcePath $ResourcePath).Replace('/', '\')
    $candidates = @()
    if ($RuntimeRoot) {
        $candidates += (Join-Path $RuntimeRoot $rel)
        $candidates += (Join-Path (Join-Path $RuntimeRoot "Assets") $rel)
    }
    $candidates += (Join-Path (Join-Path $rootDir "Assets") $rel)
    $candidates += (Join-Path $rootDir $rel)
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) { return $candidate }
    }
    return ""
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
        return @{ Ok = $true; Missing = @(); ScenePath = ""; Required = @() }
    }
    $sceneTag = $cmbScene.SelectedItem.Tag.ToString()
    if (($sceneTag -ne "2") -and ($sceneTag -ne "4") -and (($sceneTag -ne "0") -or ((Get-SandboxInputMode) -ne "scene"))) {
        return @{ Ok = $true; Missing = @(); ScenePath = ""; Required = @() }
    }
    $scenePath = Get-SelectedSceneFilePath
    if (-not $scenePath -or -not (Test-Path $scenePath)) {
        return @{ Ok = $false; Missing = @("Selected .t8scene file"); ScenePath = $scenePath; Required = @() }
    }
    if (Test-AndroidTarget) {
        $filteredAssets = Join-Path $rootDir "android\app\build\generated\t850FilteredAssets"
        $runtimeRoot = if (Test-Path $filteredAssets) { $filteredAssets } else { Join-Path $rootDir "Assets" }
    } else {
        $runtimeRoot = Get-WindowsRuntimeRoot
    }
    $missing = @()
    $required = Get-SceneRequiredMeshes $scenePath
    foreach ($mesh in $required) {
        if (-not (Resolve-SceneAssetPath $mesh $runtimeRoot)) { $missing += $mesh }
    }
    return @{ Ok = ($missing.Count -eq 0); Missing = $missing; ScenePath = $scenePath; Required = $required }
}

function Show-SceneDependencyError {
    param($Result)
    if ($Result.Ok) { return $true }
    $message = "The selected scene cannot be launched because required files are missing:" + "`n`n" +
        (($Result.Missing | ForEach-Object { "- $_" }) -join "`n")
    [System.Windows.MessageBox]::Show($message, "T850 Launcher", "OK", "Error") | Out-Null
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
    $targetTag = if ($cmbTarget) { Get-ComboBoxItemKey $cmbTarget.SelectedItem } else { "" }
    $archTag = if ($cmbArch) { Get-ComboBoxItemKey $cmbArch.SelectedItem } else { "" }
    $configTag = if ($cmbConfig) { Get-ComboBoxItemKey $cmbConfig.SelectedItem } else { "" }
    $sceneTag = if ($cmbScene) { Get-ComboBoxItemKey $cmbScene.SelectedItem } else { "" }
    return (($targetTag, $archTag, $configTag, (Get-SandboxInputMode), $sceneTag, (Get-SelectedSceneFilePath)) -join "|")
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

function Get-AndroidRepoRoot {
    return (Split-Path -Parent $rootDir)
}

function Get-AndroidProjectRoot {
    return (Join-Path $rootDir "android")
}

function Get-AndroidBuildScript {
    return (Join-Path (Get-AndroidRepoRoot) "T850\scripts\android\BuildAndroid.bat")
}

function Get-AndroidGradleWrapperPath {
    return (Join-Path (Get-AndroidProjectRoot) "gradlew.bat")
}

function Get-AndroidAbiFilters {
    $arch = if ($cmbArch -and $cmbArch.SelectedItem) { ($cmbArch.SelectedItem).Content.ToString().ToLowerInvariant() } else { "arm64" }
    switch ($arch) {
        "x64" { return "x86_64" }
        "arm64" { return "arm64-v8a" }
        default { return "arm64-v8a" }
    }
}

function Get-AndroidVcpkgTriplets {
    $triplets = New-Object System.Collections.Generic.List[string]
    foreach ($abi in ((Get-AndroidAbiFilters) -split ",")) {
        switch ($abi.Trim()) {
            "x86_64" { if (-not $triplets.Contains("x64-android")) { $triplets.Add("x64-android") } }
            "arm64-v8a" { if (-not $triplets.Contains("arm64-android")) { $triplets.Add("arm64-android") } }
        }
    }
    return @($triplets)
}

function Test-VcpkgFiles {
    param(
        [Parameter(Mandatory = $true)] [string]$TripletRoot,
        [Parameter(Mandatory = $true)] [string[]]$RelativePaths
    )

    foreach ($relativePath in $RelativePaths) {
        if (-not (Test-Path (Join-Path $TripletRoot $relativePath))) { return $false }
    }
    return $true
}

function Test-AndroidVcpkgTripletReady {
    param(
        [Parameter(Mandatory = $true)] [string]$VcpkgRoot,
        [Parameter(Mandatory = $true)] [string]$Triplet
    )

    $tripletRoot = Join-Path $VcpkgRoot "installed\$Triplet"
    if (-not (Test-Path $tripletRoot)) { return $false }
    return (Test-VcpkgFiles -TripletRoot $tripletRoot -RelativePaths @(
        "include\Jolt\Jolt.h",
        "share\Jolt\JoltConfig.cmake",
        "include\imgui_impl_android.h",
        "include\imgui_impl_vulkan.h",
        "include\glslang\Public\ShaderLang.h",
        "include\draco\compression\encode.h"
    ))
}

function Get-AndroidAdbPath {
    $sdk = Get-AndroidSdkRoot
    return (Join-Path $sdk "platform-tools\adb.exe")
}

function Get-AndroidConfigName {
    return ($cmbConfig.SelectedItem).Content.ToString()
}

function Get-AndroidApkPath {
    $variant = if ((Get-AndroidConfigName) -ieq "Debug") { "debug" } else { "release" }
    return (Join-Path (Get-AndroidProjectRoot) "app\build\outputs\apk\$variant\app-$variant.apk")
}

function Get-SelectedAndroidDeviceSerial {
    if ($cmbAndroidDevice -and $cmbAndroidDevice.SelectedItem) {
        $serial = $cmbAndroidDevice.SelectedItem.Tag
        if ($serial) { return $serial.ToString() }
    }
    return ""
}

function Get-SelectedAndroidDeviceState {
    if ($cmbAndroidDevice -and $cmbAndroidDevice.SelectedItem) {
        $state = $cmbAndroidDevice.SelectedItem.DataContext
        if ($state) { return $state.ToString() }
    }
    return ""
}

function Get-AndroidAdbArguments {
    param([string[]]$CommandArguments)
    $serial = Get-SelectedAndroidDeviceSerial
    if ($serial) { return @("-s", $serial) + $CommandArguments }
    return $CommandArguments
}

function Invoke-AndroidAdbCapture {
    param([string[]]$CommandArguments)
    $adb = Get-AndroidAdbPath
    if (-not (Test-Path $adb)) {
        return [pscustomobject]@{ ExitCode = 1; Output = "adb.exe not found: $adb" }
    }

    $allArgs = Get-AndroidAdbArguments $CommandArguments
    $output = & $adb @allArgs 2>&1
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Output = (($output | ForEach-Object { $_.ToString() }) -join [System.Environment]::NewLine)
    }
}

function Test-AndroidPackageInstalled {
    if (-not (Test-AndroidDeviceReady)) { return $false }
    $result = Invoke-AndroidAdbCapture -CommandArguments @("shell", "pm", "path", "com.t850.engine")
    if ($result.ExitCode -eq 0 -and $result.Output -match "package:") { return $true }

    [System.Windows.MessageBox]::Show(
        ("T850 is not installed on the selected Android device." + "`n`n" + "Click Install first to install the APK, then use Deploy to run it."),
        "T850 Launcher", "OK", "Warning")
    return $false
}

function Test-AndroidDeviceReady {
    $serial = Get-SelectedAndroidDeviceSerial
    if (-not $serial) {
        [System.Windows.MessageBox]::Show("No Android device is selected.", "T850 Launcher", "OK", "Error")
        return $false
    }
    $state = Get-SelectedAndroidDeviceState
    if ($state -ne "device") {
        [System.Windows.MessageBox]::Show(("Selected Android device is not ready:" + "`n" + $serial + " (" + $state + ")"), "T850 Launcher", "OK", "Error")
        return $false
    }
    return $true
}

function Invoke-AdbDevices {
    $adb = Get-AndroidAdbPath
    if (-not (Test-Path $adb)) {
        return @{ Error = "adb.exe not found"; Devices = @() }
    }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $adb
    $psi.Arguments = "devices -l"
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    try {
        $proc.Start() | Out-Null
        if (-not $proc.WaitForExit(1500)) {
            try { $proc.Kill() } catch {}
            return @{ Error = "adb devices timed out"; Devices = @() }
        }
        $stdout = $proc.StandardOutput.ReadToEnd()
        $stderr = $proc.StandardError.ReadToEnd()
        if ($proc.ExitCode -ne 0) {
            $message = if ($stderr.Trim()) { $stderr.Trim() } else { "adb devices failed" }
            return @{ Error = $message; Devices = @() }
        }

        $devices = @()
        foreach ($line in ($stdout -split "\r?\n")) {
            $trimmed = $line.Trim()
            if (-not $trimmed -or $trimmed -like "List of devices*") { continue }
            $parts = $trimmed -split "\s+", 3
            if ($parts.Count -lt 2) { continue }
            $serial = $parts[0]
            $state = $parts[1]
            $details = if ($parts.Count -ge 3) { $parts[2] } else { "" }
            $model = ""
            if ($details -match "model:([^\s]+)") { $model = $Matches[1] }
            $device = ""
            if ($details -match "device:([^\s]+)") { $device = $Matches[1] }
            $labelParts = @($serial, "($state)")
            if ($model) { $labelParts += $model }
            elseif ($device) { $labelParts += $device }
            $devices += [pscustomobject]@{
                Serial = $serial
                State = $state
                Label = ($labelParts -join " ")
            }
        }
        return @{ Error = ""; Devices = $devices }
    } finally {
        if ($proc -and -not $proc.HasExited) {
            try { $proc.Kill() } catch {}
        }
    }
}

function Refresh-AndroidDevices {
    if ($script:AndroidDeviceRefreshInProgress) { return }
    $script:AndroidDeviceRefreshInProgress = $true
    try {
        $previousSerial = Get-SelectedAndroidDeviceSerial
        if (-not $previousSerial) { $previousSerial = $script:SelectedAndroidDeviceSerial }
        $result = Invoke-AdbDevices
        $devices = @($result.Devices)

        $cmbAndroidDevice.Items.Clear()
        foreach ($device in $devices) {
            $item = New-Object System.Windows.Controls.ComboBoxItem
            $item.Content = $device.Label
            $item.Tag = $device.Serial
            $item.DataContext = $device.State
            $item.IsEnabled = ($device.State -eq "device")
            $cmbAndroidDevice.Items.Add($item) | Out-Null
        }

        $selected = $false
        foreach ($item in $cmbAndroidDevice.Items) {
            if ($item.Tag -eq $previousSerial) {
                $cmbAndroidDevice.SelectedItem = $item
                $selected = $true
                break
            }
        }
        if (-not $selected) {
            foreach ($item in $cmbAndroidDevice.Items) {
                if ($item.DataContext -eq "device") {
                    $cmbAndroidDevice.SelectedItem = $item
                    $selected = $true
                    break
                }
            }
        }
        if (-not $selected -and $cmbAndroidDevice.Items.Count -gt 0) {
            $cmbAndroidDevice.SelectedIndex = 0
        }

        $script:SelectedAndroidDeviceSerial = Get-SelectedAndroidDeviceSerial
        $readyCount = @($devices | Where-Object { $_.State -eq "device" }).Count
        if ($result.Error) {
            $txtAndroidDeviceStatus.Text = $result.Error
            $txtAndroidDeviceStatus.Foreground = $window.FindResource("RedBrush")
        } elseif ($devices.Count -eq 0) {
            $txtAndroidDeviceStatus.Text = "No Android devices connected"
            $txtAndroidDeviceStatus.Foreground = $window.FindResource("RedBrush")
        } elseif ($readyCount -eq 0) {
            $txtAndroidDeviceStatus.Text = "$($devices.Count) device(s), none ready"
            $txtAndroidDeviceStatus.Foreground = $window.FindResource("RedBrush")
        } else {
            $txtAndroidDeviceStatus.Text = "$readyCount ready / $($devices.Count) connected"
            $txtAndroidDeviceStatus.Foreground = $window.FindResource("GreenBrush")
        }
    } finally {
        $script:AndroidDeviceRefreshInProgress = $false
    }
}

function Append-BuildOutput {
    param([string]$Line)
    if ($pnlBuildOutput.Visibility -ne [System.Windows.Visibility]::Visible) {
        $pnlBuildOutput.Visibility = [System.Windows.Visibility]::Visible
    }
    $txtBuildOutput.Text += $Line + [System.Environment]::NewLine
    $svBuildOutput.ScrollToEnd()
    $window.Dispatcher.Invoke([Action]{}, [System.Windows.Threading.DispatcherPriority]::Background)
}

function Invoke-LoggedProcess {
    param(
        [Parameter(Mandatory = $true)] [string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = $rootDir,
        [string]$StatusPrefix = "Running"
    )

    $quotedFile = '"' + $FilePath + '"'
    $quotedArgs = @()
    foreach ($arg in $Arguments) {
        if ($arg -match '[\s",]') { $quotedArgs += ('"' + ($arg -replace '"', '\"') + '"') }
        else { $quotedArgs += $arg }
    }
    $commandLine = ($quotedFile + ' ' + ($quotedArgs -join ' ')).Trim() + ' 2>&1'

    Append-BuildOutput ("> " + $commandLine)

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "cmd.exe"
    $psi.Arguments = "/d /s /c " + ('"' + $commandLine + '"')
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $false
    $psi.CreateNoWindow = $true
    if ([IO.Path]::GetFileName($FilePath) -ieq 'powershell.exe') {
        $psi.EnvironmentVariables.Remove('PSModulePath')
    }

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        $proc.Start() | Out-Null
        while (-not $proc.StandardOutput.EndOfStream) {
            $line = $proc.StandardOutput.ReadLine()
            Append-BuildOutput $line
            $txtStatus.Text = "$StatusPrefix ... ($($sw.Elapsed.ToString('mm\:ss')))"
        }
        $proc.WaitForExit()
        return $proc.ExitCode
    } finally {
        $sw.Stop()
        if ($proc -and -not $proc.HasExited) {
            try { $proc.Kill() } catch {}
        }
    }
}

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
    $cmbPostProcessMode.SelectedIndex = 0
    $cmbShaderFlow.SelectedIndex = 0
    Update-InstalledBrowsers
    $cmbBrowser.SelectedIndex = 0
    if (-not (Test-Path $configPath)) { return }
    try {
        $cfg = Get-Content $configPath -Raw | ConvertFrom-Json

        # Target platform
        if ($cfg.PSObject.Properties['targetPlatform']) {
            foreach ($item in $cmbTarget.Items) {
                if ($item.Tag -ieq $cfg.targetPlatform) {
                    $cmbTarget.SelectedItem = $item; break
                }
            }
        }
        if ($cfg.PSObject.Properties['androidDeviceSerial']) {
            $script:SelectedAndroidDeviceSerial = $cfg.androidDeviceSerial.ToString()
        }

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
        foreach ($item in $cmbPostProcessMode.Items) {
            if ($item.Tag -ieq $cfg.postProcessMode) {
                $cmbPostProcessMode.SelectedItem = $item; break
            }
        }
        foreach ($item in $cmbShaderFlow.Items) {
            if ($item.Tag -ieq $cfg.webgpuShaderFlow) {
                $cmbShaderFlow.SelectedItem = $item; break
            }
        }
        foreach ($item in $cmbBrowser.Items) {
            if ($item.Tag -ieq $cfg.webBrowser) {
                $cmbBrowser.SelectedItem = $item; break
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
                    if ($item.Tag -eq $cfg.display.sceneFile -or $item.Tag -eq (Resolve-SceneAssetPath $cfg.display.sceneFile (Get-WindowsRuntimeRoot))) {
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
        $display.sceneFile = Get-SelectedSceneFilePath
    } else {
        $display.model = if ($cmbModel.SelectedItem) { ($cmbModel.SelectedItem).Tag.ToString() } else { "Models/DamagedHelmet.glb" }
    }
    $cfg = @{
        targetPlatform = (Get-TargetPlatform)
        androidDeviceSerial = (Get-SelectedAndroidDeviceSerial)
        architecture  = ($cmbArch.SelectedItem).Content.ToString().ToLower()
        configuration = ($cmbConfig.SelectedItem).Content.ToString()
        api           = if (Test-AndroidTarget) { "vulkan" } else { ($cmbApi.SelectedItem).Tag.ToString() }
        postProcessMode = $cmbPostProcessMode.SelectedItem.Tag.ToString()
        webgpuShaderFlow = $cmbShaderFlow.SelectedItem.Tag.ToString()
        webBrowser = [string]$cmbBrowser.SelectedItem.Tag
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
        devTools = @{
            logLevel  = ($cmbLogLevel.SelectedItem).Tag.ToString()
            logToFile = [bool]$chkLogToFile.IsChecked
            d3d12Debug = [bool]$chkD3D12Debug.IsChecked
        }
        telemetry = @{
            enabled = [bool]$chkTelemetry.IsChecked
            frequencyFrames = [int]$txtTelemetryFrequency.Text
            outputPath = "logs\perf_telemetry.json"
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

function Find-PowerShellHost {
    $windowsPowerShell = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
    if (Test-Path $windowsPowerShell) { return $windowsPowerShell }

    $pwsh = Get-Command "pwsh.exe" -ErrorAction SilentlyContinue
    if ($pwsh) { return $pwsh.Source }

    $powershell = Get-Command "powershell.exe" -ErrorAction SilentlyContinue
    if ($powershell) { return $powershell.Source }

    return $null
}

function Test-CommandExists {
    param([string]$Name)
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Get-BuildWorkerCount {
    $workers = [Math]::Max(1, [Environment]::ProcessorCount - 1)
    if ($env:T850_BUILD_WORKERS) {
        $parsedWorkers = 0
        if ([int]::TryParse($env:T850_BUILD_WORKERS, [ref]$parsedWorkers) -and $parsedWorkers -gt 0) {
            $workers = $parsedWorkers
        }
    }
    return $workers
}

function Test-JavaExecutable17OrNewer {
    param([string]$JavaExe)
    if (-not $JavaExe -or -not (Test-Path $JavaExe)) { return $false }
    try {
        $versionLine = (& $JavaExe -version 2>&1 | Select-Object -First 1).ToString()
        $versionText = $null
        if ($versionLine -match 'version "([^"]+)"') { $versionText = $Matches[1] }
        elseif ($versionLine -match 'openjdk\s+([0-9][^\s]*)') { $versionText = $Matches[1] }
        if (-not $versionText) { return $false }

        if ($versionText -match '^1\.(\d+)') { return ([int]$Matches[1] -ge 17) }
        if ($versionText -match '^(\d+)') { return ([int]$Matches[1] -ge 17) }
    } catch {
        return $false
    }
    return $false
}

function Find-InstalledJavaHome {
    $candidates = New-Object System.Collections.Generic.List[string]
    if ($env:JAVA_HOME) { $candidates.Add($env:JAVA_HOME) }

    $programFiles = [System.Environment]::GetFolderPath("ProgramFiles")
    $programFilesX86 = [System.Environment]::GetFolderPath("ProgramFilesX86")
    $jdkRoots = @(
        (Join-Path $programFiles "Eclipse Adoptium"),
        (Join-Path $programFiles "Java"),
        (Join-Path $programFilesX86 "Eclipse Adoptium"),
        (Join-Path $programFilesX86 "Java")
    )
    foreach ($root in $jdkRoots) {
        if (-not (Test-Path $root)) { continue }
        Get-ChildItem -Path $root -Directory -Filter "jdk-*" -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { $candidates.Add($_.FullName) }
    }

    $androidStudioJbr = Join-Path $programFiles "Android\Android Studio\jbr"
    if (Test-Path $androidStudioJbr) { $candidates.Add($androidStudioJbr) }

    $seen = @{}
    foreach ($candidate in $candidates) {
        if (-not $candidate -or $seen.ContainsKey($candidate)) { continue }
        $seen[$candidate] = $true
        $javaExe = Join-Path $candidate "bin\java.exe"
        if (Test-JavaExecutable17OrNewer $javaExe) { return $candidate }
    }
    return $null
}

function Test-JavaAvailable {
    $javaHome = Find-InstalledJavaHome
    if ($javaHome) {
        $javaBin = Join-Path $javaHome "bin"
        $env:JAVA_HOME = $javaHome
        if (-not (($env:PATH -split ';') -contains $javaBin)) { $env:PATH = "$javaBin;$env:PATH" }
        return $true
    }

    $javaCommand = Get-Command "java.exe" -ErrorAction SilentlyContinue
    if ($javaCommand -and (Test-JavaExecutable17OrNewer $javaCommand.Source)) { return $true }
    return $false
}

function Test-GlslangAvailable {
    if (Test-CommandExists "glslangValidator.exe") { return $true }
    if ($env:VULKAN_SDK -and (Test-Path (Join-Path $env:VULKAN_SDK "Bin\glslangValidator.exe"))) { return $true }
    $vulkanRoot = "C:\VulkanSDK"
    if (Test-Path $vulkanRoot) {
        $latest = Get-ChildItem $vulkanRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            Select-Object -First 1
        if ($latest -and (Test-Path (Join-Path $latest.FullName "Bin\glslangValidator.exe"))) {
            $env:VULKAN_SDK = $latest.FullName
            $env:PATH = (Join-Path $latest.FullName "Bin") + ";" + $env:PATH
            return $true
        }
    }
    return $false
}

function Read-AndroidSigningProperties {
    $props = @{}
    $project = Get-AndroidProjectRoot
    $files = @(
        (Join-Path $project "signing.properties"),
        (Join-Path $project "app\signing.properties"),
        (Join-Path $env:USERPROFILE ".android\t850-release-signing.properties")
    )

    foreach ($file in $files) {
        if (-not (Test-Path $file)) { continue }
        foreach ($rawLine in (Get-Content $file -ErrorAction SilentlyContinue)) {
            $line = $rawLine.Trim()
            if (-not $line -or $line.StartsWith("#") -or $line.StartsWith("!") -or -not $line.Contains("=")) { continue }
            $idx = $line.IndexOf("=")
            $key = $line.Substring(0, $idx).Trim()
            $value = $line.Substring($idx + 1).Trim()
            if ($key) { $props[$key] = $value }
        }
    }
    return $props
}

function Get-AndroidSigningValue {
    param(
        [hashtable]$Properties,
        [string[]]$Names
    )

    foreach ($name in $Names) {
        $envValue = [System.Environment]::GetEnvironmentVariable($name)
        if (-not [string]::IsNullOrWhiteSpace($envValue)) { return $envValue }
        if ($Properties.ContainsKey($name) -and -not [string]::IsNullOrWhiteSpace($Properties[$name])) {
            return $Properties[$name]
        }
    }
    return $null
}

function Resolve-AndroidSigningStorePath {
    param([string]$StoreFile)

    if (-not $StoreFile) { return $StoreFile }
    if ([System.IO.Path]::IsPathRooted($StoreFile)) { return $StoreFile }

    $project = Get-AndroidProjectRoot
    $candidates = @(
        (Join-Path $project "app\$StoreFile"),
        (Join-Path $project $StoreFile),
        (Join-Path (Get-AndroidRepoRoot) $StoreFile)
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $candidates[0]
}

function Get-AndroidReleaseSigningStatus {
    $props = Read-AndroidSigningProperties
    $storeFile = Get-AndroidSigningValue -Properties $props -Names @("T850_RELEASE_STORE_FILE", "ANDROID_KEYSTORE_PATH")
    $storePassword = Get-AndroidSigningValue -Properties $props -Names @("T850_RELEASE_STORE_PASSWORD", "ANDROID_KEYSTORE_PASSWORD")
    $keyAlias = Get-AndroidSigningValue -Properties $props -Names @("T850_RELEASE_KEY_ALIAS", "ANDROID_KEY_ALIAS")
    $keyPassword = Get-AndroidSigningValue -Properties $props -Names @("T850_RELEASE_KEY_PASSWORD", "ANDROID_KEY_PASSWORD")

    $missing = New-Object System.Collections.Generic.List[string]
    if (-not $storeFile) { $missing.Add("ANDROID_KEYSTORE_PATH or T850_RELEASE_STORE_FILE") }
    if (-not $storePassword) { $missing.Add("ANDROID_KEYSTORE_PASSWORD or T850_RELEASE_STORE_PASSWORD") }
    if (-not $keyAlias) { $missing.Add("ANDROID_KEY_ALIAS or T850_RELEASE_KEY_ALIAS") }
    if (-not $keyPassword) { $missing.Add("ANDROID_KEY_PASSWORD or T850_RELEASE_KEY_PASSWORD") }

    $resolvedStoreFile = Resolve-AndroidSigningStorePath $storeFile
    if ($storeFile -and -not (Test-Path $resolvedStoreFile)) {
        $missing.Add("release keystore file: $storeFile")
    }

    return [pscustomobject]@{
        Ready = ($missing.Count -eq 0)
        Missing = @($missing)
        StoreFile = $resolvedStoreFile
    }
}

function Show-AndroidReleaseSigningWarning {
    param($SigningStatus)

    $message = "Android Release builds must be signed before Gradle can produce app-release.apk." +
        [System.Environment]::NewLine + [System.Environment]::NewLine +
        "Missing:" + [System.Environment]::NewLine +
        (($SigningStatus.Missing | ForEach-Object { "- $_" }) -join [System.Environment]::NewLine) +
        [System.Environment]::NewLine + [System.Environment]::NewLine +
        "Set the ANDROID_KEYSTORE_PATH, ANDROID_KEYSTORE_PASSWORD, ANDROID_KEY_ALIAS, and ANDROID_KEY_PASSWORD variables, or the T850_RELEASE_* equivalents, in the environment or in android\signing.properties, android\app\signing.properties, or ~/.android/t850-release-signing.properties."
    [System.Windows.MessageBox]::Show($message, "T850 Launcher", "OK", "Warning") | Out-Null
}

function Get-AndroidSdkRoot {
    if ($env:ANDROID_HOME) { return $env:ANDROID_HOME }
    if ($env:ANDROID_SDK_ROOT) { return $env:ANDROID_SDK_ROOT }
    return (Join-Path $env:LOCALAPPDATA "Android\Sdk")
}

function Get-AndroidToolchainStatus {
    $sdk = Get-AndroidSdkRoot
    $repoRoot = Get-AndroidRepoRoot
    $missing = New-Object System.Collections.Generic.List[string]
    $ndkVersion = "27.2.12479018"
    $cmakeVersion = "3.22.1"
    $buildToolsVersion = "35.0.0"

    if (-not (Test-Path $sdk)) { $missing.Add("Android SDK at $sdk") }
    if (-not (Test-Path (Join-Path $sdk "platform-tools\adb.exe"))) { $missing.Add("Android platform-tools / adb") }
    if (-not (Test-Path (Join-Path $sdk "cmdline-tools\latest\bin\sdkmanager.bat"))) { $missing.Add("Android command-line tools") }
    if (-not (Test-Path (Join-Path $sdk "platforms\android-35"))) { $missing.Add("Android platform android-35") }
    if (-not (Test-Path (Join-Path $sdk "build-tools\$buildToolsVersion\apksigner.bat"))) { $missing.Add("Android build-tools $buildToolsVersion") }
    if (-not (Test-Path (Join-Path $sdk "ndk\$ndkVersion"))) { $missing.Add("Android NDK $ndkVersion") }
    if (-not (Test-Path (Join-Path $sdk "cmake\$cmakeVersion"))) { $missing.Add("Android CMake $cmakeVersion") }
    if (-not (Test-Path (Get-AndroidGradleWrapperPath))) { $missing.Add("repo Gradle wrapper at T850\android\gradlew.bat") }
    if (-not (Test-Path (Join-Path (Get-AndroidProjectRoot) "gradle\wrapper\gradle-wrapper.jar"))) { $missing.Add("Gradle wrapper jar") }
    if (-not (Test-JavaAvailable)) { $missing.Add("JDK 17+") }
    if (-not (Test-GlslangAvailable)) { $missing.Add("Vulkan SDK / glslangValidator") }

    $vcpkgRoot = Join-Path $repoRoot "T850\Librerias\vcpkg"
    if (-not (Test-Path (Join-Path $vcpkgRoot "vcpkg.exe"))) { $missing.Add("vcpkg executable") }
    foreach ($triplet in (Get-AndroidVcpkgTriplets)) {
        if (-not (Test-AndroidVcpkgTripletReady -VcpkgRoot $vcpkgRoot -Triplet $triplet)) {
            $missing.Add("vcpkg Android dependencies for $triplet (Jolt/imgui/glslang/draco)")
        }
    }

    return [pscustomobject]@{
        SdkRoot = $sdk
        Missing = @($missing)
        SetupScript = (Join-Path $repoRoot "SetupAndroidToolchain.bat")
    }
}

function Ensure-AndroidToolchain {
    $status = Get-AndroidToolchainStatus
    if ($status.Missing.Count -eq 0) { return $true }

    $message = "Android toolchain is incomplete:" + [System.Environment]::NewLine + [System.Environment]::NewLine +
        (($status.Missing | ForEach-Object { "- $_" }) -join [System.Environment]::NewLine) +
        [System.Environment]::NewLine + [System.Environment]::NewLine +
        "Install the missing Android toolchain now?"
    $answer = [System.Windows.MessageBox]::Show($message, "T850 Launcher", "YesNo", "Warning")
    if ($answer -ne [System.Windows.MessageBoxResult]::Yes) { return $false }
    if (-not (Test-Path $status.SetupScript)) {
        [System.Windows.MessageBox]::Show(("Setup script not found:" + "`n" + $status.SetupScript), "T850 Launcher", "OK", "Error")
        return $false
    }

    $txtBuildOutput.Text = ""
    $pnlBuildOutput.Visibility = [System.Windows.Visibility]::Visible
    Set-LauncherBusy $true "SETUP..."
    $txtStatus.Text = "Installing Android toolchain..."
    $txtStatus.Foreground = $window.FindResource("AccentBrush")
    try {
        $setupArgs = @("--android-abis", (Get-AndroidAbiFilters))
        $exitCode = Invoke-LoggedProcess -FilePath $status.SetupScript -Arguments $setupArgs -WorkingDirectory (Get-AndroidRepoRoot) -StatusPrefix "Android toolchain setup"
        if ($exitCode -ne 0) {
            $txtStatus.Text = "Android toolchain setup failed (exit code $exitCode)"
            $txtStatus.Foreground = $window.FindResource("RedBrush")
            return $false
        }
        $status = Get-AndroidToolchainStatus
        if ($status.Missing.Count -gt 0) {
            [System.Windows.MessageBox]::Show(("Android setup finished, but these pieces are still missing:" + "`n`n" + (($status.Missing | ForEach-Object { "- $_" }) -join "`n")), "T850 Launcher", "OK", "Warning")
            return $false
        }
        $txtStatus.Text = "Android toolchain ready"
        $txtStatus.Foreground = $window.FindResource("GreenBrush")
        Refresh-AndroidDevices
        return $true
    } finally {
        Set-LauncherBusy $false
        Update-Preview
    }
}

function Get-WindowsTripletsForPlatform {
    param([string]$TargetPlatform)
    switch ($TargetPlatform) {
        "x86" { return @("x86-windows-static", "x86-windows") }
        "ARM64" { return @("arm64-windows-static", "arm64-windows") }
        default { return @("x64-windows-static", "x64-windows") }
    }
}

function Test-WindowsVcpkgTripletReady {
    param(
        [Parameter(Mandatory = $true)] [string]$VcpkgRoot,
        [Parameter(Mandatory = $true)] [string]$Triplet,
        [Parameter(Mandatory = $true)] [string]$TargetPlatform
    )

    $tripletRoot = Join-Path $VcpkgRoot "installed\$Triplet"
    if (-not (Test-Path $tripletRoot)) { return $false }

    if ($Triplet -like "*-windows-static") {
        $required = New-Object System.Collections.Generic.List[string]
        @(
            "include\imgui.h",
            "include\imgui_impl_dx11.h",
            "include\imgui_impl_vulkan.h",
            "include\imgui_impl_opengl3.h",
            "include\imgui_impl_sdl3.h"
        ) | ForEach-Object { $required.Add($_) }

        if ($TargetPlatform -ne "x86") {
            $required.Add("include\imgui_impl_dx12.h")
            $required.Add("include\Jolt\Jolt.h")
            $required.Add("lib\Jolt.lib")
            $required.Add("share\Jolt\JoltConfig.cmake")
        }

        return (Test-VcpkgFiles -TripletRoot $tripletRoot -RelativePaths @($required))
    }

    return (Test-VcpkgFiles -TripletRoot $tripletRoot -RelativePaths @(
        "lib\draco.lib",
        "bin\libEGL.dll",
        "bin\libGLESv2.dll"
    ))
}

function Get-DependencySetupActivity {
    param([string]$SourceRoot = $rootDir, [object[]]$Processes)
    if (-not $PSBoundParameters.ContainsKey('Processes')) {
        try {
            $Processes = @(Get-CimInstance Win32_Process -Filter "Name = 'vcpkg.exe' OR Name = 'powershell.exe' OR Name = 'pwsh.exe' OR Name = 'cmd.exe'" -ErrorAction Stop)
        } catch {
            return [pscustomobject]@{ State = 'Unknown'; Message = 'Cannot check whether dependency setup is running. Build/setup is paused to avoid a duplicate installer.' }
        }
    }
    $sourcePath = [IO.Path]::GetFullPath($SourceRoot).TrimEnd('\', '/')
    $vcpkgPath = Join-Path $sourcePath 'Librerias\vcpkg\vcpkg.exe'
    $setupPath = Join-Path $sourcePath 'scripts\SetupDawn.ps1'
    $launchPath = Join-Path (Split-Path -Parent $sourcePath) 'LaunchSolution.bat'
    foreach ($process in $Processes) {
        $command = [string]$process.CommandLine
        $executable = [string]$process.ExecutablePath
        $mutatingVcpkg = ($executable -ieq $vcpkgPath -or $command -match ('(?:^|["\s])' + [regex]::Escape($vcpkgPath) + '(?:["\s]|$)')) -and
            $command -match '(?:^|\s)(install|remove|upgrade|build|ci)(?:\s|$)' -and $command -notmatch '(?:^|\s)--dry-run(?:\s|$)'
        $setupScript = $command -match [regex]::Escape($setupPath) -and $command -notmatch '(?:^|\s)-Mode\s+["'']?(Check|Plan)["'']?(?:\s|$)'
        $setupLauncher = $command -match [regex]::Escape($launchPath) -and $command -match '(?:^|\s)--setup-only(?:\s|$)'
        if ($mutatingVcpkg -or $setupScript -or $setupLauncher) {
            return [pscustomobject]@{
                State = 'Running'
                Message = "Dependencies are being updated by another process (PID $($process.ProcessId)). Package files may be temporarily absent. Build/setup will be available when it finishes."
            }
        }
    }
    return [pscustomobject]@{ State = 'Idle'; Message = '' }
}

function Test-DependencySetupAvailable {
    $activity = Get-DependencySetupActivity
    $script:DependencySetupActivity = $activity
    Update-DependencySetupControls
    if ($activity.State -eq 'Idle') { return $true }
    $txtStatus.Text = $activity.Message
    $txtStatus.Foreground = $window.FindResource('AccentBrush')
    return $false
}

function Update-DependencySetupControls {
    $notice = $window.FindName('txtDependencyStatus')
    if (-not $notice) { return }
    $activity = $script:DependencySetupActivity
    $blocked = $activity -and $activity.State -ne 'Idle'
    $notice.Text = if ($blocked) { $activity.Message } else { '' }
    $notice.Visibility = if ($blocked) { [System.Windows.Visibility]::Visible } else { [System.Windows.Visibility]::Collapsed }
    foreach ($button in @($btnBuild, $btnRebuild)) {
        $button.IsEnabled = -not $script:LauncherBusy -and -not $blocked
        $button.ToolTip = if ($blocked) { $activity.Message } else { $null }
    }
    if ($blocked) { $btnCompileShaders.IsEnabled = $false }
}

function Refresh-DependencySetupActivity {
    if ($script:LauncherBusy) { return }
    $activity = Get-DependencySetupActivity
    if (-not $script:DependencySetupActivity -or
        $activity.State -ne $script:DependencySetupActivity.State -or
        $activity.Message -ne $script:DependencySetupActivity.Message) {
        $script:DependencySetupActivity = $activity
        Update-Preview
    }
}

function Invoke-DawnPackageCheck {
    $ErrorActionPreference = "Continue"
    $output = & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $rootDir "scripts\SetupDawn.ps1") -Mode Check 2>&1
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = ($output | Out-String).Trim() }
}

function Get-DawnSetupStatus {
    param([string]$TargetPlatform)
    if ($TargetPlatform -ine "x64") { return [pscustomobject]@{ Missing = @(); Diagnostic = "" } }
    if (-not (Test-CommandExists "cmake")) {
        return [pscustomobject]@{ Missing = @("CMake 3.21+ on PATH (required for Windows x64 Dawn setup)"); Diagnostic = "Install CMake and restart the Launcher." }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $rootDir "scripts\SetupDawn.ps1"))) {
        return [pscustomobject]@{ Missing = @("Dawn setup script"); Diagnostic = "scripts\SetupDawn.ps1 is missing." }
    }
    $check = Invoke-DawnPackageCheck
    if ($check.ExitCode -ne 0) {
        return [pscustomobject]@{ Missing = @("Dawn package or generated metadata (missing/stale)"); Diagnostic = $check.Output }
    }
    return [pscustomobject]@{ Missing = @(); Diagnostic = "" }
}

function Invoke-DawnPackageSetup {
    if (-not (Test-DependencySetupAvailable)) { return $false }
    $txtBuildOutput.Text = ""
    $pnlBuildOutput.Visibility = [System.Windows.Visibility]::Visible
    Set-LauncherBusy $true "SETUP..."
    try {
        $arguments = @("-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $rootDir "scripts\SetupDawn.ps1"))
        $exitCode = Invoke-LoggedProcess -FilePath "powershell.exe" -Arguments $arguments -WorkingDirectory $rootDir -StatusPrefix "Dawn dependency setup"
        return $exitCode -eq 0
    } finally {
        Set-LauncherBusy $false
        Update-Preview
    }
}

function Get-WindowsToolchainStatus {
    param([string]$TargetPlatform)
    $repoRoot = Get-AndroidRepoRoot
    $missing = New-Object System.Collections.Generic.List[string]
    $activity = Get-DependencySetupActivity
    if ($activity.State -ne 'Idle') {
        return [pscustomobject]@{
            Missing = @()
            Dawn = [pscustomobject]@{ Missing = @(); Diagnostic = '' }
            DependencyActivity = $activity
            SetupScript = (Join-Path $repoRoot 'LaunchSolution.bat')
        }
    }

    if (-not (Find-MSBuild -TargetPlatform $TargetPlatform)) {
        $missing.Add("Visual Studio 2022 C++ Build Tools / MSBuild for $TargetPlatform")
    }
    if (-not (Test-CommandExists "git")) { $missing.Add("Git on PATH") }

    $vcpkgRoot = Join-Path $repoRoot "T850\Librerias\vcpkg"
    if (-not (Test-Path (Join-Path $vcpkgRoot "vcpkg.exe"))) {
        $missing.Add("vcpkg executable and dependencies")
    } else {
        foreach ($triplet in (Get-WindowsTripletsForPlatform $TargetPlatform)) {
            if (-not (Test-WindowsVcpkgTripletReady -VcpkgRoot $vcpkgRoot -Triplet $triplet -TargetPlatform $TargetPlatform)) {
                $missing.Add("vcpkg dependencies for $triplet")
            }
        }
    }

    $dawnStatus = Get-DawnSetupStatus -TargetPlatform $TargetPlatform
    foreach ($item in $dawnStatus.Missing) { $missing.Add($item) }
    return [pscustomobject]@{
        Missing = @($missing)
        Dawn = $dawnStatus
        DependencyActivity = $activity
        SetupScript = (Join-Path $repoRoot "LaunchSolution.bat")
    }
}

function Invoke-WindowsVcpkgSetup {
    param([string]$TargetPlatform)
    if (-not (Test-DependencySetupAvailable)) { return $false }
    $setupScript = Join-Path (Get-AndroidRepoRoot) "LaunchSolution.bat"
    if (-not (Test-Path $setupScript)) {
        [System.Windows.MessageBox]::Show(("Setup script not found:" + "`n" + $setupScript), "T850 Launcher", "OK", "Error")
        return $false
    }
    $args = @("--setup-only")
    if ($TargetPlatform -eq "x86") { $args += "--x86" }
    elseif ($TargetPlatform -eq "ARM64") { $args += "--arm64" }

    $txtBuildOutput.Text = ""
    $pnlBuildOutput.Visibility = [System.Windows.Visibility]::Visible
    Set-LauncherBusy $true "SETUP..."
    $txtStatus.Text = "Installing Windows dependencies..."
    $txtStatus.Foreground = $window.FindResource("AccentBrush")
    try {
        $exitCode = Invoke-LoggedProcess -FilePath $setupScript -Arguments $args -WorkingDirectory (Get-AndroidRepoRoot) -StatusPrefix "Windows dependency setup"
        if ($exitCode -ne 0) {
            $txtStatus.Text = "Windows dependency setup failed (exit code $exitCode)"
            $txtStatus.Foreground = $window.FindResource("RedBrush")
            return $false
        }
        return $true
    } finally {
        Set-LauncherBusy $false
        Update-Preview
    }
}

function Ensure-WindowsToolchain {
    param([string]$TargetPlatform)
    if (-not (Test-DependencySetupAvailable)) { return $false }
    $status = Get-WindowsToolchainStatus -TargetPlatform $TargetPlatform
    if ($status.DependencyActivity.State -ne 'Idle') {
        $txtStatus.Text = $status.DependencyActivity.Message
        return $false
    }
    if ($status.Missing.Count -eq 0) { return $true }
    if ($status.Missing -match "^CMake") {
        [System.Windows.MessageBox]::Show("CMake 3.21+ is required for Windows x64 builds. Install CMake, add it to PATH, restart the Launcher, then build again.", "T850 Launcher", "OK", "Warning") | Out-Null
        return $false
    }

    $missingText = ($status.Missing | ForEach-Object { "- $_" }) -join [System.Environment]::NewLine
    if ($status.Missing -match "Visual Studio") {
        $answer = [System.Windows.MessageBox]::Show(("Windows C++ build tools are missing:" + "`n`n" + $missingText + "`n`nInstall Visual Studio Build Tools with winget now?"), "T850 Launcher", "YesNo", "Warning")
        if ($answer -eq [System.Windows.MessageBoxResult]::Yes) {
            $winget = Get-Command winget -ErrorAction SilentlyContinue
            if ($winget) {
                $txtBuildOutput.Text = ""
                $pnlBuildOutput.Visibility = [System.Windows.Visibility]::Visible
                Set-LauncherBusy $true "SETUP..."
                try {
                    $override = "--wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.ATL --add Microsoft.VisualStudio.Component.VC.Tools.ARM64 --add Microsoft.VisualStudio.Component.VC.ATL.ARM64 --add Microsoft.VisualStudio.Component.Windows11SDK.26100 --includeRecommended"
                    $exitCode = Invoke-LoggedProcess -FilePath $winget.Source -Arguments @("install", "--id", "Microsoft.VisualStudio.2022.BuildTools", "-e", "--accept-package-agreements", "--accept-source-agreements", "--override", $override) -WorkingDirectory (Get-AndroidRepoRoot) -StatusPrefix "Visual Studio Build Tools setup"
                    if ($exitCode -ne 0) { return $false }
                } finally {
                    Set-LauncherBusy $false
                    Update-Preview
                }
            } else {
                Start-Process "https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022"
                return $false
            }
        } else {
            return $false
        }
    }

    $status = Get-WindowsToolchainStatus -TargetPlatform $TargetPlatform
    if ($status.DependencyActivity.State -ne 'Idle') {
        $txtStatus.Text = $status.DependencyActivity.Message
        return $false
    }
    $vcpkgMissing = @($status.Missing | Where-Object { $_ -like "vcpkg*" })
    if ($vcpkgMissing.Count -gt 0) {
        $answer = [System.Windows.MessageBox]::Show(("Windows dependencies are missing:" + "`n`n" + (($vcpkgMissing | ForEach-Object { "- $_" }) -join "`n") + "`n`nInstall them now?"), "T850 Launcher", "YesNo", "Warning")
        if ($answer -ne [System.Windows.MessageBoxResult]::Yes) { return $false }
        if (-not (Invoke-WindowsVcpkgSetup -TargetPlatform $TargetPlatform)) { return $false }
    }

    $status = Get-WindowsToolchainStatus -TargetPlatform $TargetPlatform
    if ($status.DependencyActivity.State -ne 'Idle') {
        $txtStatus.Text = $status.DependencyActivity.Message
        return $false
    }
    if ($status.Dawn.Missing.Count -gt 0) {
        $answer = [System.Windows.MessageBox]::Show(("The required Dawn package or build metadata is missing/stale." + "`n`n" + $status.Dawn.Diagnostic + "`n`nRun Dawn setup now?"), "T850 Launcher", "YesNo", "Warning")
        if ($answer -ne [System.Windows.MessageBoxResult]::Yes) { return $false }
        if (-not (Invoke-DawnPackageSetup)) { return $false }
        $status = Get-WindowsToolchainStatus -TargetPlatform $TargetPlatform
    }
    if ($status.DependencyActivity.State -ne 'Idle') {
        $txtStatus.Text = $status.DependencyActivity.Message
        return $false
    }
    if ($status.Missing.Count -gt 0) {
        [System.Windows.MessageBox]::Show(("Windows setup finished, but these pieces are still missing:" + "`n`n" + (($status.Missing | ForEach-Object { "- $_" }) -join "`n")), "T850 Launcher", "OK", "Warning")
        return $false
    }
    return $true
}

function Test-WebGpuSelected {
    return $cmbApi.SelectedItem -and $cmbApi.SelectedItem.Tag -eq "webgpu"
}

function Test-BrowserSelected {
    return $cmbApi.SelectedItem -and $cmbApi.SelectedItem.Tag -eq "webgpu-browser"
}

function Get-InstalledBrowsers {
    param(
        [string[]]$RegistryRoots = @('HKCU:\Software\Clients\StartMenuInternet', 'HKLM:\Software\Clients\StartMenuInternet', 'HKLM:\Software\WOW6432Node\Clients\StartMenuInternet'),
        [string[]]$InstallRoots = @($env:LOCALAPPDATA, $env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramW6432)
    )
    $candidates = @()
    foreach ($registryRoot in $RegistryRoots) {
        foreach ($registration in @(Get-ChildItem -LiteralPath $registryRoot -ErrorAction SilentlyContinue)) {
            $commandKey = Get-Item -LiteralPath ($registration.PSPath + '\shell\open\command') -ErrorAction SilentlyContinue
            if ($commandKey -and $commandKey.GetValue('') -match '^\s*(?:"(?<path>[^"]+\.exe)"|(?<path>.+?\.exe))(?:\s|$)') {
                $candidates += [pscustomobject]@{ Name = [string]$registration.GetValue(''); Path = [Environment]::ExpandEnvironmentVariables($Matches.path) }
            }
        }
    }
    foreach ($installRoot in $InstallRoots | Where-Object { $_ } | Select-Object -Unique) {
        foreach ($browser in @(
            @{ Name = 'Google Chrome'; RelativePath = 'Google\Chrome\Application\chrome.exe' },
            @{ Name = 'Mozilla Firefox'; RelativePath = 'Mozilla Firefox\firefox.exe' },
            @{ Name = 'Microsoft Edge'; RelativePath = 'Microsoft\Edge\Application\msedge.exe' }
        )) {
            $candidates += [pscustomobject]@{ Name = $browser.Name; Path = Join-Path $installRoot $browser.RelativePath }
        }
    }
    $installed = @{}
    foreach ($candidate in $candidates) {
        if ([IO.Path]::GetFileName($candidate.Path) -ieq 'iexplore.exe') { continue }
        if (-not (Test-Path -LiteralPath $candidate.Path -PathType Leaf)) { continue }
        $path = [IO.Path]::GetFullPath($candidate.Path)
        if (-not $installed.ContainsKey($path)) {
            $name = if ($candidate.Name) { $candidate.Name } else { [IO.Path]::GetFileNameWithoutExtension($path) }
            $installed[$path] = [pscustomobject]@{ Name = $name; Path = $path }
        }
    }
    return @($installed.Values | Sort-Object Name, Path)
}

function Update-InstalledBrowsers {
    $selectedPath = [string]$cmbBrowser.SelectedItem.Tag
    $cmbBrowser.Items.Clear()
    $default = [Windows.Controls.ComboBoxItem]::new()
    $default.Content = 'System default'
    $default.Tag = ''
    [void]$cmbBrowser.Items.Add($default)
    foreach ($browser in Get-InstalledBrowsers) {
        $item = [Windows.Controls.ComboBoxItem]::new()
        $item.Content = $browser.Name
        $item.Tag = $browser.Path
        $item.ToolTip = $browser.Path
        [void]$cmbBrowser.Items.Add($item)
    }
    $cmbBrowser.SelectedIndex = 0
    foreach ($item in $cmbBrowser.Items) {
        if ($item.Tag -ieq $selectedPath) { $cmbBrowser.SelectedItem = $item; break }
    }
}

function Get-BrowserLaunchCommand {
    if (Test-AndroidTarget) { throw "Browser mode runs on the desktop, not the Android target." }
    $browserRoot = $rootDir
    $server = Join-Path $browserRoot "web\server.mjs"
    foreach ($file in @('web\server.mjs', 'build\web\site\DayScene.html', 'build\web\site\DayScene.js', 'build\web\site\DayScene.wasm', 'build\web\site\scenes.json', 'build\web\WebShaders', 'Assets')) {
        if (-not (Test-Path (Join-Path $browserRoot $file))) { throw "Browser build missing. Click BUILD WEB or run scripts\BuildWeb.ps1." }
    }
    $node = Get-Command node -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $node) { throw "Browser launch requires Node.js on PATH." }
    $scene = $cmbScene.SelectedItem.Tag.ToString()
    $parameters = [ordered]@{ scene = $scene; width = $txtWidth.Text; height = $txtHeight.Text; culling = (Get-CullingMode); logLevel = $cmbLogLevel.SelectedItem.Tag.ToString() }
    $parameters.postProcessMode = $cmbPostProcessMode.SelectedItem.Tag.ToString()
    if ($scene -eq '2' -or $scene -eq '4' -or ($scene -eq '0' -and (Get-SandboxInputMode) -eq 'scene')) {
        $sceneFile = Get-SelectedSceneFilePath
        if ($sceneFile) { $parameters.sceneFile = Get-SceneFileResourcePath $sceneFile }
    } elseif (($scene -eq '0' -or $scene -eq '3') -and $cmbModel.SelectedItem) {
        $parameters.model = $cmbModel.SelectedItem.Tag.ToString()
    }
    $query = ($parameters.GetEnumerator() | ForEach-Object { [uri]::EscapeDataString($_.Key) + '=' + [uri]::EscapeDataString([string]$_.Value) }) -join '&'
    $arguments = @(('"{0}"' -f $server), '--open', '--query', ('"{0}"' -f $query))
    $browserPath = [string]$cmbBrowser.SelectedItem.Tag
    if ($browserPath) {
        if (-not (Test-Path -LiteralPath $browserPath -PathType Leaf)) { throw "Selected browser is no longer installed: $browserPath" }
        $arguments += @('--browser', ('"{0}"' -f $browserPath))
    }
    return @{ ExePath = $node.Source; Args = $arguments; WorkingDirectory = $browserRoot; Display = ('"{0}" {1}' -f $node.Source, ($arguments -join ' ')) }
}

function Get-BrowserBuildCommand {
    param([ValidateSet('Build', 'Rebuild')][string]$BuildTarget = 'Build')
    if (Test-AndroidTarget) { throw 'Browser builds require the Windows development target.' }
    $buildScript = Join-Path $rootDir 'scripts\BuildWeb.ps1'
    if (-not (Test-Path -LiteralPath $buildScript)) { throw "Browser build script missing: $buildScript" }
    $powerShellExe = (Get-Command powershell.exe -CommandType Application -ErrorAction Stop).Source
    $arguments = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $buildScript, '-Configuration', $cmbConfig.SelectedItem.Content.ToString())
    if ($BuildTarget -eq 'Rebuild') { $arguments += '-Clean' }
    return @{ ExePath = $powerShellExe; Args = $arguments; WorkingDirectory = $rootDir; Display = (Format-LauncherCommandLine -FilePath $powerShellExe -Arguments $arguments) }
}

function Update-BrowserPreview {
    $btnEditor.IsEnabled = $false
    $btnRun.Content = "OPEN BROWSER"
    try {
        $command = Get-BrowserLaunchCommand
        $txtCmdPreview.Text = $command.Display
        $btnRun.IsEnabled = $true
        $txtStatus.Text = "Browser WebGPU: prepared shaders; editor and native diagnostics unavailable."
        $txtStatus.Foreground = $window.FindResource("AccentBrush")
    } catch {
        $btnRun.IsEnabled = $false
        $txtCmdPreview.Text = ""
        $txtStatus.Text = $_.Exception.Message
        $txtStatus.Foreground = $window.FindResource("RedBrush")
    }
    return $true
}

function Start-BrowserRuntime {
    try {
        $command = Get-BrowserLaunchCommand
        Save-Config
        Start-Process -FilePath $command.ExePath -ArgumentList $command.Args -WorkingDirectory $command.WorkingDirectory
        $txtStatus.Text = "Opening browser; the server console reports its URL and startup errors."
    } catch {
        $txtStatus.Text = $_.Exception.Message
        $txtStatus.Foreground = $window.FindResource("RedBrush")
    }
}

function Test-WebGpuSupported {
    return -not (Test-AndroidTarget) -and $cmbArch.SelectedItem.Content -ieq "x64"
}

function Update-WebGpuControls {
    $selected = Test-WebGpuSelected
    $browser = Test-BrowserSelected
    $pnlBrowser.Visibility = if ($browser) { [System.Windows.Visibility]::Visible } else { [System.Windows.Visibility]::Collapsed }
    $cmbBrowser.IsEnabled = $browser -and -not $script:LauncherBusy
    foreach ($name in @('cmbArch', 'chkFullscreen', 'chkDump', 'chkDebugFrames', 'chkReplaySnapshot', 'chkKeepRunning', 'chkTelemetry', 'chkLogToFile', 'chkBenchmark', 'btnBenchmarkMatrix')) {
        $control = $window.FindName($name)
        if ($control) { $control.IsEnabled = -not $browser -and -not $script:LauncherBusy }
    }
    if (Test-AndroidTarget) { $chkFullscreen.IsEnabled = $false }
    $cmbConfig.IsEnabled = -not $script:LauncherBusy
    $btnBuild.Content = if ($browser) { "BUILD WEB" } else { "BUILD" }
    $btnRebuild.Content = if ($browser) { "REBUILD WEB" } else { "REBUILD" }
    $btnRebuild.IsEnabled = -not $script:LauncherBusy
    $compileRoot = Join-Path $rootDir ("bin\{0}\{1}" -f (Get-ArchFolder), $cmbConfig.SelectedItem.Content)
    $btnCompileShaders.IsEnabled = -not $browser -and -not $script:LauncherBusy -and -not (Test-AndroidTarget) -and (Test-Path (Join-Path $compileRoot "DayScene.exe"))
    $pnlShaderFlow.Visibility = if ($selected) { [System.Windows.Visibility]::Visible } else { [System.Windows.Visibility]::Collapsed }
    $cmbShaderFlow.IsEnabled = $selected -and (Test-WebGpuSupported)
    foreach ($item in $cmbApi.Items) {
        if ($item.Tag -eq "webgpu") { $item.IsEnabled = Test-WebGpuSupported }
    }
    $btnRun.ToolTip = if ($selected) { "WebGPU supports forward and deferred runtime scenes on Windows x64. Editor support is unavailable." } else { $null }
    $btnEditor.ToolTip = if ($selected) { "WebGPU editor support is not implemented." } else { $null }
}

function Update-WebGpuPreview {
    if (Test-BrowserSelected) { return Update-BrowserPreview }
    if (-not (Test-WebGpuSelected) -or (Test-WebGpuSupported)) { return $false }
    $btnRun.IsEnabled = $false
    $btnEditor.IsEnabled = $false
    $txtCmdPreview.Text = ""
    $txtStatus.Text = "WebGPU requires Windows x64."
    $txtStatus.Foreground = $window.FindResource("RedBrush")
    return $true
}

function Get-LaunchCommand {
    if (Test-BrowserSelected) { return Get-BrowserLaunchCommand }
    $arch   = ($cmbArch.SelectedItem).Content.ToString().ToLower()
    $config = ($cmbConfig.SelectedItem).Content.ToString()
    $apiTag = ($cmbApi.SelectedItem).Tag.ToString()

    $archFolder = Get-ArchFolder

    $exePath = Join-Path $rootDir "bin\$archFolder\$config\DayScene.exe"
    if ($apiTag -eq "webgpu" -and -not (Test-WebGpuSupported)) { throw "WebGPU requires Windows x64." }
    $argList = @("--api", $apiTag)
    $argList += @("--postProcessMode", $cmbPostProcessMode.SelectedItem.Tag.ToString())
    if ($apiTag -eq "webgpu") {
        $argList += @("--shaderFlow", $cmbShaderFlow.SelectedItem.Tag.ToString())
    }

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
                $argList += @("--sceneFile", ('"{0}"' -f $sceneFile))
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

    if ($chkTelemetry.IsChecked) {
        $argList += @("--telemetry", "--telemetryFrequencyFrames", $txtTelemetryFrequency.Text, "--telemetryOutput", "logs\perf_telemetry.json")
    }

    return @{
        ExePath = $exePath
        Args    = $argList
        Display = ('"' + $exePath + '" ' + ($argList -join ' '))
    }
}

function Get-ShaderCompileCommands {
    if (Test-AndroidTarget) { throw "Desktop shader compilation requires the Windows target. Android shaders are compiled by the APK build." }
    $runtime = Join-Path $rootDir ("bin\{0}\{1}" -f (Get-ArchFolder), $cmbConfig.SelectedItem.Content)
    $exe = Join-Path $runtime "DayScene.exe"
    foreach ($api in @("d3d11", "d3d12", "vulkan", "gl", "webgpu")) {
        if ($api -eq "webgpu" -and -not (Test-WebGpuSupported)) { continue }
        $flows = if ($api -eq "webgpu") { @("auto", "spirv") } else { @("native") }
        foreach ($flow in $flows) {
            $arguments = @("--compileShaders", "--api", $api, "--logLevel", "info")
            if ($api -eq "webgpu") { $arguments += @("--shaderFlow", $flow) }
            [pscustomobject]@{ Name = "$api-$flow"; ExePath = $exe; WorkingDirectory = $runtime; Args = $arguments }
        }
    }
}

function Update-ShaderCompileQueue($State) {
    if ($State.Process) {
        if (Test-Path $State.Stdout) {
            $stream = [IO.File]::Open($State.Stdout, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
            $reader = [IO.StreamReader]::new($stream)
            try { $State.Output.Text = $reader.ReadToEnd(); $State.Output.ScrollToEnd() } finally { $reader.Dispose() }
        }
        if (-not $State.Process.HasExited) { return }
        $State.Process.WaitForExit()
        if (Test-Path $State.Stdout) {
            $State.Output.Text = [IO.File]::ReadAllText($State.Stdout)
            $State.Output.ScrollToEnd()
        }
        $passed = $State.Process.ExitCode -eq 0 -and $State.Output.Text -match '\[ShaderPrecompile\] complete: \d+ succeeded, 0 failed'
        if (-not $passed) { $State.Failures++ }
        [void]$State.Results.Add([pscustomobject]@{ Job = $State.Jobs[$State.Index - 1].Name; ExitCode = $State.Process.ExitCode; Passed = $passed })
        $State.Process.Dispose()
        $State.Process = $null
        $State.Progress.Value = $State.Index
    }
    if ($State.CancelRequested) {
        $State.Timer.Stop()
        $State.Status.Text = "Cancelled. Logs: $($State.Directory)"
        $State.Cancel.Content = "Close"
        $State.Cancel.IsEnabled = $true
        $State.Results | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $State.Directory "summary.json") -Encoding UTF8
        return
    }
    if ($State.Index -ge $State.Jobs.Count) {
        $State.Timer.Stop()
        $State.Status.Text = "Completed: $($State.Jobs.Count - $State.Failures) passed, $($State.Failures) failed. Logs: $($State.Directory)"
        $State.Cancel.Content = "Close"
        $State.Results | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $State.Directory "summary.json") -Encoding UTF8
        return
    }
    $job = $State.Jobs[$State.Index]
    $State.Stdout = Join-Path $State.Directory "$($job.Name).log"
    $stderr = Join-Path $State.Directory "$($job.Name)-errors.log"
    $State.Status.Text = "[$($State.Index + 1)/$($State.Jobs.Count)] $($job.Name)"
    $State.Output.Text = ""
    $arguments = $job.Args + @("--shaderCompileCancelFile", ('"{0}"' -f $State.CancelFile))
    $State.Process = Start-Process -FilePath $job.ExePath -ArgumentList $arguments -WorkingDirectory $job.WorkingDirectory -RedirectStandardOutput $State.Stdout -RedirectStandardError $stderr -PassThru
    $null = $State.Process.Handle
    $State.Index++
}

function Invoke-ShaderCompilation {
    $jobs = @(Get-ShaderCompileCommands)
    if (-not $jobs.Count -or -not (Test-Path $jobs[0].ExePath)) { throw "Build DayScene before compiling shaders." }
    $help = & $jobs[0].ExePath --help 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $help -notmatch '--shaderCompileCancelFile') { throw "This DayScene build does not support cancellable shader precompilation. Rebuild or update the engine." }
    $manifest = Join-Path $jobs[0].WorkingDirectory "Shaders\shader_permutations.json"
    if (-not (Test-Path $manifest)) { throw "Missing shader permutation manifest: $manifest" }
    $directory = Join-Path $jobs[0].WorkingDirectory ("logs\shader-compile-" + (Get-Date -Format "yyyyMMdd-HHmmss-fff"))
    [void][IO.Directory]::CreateDirectory($directory)
    $dialog = [Windows.Markup.XamlReader]::Parse(@'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Title="Compile Shaders" Width="760" Height="480" MinWidth="480" MinHeight="320" WindowStartupLocation="CenterOwner" Background="#1E1E2E" Foreground="#CDD6F4">
  <DockPanel Margin="16">
    <TextBlock Name="status" DockPanel.Dock="Top" TextWrapping="Wrap" Margin="0,0,0,12"/>
    <ProgressBar Name="progress" DockPanel.Dock="Top" Height="8" Margin="0,0,0,12"/>
    <Button Name="cancel" DockPanel.Dock="Bottom" Content="Cancel" Width="100" Height="30" HorizontalAlignment="Right" Margin="0,12,0,0"/>
    <TextBox Name="output" IsReadOnly="True" TextWrapping="Wrap" VerticalScrollBarVisibility="Auto" FontFamily="Consolas" FontSize="12" Background="#0D1117" Foreground="#C9D1D9"/>
  </DockPanel>
</Window>
'@)
    $dialog.Owner = $window
    $state = [pscustomobject]@{ Jobs = $jobs; Index = 0; Process = $null; Stdout = ""; Directory = $directory; CancelFile = (Join-Path $directory "cancel.request"); CancelRequested = $false; Failures = 0; Results = [Collections.ArrayList]::new(); Timer = [Windows.Threading.DispatcherTimer]::new(); Status = $dialog.FindName("status"); Progress = $dialog.FindName("progress"); Output = $dialog.FindName("output"); Cancel = $dialog.FindName("cancel") }
    $state.Progress.Maximum = $jobs.Count
    $state.Status.Text = if ($jobs.Count -eq 6) { "6 API/flow jobs" } else { "4 API jobs; WebGPU requires an x64 runtime" }
    $state.Timer.Interval = [TimeSpan]::FromMilliseconds(300)
    $updateQueue = ${function:Update-ShaderCompileQueue}
    $state.Timer.Add_Tick({
        try { & $updateQueue $state } catch {
            $state.Timer.Stop()
            $state.Status.Text = "Compilation stopped: $($_.Exception.Message). Logs: $($state.Directory)"
        }
    }.GetNewClosure())
    $state.Cancel.Add_Click({ $dialog.Close() }.GetNewClosure())
    $dialog.Add_Closing({
        param($sender, $eventArgs)
        if ($state.Process) {
            $eventArgs.Cancel = $true
            $state.CancelRequested = $true
            [IO.File]::WriteAllText($state.CancelFile, "cancel")
            $state.Status.Text = "Cancelling after the current shader..."
            $state.Cancel.IsEnabled = $false
            $state.Timer.Start()
        } else {
            $state.Timer.Stop()
        }
    }.GetNewClosure())
    $state.Timer.Start()
    [void]$dialog.ShowDialog()
}

function Get-EditorLaunchCommand {
    if (Test-BrowserSelected) { throw "The browser target does not support T8ditor." }
    if ((Test-WebGpuSelected) -and -not (Test-WebGpuSupported)) { throw "WebGPU requires Windows x64." }
    $arch   = ($cmbArch.SelectedItem).Content.ToString().ToLower()
    $config = ($cmbConfig.SelectedItem).Content.ToString()
    $apiTag = ($cmbApi.SelectedItem).Tag.ToString()

    $archFolder = Get-ArchFolder

    $exePath = Join-Path $rootDir "bin\$archFolder\$config\T8ditor.exe"
    $argList = @()

    # Win32 ImGui is provisioned without the D3D12 backend; use D3D11 there.
    $editorApi = if ($apiTag -eq "webgpu") {
        "webgpu"
    } elseif ($arch -eq "x86") {
        if ($apiTag -eq "vulkan" -or $apiTag -eq "gl") { "vulkan" } else { "d3d11" }
    } else {
        if ($apiTag -eq "vulkan" -or $apiTag -eq "gl") { "vulkan" } else { "d3d12" }
    }
    $argList += @("--api", $editorApi)

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

function Set-ComboByTag {
    param($Combo, [string]$Tag)
    foreach ($item in $Combo.Items) {
        if ($item.Tag -ieq $Tag) {
            $Combo.SelectedItem = $item
            return
        }
    }
}

function Set-ComboByContent {
    param($Combo, [string]$Content)
    foreach ($item in $Combo.Items) {
        if ($item.Content -ieq $Content) {
            $Combo.SelectedItem = $item
            return
        }
    }
}

function Set-ArchitectureOptionVisibility {
    param([bool]$IsAndroid)
    foreach ($item in $cmbArch.Items) {
        if ($item.Content -ieq "x86") {
            $item.Visibility = if ($IsAndroid) { [System.Windows.Visibility]::Collapsed } else { [System.Windows.Visibility]::Visible }
        }
    }
    if ($IsAndroid -and $cmbArch.SelectedItem -and $cmbArch.SelectedItem.Content -ieq "x86") {
        Set-ComboByContent $cmbArch "ARM64"
    }
}

function Update-TargetPlatformState {
    $isAndroid = Test-AndroidTarget
    Set-ArchitectureOptionVisibility $isAndroid
    if ($isAndroid) {
        Set-ComboByTag $cmbApi "vulkan"
        $cmbApi.IsEnabled = $false
        $pnlAndroidDevice.Visibility = [System.Windows.Visibility]::Visible
        $txtWidth.IsEnabled = $false
        $txtHeight.IsEnabled = $false
        $chkFullscreen.IsEnabled = $false
        $btnRun.Content = "Install"
        $btnEditor.Content = "Deploy"
    } else {
        $cmbApi.IsEnabled = $true
        $pnlAndroidDevice.Visibility = [System.Windows.Visibility]::Collapsed
        $txtWidth.IsEnabled = $true
        $txtHeight.IsEnabled = $true
        $chkFullscreen.IsEnabled = $true
        $btnRun.Content = ([char]0x25B6).ToString() + "  RUN"
        $btnEditor.Content = ([char]0x270E).ToString() + "  EDITOR"
    }
}

function Get-AndroidLaunchArguments {
    $sceneTag = ($cmbScene.SelectedItem).Tag.ToString()
    $logTag = ($cmbLogLevel.SelectedItem).Tag.ToString()
    $logLevel = switch ($logTag) {
        "error" { 0 }
        "info" { 1 }
        "debug" { 2 }
        "verbose" { 3 }
        "trace" { 4 }
        default { 2 }
    }

    $args = @(
        "shell", "am", "start", "-n", "com.t850.engine/.LauncherActivity",
        "--ez", "com.t850.engine.extra.AUTO_RUN", "true",
        "--ei", "com.t850.engine.extra.SCENE", $sceneTag,
        "--ei", "com.t850.engine.extra.LOG_LEVEL", $logLevel.ToString(),
        "--ez", "com.t850.engine.extra.RETURN_TO_NATIVE", "false",
        "--es", "com.t850.engine.extra.RUN_ID", ([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds().ToString())
    )
    if ((($sceneTag -eq "2") -or ($sceneTag -eq "4") -or (($sceneTag -eq "0") -and ((Get-SandboxInputMode) -eq "scene"))) -and (Get-SelectedSceneFilePath)) {
        $args += @("--es", "com.t850.engine.extra.SCENE_FILE", (Get-SceneFileResourcePath (Get-SelectedSceneFilePath)))
    } elseif (($sceneTag -eq "0" -or $sceneTag -eq "3") -and $cmbModel.SelectedItem) {
        $args += @("--es", "com.t850.engine.extra.MODEL", ($cmbModel.SelectedItem).Tag.ToString())
    }
    if ($chkDump.IsChecked) {
        if ($rbFrame.IsChecked) { $args += @("--ei", "com.t850.engine.extra.DUMP_FRAME", $txtFrame.Text) }
        else { $args += @("--ef", "com.t850.engine.extra.DUMP_SECONDS", $txtSeconds.Text) }
    }
    if ($chkDebugFrames.IsChecked) { $args += @("--ez", "com.t850.engine.extra.DEBUG_FRAMES", "true") }
    if ($chkKeepRunning.IsChecked) { $args += @("--ez", "com.t850.engine.extra.KEEP_RUNNING", "true") }
    if ($chkTelemetry.IsChecked) {
        $args += @("--ez", "com.t850.engine.extra.TELEMETRY", "true")
        $args += @("--ei", "com.t850.engine.extra.TELEMETRY_FREQUENCY_FRAMES", $txtTelemetryFrequency.Text)
        $args += @("--es", "com.t850.engine.extra.TELEMETRY_OUTPUT", "logs/perf_telemetry.json")
    }
    if ($chkReplaySnapshot.IsChecked -and $txtReplaySnapshotPath.Text) { $args += @("--es", "com.t850.engine.extra.REPLAY_SNAPSHOT", $txtReplaySnapshotPath.Text) }
    return $args
}

function Get-AndroidForceStopArguments {
    return @("shell", "am", "force-stop", "com.t850.engine")
}

function Update-Preview {
    Update-TargetPlatformState
    Update-WebGpuControls
    Update-DownloadAssetsButton
    Update-DependencySetupControls
    if ($script:LauncherBusy) { return }
    if (Update-WebGpuPreview) { return }
    $sceneDeps = Get-CachedSceneDependencyResult
    $assetStatus = $script:CloudAssetStatus
    $assetsMissing = ($assetStatus -and $assetStatus.Configured -and ($assetStatus.Missing -gt 0 -or -not $assetStatus.Ok))
    if (Test-AndroidTarget) {
        $apkPath = Get-AndroidApkPath
        $abiFilters = Get-AndroidAbiFilters
        $deviceReady = (((Get-SelectedAndroidDeviceSerial) -ne "") -and ((Get-SelectedAndroidDeviceState) -eq "device"))
        $adbPath = Get-AndroidAdbPath
        $installArgs = Get-AndroidAdbArguments @("install", "-r", $apkPath)
        $deployArgs = Get-AndroidAdbArguments (Get-AndroidLaunchArguments)
        $txtCmdPreview.Text = "Install: " + (Format-LauncherCommandLine -FilePath $adbPath -Arguments $installArgs) + [System.Environment]::NewLine +
            "Deploy: " + (Format-LauncherCommandLine -FilePath $adbPath -Arguments $deployArgs)
        $btnRun.IsEnabled = ((Test-Path $apkPath) -and $deviceReady -and $sceneDeps.Ok -and -not $assetsMissing)
        $btnEditor.IsEnabled = ($deviceReady -and $sceneDeps.Ok -and -not $assetsMissing)
        if (-not $sceneDeps.Ok) {
            $txtStatus.Text = "Scene missing: $($sceneDeps.Missing -join ', ')"
            $txtStatus.Foreground = $window.FindResource("RedBrush")
        } elseif (-not $deviceReady) {
            $txtStatus.Text = "Select a connected Android device"
            $txtStatus.Foreground = $window.FindResource("RedBrush")
        } elseif ($assetsMissing) {
            $txtStatus.Text = "Cloud assets missing: $($assetStatus.Missing)/$($assetStatus.Total). Click Download Assets before running."
            $txtStatus.Foreground = $window.FindResource("AccentBrush")
        } elseif ($btnRun.IsEnabled) {
            $txtStatus.Text = "Android APK ready - Install; Deploy runs installed app"
            $txtStatus.Foreground = $window.FindResource("GreenBrush")
        } else {
            $txtStatus.Text = "Build APK to Install, or Deploy an already-installed app"
            $txtStatus.Foreground = $window.FindResource("AccentBrush")
        }
        return
    }

    $cmd = Get-LaunchCommand
    $txtCmdPreview.Text = $cmd.Display

    $sceneOk = Test-Path $cmd.ExePath
    $editorCmd = Get-EditorLaunchCommand
    $editorOk = Test-Path $editorCmd.ExePath

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
    if (Test-WebGpuSelected) {
        $btnEditor.IsEnabled = $false
        if ($sceneOk -and $sceneDeps.Ok -and -not $assetsMissing) {
            $txtStatus.Text = "WebGPU: runtime forward/deferred rendering; editor unavailable."
            $txtStatus.Foreground = $window.FindResource("AccentBrush")
        }
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

$cmbAndroidDevice.Add_SelectionChanged({
    $script:SelectedAndroidDeviceSerial = Get-SelectedAndroidDeviceSerial
    Update-Preview
})
$cmbTarget.Add_SelectionChanged({
    Populate-ModelList
    Populate-SceneFileList
    Update-LauncherCloudAssetStatus | Out-Null
    if (Test-AndroidTarget) { Refresh-AndroidDevices }
    Update-Preview
})
$cmbArch.Add_SelectionChanged({ Populate-ModelList; Populate-SceneFileList; Update-LauncherCloudAssetStatus | Out-Null; Update-Preview })
$cmbConfig.Add_SelectionChanged({ Populate-ModelList; Populate-SceneFileList; Update-LauncherCloudAssetStatus | Out-Null; Update-Preview })
$cmbApi.Add_SelectionChanged({ Update-Preview })
$cmbPostProcessMode.Add_SelectionChanged({ Update-Preview })
$btnCompileShaders.Add_Click({
    try { Invoke-ShaderCompilation } catch { [System.Windows.MessageBox]::Show($_.Exception.Message, "Compile Shaders", "OK", "Error") }
})
$cmbShaderFlow.Add_SelectionChanged({ Update-Preview })
$cmbBrowser.Add_SelectionChanged({ Update-Preview })
$cmbBrowser.Add_DropDownOpened({ Update-InstalledBrowsers })
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
$chkLogToFile.Add_Checked({ Update-Preview })
$chkLogToFile.Add_Unchecked({ Update-Preview })
$cmbLogLevel.Add_SelectionChanged({ Update-Preview })
$chkTelemetry.Add_Checked({ Update-Preview })
$chkTelemetry.Add_Unchecked({ Update-Preview })
$txtTelemetryFrequency.Add_TextChanged({ Update-Preview })
$txtSeconds.Add_TextChanged({ Update-Preview })
$txtFrame.Add_TextChanged({ Update-Preview })
$txtWidth.Add_TextChanged({ Update-Preview })
$txtHeight.Add_TextChanged({ Update-Preview })

function Set-LauncherBusy {
    param([bool]$Busy, [string]$BuildText = "BUILD")
    $script:LauncherBusy = $Busy
    $btnBuild.IsEnabled = -not $Busy
    $btnCompileShaders.IsEnabled = -not $Busy -and -not (Test-AndroidTarget)
    $btnRebuild.IsEnabled = -not $Busy
    $btnRun.IsEnabled = -not $Busy
    Update-DownloadAssetsButton
    $btnBenchmarkMatrix.IsEnabled = -not $Busy
    $btnEditor.IsEnabled = -not $Busy
    $btnBuild.Content = $BuildText
    if (Test-BrowserSelected) {
        foreach ($control in @($cmbBrowser, $cmbConfig, $cmbApi, $cmbTarget)) { $control.IsEnabled = -not $Busy }
    }
}

function Invoke-AndroidBuild {
    param([bool]$Clean = $false, [bool]$Install = $false)

    $config = Get-AndroidConfigName
    if (-not (Ensure-AndroidToolchain)) { return $false }
    if ($config -ieq "Release") {
        $signingStatus = Get-AndroidReleaseSigningStatus
        if (-not $signingStatus.Ready) {
            Show-AndroidReleaseSigningWarning $signingStatus
            $txtStatus.Text = "Android Release signing is not configured"
            $txtStatus.Foreground = $window.FindResource("RedBrush")
            return $false
        }
    }

    $buildScript = Get-AndroidBuildScript
    if (-not (Test-Path $buildScript)) {
        [System.Windows.MessageBox]::Show(("Android build script not found:" + "`n" + $buildScript), "T850 Launcher", "OK", "Error")
        return $false
    }

    Save-Config
    $txtBuildOutput.Text = ""
    $pnlBuildOutput.Visibility = [System.Windows.Visibility]::Visible
    Set-LauncherBusy $true "BUILDING..."
    $txtStatus.Text = "Android build starting..."
    $txtStatus.Foreground = $window.FindResource("AccentBrush")

    $args = @($config, "--abi", (Get-AndroidAbiFilters))
    if ($Clean) { $args += "--clean" }
    if ($Install) { $args += "--install" }

    try {
        $exitCode = Invoke-LoggedProcess -FilePath $buildScript -Arguments $args -WorkingDirectory (Get-AndroidRepoRoot) -StatusPrefix "Android $config build"
        if ($exitCode -eq 0) {
            $txtStatus.Text = if ($Install) { "Android $config build/install succeeded" } else { "Android $config build succeeded" }
            $txtStatus.Foreground = $window.FindResource("GreenBrush")
            return $true
        }
        $txtStatus.Text = "Android $config build failed (exit code $exitCode)"
        $txtStatus.Foreground = $window.FindResource("RedBrush")
        return $false
    } catch {
        Append-BuildOutput ("Exception: " + $_.Exception.Message)
        $txtStatus.Text = "Android build error"
        $txtStatus.Foreground = $window.FindResource("RedBrush")
        return $false
    } finally {
        Set-LauncherBusy $false
        Update-Preview
    }
}

function Invoke-AndroidInstall {
    param([bool]$AppendLog = $false, [bool]$KeepBusy = $false)

    $apkPath = Get-AndroidApkPath
    if (-not (Test-Path $apkPath)) {
        [System.Windows.MessageBox]::Show(("APK not found:" + "`n" + $apkPath + "`n`nBuild this Android configuration first."), "T850 Launcher", "OK", "Error")
        return $false
    }
    $adb = Get-AndroidAdbPath
    if (-not (Test-Path $adb)) {
        if (-not (Ensure-AndroidToolchain)) { return $false }
        $adb = Get-AndroidAdbPath
        if (-not (Test-Path $adb)) {
            [System.Windows.MessageBox]::Show(("adb.exe not found:" + "`n" + $adb), "T850 Launcher", "OK", "Error")
            return $false
        }
    }
    if (-not (Test-AndroidDeviceReady)) { return $false }

    Save-Config
    if (-not $AppendLog) { $txtBuildOutput.Text = "" }
    $pnlBuildOutput.Visibility = [System.Windows.Visibility]::Visible
    if (-not $KeepBusy) { Set-LauncherBusy $true "INSTALL" }
    $txtStatus.Text = "Installing Android APK..."
    $txtStatus.Foreground = $window.FindResource("AccentBrush")

    try {
        $exitCode = Invoke-LoggedProcess -FilePath $adb -Arguments (Get-AndroidAdbArguments @("install", "-r", $apkPath)) -WorkingDirectory $rootDir -StatusPrefix "Installing Android APK"
        if ($exitCode -eq 0) {
            $txtStatus.Text = "Android APK installed"
            $txtStatus.Foreground = $window.FindResource("GreenBrush")
            return $true
        }
        $txtStatus.Text = "Android install failed (exit code $exitCode)"
        $txtStatus.Foreground = $window.FindResource("RedBrush")
        return $false
    } finally {
        if (-not $KeepBusy) {
            Set-LauncherBusy $false
            Update-Preview
        }
    }
}

function Invoke-AndroidDeploy {
    if (-not (Test-AndroidDeviceReady)) { return }
    $adb = Get-AndroidAdbPath
    if (-not (Test-Path $adb)) {
        if (-not (Ensure-AndroidToolchain)) { return }
        $adb = Get-AndroidAdbPath
        if (-not (Test-Path $adb)) {
            [System.Windows.MessageBox]::Show(("adb.exe not found:" + "`n" + $adb), "T850 Launcher", "OK", "Error")
            return
        }
    }
    if (-not (Test-AndroidDeviceReady)) { return }
    if (-not (Test-AndroidPackageInstalled)) { return }

    Save-Config
    $txtBuildOutput.Text = ""
    $pnlBuildOutput.Visibility = [System.Windows.Visibility]::Visible
    Set-LauncherBusy $true "DEPLOY"
    $txtStatus.Text = "Launching installed Android app..."
    $txtStatus.Foreground = $window.FindResource("AccentBrush")
    try {
        $txtStatus.Text = "Restarting Android app..."
        $stopExitCode = Invoke-LoggedProcess -FilePath $adb -Arguments (Get-AndroidAdbArguments (Get-AndroidForceStopArguments)) -WorkingDirectory $rootDir -StatusPrefix "Stopping Android app"
        if ($stopExitCode -ne 0) {
            $txtStatus.Text = "Android deploy stop failed (exit code $stopExitCode)"
            $txtStatus.Foreground = $window.FindResource("RedBrush")
            return
        }
        $exitCode = Invoke-LoggedProcess -FilePath $adb -Arguments (Get-AndroidAdbArguments (Get-AndroidLaunchArguments)) -WorkingDirectory $rootDir -StatusPrefix "Launching Android app"
        if ($exitCode -eq 0) {
            $txtStatus.Text = "Android app launched"
            $txtStatus.Foreground = $window.FindResource("GreenBrush")
        } else {
            $txtStatus.Text = "Android deploy launch failed (exit code $exitCode)"
            $txtStatus.Foreground = $window.FindResource("RedBrush")
        }
    } finally {
        Set-LauncherBusy $false
        Update-Preview
    }
}

# Shared build function — $buildTarget is "Build" (incremental) or "Rebuild" (clean)
function Invoke-Build {
    param([string]$buildTarget = "Build")

    if (Test-BrowserSelected) {
        Save-Config
        $txtBuildOutput.Text = ""
        $pnlBuildOutput.Visibility = [System.Windows.Visibility]::Visible
        $failureMessage = $null
        Set-LauncherBusy $true "BUILDING WEB..."
        try {
            $command = Get-BrowserBuildCommand -BuildTarget $buildTarget
            $result = Invoke-LoggedProcess -FilePath $command.ExePath -Arguments $command.Args -WorkingDirectory $command.WorkingDirectory -StatusPrefix "Building browser runtime"
            if ($result -ne 0) { throw "Browser build failed (exit code $result)." }
            Append-BuildOutput ("Browser {0} {1} succeeded." -f $cmbConfig.SelectedItem.Content, $buildTarget)
        } catch {
            $failureMessage = "Browser build failed: " + $_.Exception.Message
            Append-BuildOutput $failureMessage
        } finally {
            Set-LauncherBusy $false
            Update-Preview
            if ($failureMessage) {
                $txtStatus.Text = $failureMessage
                $txtStatus.Foreground = $window.FindResource("RedBrush")
            }
        }
        return
    }

    if (Test-AndroidTarget) {
        Invoke-AndroidBuild -Clean:($buildTarget -eq "Rebuild") | Out-Null
        return
    }

    $arch   = ($cmbArch.SelectedItem).Content.ToString().ToLower()
    $config = ($cmbConfig.SelectedItem).Content.ToString()

    $platform = switch ($arch) {
        "arm64" { "ARM64" }
        "x86"   { "x86" }
        default { "x64" }
    }

    if (-not (Ensure-WindowsToolchain -TargetPlatform $platform)) { return }

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
    $buildWorkers = Get-BuildWorkerCount
    $txtStatus.Text = "$label $config|$platform using $buildWorkers workers..."
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

    # Run the same build entry point used by GitHub Actions. Do not reuse the
    # current process path because a ps2exe launcher would recursively start
    # T850Launcher.exe instead of a PowerShell host.
    $powerShellExe = Find-PowerShellHost
    if (-not $powerShellExe) {
        $txtBuildOutput.Text = "ERROR: PowerShell host not found."
        $txtStatus.Text = "Build failed - PowerShell not found"
        $txtStatus.Foreground = $window.FindResource("RedBrush")
        $btnBuild.IsEnabled = $true
        $btnRebuild.IsEnabled = $true
        $btnBuild.Content = "BUILD"
        Update-Preview
        return
    }
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $powerShellExe
    $psi.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -Config {1} -Platform {2} -Action {3}' -f $buildScript, $config, $platform, $buildTarget
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $psi.CreateNoWindow = $true
    $psi.WorkingDirectory = $rootDir

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi

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
            $runtimeRoot = Get-WindowsRuntimeRoot
            $missingOutputs = @("DayScene.exe", "T8ditor.exe") |
                Where-Object { -not (Test-Path (Join-Path $runtimeRoot $_)) }
            if ($missingOutputs.Count -gt 0) {
                $txtStatus.Text = "Build completed, but runtime deployment is incomplete: $($missingOutputs -join ', ')"
                $txtStatus.Foreground = $window.FindResource("RedBrush")
                Append-BuildOutput ("ERROR: Missing expected output(s) in ${runtimeRoot}: " + ($missingOutputs -join ", "))
            } else {
                $txtStatus.Text = "Build and local deployment succeeded - $config|$platform"
                $txtStatus.Foreground = $window.FindResource("GreenBrush")
                # Refresh model list — build creates symlinks to Models/ etc.
                Populate-ModelList
            }
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

# DOWNLOAD ASSETS button — downloads only missing cloud assets
$btnDownloadAssets.Add_Click({
    $script:AssetDownloadInProgress = $true
    Set-LauncherBusy $true "DOWNLOADING..."
    try {
        if (-not (Invoke-LauncherModelDownload)) { return }
        Populate-ModelList
        Populate-SceneFileList
        Update-SceneDependencyCache
    } finally {
        $script:AssetDownloadInProgress = $false
        Set-LauncherBusy $false
        Update-Preview
    }
})

# RUN button — launch the app with current settings (no dump override)
$btnRun.Add_Click({
    if (Test-BrowserSelected) { Start-BrowserRuntime; return }
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
    if (Test-AndroidTarget) {
        Invoke-AndroidInstall | Out-Null
        return
    }

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
    if ((Test-WebGpuSelected) -or (Test-BrowserSelected)) {
        [System.Windows.MessageBox]::Show("WebGPU editor support is not implemented. Select another API for EDITOR.", "T850 Launcher", "OK", "Information") | Out-Null
        return
    }
    Populate-ModelList
    if (Test-AndroidTarget) {
        Update-SceneDependencyCache
        $sceneDeps = $script:SceneDependencyResult
        if (-not (Show-SceneDependencyError $sceneDeps)) { return }
        Invoke-AndroidDeploy
        return
    }

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
    $previous = if ($cmbModel.SelectedItem -and $cmbModel.SelectedItem.Tag) { $cmbModel.SelectedItem.Tag.ToString() } else { "" }
    $cmbModel.Items.Clear()
    $modelsDir = Join-Path $rootDir "Assets\Models"
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
        if (Test-AndroidTarget) {
            $scenesDir = Join-Path $rootDir "Assets\Scenes"
        } else {
            $runtimeScenes = Join-Path (Get-WindowsRuntimeRoot) "Scenes"
            $sourceScenes = Join-Path $rootDir "Assets\Scenes"
            $scenesDir = if (Test-Path $runtimeScenes) { $runtimeScenes } else { $sourceScenes }
        }
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
                if ($item.Tag -eq $previous) {
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
    Populate-ModelList
    Populate-SceneFileList
    Load-Config
    Update-LauncherCloudAssetStatus | Out-Null
    if (Test-AndroidTarget) { Refresh-AndroidDevices }
    Update-SceneOptionVisibility
    $script:DependencySetupActivity = Get-DependencySetupActivity
    Update-Preview
} finally {
    $script:LauncherInitializing = $false
}

$deviceRefreshTimer = New-Object System.Windows.Threading.DispatcherTimer
$deviceRefreshTimer.Interval = [TimeSpan]::FromSeconds(5)
$deviceRefreshTimer.Add_Tick({
    Refresh-DependencySetupActivity
    if (Test-AndroidTarget) {
        Refresh-AndroidDevices
        Update-Preview
    }
})
$window.Add_Closed({ $deviceRefreshTimer.Stop() })
$deviceRefreshTimer.Start()

$window.ShowDialog() | Out-Null
