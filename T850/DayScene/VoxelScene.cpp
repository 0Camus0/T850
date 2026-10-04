#include <pch.h>

#include <VoxelScene.h>

#include <core/Config.h>
#include <core/EngineContext.h>
#include <debug/RuntimeTelemetry.h>
#include <physics/JoltPhysicsSystem.h>
#include <terrain/VoxelCollision.h>
#include <scene/SceneConversions.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

VoxelScene::VoxelScene() = default;

void VoxelScene::InitVars() {
  m_deltaSeconds = 0.0f;
  m_remeshRequested = false;
  m_assetsCreated = false;
  m_chunkRenders.clear();
  m_world.Clear();
  m_streaming.Reset();
  m_deltas.Clear();
  m_blockRegistry = t850::terrain::BlockRegistry{};

  const std::string scenePath = t850::g_config.sceneFilePath.empty()
      ? "Scenes/VoxelScene.t8scene" : t850::g_config.sceneFilePath;
  std::string sceneError;
  if (!t850::scene::LoadEditorSceneFile(scenePath, m_sceneFile, &sceneError) ||
      !m_sceneFile.runtime_setup || !m_sceneSetup.Load(*m_sceneFile.runtime_setup) ||
      m_sceneSetup.cameras.size() != 1 || m_sceneSetup.lightCameras.size() != 1) {
    throw std::runtime_error("Voxel scene requires runtime setup with one camera and light camera: " + sceneError);
  }
  if (!m_sceneFile.streamed_voxels)
    throw std::runtime_error("Voxel scene requires streamed_voxels data");
  const auto& voxels = *m_sceneFile.streamed_voxels;
  t850::scene::BuildStreamedVoxelPalette(voxels, m_blockRegistry);
  m_world = t850::terrain::VoxelWorld(voxels.chunk_dimensions);
  m_streaming.Reset(voxels.chunk_dimensions);
  m_stone = m_blockRegistry.Find(voxels.deep_block);
  m_dirt = m_blockRegistry.Find(voxels.fill_block);
  m_grass = m_blockRegistry.Find(voxels.surface_block);
  m_deltaPath = t850::ResourceLocator::Instance().ResolveCachePath(voxels.edits_path).string();
  if (t850::g_config.regressionFixedDt <= 0.0f && std::filesystem::exists(m_deltaPath)) {
    std::string error;
    if (!m_deltas.Load(m_deltaPath, &error)) {
      T8_LOG_ERROR("[VoxelScene] Ignoring invalid saved edits: %s", error.c_str());
      m_deltas.Clear();
    }
  }
  SceneProp = SceneProps{};
  m_sceneSetup.Apply(SceneProp);
  m_sceneSetup.ApplyInputSettings(SceneProp, m_sceneFile.mouse_capture);
  m_camera = *m_sceneSetup.GetCamera();
  m_lightCamera = *m_sceneSetup.GetLightCamera();
  SceneProp.pCameras = {&m_camera};
  SceneProp.pLightCameras = {&m_lightCamera};
  m_cameraController.SetActiveProfile(t850::CameraProfileTypeFromIndex(voxels.camera_profile));
  m_cameraController.AttachCamera(&m_camera);

  m_streaming.SetSettings(voxels.streaming);

  t850::FrameDumperConfig dumpConfig;
  dumpConfig.dumpEnabled = t850::g_config.flags.dumpEnabled;
  dumpConfig.dumpByFrame = t850::g_config.flags.dumpByFrame;
  dumpConfig.dumpFrame = t850::g_config.dumpFrame;
  dumpConfig.dumpSeconds = t850::g_config.dumpSeconds;
  dumpConfig.debugFrames = t850::g_config.flags.debugFrames;
  dumpConfig.keepRunning = t850::g_config.flags.keepRunning;
  dumpConfig.replaySnapshotPath = t850::g_config.replaySnapshotPath;
  dumpConfig.sceneIndex = kSceneIndex;
  m_dumper.Init(dumpConfig);
}

t850::terrain::VoxelChunkBuildResult VoxelScene::BuildStreamedChunk(
    const t850::terrain::VoxelChunkBuildRequest& request) const {
  t850::terrain::VoxelChunkBuildResult result;
  result.key = request.key;
  result.epoch = request.epoch;
  result.chunk = t850::terrain::GenerateLayeredVoxelChunk(
      request, m_sceneFile.streamed_voxels->terrain, m_grass, m_dirt, m_stone);
  if (!result.chunk) {
    result.cancelled = true;
    return result;
  }
  m_deltas.ApplyToChunk(*result.chunk);
  if (!t850::terrain::BuildGreedyVoxelMesh(
          *result.chunk, m_blockRegistry, {}, result.mesh, &result.error)) {
    result.chunk.reset();
  }
  return result;
}

