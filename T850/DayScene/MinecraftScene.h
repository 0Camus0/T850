#pragma once
#include <core/Core.h>
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <scene/MutableMeshData.h>
#include <utils/xMaths.h>
#include <utils/Camera.h>
#include <utils/Timer.h>
#include <scene/SceneSetup.h>
#include <scene/EditorSceneFile.h>
#include <scene/RenderGraph.h>
#include <scene/LineRenderer.h>
#include <scene/TextRenderer.h>
#include <physics/PhysicsTypes.h>
#include <physics/CharacterController.h>
#include <navigation/NavigationSystem.h>
#include <navigation/NavigationDebugRenderer.h>
#include <debug/FrameDumper.h>
#include <utils/XDataBase.h>
#include <Config.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

namespace t850 {
class JoltPhysicsSystem;
}

// Atlas tile coordinates (16x16 tiles in a 256x256 atlas)
struct BlockTile {
  int u; // tile column
  int v; // tile row
};

// Per-face tile lookup: index = face (0=+X,1=-X,2=+Y,3=-Y,4=+Z,5=-Z)
struct BlockDef {
  std::string name;
  bool opaque;      // blocks light / culls neighbors
  bool solid;       // collides with player
  BlockTile tiles[6];
  std::array<unsigned char, 4> color = {255, 255, 255, 255};
};

// ── Voxel world constants ────────────────────────────────────────────
constexpr int kMaxChunkSize = 16;
constexpr int kMaxWorldHeight = 64;
constexpr int kMaxRenderDistance = 8;
constexpr int kMaxChunkCount = kMaxRenderDistance * 2 + 1;
constexpr int kMaxChunks = kMaxChunkCount * kMaxChunkCount;
constexpr int kMaxRenderMeshCount = kMaxChunks + 2;

struct MinecraftMob {
  XVECTOR3 position = XVECTOR3(24.5f, 40.0f, 24.5f, 1.0f);
  std::vector<XVECTOR3> path;
  std::size_t pathCursor = 0;
  float repathTimer = 0.0f;
  bool pathReady = false;
};

class MinecraftScene : public t850::SceneBase, public t850::CharacterCollisionWorld {
public:
  MinecraftScene() {}
  void OnUpdate(float _DtSecs) override;
  void OnDraw() override;
  void OnInput(InputManager* IManager) override;
  void OnLoadScene() override;
  void OnDestoryScene() override;
  void InitVars() override;
  void CreateAssets() override;
  void DestroyAssets() override;

  void DrawDevGui(t850::DevGuiContext& gui) override;
  void DrawGameplayGui(t850::DevGuiContext& /*gui*/) override { DrawGameplayHud(); }
  void DrawGameplayHud();
  void DrawCascadeLightBounds();
  void DrawVoxelDebugBounds();
  void ApplyShadowSettings();
  void SaveSceneSettings();
  void RequestDump() override { m_dumper.RequestDump(); }
  void ResetViewInput() override;

    // Benchmark final-frame capture: dumps the backbuffer once the benchmark
    // duration elapses, then exits. (DayScene has its own capture path; other
    // scenes reuse the FrameDumper with a timed dump instead.)
    float m_benchmarkElapsedSecs = 0.0f;
    bool m_benchmarkFinalDumpDone = false;
  // CharacterCollisionWorld
  bool SweepCapsule(const t850::CharacterCollisionSweep& sweep, t850::CharacterCollisionHit& outHit) const override;
  bool SweepBox(const t850::CharacterBoxSweep& sweep, t850::CharacterCollisionHit& outHit) const override;
  bool QueryTriggerTouch(const t850::CharacterTriggerQuery& query, t850::CharacterTriggerTouch& outTouch) const override;

  float DtSecs = 0.0f;
  t850::PrimitiveManager PrimitiveMgr;
  t850::PrimitiveInst Meshes[kMaxRenderMeshCount];
  t850::PrimitiveInst Quads[10];
  int m_meshCount = 0;

  t850::RenderGraph m_renderGraph;
  t850::FrameDumper m_dumper;
  t850::SceneSetup m_controlSetup;
  t850::scene::EditorSceneFile m_sceneFile;
  t850::scene::SceneVoxelWorldDesc m_voxelSettings;
  std::string m_sceneFilePath = "Scenes/Minecraft.t8scene";

