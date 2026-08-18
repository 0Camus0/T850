$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$AndroidProject = Join-Path $Root 'T850\android'
$Configuration = 'Release'
$AndroidSdk = $env:ANDROID_HOME
if (-not $AndroidSdk) { $AndroidSdk = $env:ANDROID_SDK_ROOT }
if (-not $AndroidSdk) { $AndroidSdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$AbiFilters = 'arm64-v8a'
$TemplateApk = $null
$OutputApk = $null
$Install = $false
$Launch = $false
$SkipNativeBuild = $false
$VulkanValidation = $false
$NdkVersion = '27.2.12479018'

$BuildWorkers = [Math]::Max(1, [Environment]::ProcessorCount - 1)
if ($env:T850_BUILD_WORKERS) {
  $parsedWorkers = 0
  if ([int]::TryParse($env:T850_BUILD_WORKERS, [ref]$parsedWorkers) -and $parsedWorkers -gt 0) {
    $BuildWorkers = $parsedWorkers
  }
}
$env:CMAKE_BUILD_PARALLEL_LEVEL = $BuildWorkers.ToString()

function Show-Usage {
  Write-Host 'Usage: T850\scripts\android\BuildAndroidFastApk.bat [Debug|Release] [--sdk C:\Android\Sdk] [--abi ABI[,ABI...]] [--template APK] [--out APK] [--install] [--launch] [--skip-native-build] [--vulkan-validation]'
  Write-Host ''
  Write-Host 'Builds/strips the native .so, copies an existing APK as a template, replaces only lib/<abi>/libT850Android.so, zipaligns, and signs it.'
}

for ($i = 0; $i -lt $args.Count; $i++) {
  $arg = $args[$i]
  switch -Regex ($arg) {
    '^(?i)Debug$' { $Configuration = 'Debug'; continue }
    '^(?i)Release$' { $Configuration = 'Release'; continue }
    '^(?i)--configuration$|^(?i)--config$' {
      if (++$i -ge $args.Count) { Show-Usage; exit 1 }
      if ($args[$i] -notmatch '^(?i)(Debug|Release)$') { Show-Usage; exit 1 }
      $Configuration = if ($args[$i] -match '^(?i)Debug$') { 'Debug' } else { 'Release' }
      continue
    }
    '^(?i)--sdk$' {
      if (++$i -ge $args.Count) { Show-Usage; exit 1 }
      $AndroidSdk = $args[$i]
      continue
    }
    '^(?i)--abi$|^(?i)--abis$' {
      if (++$i -ge $args.Count) { Show-Usage; exit 1 }
      $AbiFilters = $args[$i]
      continue
    }
    '^(?i)--emulator$' { $AbiFilters = 'x86_64'; continue }
    '^(?i)--template$|^(?i)--template-apk$' {
      if (++$i -ge $args.Count) { Show-Usage; exit 1 }
      $TemplateApk = $args[$i]
      continue
    }
    '^(?i)--out$|^(?i)--output$|^(?i)--output-apk$' {
      if (++$i -ge $args.Count) { Show-Usage; exit 1 }
      $OutputApk = $args[$i]
      continue
    }
    '^(?i)--install$' { $Install = $true; continue }
    '^(?i)--launch$' { $Install = $true; $Launch = $true; continue }
    '^(?i)--skip-native-build$' { $SkipNativeBuild = $true; continue }
    '^(?i)--vulkan-validation$' { $VulkanValidation = $true; continue }
    '^(?i)-h$|^(?i)--help$' { Show-Usage; exit 0 }
    default { Show-Usage; exit 1 }
  }
}

if (-not (Test-Path (Join-Path $AndroidProject 'build.gradle'))) {
  throw "Android project was not found at '$AndroidProject'."
}
$GradleWrapper = Join-Path $AndroidProject 'gradlew.bat'
if (-not (Test-Path $GradleWrapper)) {
  throw "Gradle wrapper was not found at '$GradleWrapper'. Restore T850\android\gradlew.bat and gradle\wrapper before building."
}
if (-not (Test-Path $AndroidSdk)) {
  throw "Android SDK was not found at '$AndroidSdk'. Pass --sdk or set ANDROID_HOME."
}

$env:ANDROID_HOME = $AndroidSdk
$env:ANDROID_SDK_ROOT = $AndroidSdk
$env:ANDROID_NDK_HOME = Join-Path $AndroidSdk "ndk\$NdkVersion"
$env:ANDROID_NDK_ROOT = $env:ANDROID_NDK_HOME
if (-not (Test-Path $env:ANDROID_NDK_HOME)) {
  throw "Android NDK $NdkVersion was not found at '$env:ANDROID_NDK_HOME'."
}

if (-not $env:JAVA_HOME) {
  $javaHomes = @(
    "${env:ProgramFiles}\Android\Android Studio\jbr",
    "${env:ProgramFiles(x86)}\Android\Android Studio\jbr"
  )
  $javaHomes += Get-ChildItem -Path "${env:ProgramFiles}\Eclipse Adoptium","${env:ProgramFiles}\Java" -Directory -Filter 'jdk-*' -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    Select-Object -ExpandProperty FullName
  $jdk = $javaHomes | Where-Object { $_ -and (Test-Path (Join-Path $_ 'bin\java.exe')) } | Select-Object -First 1
  if ($jdk) { $env:JAVA_HOME = $jdk }
}
if ($env:JAVA_HOME) { $env:PATH = "$env:JAVA_HOME\bin;$env:PATH" }
if (-not (Get-Command java -ErrorAction SilentlyContinue)) {
  throw 'JDK 17+ was not found. Run SetupAndroidToolchain.bat first.'
}
$jarExe = if ($env:JAVA_HOME) { Join-Path $env:JAVA_HOME 'bin\jar.exe' } else { $null }
if (-not $jarExe -or -not (Test-Path $jarExe)) {
  $jarCommand = Get-Command jar -ErrorAction SilentlyContinue
  if ($jarCommand) { $jarExe = $jarCommand.Source }
}
if (-not $jarExe -or -not (Test-Path $jarExe)) {
  throw 'jar.exe was not found in the JDK. Run SetupAndroidToolchain.bat first.'
}

$buildToolsDir = Get-ChildItem (Join-Path $AndroidSdk 'build-tools') -Directory -ErrorAction Stop |
  Where-Object { (Test-Path (Join-Path $_.FullName 'zipalign.exe')) -and (Test-Path (Join-Path $_.FullName 'apksigner.bat')) } |
  Sort-Object Name -Descending |
  Select-Object -First 1
if (-not $buildToolsDir) {
  throw "Android build-tools with zipalign/apksigner were not found under '$AndroidSdk\build-tools'."
}
$zipalign = Join-Path $buildToolsDir.FullName 'zipalign.exe'
$apksigner = Join-Path $buildToolsDir.FullName 'apksigner.bat'
$adb = Join-Path $AndroidSdk 'platform-tools\adb.exe'

$variant = $Configuration.ToLowerInvariant()
$stripTask = "strip${Configuration}DebugSymbols"
$validationValue = if ($VulkanValidation) { 'true' } else { 'false' }

Write-Host ''
Write-Host '========================================'
Write-Host " T850 - Android $Configuration Fast APK"
Write-Host '========================================'
Write-Host "SDK root: $AndroidSdk"
Write-Host "Project : $AndroidProject"
Write-Host "ABIs    : $AbiFilters"
Write-Host "Vulkan validation: $validationValue"
Write-Host "Workers : $BuildWorkers (cores - 1)"
Write-Host 'Mode    : native .so rebuild + APK repack'
Write-Host ''

if (-not $SkipNativeBuild) {
  Push-Location $AndroidProject
  try {
    & $GradleWrapper --no-daemon --console=plain --parallel "--max-workers=$BuildWorkers" "-Pt850AndroidAbis=$AbiFilters" "-Pt850VulkanValidation=$validationValue" ":app:$stripTask"
    if ($LASTEXITCODE -ne 0) { throw "Gradle $stripTask failed with exit code $LASTEXITCODE." }
  } finally {
    Pop-Location
  }
}

if (-not $TemplateApk) {
  $candidate = Join-Path $AndroidProject "app\build\outputs\apk\$variant\app-$variant.apk"
  if ($variant -eq 'release') {
    $unsigned = Join-Path $AndroidProject 'app\build\outputs\apk\release\app-release-unsigned.apk'
    if (Test-Path $unsigned) { $candidate = $unsigned }
  }
  $TemplateApk = $candidate
}
if (-not (Test-Path $TemplateApk)) {
  throw "Template APK was not found at '$TemplateApk'. Run a full T850\scripts\android\BuildAndroid.bat once, or pass --template."
}

$defaultOutDir = Join-Path $AndroidProject "app\build\outputs\apk\$variant"
New-Item -ItemType Directory -Force -Path $defaultOutDir | Out-Null
if (-not $OutputApk) {
  $OutputApk = Join-Path $defaultOutDir "app-$variant-fast-signed.apk"
}
$OutputApk = [System.IO.Path]::GetFullPath($OutputApk)
$workApk = Join-Path $defaultOutDir "app-$variant-fast-unsigned.apk"
$alignedApk = Join-Path $defaultOutDir "app-$variant-fast-aligned.apk"

Copy-Item -Force $TemplateApk $workApk

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stageDir = Join-Path ([System.IO.Path]::GetTempPath()) ('t850-fast-apk-' + [guid]::NewGuid().ToString('N'))
$zip = [System.IO.Compression.ZipFile]::Open($workApk, [System.IO.Compression.ZipArchiveMode]::Update)
try {
  @($zip.Entries | Where-Object { $_.FullName -like 'META-INF/*' }) | ForEach-Object { $_.Delete() }

  $abis = $AbiFilters.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ }
  foreach ($abi in $abis) {
    $entryName = "lib/$abi/libT850Android.so"
    @($zip.Entries | Where-Object { $_.FullName -eq $entryName }) | ForEach-Object { $_.Delete() }

    $soCandidates = @(
      (Join-Path $AndroidProject "app\build\intermediates\stripped_native_libs\$variant\$stripTask\out\lib\$abi\libT850Android.so"),
      (Join-Path $AndroidProject "app\build\intermediates\merged_native_libs\$variant\merge${Configuration}NativeLibs\out\lib\$abi\libT850Android.so")
    )
    $soPath = $soCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $soPath) {
      $soPath = Get-ChildItem (Join-Path $AndroidProject 'app\build\intermediates\cxx') -Filter libT850Android.so -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\$abi\*" } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $soPath) { throw "Native library for ABI '$abi' was not found." }

    $stageAbiDir = Join-Path $stageDir "lib\$abi"
    New-Item -ItemType Directory -Force -Path $stageAbiDir | Out-Null
    Copy-Item -Force $soPath (Join-Path $stageAbiDir 'libT850Android.so')
    Write-Host "Replaced $entryName with $soPath"
  }
} finally {
  $zip.Dispose()
}