void VoxelScene::CreateAssets() {
  if (m_assetsCreated || !pFramework || !pFramework->pVideoDriver) return;
  SceneProp.SSAOKernel.InitTexture();
  t850::RenderContainerDesc descriptor;
  descriptor.name = m_sceneSetup.name;
  descriptor.renderGraphPath = m_sceneFile.render_graph;
  descriptor.width = pFramework->pVideoDriver->width;
  descriptor.height = pFramework->pVideoDriver->height;
  descriptor.sceneProps = &SceneProp;
  if (!m_renderContainer.Initialize(pFramework->pVideoDriver, pEngineContext, descriptor)) {
    T8_LOG_ERROR("[VoxelScene] Failed to initialize render container");
    return;
  }
  m_renderContainer.SetMainCamera(&m_camera);
  m_renderContainer.SetLightCamera(&m_lightCamera);
  for (const auto& pass : m_sceneFile.disabled_render_passes)
    m_renderContainer.Graph().DisablePass(pass);
  const auto& voxels = *m_sceneFile.streamed_voxels;
  if (pEngineContext && pEngineContext->device) {
    m_blockAtlas = pEngineContext->device->CreateTextureFromMemory(
        voxels.atlas_rgba.data(), voxels.atlas_width, voxels.atlas_height, 4, "voxel_block_atlas");
    if (m_blockAtlas) {
      m_blockAtlas->params = t850::TextBasicParams::CLAMP_TO_EDGE |
          t850::TextBasicParams::NEAREST_FILTER;
      m_blockAtlas->SetTextureParams();
    }
  }
  m_assetsCreated = true;
  UpdateStreaming();
}

void VoxelScene::DestroyAssets() {
  if (!m_assetsCreated) return;
  m_streaming.Reset();
  if (t850::g_config.regressionFixedDt <= 0.0f && !m_deltaPath.empty() && m_deltas.Count() > 0) {
    std::string error;
    if (!m_deltas.Save(m_deltaPath, &error)) {
      T8_LOG_ERROR("[VoxelScene] Failed to save edits during teardown: %s", error.c_str());
    }
  }
  m_renderContainer.ClearMeshes();
  m_renderContainer.Destroy(pFramework ? pFramework->pVideoDriver : nullptr);
  for (auto& [key, render] : m_chunkRenders) {
    (void)key;
    if (render.body.IsValid() && pEngineContext && pEngineContext->physics) {
      pEngineContext->physics->DestroyBody(render.body);
    }
    if (render.mesh) render.mesh->Destroy();
  }
  m_chunkRenders.clear();
  if (m_blockAtlas) {
    if (pFramework && pFramework->pVideoDriver) pFramework->pVideoDriver->WaitForGPU();
    m_blockAtlas->release();
    m_blockAtlas = nullptr;
  }
  SceneProp.SSAOKernel.Destroy();
  m_assetsCreated = false;
}

void VoxelScene::OnLoadScene() {
  InitVars();
  CreateAssets();
}

void VoxelScene::OnDestoryScene() {
  DestroyAssets();
}

t850::terrain::BlockId VoxelScene::SampleNeighbor(
    const t850::terrain::ChunkKey& key, int localX, int localY, int localZ) const {
  const auto dimensions = m_world.Dimensions();
  return m_world.GetBlock(
      key.x * dimensions.x + localX,
      key.y * dimensions.y + localY,
      key.z * dimensions.z + localZ);
}

