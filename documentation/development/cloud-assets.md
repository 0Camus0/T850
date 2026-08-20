# Cloud Asset Workflow

Status: verified against `LaunchSolution.bat`, `DownloadModels.ps1`, `DownloadTextures.ps1`, and `ModelCloud.ps1` on 2026-08-19.

Heavy model and texture binaries are intentionally not all tracked by Git. Setup, the launcher, Steam Deck runtime, and CI use manifests to obtain them.

## Normal Workflow

From the repository root:

```powershell
.\LaunchSolution.bat --assets-only
```

This downloads the runtime model manifest and texture manifest, validates existing files, downloads only missing/invalid files, and exits.

Download the larger model set:

```powershell
.\LaunchSolution.bat --all-models --assets-only
```

Manual source-root commands:

```powershell
Set-Location F:\T850\T850
.\scripts\DownloadModels.ps1 -RootDir . -MaxThreads 7
.\scripts\DownloadTextures.ps1 -RootDir . -MaxThreads 7
```

Expected success messages end with `Models ready (...)` and `Textures ready (...)`. A nonzero exit means at least one required asset failed validation or download.

## Default Manifests

| Asset set | Default manifest |
|---|---|
| Runtime models | `https://pub-2fa5c50bbfbc4b829da0d6c6300815b0.r2.dev/runtime_assets.json` |
| Full models | `https://pub-2fa5c50bbfbc4b829da0d6c6300815b0.r2.dev/manifest.json` |
| Textures | `https://pub-ef5de729f9044220aa32f0601d99faa8.r2.dev/manifest.json` |

`LaunchSolution.bat` passes these URLs explicitly. To override them, invoke the PowerShell download scripts directly with `-ManifestUrl` or `-ManifestPath`.

## Manifest Resolution

When no URL is supplied, model manifest file lookup is:

1. explicit `-ManifestPath`;
2. `T850_MODEL_MANIFEST` environment variable;
3. `<RootDir>/Assets/model-cloud-manifest.json`;
4. `<RootDir>/model-cloud-manifest.json`;
5. a manifest beside `ModelCloud.ps1`.

Base URL precedence:

1. per-entry `url`;
2. explicit/base environment (`T850_MODEL_BASE_URL` or `T850_TEXTURE_BASE_URL`);
3. manifest `baseUrl`;
4. manifest `publicBaseUrl`.

URL environment variables used by lower-level helpers include `T850_MODEL_MANIFEST_URL` and `T850_TEXTURE_MANIFEST_URL`; wrapper scripts with a default URL must be passed an override explicitly.

## Supported Manifest Shapes

Model manifests may expose `models` or a common `assets` array. Texture manifests use `assets`.

A normalized asset entry can provide:

```json
{
  "kind": "model",
  "key": "Models/DamagedHelmet.glb",
  "url": "https://example.invalid/Models/DamagedHelmet.glb",
  "size": 123456,
  "sha256": "HEX_SHA256"
}
```

Path normalization:

- converts backslashes to slashes;
- removes a leading slash;
- removes a leading `Assets/`;
- prefixes `Models/` or `Textures/` when required;
- writes beneath the selected `AssetRoot`.

Never allow manifest paths to escape `Assets/`.

## Validation and Atomic Download

For each target file:

1. file must exist;
2. if `size` is present and positive, exact byte length must match;
3. otherwise file must be non-empty;
4. if `sha256` is present, SHA-256 must match case-insensitively.

Invalid files are treated as missing. Downloads use `<target>.download`, validate the temporary file, then move it into place. Failed temporary files are removed.

Downloads use a runspace pool. `-MaxThreads` defaults to 7 and is clamped to at least 1 and no more than the number of pending files.

## Status Without Downloading

Use the helper directly:

```powershell
Set-Location F:\T850\T850
. .\scripts\ModelCloud.ps1
Get-T850CloudAssetsStatus -RootDir . | Format-List
```

Important fields:

- `Ok`: manifest configuration has no errors;
- `Configured`: at least one asset manifest is active;
- `Total`, `Ready`, `Missing`;
- `MissingPaths`, `Errors`, `Details`.

## Launcher Behavior

The WPF launcher checks cloud status and scene dependencies before enabling Run/Install. `Download Assets` invokes the same model/texture logic and refreshes model/scene lists afterward.

On Steam Deck, `steamdeck/T850.sh` runs `DownloadCloudAssets.py` unless `T850_SKIP_ASSET_DOWNLOAD=1`. It writes failures to `logs/steamdeck_asset_download.log` but continues so tracked/package assets can still run.

## Asset Locations and Caches

Downloaded payloads belong under:

```text
Assets/Models/
Assets/Textures/
```

Generated caches are different from source/downloaded assets and are ignored:

```text
Assets/Models/**/.t8cache/
Assets/Shaders/.t8shadercache/
Assets/Textures/GeneratedIBLCache/
```

Do not publish generated cache files as source assets. Do not delete authored `.t8scene`, shader, font, layout, or ragdoll metadata while cleaning caches.

## Missing Assets in Visual Regression

The capture harness checks every case's declared asset list before launch. Missing assets become `skipped_missing_assets` in the manifest, not a passing image.

At the 2026-08-19 gate:

- Nexus is skipped because `Models/nexus_wars_terrain.glb` and `Models/marine.glb` are unavailable;
- Q3 Vulkan is a hardware skip on the 2 GiB Quadro P620, not an asset skip.

Do not convert a missing required asset into an accepted baseline.

## Troubleshooting

| Symptom | Action |
|---|---|
| manifest has no URL/base URL | pass `-ManifestUrl`, set a base URL, or add per-entry URLs |
| file repeatedly downloads | compare manifest `size`/`sha256` with the local file |
| `.download` remains | previous process was interrupted; rerun, which removes/replaces it |
| launcher blocks a scene | inspect its missing dependency list, then download the correct model set |
| private bucket paths fail | verify URL escaping and that manifest keys are resource-relative |
| offline work | retain already validated assets and use `--skip-assets`; unavailable scenes must remain skipped |

## Related Documents

- [Windows setup, build, and run](windows-build-and-run.md)
- [Resource locator](../architecture/resource-locator.md)
- [Visual regression](../debug/visual-regression.md)
- [Android deployment](../platform/android.md)
- [Steam Deck deployment](../platform/steam-deck.md)
