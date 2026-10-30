# Cloud Asset Workflow

Status: verified against `LaunchSolution.bat`, `DownloadModels.ps1`, `DownloadTextures.ps1`, and `ModelCloud.ps1` on 2026-08-19; publishing verified against `PushCloudTextures.ps1` on 2026-10-30.

Heavy model and texture binaries are intentionally not all tracked by Git. Setup,
the launcher and Steam Deck runtime use manifests to obtain them. Browser CI uses
the same manifests only to generate runtime cloud routes.

## Browser Delivery Modes

The Emscripten runtime has two explicit asset delivery modes. Neither mode commits
downloaded model, texture, IBL or archive payloads to Git.

### A. Bundled Deployment

```powershell
.\scripts\BuildWeb.ps1 -AssetMode Embedded
```

`Embedded` is the default. It runs the full native scene-startup shader export
against locally available assets. The existing deployment preparation command:

```powershell
node .\web\prepare-pages.mjs --minecraft-only
```

then creates a content-addressed ZIP containing
the selected scene assets and prepared shaders. The browser verifies the ZIP hash
and mounts it under `/assets` before starting Emscripten. This mode supports an
offline, self-contained deployment package; source asset payloads remain ignored.

### B. Runtime Cloud Delivery

```powershell
.\scripts\BuildWeb.ps1 -AssetMode Cloud -Clean
```

`Cloud` compiles the checked-in graphics/compute shader inventory without launching
asset-dependent native scenes. It reads the two pinned public manifests and writes
only `build/web/CloudAssets/routes.json`. This file contains validated public HTTPS
URLs, content types and optional sizes; it contains no asset bytes.

`web/server.mjs` detects the catalog and combines its paths with tracked local
resources and `WebShaders`. When Emscripten requests `/assets/<resource>`, local
files remain local and cataloged cloud models/textures are proxied from their public
origin on demand. Path traversal, credentials, redirects and unapproved manifest
origins are rejected. Internet access is required while uncached cloud resources
are used.

GitHub CI uses mode B. Its `T850-Web-Release` artifact contains only `site/`,
`WebShaders/` and the small route catalog. Native x64 and ARM64 Edge jobs consume
the same architecture-neutral artifact. CI does not download or upload the 1.61 GiB
runtime model/texture payload.

## Publishing Assets to R2

`scripts/PushCloudTextures.ps1` is the other half of the workflow: it uploads texture
files to the `t850-textures` bucket and republishes the shared manifest. It is meant for
the machine that authors assets, not for players.

```powershell
# review first: prints the uploads, the entry changes and any prune candidates
.\scripts\PushCloudTextures.ps1 -Prefix Textures\Ball

# publish
.\scripts\PushCloudTextures.ps1 -Prefix Textures\Ball -Apply
```

The script needs an API token with R2 object read/write on the account. It reads
`C:\Users\camus\Documents\cloud.txt` by default, or `$env:CLOUDFLARE_API_TOKEN` when that
is set; the token is never written into the repository and never printed. Both public
buckets live in account `66cd75c80eda50d1a2ae018f7f7136b6`.

Rules the script enforces, and why:

- **Objects before manifest, manifest before deletes.** A consumer must never read a
  manifest that names an object which is not stored yet.
- **The manifest is the source of truth and is only in the cloud.** There is no committed
  local copy of the union manifest; the script downloads it from both buckets, aborts if
  the two copies have drifted, and changes only the entries for the files it was pointed
  at. Untouched entries keep their bytes (a PowerShell JSON round trip of the current
  manifest is byte-identical, so publishing does not churn 114 entries).
- **Both buckets hold the same manifest.** `t850-models` and `t850-textures` each serve a
  copy because the Pages worker reads the manifest through its own bucket binding. The
  script publishes to both and re-verifies both afterwards.
- **Verify the object, not the status code.** Every upload is read back through the
  authenticated API and its SHA-256 compared with the local file.
- **New entries carry a `sha256`; older entries stay size-only.** The downloaders already
  verify `sha256` when present (case-insensitively), so new files get integrity checks
  without rewriting history.
- **Idempotent.** A run with nothing to change reports "the manifest is already current"
  and publishes nothing. `-Force` re-uploads everything under the prefix; `-Prune`
  deletes objects (and their entries) that no longer exist locally, and is never implied.
- **r2.dev propagates, it does not race.** A freshly stored key can take a minute or two
  to appear on the public `r2.dev` hostname; the script reports that as a warning and
  still exits successfully because the object itself was verified.
- **Manifest caching.** `manifest.json` is stored with `public, max-age=300`, so a publish
  is visible within five minutes even through the edge cache. Texture objects use
  `immutable` caching because a key's content is expected to stay fixed.

Adding a new asset family is a palette or a prefix, not a code change: point `-Prefix` at
the new directory, run the dry run, then `-Apply`. `Assets/Textures/Ball/` (the Backyard
ball PBR palette) is published this way and is not tracked by Git.

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

The Backyard ball palette is one of them:

```text
Assets/Textures/Ball/<MaterialId>/Color.jpg
Assets/Textures/Ball/<MaterialId>/Normal.png
Assets/Textures/Ball/<MaterialId>/ORM.png
```

It is produced by `scripts/textures/fetch_ball_materials.py` (authoring only: ambientCG
CC0 archives, packed to 512 px and to the glTF metallic-roughness layout) and published by
`scripts/PushCloudTextures.ps1`. The material list itself is tracked in
`Assets/Scenes/BackyardBallMaterials.json`, so the palette is data, not code.

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