void VoxelScene::CommitStreamedChunk(t850::terrain::VoxelChunkBuildResult result) {
  if (!result.Succeeded() || !m_streaming.IsDesired(result.key)) return;
  const t850::terrain::ChunkKey key = result.key;
  const t850::PhysicsBodyHandle replacementBody = CreateChunkPhysics(key, result.mesh);

  auto found = m_chunkRenders.find(key);
  if (found != m_chunkRenders.end()) {
    std::string error;
    if (!found->second.mesh->ReplaceSnapshot(std::move(result.mesh), &error)) {
      T8_LOG_ERROR("[VoxelScene] Streamed chunk replacement failed: %s", error.c_str());
      if (replacementBody.IsValid() && pEngineContext && pEngineContext->physics) {
        pEngineContext->physics->DestroyBody(replacementBody);
      }
      return;
    }
    if (!m_world.AdoptChunk(std::move(result.chunk))) {
      if (replacementBody.IsValid() && pEngineContext && pEngineContext->physics) {
        pEngineContext->physics->DestroyBody(replacementBody);
      }
      return;
    }
    if (pEngineContext && pEngineContext->physics) {
      if (found->second.body.IsValid()) pEngineContext->physics->DestroyBody(found->second.body);
      found->second.body = replacementBody;
    }
    return;
  }

  ChunkRender render;
  render.mesh = std::make_unique<t850::MutableMesh>();
  render.mesh->SetEngineContext(pEngineContext);
  render.mesh->Create();
  std::string error;
  if (!render.mesh->ReplaceSnapshot(std::move(result.mesh), &error)) {
    T8_LOG_ERROR("[VoxelScene] Streamed chunk GPU commit failed: %s", error.c_str());
    if (replacementBody.IsValid() && pEngineContext && pEngineContext->physics) {
      pEngineContext->physics->DestroyBody(replacementBody);
    }
    return;
  }
  t850::PrimitiveInst instance;
  instance.CreateInstance(render.mesh.get(), &m_camera.VP);
  const auto dimensions = m_world.Dimensions();
  instance.TranslateAbsolute(
      static_cast<float>(key.x * dimensions.x),
      static_cast<float>(key.y * dimensions.y),
      static_cast<float>(key.z * dimensions.z));
  instance.SetTexture(m_blockAtlas, 0);
  instance.Update();
  render.instance = m_renderContainer.AddMeshInstance(instance);
  render.body = replacementBody;
  if (!render.instance.IsValid() || !m_world.AdoptChunk(std::move(result.chunk))) {
    if (render.instance.IsValid()) m_renderContainer.RemoveMesh(render.instance);
    if (render.body.IsValid() && pEngineContext && pEngineContext->physics) {
      pEngineContext->physics->DestroyBody(render.body);
    }
    render.mesh->Destroy();
    return;
  }
  m_chunkRenders.emplace(key, std::move(render));
}

void VoxelScene::UnloadChunk(t850::terrain::ChunkKey key) {
  const auto found = m_chunkRenders.find(key);
  if (found != m_chunkRenders.end()) {
    m_renderContainer.RemoveMesh(found->second.instance);
    if (found->second.body.IsValid() && pEngineContext && pEngineContext->physics) {
      pEngineContext->physics->DestroyBody(found->second.body);
    }
    if (found->second.mesh) found->second.mesh->Destroy();
    m_chunkRenders.erase(found);
  }
  m_world.RemoveChunk(key);
}

t850::PhysicsBodyHandle VoxelScene::CreateChunkPhysics(
    t850::terrain::ChunkKey key, const t850::MutableMeshSnapshot& snapshot) const {
  if (snapshot.Empty() || !pEngineContext || !pEngineContext->physics ||
      !pEngineContext->physics->IsInitialized()) {
    return {};
  }
  t850::PhysicsTriangleMeshBodyDesc descriptor;
  uint32_t entityId = 2166136261u;
  auto mix = [&](int value) {
    entityId ^= static_cast<uint32_t>(value);
    entityId *= 16777619u;
  };
  mix(key.x);
  mix(key.y);
  mix(key.z);
  descriptor.entityId = entityId == 0 ? 1u : entityId;
  descriptor.debugName = "voxel_chunk_" + std::to_string(key.x) + "_" +
      std::to_string(key.y) + "_" + std::to_string(key.z);
  descriptor.mesh.vertices.reserve(snapshot.vertices.size());
  for (const t850::MutableMeshVertex& vertex : snapshot.vertices) {
    descriptor.mesh.vertices.push_back(vertex.position);
  }
  descriptor.mesh.indices = snapshot.indices;
  descriptor.mesh.localBounds = snapshot.localBounds;
  descriptor.mesh.settings.buildQuality = t850::PhysicsMeshBuildQuality::FavorBuildSpeed;
  descriptor.mesh.settings.useDiskCache = false;
  descriptor.worldTransform.Identity();
  const auto dimensions = m_world.Dimensions();
  descriptor.worldTransform.m41 = static_cast<float>(key.x * dimensions.x);
  descriptor.worldTransform.m42 = static_cast<float>(key.y * dimensions.y);
  descriptor.worldTransform.m43 = static_cast<float>(key.z * dimensions.z);
  descriptor.gameplayLayer = t850::GameplayLayer::WorldStatic;
  return pEngineContext->physics->CreateTriangleMeshBody(descriptor);
}