  Camera Cam;
  Camera SpectatorCam;
  Camera LightCam;
  Camera* ActiveCam = nullptr;
  XMATRIX44 VP;
  XMATRIX44 m;

  GaussFilter ShadowFilter;
  GaussFilter BloomFilter;
  GaussFilter NearDOFFilter;

  int EnvMapTexIndex = -1;
  int DiffuseIBLTexIndex = -1;
  int SpecularIBLTexIndex = -1;
  int BrdfLUTTexIndex = -1;
  int SheenIBLTexIndex = -1;
  int CharlieLUTTexIndex = -1;
  int SheenELUTTexIndex = -1;
  t850::EnvironmentMapSet EnvMaps;
  int GBufferPass = -1;
  int DeferredPass = -1;
  int Extra16FPass = -1;
  int DepthPass = -1;
  int ShadowAccumPass = -1;
  int ExtraHelperPass = -1;
  int BloomAccumPass = -1;
  int BrightPass = -1;
  int CoCPass = -1;
  int AdaptedLumCurrentPass = -1;
  int AdaptedLumPrevPass = -1;
  int m_debugRTSelection = 0;
  int m_selectedGaussKernel = 0;

  t850::TextRenderer m_debugText;
  t850::LineRenderer m_lineRenderer;
  t850::VertexBuffer* m_cascadeDebugVB = nullptr;
  t850::IndexBuffer*  m_cascadeDebugIB = nullptr;
  t850::IndexBuffer*  m_cascadeDebugSolidIB = nullptr;
  int m_cascadeDebugVBCapacity = 0;
  t850::VertexBuffer* m_voxelDebugVB = nullptr;
  t850::IndexBuffer*  m_voxelDebugIB = nullptr;

  // ── Shadow debug / control state (ImGui panel) ──
  bool  m_showCascadeFrustums = true;
  int   m_cascadeDebugMode = 0;       // 0=cascade regions, 1=light bounds, 2=both
  float m_cascadeDebugOpacity = 0.12f;
  std::array<XVECTOR3, 6> m_cascadeDebugColors;
  int   m_cameraMode = 0;             // 0=player, 1=free spectator, 2=light
  int   m_debugCascadeIndex = 0;
  bool  m_debugCameraOrtho = false;
  bool  m_lightCameraEditMode = false;
  bool  m_sunTrajectoryPaused = false;
  int   m_sunLightIndex = -1;
  float m_spectatorYaw = 0.0f;
  float m_spectatorPitch = 0.0f;
  float m_lightYaw = 0.0f;
  float m_lightPitch = 0.0f;
  float m_shadowResolution = 0.0f;
  int   m_cascadeCount = 0;
  float m_splitLambda = 0.0f;
  float m_shadowBias = 0.0f;
  float m_shadowMin = 0.0f;
  bool  m_shadowsEnabled = true;       // master shadow toggle
  bool  m_shadowConfigDirty = false;   // resolution/cascade count changed -> recreate RTs

  // ── Voxel world state ──
  uint8_t m_blocks[kMaxChunkCount][kMaxWorldHeight][kMaxChunkCount][kMaxChunkSize][kMaxChunkSize];
  bool m_chunkDirty[kMaxChunkCount][kMaxChunkCount];
  bool m_chunkBuilt[kMaxChunkCount][kMaxChunkCount];
  int m_chunkSize = 0;
  int m_worldHeight = 0;
  int m_waterLevel = 0;
  int m_renderDistance = 0;
  int m_streamingRecenterThreshold = 0;
  int m_chunkCountX = 0;
  int m_chunkCountZ = 0;
  int m_maxChunks = 0;
  int m_mobMeshIndex = 0;
  int m_weaponMeshIndex = 0;
  int m_renderMeshCount = 0;
  int m_centerChunkX = 0;
  int m_centerChunkZ = 0;

