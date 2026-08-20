#include <pch.h>

#include <VoxelScene.h>

#include <core/Config.h>
#include <core/EngineContext.h>
#include <debug/RuntimeTelemetry.h>
#include <physics/JoltPhysicsSystem.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

bool SweepPointAgainstExpandedBlock(const XVECTOR3& start,
                                    const XVECTOR3& displacement,
                                    const XVECTOR3& minimum,
                                    const XVECTOR3& maximum,
                                    float& fraction,
                                    XVECTOR3& normal) {
  float enter = 0.0f;
  float exit = 1.0f;
  XVECTOR3 enterNormal(0.0f, 0.0f, 0.0f, 0.0f);
  for (int axis = 0; axis < 3; ++axis) {
    const float origin = axis == 0 ? start.x : (axis == 1 ? start.y : start.z);
    const float delta = axis == 0 ? displacement.x : (axis == 1 ? displacement.y : displacement.z);
    const float low = axis == 0 ? minimum.x : (axis == 1 ? minimum.y : minimum.z);
    const float high = axis == 0 ? maximum.x : (axis == 1 ? maximum.y : maximum.z);
    if (std::abs(delta) <= 0.000001f) {
      if (origin < low || origin > high) return false;
      continue;
    }
    float nearTime = (low - origin) / delta;
    float farTime = (high - origin) / delta;
    float nearSign = -1.0f;
    if (nearTime > farTime) {
      std::swap(nearTime, farTime);
      nearSign = 1.0f;
    }
    if (nearTime > enter) {
      enter = nearTime;
      enterNormal = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
      if (axis == 0) enterNormal.x = nearSign;
      else if (axis == 1) enterNormal.y = nearSign;
      else enterNormal.z = nearSign;
    }
    exit = (std::min)(exit, farTime);
    if (enter > exit) return false;
  }
  if (enter < 0.0f || enter > 1.0f) return false;
  fraction = enter;
  normal = enterNormal;
  return true;
}

} // namespace

VoxelScene::VoxelScene()
    : m_world(t850::terrain::ChunkDimensions{16, 16, 16}),
      m_streaming(t850::terrain::ChunkDimensions{16, 16, 16}) {}