try {
  & $jarExe uf0 $workApk -C $stageDir lib
  if ($LASTEXITCODE -ne 0) { throw "jar failed with exit code $LASTEXITCODE." }
} finally {
  Remove-Item -Recurse -Force $stageDir -ErrorAction SilentlyContinue
}

& $zipalign -f -p 4 $workApk $alignedApk
if ($LASTEXITCODE -ne 0) { throw "zipalign failed with exit code $LASTEXITCODE." }

$debugKeystore = Join-Path $env:USERPROFILE '.android\debug.keystore'
if (-not (Test-Path $debugKeystore)) {
  throw "Debug keystore was not found at '$debugKeystore'. Build/install a Debug APK once or create a signing keystore."
}

& $apksigner sign --ks $debugKeystore --ks-pass pass:android --key-pass pass:android --out $OutputApk $alignedApk
if ($LASTEXITCODE -ne 0) { throw "apksigner sign failed with exit code $LASTEXITCODE." }
& $apksigner verify --verbose $OutputApk
if ($LASTEXITCODE -ne 0) { throw "apksigner verify failed with exit code $LASTEXITCODE." }

foreach ($tempApk in @($workApk, $alignedApk)) {
  if ([System.IO.Path]::GetFullPath($tempApk) -ne $OutputApk) {
    Remove-Item -Force $tempApk -ErrorAction SilentlyContinue
  }
}

if ($Install) {
  if (-not (Test-Path $adb)) { throw "adb.exe was not found at '$adb'." }
  & $adb install -r $OutputApk
  if ($LASTEXITCODE -ne 0) { throw "adb install failed with exit code $LASTEXITCODE." }
}
if ($Launch) {
  & $adb shell am start -n com.t850.engine/.LauncherActivity
  if ($LASTEXITCODE -ne 0) { throw "adb launch failed with exit code $LASTEXITCODE." }
}

Write-Host ''
Write-Host "[T850] Android $Configuration fast APK complete."
Write-Host "APK: $OutputApk"