  // ── Async chunk streaming ──
  struct GeneratedChunkData {
    int cx = 0;
    int cz = 0;
    std::vector<uint8_t> blocks;
  };
  // A chunk whose geometry is being built on a background thread. The
  // block data is snapshotted so the worker never races with the render
  // thread (which may shift m_blocks when the player moves).
  struct PendingChunk {
    int cx = 0;
    int cz = 0;
    int gx = 0;   // grid slot (relative to center at enqueue time)
    int gz = 0;
    std::vector<uint8_t> blockSnapshot; // (chunkSize+2)^2 * worldHeight
    std::shared_ptr<t850::MutableMeshSnapshot> meshSnapshot;
    std::future<void> future;           // background geometry build
    bool geometryReady = false;
    bool uploadDone = false;
  };
  std::deque<PendingChunk> m_pendingChunks;
  std::mutex m_pendingMutex;
  std::vector<std::future<void>> m_retiredChunkBuilds;
  std::future<void> m_chunkGenerationFuture;
  std::shared_ptr<std::vector<GeneratedChunkData>> m_generatedChunkBatch;
  std::deque<std::pair<int, int>> m_chunksAwaitingMesh;
  int m_maxUploadsPerFrame = 0;
  bool m_asyncStreaming = false;

  // Navigation and mob test agent
  t850::navigation::NavMesh m_navMesh;
  t850::navigation::NavMeshDebugRenderer m_navMeshDebugRenderer;
  t850::navigation::NavMeshBuildSettings m_navMeshSettings;
  MinecraftMob m_mob;
  bool m_navMeshReady = false;
  bool m_showNavMesh = false;
  float m_navMeshBuildMs = 0.0f;
  struct PendingNavMeshBuild {
    t850::navigation::NavMesh navMesh;
    std::string error;
    float buildMs = 0.0f;
    bool success = false;
    int centerChunkX = 0;
    int centerChunkZ = 0;
  };
  std::future<void> m_navMeshBuildFuture;
  std::shared_ptr<PendingNavMeshBuild> m_pendingNavMeshBuild;
  // Set when a block is placed/removed so the navmesh is rebuilt (throttled)
  // and the mob re-paths around the new obstacle.
  bool m_navMeshDirty = false;
  float m_navMeshRebuildTimer = 0.0f;

  // First-person weapon (sword)
  float m_weaponSwing = 0.0f;   // 0..1 swing animation progress
  bool m_weaponSwinging = false;
  float m_weaponBob = 0.0f;     // walk bob phase

  // Player
  t850::KinematicCharacterController m_player;
  t850::KinematicCharacterSettings m_playerSettings;
  t850::KinematicCharacterInput m_playerInput;
  XVECTOR3 m_playerEye = XVECTOR3(0.0f, 40.0f, 0.0f, 1.0f);
  float m_playerYaw = 0.0f;
  float m_playerPitch = 0.0f;
  bool m_mouseCaptured = true;
  bool m_showPhysics = false;
  bool m_showChunkBounds = false;
  bool m_showLights = false;
  int m_seed = 0;
  float m_mouseSensitivity = 0.0f;
  float m_debugCameraSpeed = 0.0f;

  // Day/night cycle
  float m_timeOfDay = 0.0f;
  float m_dayLengthSecs = 0.0f;
  bool m_dayNightEnabled = false;
  std::vector<BlockDef> m_blockDefs;
  std::unordered_map<std::string, uint8_t> m_blockIds;
  std::vector<uint8_t> m_hotbar;
  int m_atlasSize = 0;
  int m_atlasTiles = 0;
  std::string m_atlasTexturePath; // empty => procedural solid-color atlas
  int m_atlasTilePx = 16;