void VoxelScene::InitVars() {
  m_deltaSeconds = 0.0f;
  m_remeshRequested = false;
  m_assetsCreated = false;
  m_chunkRenders.clear();
  m_world.Clear();
  m_streaming.Reset();
  m_deltas.Clear();
  m_blockRegistry = t850::terrain::BlockRegistry{};

  t850::terrain::BlockDefinition stone;
  stone.name = "stone";
  stone.color = XVECTOR3(0.38f, 0.43f, 0.48f, 1.0f);
  stone.usesBaseColorTexture = true;
  stone.atlasU0 = 0.0f;
  stone.atlasU1 = 1.0f / 3.0f;
  m_stone = m_blockRegistry.Register(std::move(stone));
  t850::terrain::BlockDefinition dirt;
  dirt.name = "dirt";
  dirt.color = XVECTOR3(0.40f, 0.25f, 0.12f, 1.0f);
  dirt.roughness = 1.0f;
  dirt.usesBaseColorTexture = true;
  dirt.atlasU0 = 1.0f / 3.0f;
  dirt.atlasU1 = 2.0f / 3.0f;
  m_dirt = m_blockRegistry.Register(std::move(dirt));
  t850::terrain::BlockDefinition grass;
  grass.name = "grass";
  grass.color = XVECTOR3(0.16f, 0.55f, 0.20f, 1.0f);
  grass.usesBaseColorTexture = true;
  grass.atlasU0 = 2.0f / 3.0f;
  grass.atlasU1 = 1.0f;
  m_grass = m_blockRegistry.Register(std::move(grass));

  m_deltaPath = t850::ResourceLocator::Instance()
      .ResolveCachePath("VoxelWorlds/default/edits.t8vox")
      .string();
  if (t850::g_config.regressionFixedDt <= 0.0f && std::filesystem::exists(m_deltaPath)) {
    std::string error;
    if (!m_deltas.Load(m_deltaPath, &error)) {
      T8_LOG_ERROR("[VoxelScene] Ignoring invalid saved edits: %s", error.c_str());
      m_deltas.Clear();
    }
  }

  m_camera.InitPerspective(XVECTOR3(16.0f, 10.0f, -6.0f), Deg2Rad(65.0f), 1280.0f / 720.0f, 0.05f, 1000.0f);
  m_camera.Eye = XVECTOR3(16.0f, 10.0f, -6.0f, 1.0f);
  m_camera.Yaw = 0.0f;
  m_camera.Pitch = -0.15f;
  m_camera.Update(0.0f);
  m_cameraController.SetActiveProfile(t850::CameraProfileType::GroundedFps);
  m_cameraController.AttachCamera(&m_camera);

  m_lightCamera.InitPerspective(XVECTOR3(16.0f, 40.0f, -10.0f), Deg2Rad(55.0f), 1.0f, 0.1f, 200.0f);
  m_lightCamera.Eye = XVECTOR3(16.0f, 40.0f, -10.0f, 1.0f);
  m_lightCamera.Pitch = 0.8f;
  m_lightCamera.Yaw = 0.0f;
  m_lightCamera.Update(0.0f);

  SceneProp = SceneProps{};
  SceneProp.AddCamera(&m_camera);
  SceneProp.AddLightCamera(&m_lightCamera);
  SceneProp.AddDirectionalLight(XVECTOR3(-0.35f, -1.0f, 0.2f, 0.0f), XVECTOR3(1.0f, 0.96f, 0.86f, 1.0f), 3.0f, true);
  SceneProp.ActiveLights = 1;
  SceneProp.AmbientColor = XVECTOR3(0.18f, 0.22f, 0.28f, 1.0f);
  SceneProp.ToogleDOF = 0;
  SceneProp.ToogleParallax = 0;
  SceneProp.IBLFactor = 0.0f;
  SceneProp.FrustumCullingEnabled = true;

  m_shadowFilter.kernelSize = 4;
  m_shadowFilter.radius = 1.0f;
  m_shadowFilter.sigma = 1.0f;
  m_shadowFilter.Update();
  m_bloomFilter.kernelSize = 11;
  m_bloomFilter.radius = 2.5f;
  m_bloomFilter.sigma = 4.5f;
  m_bloomFilter.Update();
  m_dofFilter.kernelSize = 23;
  m_dofFilter.radius = 3.0f;
  m_dofFilter.sigma = 6.0f;
  m_dofFilter.Update();
  SceneProp.AddGaussKernel(&m_shadowFilter);
  SceneProp.AddGaussKernel(&m_bloomFilter);
  SceneProp.AddGaussKernel(&m_dofFilter);

  t850::terrain::VoxelStreamingSettings streamingSettings;
  streamingSettings.horizontalRadius = 2;
  streamingSettings.verticalRadius = 0;
  streamingSettings.maxInFlight = 4;
  streamingSettings.maxLaunchesPerUpdate = 4;
  streamingSettings.maxCommitsPerUpdate = 4;
  streamingSettings.maxUnloadsPerUpdate = 4;
  m_streaming.SetSettings(streamingSettings);

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
  result.chunk = std::make_unique<t850::terrain::VoxelChunk>(request.key, request.dimensions);
  for (int z = 0; z < request.dimensions.z; ++z) {
    if (request.IsCancelled()) {
      result.cancelled = true;
      result.chunk.reset();
      return result;
    }
    for (int x = 0; x < request.dimensions.x; ++x) {
      const int worldX = request.key.x * request.dimensions.x + x;
      const int worldZ = request.key.z * request.dimensions.z + z;
      const int height = 3 + ((worldX * 13 + worldZ * 7 + (worldX ^ worldZ)) & 3);
      for (int y = 0; y < request.dimensions.y; ++y) {
        const int worldY = request.key.y * request.dimensions.y + y;
        if (worldY > height) continue;
        result.chunk->Set(
            x, y, z,
            worldY == height ? m_grass : (worldY + 2 >= height ? m_dirt : m_stone));
      }
    }
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
  descriptor.name = "VoxelScene";
  descriptor.renderGraphPath = "Scenes/SceneTemplate_RenderGraph.json";
  descriptor.width = pFramework->pVideoDriver->width;
  descriptor.height = pFramework->pVideoDriver->height;
  descriptor.sceneProps = &SceneProp;
  if (!m_renderContainer.Initialize(pFramework->pVideoDriver, pEngineContext, descriptor)) {
    T8_LOG_ERROR("[VoxelScene] Failed to initialize render container");
    return;
  }
  m_renderContainer.SetMainCamera(&m_camera);
  m_renderContainer.SetLightCamera(&m_lightCamera);
  m_renderContainer.Graph().DisablePass("Light Add");
  const unsigned char atlasPixels[] = {
      132, 142, 154, 255, 132, 142, 154, 255,
      116,  76,  42, 255, 116,  76,  42, 255,
       66, 158,  72, 255,  66, 158,  72, 255,
      132, 142, 154, 255, 132, 142, 154, 255,
      116,  76,  42, 255, 116,  76,  42, 255,
       66, 158,  72, 255,  66, 158,  72, 255};
  if (pEngineContext && pEngineContext->device) {
    m_blockAtlas = pEngineContext->device->CreateTextureFromMemory(
        atlasPixels, 6, 2, 4, "voxel_block_atlas");
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
  t850::RuntimeTelemetry::SetCounter("terrain.voxel.desired_chunks", static_cast<double>(stats.desired));
  t850::RuntimeTelemetry::SetCounter("terrain.voxel.queued_chunks", static_cast<double>(stats.queued));
  t850::RuntimeTelemetry::SetCounter("terrain.voxel.in_flight_chunks", static_cast<double>(stats.inFlight));
  t850::RuntimeTelemetry::SetCounter("terrain.voxel.loaded_chunks", static_cast<double>(m_world.ChunkCount()));
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
  t850::RuntimeTelemetry::SetCounter("terrain.voxel.loaded_chunks", static_cast<double>(m_world.ChunkCount()));
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
    if (m_world.Raycast(m_camera.Eye, m_camera.Look, 8.0f, m_blockRegistry, hit)) {
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
  hit = t850::CharacterCollisionHit{};
  const XVECTOR3 end = start + displacement;
  const int minX = static_cast<int>(std::floor((std::min)(start.x, end.x) - halfExtents.x)) - 1;
  const int minY = static_cast<int>(std::floor((std::min)(start.y, end.y) - halfExtents.y)) - 1;
  const int minZ = static_cast<int>(std::floor((std::min)(start.z, end.z) - halfExtents.z)) - 1;
  const int maxX = static_cast<int>(std::ceil((std::max)(start.x, end.x) + halfExtents.x)) + 1;
  const int maxY = static_cast<int>(std::ceil((std::max)(start.y, end.y) + halfExtents.y)) + 1;
  const int maxZ = static_cast<int>(std::ceil((std::max)(start.z, end.z) + halfExtents.z)) + 1;
  float bestFraction = 1.0f;
  XVECTOR3 bestNormal;
  bool found = false;
  for (int z = minZ; z <= maxZ; ++z) {
    for (int y = minY; y <= maxY; ++y) {
      for (int x = minX; x <= maxX; ++x) {
        const auto block = m_world.GetBlock(x, y, z);
        if (!m_blockRegistry.Get(block).collidable) continue;
        const XVECTOR3 minimum(
            static_cast<float>(x) - halfExtents.x,
            static_cast<float>(y) - halfExtents.y,
            static_cast<float>(z) - halfExtents.z,
            1.0f);
        const XVECTOR3 maximum(
            static_cast<float>(x + 1) + halfExtents.x,
            static_cast<float>(y + 1) + halfExtents.y,
            static_cast<float>(z + 1) + halfExtents.z,
            1.0f);
        float fraction = 1.0f;
        XVECTOR3 normal;
        if (SweepPointAgainstExpandedBlock(start, displacement, minimum, maximum, fraction, normal) &&
            fraction < bestFraction) {
          bestFraction = fraction;
          bestNormal = normal;
          found = true;
        }
      }
    }
  }
  if (!found) return false;
  hit.hit = true;
  hit.fraction = bestFraction;
  hit.position = start + displacement * bestFraction;
  hit.normal = bestNormal;
  return true;
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
