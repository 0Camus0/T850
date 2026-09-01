---
name: t850-voxel-terrain
description: "Use when implementing, changing, debugging, testing, or extending T850 voxel terrain, mutable procedural meshes, block placement/removal, greedy meshing, chunk streaming, voxel collision, or world persistence."
argument-hint: "State whether the task affects blocks, chunks, meshing, rendering, streaming, collision, persistence, or VoxelScene gameplay."
---

# T850 Voxel Terrain Workflow

Use the Framework terrain APIs. Do not put backend-specific graphics code in `VoxelScene`, generate one render object per block, or extend `SceneTemplate.cpp` for voxel behavior.

## Roots and Owner Files

```powershell
$RepoRoot = git rev-parse --show-toplevel
$SourceRoot = Join-Path $RepoRoot 'T850'
Set-Location $SourceRoot
```

Read first:

```text
documentation/terrain/voxel-terrain.md
Framework/include/terrain/
Framework/include/scene/MutableMeshData.h
Framework/include/scene/MutableMesh.h
DayScene/VoxelScene.h
```

Route ownership:

| Change | Owner |
|---|---|
| block definition | `BlockRegistry` |
| chunk storage/version | `VoxelChunk` |
| world/chunk coordinates, DDA | `VoxelWorld` |
| face generation/merging | `VoxelMesher` |
| atlas tile metadata | `BlockRegistry` |
| CPU snapshot validation | `MutableMeshData` |
| GPU upload/draw/lifetime | `MutableMesh` and backend `RetireBuffer` |
| desired radius/jobs/budgets | `VoxelStreamingManager` |
| sparse save/load | `VoxelDeltaStore` |
| complete standable-cell paths | `VoxelNavigation` |
| exact swept-box voxel collision | `VoxelCollision` |
| player/input/reference integration | `VoxelScene` |

## Non-Negotiable Contracts

- Workers produce owned `VoxelChunkBuildResult`; they never create GPU resources.
- `MutableMesh::ReplaceSnapshot` runs on the render/main thread.
- Check `request.IsCancelled()` inside long generation loops.
- Result key and epoch must still be desired before commit.
- Respect launch, in-flight, commit, and unload budgets.
- One chunk produces material sections; never create one primitive/entity/Jolt body per block.
- Use floor division/modulo for negative world coordinates through `VoxelWorld`.
- Block ID 0 is air.
- Persist authored deltas, not regenerated base terrain.
- Regression mode must not consume user save data.
- Add every new Framework source to `.vcxproj`, filters, and CMake.

## Add a Block Type

Register before streaming jobs start:

```cpp
t850::terrain::BlockDefinition block;
block.name = "granite";
block.color = XVECTOR3(0.45f, 0.42f, 0.40f, 1.0f);
block.roughness = 0.9f;
block.occludes = true;
block.collidable = true;
const auto granite = registry.Register(std::move(block));
```

Registration order currently defines persisted `BlockId`. Do not reorder existing blocks in a save-compatible world until a named palette migration exists.

## Generate a Chunk

The build callback is CPU-only:

```cpp
VoxelChunkBuildResult Build(const VoxelChunkBuildRequest& request) {
  VoxelChunkBuildResult result;
  result.key = request.key;
  result.epoch = request.epoch;
  result.chunk = std::make_unique<VoxelChunk>(request.key, request.dimensions);

  for (int z = 0; z < request.dimensions.z; ++z) {
    if (request.IsCancelled()) {
      result.cancelled = true;
      result.chunk.reset();
      return result;
    }
    // Deterministically populate blocks.
  }

  deltas.ApplyToChunk(*result.chunk);
  if (!BuildGreedyVoxelMesh(*result.chunk, registry, {}, result.mesh, &result.error)) {
    result.chunk.reset();
  }
  return result;
}
```

Generation must depend only on stable world seed, chunk key, settings, and saved deltas.

## Commit and Unload

On the main thread:

1. call `streaming.Update(focus, loaded, threadPool, build)`;
2. consume at most `TakeCompleted()` budget;
3. recheck `IsDesired(key)`;
4. `VoxelWorld::AdoptChunk()`;
5. create/replace `MutableMesh`;
6. create/replace one generated Jolt triangle body when dynamic-object collision is enabled;
7. add/remove through `RenderContainer` and physics generational handles;
8. process `TakeUnloadRequests()` and destroy both handles;
9. update telemetry.