  // ── Internals ──
  void GenerateWorld();
  void BuildNavigationMesh();
  void BuildNavigationGeometry(const std::vector<uint8_t>& blockSnapshot,
                               int centerChunkX, int centerChunkZ,
                               t850::navigation::NavMeshGeometry& geometry) const;
  void StartNavigationMeshBuild();
  void ProcessNavigationMeshBuild();
  void UpdateMob(float dt);
  void CreateMobMesh();
  void UpdateMobInstance();
  void CreateWeaponMesh();
  void UpdateWeapon(float dt);
  void UpdateDayNight(float dt);
  void SyncLightCameraFromSun();
  void SyncSunFromLightCamera();
  void SetLightCameraEditMode(bool enabled);
  void GenerateChunk(int cx, int cz, bool markState = true);
  void GenerateChunkData(int cx, int cz, std::vector<uint8_t>& blocks) const;
  void GenerateChunkTrees(int cx, int cz, bool markState = true);
  void BuildChunkMesh(int cx, int cz);
  void RebuildDirtyChunks();
  void UpdateChunkStreaming();
  void ShiftWorldAndStream(int newCx, int newCz);
  void QueueChunkRemesh(int cx, int cz);
  void EnqueueChunkBuild(int cx, int cz);
  void ProcessPendingChunks();
  void CancelPendingChunk(int cx, int cz);
  void BuildChunkGeometryFromSnapshot(int cx, int cz,
                                      const std::vector<uint8_t>& snapshot,
                                      xF::XDataBase& outDb);
  void ConvertChunkDatabase(const xF::XDataBase& db,
                            t850::MutableMeshSnapshot& snapshot) const;
  void UploadChunkMesh(PendingChunk& pc);
  int  ChunkIndex(int cx, int cz) const;
  uint8_t GetBlock(int wx, int wy, int wz) const;
  void SetBlock(int wx, int wy, int wz, uint8_t block);
  bool IsBlockOpaque(uint8_t block) const;
  bool IsBlockSolid(uint8_t block) const;
  // Simple AABB box-vs-voxel collision for the mob (all geometry is boxes).
  // Returns true if the box [minX,minY,minZ .. maxX,maxY,maxZ] overlaps any
  // solid block. Used instead of the capsule sweep, which got the mob stuck
  // at block boundaries.
  bool MobBoxCollides(float minX, float minY, float minZ,
                      float maxX, float maxY, float maxZ) const;
  int  HeightAt(int wx, int wz) const;
  int  WorldToChunk(int wx) const;
  int  WorldToLocal(int wx) const;
  bool LoadAuthoredScene();
  void ApplyVoxelSettings();
  uint8_t BlockId(const std::string& name, uint8_t fallback) const;
  float Noise2D(float x, float z) const;
  float Noise3D(float x, float y, float z) const;
  void BuildTextureAtlas();
  bool BuildRealTextureAtlas();
  // Offscreen benchmark capture: blits the active offscreen RT into a private
  // RT via a fullscreen quad every frame (the swapchain backbuffer stays
  // black in offscreen mode, and reading a just-completed offscreen RT
  // directly can crash on resource-state mismatch). The dump saves the copy.
  void BlitOffscreenToCaptureRT(t850::BaseDriver* driver);
  int m_offscreenCaptureRT = -1;
  void CreateChunkMesh(int cx, int cz, xF::XDataBase& outDb);
  void AddFace(xF::xMeshGeometry& geom, int x, int y, int z, int face, uint8_t block);
  void AddVertex(xF::xMeshGeometry& geom, float x, float y, float z, float nx, float ny, float nz, float u, float v);
  void AddQuad(xF::xMeshGeometry& geom, const XVECTOR3& a, const XVECTOR3& b, const XVECTOR3& c, const XVECTOR3& d,
               const XVECTOR3& n, float u0, float v0, float u1, float v1);
  void UpdatePlayer(float dt);
  void HandleBlockInteraction(InputManager* IManager);
  void ApplyPendingCubemap();
  void RaycastBlocks(const XVECTOR3& origin, const XVECTOR3& dir, float maxDist,
                     int& outX, int& outY, int& outZ, int& outPrevX, int& outPrevY, int& outPrevZ) const;

  t850::Texture* m_atlasTexture = nullptr;
  int m_atlasTexIndex = -1;
  // Skybox selection (ImGui)
  std::string m_currentCubemapPath;
  std::string m_pendingCubemap;
  int m_highlightMeshIndex = -1;
  bool m_highlightVisible = false;
  int m_highlightX = 0, m_highlightY = 0, m_highlightZ = 0;
  bool m_lastHighlightValid = false;
  int m_lastHighlightX = 0, m_lastHighlightY = 0, m_lastHighlightZ = 0;
  int m_highlightFace = 0;
  int m_selectedBlock = 0;
  std::string m_interactionMessage;
  float m_interactionMessageTime = 0.0f;
  float m_breakCooldown = 0.0f;
  float m_placeCooldown = 0.0f;
};
