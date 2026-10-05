param(
    [Parameter(Mandatory = $true)]
    [string]$ReleaseDirectory,

    [switch]$RequireSignedAndroid
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$releaseRoot = [IO.Path]::GetFullPath($ReleaseDirectory)
if (-not (Test-Path -LiteralPath $releaseRoot -PathType Container)) {
    throw "Release directory does not exist: $releaseRoot"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem

function Test-ZipEntries {
    param(
        [Parameter(Mandatory = $true)] [IO.FileInfo]$Archive,
        [Parameter(Mandatory = $true)] [string[]]$RequiredPatterns
    )

    if ($Archive.Length -le 0) { throw "Package is empty: $($Archive.FullName)" }
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive.FullName)
    try {
        $entries = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        foreach ($pattern in $RequiredPatterns) {
            if (-not ($entries | Where-Object { $_ -match $pattern } | Select-Object -First 1)) {
                throw "$($Archive.Name) is missing an entry matching '$pattern'"
            }
        }
    } finally {
        $zip.Dispose()
    }
}

$allZipPackages = @(Get-ChildItem -LiteralPath $releaseRoot -File -Filter '*.zip')
$windowsPackages = @($allZipPackages | Where-Object Name -Match '^T850-(Win32|x64|ARM64)-Release\.zip$')
$webPackages = @($allZipPackages | Where-Object Name -EQ 'T850-Web-Release.zip')
$unknownZipPackages = @($allZipPackages | Where-Object { $_ -notin $windowsPackages -and $_ -notin $webPackages })
$androidPackages = @(Get-ChildItem -LiteralPath $releaseRoot -File -Filter '*.apk')
$steamPackages = @(Get-ChildItem -LiteralPath $releaseRoot -File -Filter 'T850-SteamDeck-*.tar.gz')
if (-not $windowsPackages.Count) { throw 'No Windows release ZIPs were found.' }
if ($webPackages.Count -ne 1) { throw "Expected one WebAssembly release ZIP, found $($webPackages.Count)." }
if ($unknownZipPackages.Count) { throw "Unknown release ZIPs were found: $($unknownZipPackages.Name -join ', ')" }
if (-not $androidPackages.Count) { throw 'No Android release APKs were found.' }
if (-not $steamPackages.Count) { throw 'No Steam Deck release tarball was found.' }

foreach ($package in $windowsPackages) {
    Test-ZipEntries $package @(
        '(^|/)DayScene\.exe$',
        '(^|/)T8ditor\.exe$',
        '(^|/)T850Launcher\.exe$',
        '(^|/)Shaders/.+',
        '(^|/)Scenes/.+',
        '(^|/)web/server\.mjs$',
        '(^|/)web/cloud-assets\.mjs$',
        '(^|/)web/site/DayScene\.wasm$',
        '(^|/)web/site/launcher\.html$',
        '(^|/)web/WebShaders/.+\.json$',
        '(^|/)web/CloudAssets/routes\.json$',
        '(^|/)web/assets/Scenes/.+'
    )
}

& (Join-Path $PSScriptRoot 'ValidateWebReleasePackage.ps1') -Archive $webPackages[0].FullName

foreach ($package in $androidPackages) {
    if ($RequireSignedAndroid -and $package.Name -match '-unsigned\.apk$') {
        throw "Tagged release contains an unsigned APK: $($package.Name)"
    }
    Test-ZipEntries $package @(
        '^AndroidManifest\.xml$',
        '^lib/(arm64-v8a|x86_64)/libT850Android\.so$',
        '^assets/.+'
    )
}

foreach ($package in $steamPackages) {
    if ($package.Length -le 0) { throw "Package is empty: $($package.FullName)" }
    $entries = @(& tar -tzf $package.FullName)
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect Steam Deck package: $($package.Name)" }
    foreach ($required in @(
        'T850-SteamDeck-Release/bin/SteamDeck/Release/DayScene',
        'T850-SteamDeck-Release/bin/SteamDeck/Release/T8ditor',
        'T850-SteamDeck-Release/bin/SteamDeck/Release/libc++.so.1',
        'T850-SteamDeck-Release/bin/SteamDeck/Release/libc++abi.so.1',
        'T850-SteamDeck-Release/bin/SteamDeck/Release/libunwind.so.1',
        'T850-SteamDeck-Release/steamdeck/T850.sh'
    )) {
        if ($entries -notcontains $required) {
            throw "$($package.Name) is missing '$required'"
        }
    }
}

$checksums = foreach ($package in Get-ChildItem -LiteralPath $releaseRoot -File |
    Where-Object Name -NE 'SHA256SUMS.txt' | Sort-Object Name) {
    $hash = [Security.Cryptography.SHA256]::Create()
    try {
        $stream = [IO.File]::OpenRead($package.FullName)
        try { $value = ([BitConverter]::ToString($hash.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
        finally { $stream.Dispose() }
    } finally {
        $hash.Dispose()
    }
    "$value  $($package.Name)"
}
[IO.File]::WriteAllLines((Join-Path $releaseRoot 'SHA256SUMS.txt'), $checksums)

Write-Host "Release package validation PASS: $($windowsPackages.Count) Windows, $($webPackages.Count) WebAssembly, $($androidPackages.Count) Android, $($steamPackages.Count) Steam Deck"