Call `streaming.Reset()` before destroying captured registry/delta/world state.

## Mutable Mesh Rules

Build complete immutable snapshots. A block edit dirties chunks, not GPU byte ranges.

Required snapshot data:

```text
version
vertices: position + normal + UV
32-bit triangle indices
sections with valid material indices
materials
local AABB
```

Call `RecalculateMutableMeshBounds()` then `ValidateMutableMeshSnapshot()` before commit.

D3D11/GL release replaced buffers immediately. D3D12/Vulkan use `BaseDriver::RetireBuffer()` and delay release across all in-flight frames. Never call backend APIs from terrain code.

For atlas blocks, set `usesBaseColorTexture` and the normalized `atlasU0/V0/U1/V1` rectangle before jobs start. Bind the shared texture at primitive slot 0 (`DiffuseTex`). The current mesher deliberately keeps textured faces as unit quads so a tile does not stretch over a greedy-merged face.

## Block Interaction

Use `VoxelWorld::Raycast()` for selection:

- hit coordinates remove a block;
- previous coordinates place a block;
- maximum distance is gameplay-owned.

After a successful edit:

```cpp
world.SetBlock(x, y, z, block);
deltas.Record(x, y, z, block);
deltas.Save(path, &error);
```

Remesh the changed chunk and any loaded neighbor sharing a boundary. The current reference scene conservatively remeshes all loaded chunks; improve this before increasing the streaming radius substantially.

## Collision and Navigation

Player traversal uses `CharacterCollisionWorld` sweeps against collidable voxels. Keep this as the primary mutable-world collision path.

The reference scene adds Jolt chunk bodies for dynamic rigid-body terrain collision:

- one body per active chunk;
- generated triangle mesh, not boxes per voxel;
- no disk cache for mutable snapshots;
- destroy/recreate through generational handles;
- build/cook off the frame path where Jolt permits it, commit bodies on the owning thread.

Minecraft uses `VoxelNavigation` complete-path A* over loaded standable cells. Keep swept voxel collision authoritative, increment the world revision after topology/residency changes, and reject stale paths. Recast is an optional Minecraft diagnostic overlay; generic mesh scenes still use it for gameplay. For many voxel NPCs, add chunk-region portals and hierarchical search before tiled Detour.

## Persistence

`VoxelDeltaStore` uses versioned `.t8vox` binary data with count bounds and payload hash. `ResourceLocator::WriteBinaryAtomic()` writes temp + replace.

Use cache-relative world paths:

```text
VoxelWorlds/<world-id>/edits.t8vox
```

Never write generated state into packaged Android assets.

## Focused Gates

Build and self-tests:

```powershell
.\scripts\build.ps1 -Config Debug -Platform x64
.\bin\x64\Debug\DayScene.exe --game-selftest
```

Expected: 43 PASS lines, exit 0.

Focused Release visuals:

```powershell
.\scripts\build.ps1 -Config Release -Platform x64
.\scripts\CaptureVisualBaselines.ps1 `
  -RunSet candidate `
  -Cases voxel-streaming `
  -Apis d3d11,d3d12,gl,vulkan `
  -Width 1280 -Height 720 `
  -DumpSeconds 5 `
  -Force -ContinueOnError
```

Expected: four `captured`, zero engine errors, image standard deviation >=1.

After changing Minecraft block definitions, face tiles, or `terrain.png`, run the exact asset contract gate:

```powershell
python .\scripts\verify_minecraft_atlas.py
```

Require the audited atlas fingerprint plus 20 blocks and 120 face mappings to pass before rendering.

For renderer/lifetime crashes, use `t850-crash-debugging` and run Debug under CDB. Debug CRT assertions do not show Abort/Retry/Ignore because entry points install `InstallUnattendedCrtReportHook()`.

For shared source changes also run:

```powershell
.\scripts\build.ps1 -Config Debug -Platform ARM64
.\scripts\build.ps1 -Config Release -Platform ARM64
```

## Completion Report

State:

- chunk dimensions/radius/budgets;
- deterministic generation inputs;
- changed data/render/stream/persistence contracts;
- self-test count/result;
- four API capture statuses/hashes;
- ARM64/platform results;
- persistence migration impact;
- remaining memory/performance/nav limits.