void VoxelScene::UpdateStreaming() {
  if (!m_assetsCreated) return;
  const int worldX = static_cast<int>(std::floor(m_camera.Eye.x));
  const int worldY = static_cast<int>(std::floor(m_camera.Eye.y));
  const int worldZ = static_cast<int>(std::floor(m_camera.Eye.z));
  const t850::terrain::ChunkKey focus = m_world.WorldToChunk(worldX, worldY, worldZ);
  const std::vector<t850::terrain::ChunkKey> loaded = m_world.LoadedChunkKeys();
  t850::ThreadPool* threadPool = pEngineContext ? pEngineContext->threadPool : nullptr;
  const t850::terrain::VoxelChunkBuildFunction build = [this](
      const t850::terrain::VoxelChunkBuildRequest& request) {
    return BuildStreamedChunk(request);
  };
  m_streaming.Update(focus, loaded, threadPool, build);
  for (auto& result : m_streaming.TakeCompleted()) CommitStreamedChunk(std::move(result));
  for (const t850::terrain::ChunkKey key : m_streaming.TakeUnloadRequests()) UnloadChunk(key);
  const auto& stats = m_streaming.Stats();
  T8_TELEMETRY_SET("terrain.voxel.desired_chunks", static_cast<double>(stats.desired));
  T8_TELEMETRY_SET("terrain.voxel.queued_chunks", static_cast<double>(stats.queued));
  T8_TELEMETRY_SET("terrain.voxel.in_flight_chunks", static_cast<double>(stats.inFlight));
  T8_TELEMETRY_SET("terrain.voxel.loaded_chunks", static_cast<double>(m_world.ChunkCount()));
}

void VoxelScene::RebuildChunkMeshes() {
  if (!m_assetsCreated) return;
  for (const t850::terrain::ChunkKey& key : m_world.LoadedChunkKeys()) {
    const t850::terrain::VoxelChunk* chunk = m_world.FindChunk(key);
    if (!chunk) continue;
    t850::MutableMeshSnapshot snapshot;
    std::string error;
    const t850::terrain::NeighborBlockSampler sample = [this, key](int x, int y, int z) {
      return SampleNeighbor(key, x, y, z);
    };
    if (!t850::terrain::BuildGreedyVoxelMesh(*chunk, m_blockRegistry, sample, snapshot, &error)) {
      T8_LOG_ERROR("[VoxelScene] Chunk (%d,%d,%d) meshing failed: %s", key.x, key.y, key.z, error.c_str());
      continue;
    }
    const t850::PhysicsBodyHandle replacementBody = CreateChunkPhysics(key, snapshot);
    auto found = m_chunkRenders.find(key);
    if (found == m_chunkRenders.end()) {
      ChunkRender render;
      render.mesh = std::make_unique<t850::MutableMesh>();
      render.mesh->SetEngineContext(pEngineContext);
      render.mesh->Create();
      if (!render.mesh->ReplaceSnapshot(std::move(snapshot), &error)) {
        T8_LOG_ERROR("[VoxelScene] Chunk GPU commit failed: %s", error.c_str());
        if (replacementBody.IsValid() && pEngineContext && pEngineContext->physics) {
          pEngineContext->physics->DestroyBody(replacementBody);
        }
        continue;
      }
      t850::PrimitiveInst instance;
      instance.CreateInstance(render.mesh.get(), &m_camera.VP);
      const auto dimensions = m_world.Dimensions();
      instance.TranslateAbsolute(
          static_cast<float>(key.x * dimensions.x),
          static_cast<float>(key.y * dimensions.y),
          static_cast<float>(key.z * dimensions.z));
      instance.SetTexture(m_blockAtlas, 0);
      instance.Update();
      render.instance = m_renderContainer.AddMeshInstance(instance);
      render.body = replacementBody;
      if (!render.instance.IsValid()) {
        if (render.body.IsValid() && pEngineContext && pEngineContext->physics) {
          pEngineContext->physics->DestroyBody(render.body);
        }
        render.mesh->Destroy();
        continue;
      }
      m_chunkRenders.emplace(key, std::move(render));
    } else {
      if (!found->second.mesh->ReplaceSnapshot(std::move(snapshot), &error)) {
        T8_LOG_ERROR("[VoxelScene] Chunk GPU replacement failed: %s", error.c_str());
        if (replacementBody.IsValid() && pEngineContext && pEngineContext->physics) {
          pEngineContext->physics->DestroyBody(replacementBody);
        }
        continue;
      }
      if (pEngineContext && pEngineContext->physics) {
        if (found->second.body.IsValid()) pEngineContext->physics->DestroyBody(found->second.body);
        found->second.body = replacementBody;
      }
    }
  }
  m_remeshRequested = false;
  T8_TELEMETRY_SET("terrain.voxel.loaded_chunks", static_cast<double>(m_world.ChunkCount()));
}

