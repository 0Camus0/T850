param(
  [string]$Repo = '0Camus0/T850',
  [string]$KeystorePath = (Join-Path $env:USERPROFILE '.android\t850-release.keystore'),
  [string]$Alias = 't850-release',
  [switch]$Create,
  [switch]$GeneratePasswords,
  [switch]$Force
)

$ErrorActionPreference = 'Stop'

function New-RandomPassword {
  param([int]$Length = 32)
  $chars = 'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789'.ToCharArray()
  $bytes = New-Object byte[] $Length
  [System.Security.Cryptography.RandomNumberGenerator]::Fill($bytes)
  $result = New-Object char[] $Length
  for ($i = 0; $i -lt $Length; $i++) {
    $result[$i] = $chars[$bytes[$i] % $chars.Length]
  }
  -join $result
}

function ConvertFrom-SecureStringPlainText {
  param([Security.SecureString]$Value)
  $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Value)
  try {
    [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
  } finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
  }
}

function Set-GitHubSecret {
  param(
    [string]$Name,
    [string]$Value
  )
  $temp = [IO.Path]::GetTempFileName()
  try {
    [IO.File]::WriteAllText($temp, $Value, [Text.UTF8Encoding]::new($false))
    Get-Content -Raw -Path $temp | & gh secret set $Name --repo $Repo --app actions
    if ($LASTEXITCODE -ne 0) { throw "Failed to set GitHub secret $Name." }
  } finally {
    Remove-Item -Force $temp -ErrorAction SilentlyContinue
  }
}

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
  throw 'GitHub CLI was not found. Install gh and authenticate with: gh auth login'
}

& gh auth status --hostname github.com | Out-Null
if ($LASTEXITCODE -ne 0) {
  throw 'GitHub CLI is not authenticated. Run: gh auth login'
}

$keytool = $null
if ($env:JAVA_HOME) {
  $candidate = Join-Path $env:JAVA_HOME 'bin\keytool.exe'
  if (Test-Path $candidate) { $keytool = $candidate }
}
if (-not $keytool) {
  $command = Get-Command keytool -ErrorAction SilentlyContinue
  if ($command) { $keytool = $command.Source }
}
if (-not $keytool) {
  throw 'keytool was not found. Install JDK 17+ or set JAVA_HOME.'
}

$keystoreDir = Split-Path -Parent $KeystorePath
New-Item -ItemType Directory -Force -Path $keystoreDir | Out-Null

$keystorePassword = $null
$keyPassword = $null

if ($Create) {
  if ((Test-Path $KeystorePath) -and -not $Force) {
    throw "Keystore already exists at '$KeystorePath'. Pass -Force to replace it, or omit -Create to upload the existing keystore."
  }

  if ($GeneratePasswords) {
    $keystorePassword = New-RandomPassword
    $keyPassword = $keystorePassword
  } else {
    $keystorePassword = ConvertFrom-SecureStringPlainText (Read-Host 'New release keystore/key password' -AsSecureString)
    $keyPassword = $keystorePassword
  }

  if ($Force -and (Test-Path $KeystorePath)) {
    Remove-Item -Force $KeystorePath
  }

  & $keytool -genkeypair -v `
    -keystore $KeystorePath `
    -storepass $keystorePassword `
    -keypass $keyPassword `
    -alias $Alias `
    -keyalg RSA `
    -keysize 4096 `
    -validity 10000 `
    -dname 'CN=T850 Android Release, OU=T850, O=0Camus0, L=Local, S=Local, C=US'
  if ($LASTEXITCODE -ne 0) { throw "keytool failed with exit code $LASTEXITCODE." }
} else {
  if (-not (Test-Path $KeystorePath)) {
    throw "Keystore was not found at '$KeystorePath'. Pass -Create to generate one."
  }
  $keystorePassword = ConvertFrom-SecureStringPlainText (Read-Host 'Release keystore password' -AsSecureString)
  $keyPasswordInput = ConvertFrom-SecureStringPlainText (Read-Host 'Release key password (leave blank to reuse keystore password)' -AsSecureString)
  $keyPassword = if ([string]::IsNullOrWhiteSpace($keyPasswordInput)) { $keystorePassword } else { $keyPasswordInput }
}

$keystoreBase64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes((Resolve-Path $KeystorePath)))

Set-GitHubSecret -Name 'ANDROID_KEYSTORE_BASE64' -Value $keystoreBase64
Set-GitHubSecret -Name 'ANDROID_KEYSTORE_PASSWORD' -Value $keystorePassword
Set-GitHubSecret -Name 'ANDROID_KEY_ALIAS' -Value $Alias
Set-GitHubSecret -Name 'ANDROID_KEY_PASSWORD' -Value $keyPassword

if ($Create -and $GeneratePasswords) {
  $backupPath = Join-Path $keystoreDir 't850-release-signing.txt'
  $backup = @(
    'T850 Android release signing backup',
    "CreatedUtc=$([DateTime]::UtcNow.ToString('o'))",
    "Repository=$Repo",
    "KeystorePath=$([IO.Path]::GetFullPath($KeystorePath))",
    "ANDROID_KEY_ALIAS=$Alias",
    "ANDROID_KEYSTORE_PASSWORD=$keystorePassword",
    "ANDROID_KEY_PASSWORD=$keyPassword"
  ) -join [Environment]::NewLine
  [IO.File]::WriteAllText($backupPath, $backup, [Text.UTF8Encoding]::new($false))
  if ($env:OS -eq 'Windows_NT') {
    & icacls $backupPath /inheritance:r /grant:r "${env:USERNAME}:(R,W)" | Out-Null
  }
  Write-Host "Local signing backup written to $backupPath"
}

Write-Host "Android release signing secrets configured for $Repo."
Write-Host "Keystore: $([IO.Path]::GetFullPath($KeystorePath))"
Write-Host 'Back up this keystore and the passwords. Losing them means users cannot update existing installs.'