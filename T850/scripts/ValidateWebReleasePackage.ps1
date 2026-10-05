[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Archive
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$archivePath = [IO.Path]::GetFullPath($Archive)
if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) { throw "Browser release package does not exist: $archivePath" }
if ((Get-Item -LiteralPath $archivePath).Length -le 0) { throw "Browser release package is empty: $archivePath" }
if ((Get-Item -LiteralPath $archivePath).Length -gt 128MB) { throw "Browser release package exceeds 128 MiB: $archivePath" }

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    $entries = @($zip.Entries | Where-Object { $_.Name } | ForEach-Object {
        [pscustomobject]@{ Path = $_.FullName.Replace('\', '/'); Entry = $_ }
    })
    foreach ($pattern in @(
        '^README\.txt$',
        '^Start-T850-Web\.cmd$',
        '^Start-T850-Web\.sh$',
        '^web/server\.mjs$',
        '^web/cloud-assets\.mjs$',
        '^web/cloudflare-config\.mjs$',
        '^web/site/DayScene\.html$',
        '^web/site/DayScene\.js$',
        '^web/site/DayScene\.wasm$',
        '^web/site/launcher\.html$',
        '^web/site/launcher\.mjs$',
        '^web/site/previews/.+\.png$',
        '^web/site/scenes\.json$',
        '^web/WebShaders/.+\.json$',
        '^web/CloudAssets/routes\.json$',
        '^web/assets/Scenes/.+'
    )) {
        if (-not ($entries | Where-Object Path -Match $pattern | Select-Object -First 1)) {
            throw "Browser release package is missing an entry matching '$pattern'"
        }
    }

    $wasm = $entries | Where-Object Path -EQ 'web/site/DayScene.wasm' | Select-Object -First 1
    if ($wasm.Entry.Length -lt 1MB) { throw 'Browser WebAssembly binary is unexpectedly small.' }
    $forbidden = @($entries | Where-Object Path -Match '^web/assets/.+\.(bin|dds|glb|gltf|t8ibl|zip)$')
    if ($forbidden.Count) { throw "Heavy cloud asset payloads were bundled: $($forbidden.Path -join ', ')" }
    $assetBytes = ($entries | Where-Object Path -Like 'web/assets/*' | Measure-Object { $_.Entry.Length } -Sum).Sum
    if ($assetBytes -gt 40MB) { throw "Tracked browser metadata exceeds 40 MiB: $assetBytes bytes" }

    $routeEntry = ($entries | Where-Object Path -EQ 'web/CloudAssets/routes.json' | Select-Object -First 1).Entry
    $reader = [IO.StreamReader]::new($routeEntry.Open())
    try { $catalog = $reader.ReadToEnd() | ConvertFrom-Json }
    finally { $reader.Dispose() }
    $routes = @($catalog.routes.PSObject.Properties)
    if ($catalog.version -ne 1 -or $routes.Count -eq 0) { throw 'Browser cloud route catalog is invalid or empty.' }
    if (-not $catalog.routes.PSObject.Properties['Textures/sky/CubeMap_SkyWater.dds']) {
        throw 'Browser cloud route catalog is missing the required sky resource.'
    }
    foreach ($route in $routes) {
        $uri = [Uri]$route.Value.url
        if ($uri.Scheme -ne 'https' -or $uri.UserInfo -or $uri.Query -or $uri.Fragment) {
            throw "Browser cloud route is not a credential-free HTTPS URL: $($route.Name)"
        }
    }

    Write-Host "Browser release package validation PASS: $($entries.Count) files, $($routes.Count) cloud routes"
} finally {
    $zip.Dispose()
}