void VoxelScene::OnUpdate(float deltaSeconds) {
  m_deltaSeconds = deltaSeconds;
  if (!m_dumper.SkipCameraUpdates()) {
    m_cameraController.Update(deltaSeconds, t850::CameraUpdateContext{this});
  }
  m_dumper.UpdateReplayState();
  UpdateStreaming();
  if (m_remeshRequested) RebuildChunkMeshes();
}

void VoxelScene::OnInput(InputManager* input) {
  t850::CameraInputState state;
  state.moveForward = input->PressedKey(T800K_w);
  state.moveBackward = input->PressedKey(T800K_s);
  state.moveLeft = input->PressedKey(T800K_a);
  state.moveRight = input->PressedKey(T800K_d);
  state.jump = input->PressedKey(T800K_SPACE);
  state.crouch = input->PressedKey(T800K_LCTRL);
  state.sprint = input->PressedKey(T800K_LSHIFT);
  state.mouseLook = true;
  state.mouseDeltaX = static_cast<float>(input->xDelta);
  state.mouseDeltaY = static_cast<float>(-input->yDelta);
  t850::ApplyGamepadToCameraInput(state, *input, m_deltaSeconds, true);
  m_cameraController.HandleInput(state);

  if (input->PressedOnceMouseButton(0) || input->PressedOnceMouseButton(1)) {
    t850::terrain::VoxelRayHit hit;
    if (m_world.Raycast(m_camera.Eye, m_camera.Look, m_sceneFile.streamed_voxels->interaction_reach, m_blockRegistry, hit)) {
      bool changed = false;
      if (input->PressedOnceMouseButton(0)) {
        changed = m_world.SetBlock(hit.blockX, hit.blockY, hit.blockZ, t850::terrain::kAirBlock);
        if (changed) m_deltas.Record(hit.blockX, hit.blockY, hit.blockZ, t850::terrain::kAirBlock);
      } else {
        changed = m_world.SetBlock(hit.previousX, hit.previousY, hit.previousZ, m_grass);
        if (changed) m_deltas.Record(hit.previousX, hit.previousY, hit.previousZ, m_grass);
      }
      if (changed) {
        m_remeshRequested = true;
        std::string error;
        if (!m_deltas.Save(m_deltaPath, &error)) {
          T8_LOG_ERROR("[VoxelScene] Failed to save block edit: %s", error.c_str());
        }
      }
    }
  }
}

void VoxelScene::OnDraw() {
  if (!m_assetsCreated) return;
  m_renderContainer.Execute(pFramework->pVideoDriver, m_deltaSeconds);
  if (m_dumper.ShouldDump(m_deltaSeconds)) {
    std::vector<t850::RTDumpEntry> targets = {
        {m_renderContainer.Graph().GetRTHandle("GBuffer"), t850::BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Albedo"},
        {m_renderContainer.Graph().GetRTHandle("GBuffer"), t850::BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"},
        {m_renderContainer.Graph().GetRTHandle("Deferred"), t850::BaseDriver::COLOR0_ATTACHMENT, "Deferred"},
        {m_renderContainer.Graph().GetRTHandle("ExtraHelper"), t850::BaseDriver::COLOR0_ATTACHMENT, "HDR_Final"}};
    m_dumper.DumpFrame(
        pFramework->pVideoDriver, m_camera, m_lightCamera, SceneProp, targets, m_deltaSeconds);
    if (m_dumper.ShouldExit()) std::exit(0);
  }
}

bool VoxelScene::SweepAabb(const XVECTOR3& start,
                           const XVECTOR3& displacement,
                           const XVECTOR3& halfExtents,
                           t850::CharacterCollisionHit& hit) const {
  t850::CharacterBoxSweep sweep;
  sweep.startCenter = start;
  sweep.displacement = displacement;
  sweep.halfExtents = halfExtents;
  return t850::terrain::SweepVoxelBox(sweep, {
      [this](int worldX, int worldY, int worldZ) {
        return m_blockRegistry.Get(m_world.GetBlock(worldX, worldY, worldZ)).collidable;
      }}, hit);
}

bool VoxelScene::SweepCapsule(
    const t850::CharacterCollisionSweep& sweep, t850::CharacterCollisionHit& hit) const {
  return SweepAabb(
      sweep.startCenter,
      sweep.displacement,
      XVECTOR3(sweep.radius, sweep.halfHeight + sweep.radius, sweep.radius, 0.0f),
      hit);
}

bool VoxelScene::SweepBox(
    const t850::CharacterBoxSweep& sweep, t850::CharacterCollisionHit& hit) const {
  return SweepAabb(sweep.startCenter, sweep.displacement, sweep.halfExtents, hit);
}
