#include <SceneTemplate.h>
#include <SandboxRenderGraphUtils.h>
#include <video/BaseDriver.h>
#include <utils/Log.h>
#include <utils/RuntimeProfile.h>
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <scene/RenderMesh.h>
#include <scene/RenderSkinnedMesh.h>
#include <scene/SceneDescriptor.h>
#include <scene/EditorSceneFile.h>
#include <scene/IBLResources.h>
#include <core/Config.h>
#include <core/EngineContext.h>
#include <physics/PhysicsAuthoring.h>
#include <utils/Picking.h>
#include <utils/ResourceLocator.h>
#include <debug/RuntimeTelemetry.h>
#include <game/examples/HealthComponent.h>
#include <game/examples/PathFollowComponent.h>
#include <game/examples/WeaponComponent.h>
#include <game/StateMachine.h>

#ifdef OS_ANDROID
#include <android/input.h>
#endif
#include <imgui/DevGuiContext.h>
#include <array>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <string>
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <utility>
#include <functional>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <chrono>

using namespace t850;
using std::string;

extern std::vector<std::string> g_args;

namespace t850 {
  extern Device* T8Device;
  extern DeviceContext* T8DeviceContext;
}
namespace {
  constexpr std::array<float, 9> kRagdollSimulationSpeedScales = {
      0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f};
  constexpr std::array<const char*, 9> kRagdollSimulationSpeedLabels = {
      "0.125x", "0.25x", "0.5x", "1x", "2x", "4x", "8x", "16x", "32x"};
  constexpr std::size_t kSandboxConsoleMaxLines = 500;
  constexpr int kNavTestModeFurthest = 0;
  constexpr int kNavTestModeRandom = 1;
  constexpr int kNavTestModeFollowPlayer = 2;
  constexpr float kNavTestDiagIntervalSec = 1.0f / 60.0f;
  constexpr float kNavTestFailedPathRetrySec = 0.25f;
  constexpr uint64_t kJoltNavLinkValidationCacheKey = 0x4a4f4c544e41564cull; // JOLT NAVL

  const std::string& DefaultSceneTemplateSceneFilePath() {
    static const std::string path = "Scenes/Q3/q3dm6_mod_3_jolt.t8scene";
    return path;
  }

  float ClampMouseSensitivity(float value) {
    return (std::max)(0.05f, (std::min)(5.0f, value));
  }

  int ClampNavTestMode(int value) {
    return (std::max)(kNavTestModeFurthest, (std::min)(kNavTestModeFollowPlayer, value));
  }

  float DistanceSquared(const XVECTOR3& a, const XVECTOR3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
  }

  float HorizontalDistanceSq3(const XVECTOR3& a, const XVECTOR3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
  }

  template <typename T>
  void HashNavCacheValue(uint64_t& hash, const T& value) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
      hash ^= bytes[i];
      hash *= 0x100000001b3ull;
    }
  }

  void HashNavCacheString(uint64_t& hash, const std::string& value) {
    for (unsigned char c : value) {
      hash ^= c;
      hash *= 0x100000001b3ull;
    }
    const unsigned char terminator = 0;
    hash ^= terminator;
    hash *= 0x100000001b3ull;
  }

  void HashNavCacheMatrix(uint64_t& hash, const XMATRIX44& matrix) {
    for (int row = 0; row < 4; ++row) {
      for (int col = 0; col < 4; ++col) {
        HashNavCacheValue(hash, matrix.m[row][col]);
      }
    }
  }

  void HashNavCacheFileSignature(uint64_t& hash, const std::string& resourcePath) {
    HashNavCacheString(hash, resourcePath);
    std::error_code ec;
    std::filesystem::path path(resourcePath);
    if (path.empty() || !std::filesystem::is_regular_file(path, ec)) {
      ec.clear();
      path = t850::ResourceLocator::Instance().ResolveFilePath(resourcePath);
    }
    if (!path.empty() && std::filesystem::is_regular_file(path, ec)) {
      ec.clear();
      const uint64_t size = static_cast<uint64_t>(std::filesystem::file_size(path, ec));
      if (!ec) {
        HashNavCacheValue(hash, size);
      }
      ec.clear();
      const auto writeTime = std::filesystem::last_write_time(path, ec);
      if (!ec) {
        const int64_t ticks = static_cast<int64_t>(writeTime.time_since_epoch().count());
        HashNavCacheValue(hash, ticks);
      }
    }
  }

  void HashNavCacheSettings(uint64_t& hash, const t850::navigation::NavMeshBuildSettings& settings) {
    HashNavCacheValue(hash, settings.cellSize);
    HashNavCacheValue(hash, settings.cellHeight);
    HashNavCacheValue(hash, settings.agentHeight);
    HashNavCacheValue(hash, settings.agentRadius);
    HashNavCacheValue(hash, settings.agentMaxClimb);
    HashNavCacheValue(hash, settings.agentMaxSlope);
    HashNavCacheValue(hash, settings.regionMinSize);
    HashNavCacheValue(hash, settings.regionMergeSize);
    HashNavCacheValue(hash, settings.edgeMaxLen);
    HashNavCacheValue(hash, settings.edgeMaxError);
    HashNavCacheValue(hash, settings.vertsPerPoly);
    HashNavCacheValue(hash, settings.detailSampleDist);
    HashNavCacheValue(hash, settings.detailSampleMaxError);
    HashNavCacheValue(hash, settings.enableAutoDropLinks);
    HashNavCacheValue(hash, settings.dropLinkMinHeight);
    HashNavCacheValue(hash, settings.dropLinkMaxHeight);
    HashNavCacheValue(hash, settings.dropLinkMaxHorizontalDistance);
    HashNavCacheValue(hash, settings.dropLinkSampleSpacing);
    HashNavCacheValue(hash, settings.dropLinkRadius);
    HashNavCacheValue(hash, settings.enableAutoJumpLinks);
    HashNavCacheValue(hash, settings.jumpLinkMaxHorizontalDistance);
    HashNavCacheValue(hash, settings.jumpLinkSampleSpacing);
    HashNavCacheValue(hash, settings.jumpLinkRadius);
    HashNavCacheValue(hash, settings.enableHybridJumpLinks);
    HashNavCacheValue(hash, settings.hybridJumpMaxLinks);
    HashNavCacheValue(hash, settings.offMeshLinkValidationKey);
  }

  int NavAreaFromSceneName(const std::string& name) {
    if (name == "walkable") return 0;
    if (name == "drop") return 1;
    if (name == "jump") return 2;
    if (name == "jump_pad") return 3;
    if (name == "jump_intent") return 4;
    if (name == "water") return 5;
    if (name == "door") return 6;
    if (name == "mud") return 7;
    if (name == "custom") return 8;
    return 8;
  }

  t850::navigation::NavMeshModifierMode NavModifierModeFromSceneName(const std::string& name) {
    if (name == "include" || name == "include_bounds" || name == "bounds")
      return t850::navigation::NavMeshModifierMode::Include;
    if (name == "area" || name == "area_cost" || name == "cost")
      return t850::navigation::NavMeshModifierMode::Area;
    if (name == "link_include" || name == "link_add" || name == "add_links")
      return t850::navigation::NavMeshModifierMode::LinkInclude;
    if (name == "link_exclude" || name == "exclude_links")
      return t850::navigation::NavMeshModifierMode::LinkExclude;
    return t850::navigation::NavMeshModifierMode::Exclude;
  }

  t850::navigation::NavMeshVolumeModifier NavVolumeModifierFromScene(
      const t850::scene::SceneNavMeshVolumeDesc& desc) {
    t850::navigation::NavMeshVolumeModifier modifier;
    modifier.name = desc.name;
    modifier.mode = NavModifierModeFromSceneName(desc.type);
    modifier.position = XVECTOR3(desc.position.x, desc.position.y, desc.position.z, 1.0f);
    modifier.rotation = XVECTOR3(desc.rotation.x, desc.rotation.y, desc.rotation.z, 0.0f);
    modifier.halfExtents = XVECTOR3(
        (std::max)(0.001f, std::abs(desc.half_extents.x)),
        (std::max)(0.001f, std::abs(desc.half_extents.y)),
        (std::max)(0.001f, std::abs(desc.half_extents.z)),
        0.0f);
    modifier.area = NavAreaFromSceneName(desc.area);
    modifier.cost = (std::max)(0.01f, desc.cost);
    modifier.enabled = desc.enabled && desc.shape == "box";
    return modifier;
  }

  void HashNavMeshVolumes(uint64_t& hash, const std::vector<t850::scene::SceneNavMeshVolumeDesc>& volumes) {
    HashNavCacheValue(hash, static_cast<uint64_t>(volumes.size()));
    for (const t850::scene::SceneNavMeshVolumeDesc& volume : volumes) {
      HashNavCacheString(hash, volume.name);
      HashNavCacheString(hash, volume.type);
      HashNavCacheString(hash, volume.shape);
      HashNavCacheValue(hash, volume.position.x);
      HashNavCacheValue(hash, volume.position.y);
      HashNavCacheValue(hash, volume.position.z);
      HashNavCacheValue(hash, volume.rotation.x);
      HashNavCacheValue(hash, volume.rotation.y);
      HashNavCacheValue(hash, volume.rotation.z);
      HashNavCacheValue(hash, volume.half_extents.x);
      HashNavCacheValue(hash, volume.half_extents.y);
      HashNavCacheValue(hash, volume.half_extents.z);
      HashNavCacheString(hash, volume.area);
      HashNavCacheValue(hash, volume.cost);
      HashNavCacheValue(hash, volume.enabled);
    }
  }

  uint64_t ComputeSandboxNavMeshCacheKey(const PrimitiveInst* instances,
                                         int instanceCount,
                                         const std::vector<std::string>& meshPaths,
                                         const std::string& scenePath,
                                         const t850::navigation::NavMeshBuildSettings& settings) {
    uint64_t hash = 0xcbf29ce484222325ull;
    constexpr uint32_t kSandboxNavCacheVersion = 3;
    HashNavCacheValue(hash, kSandboxNavCacheVersion);
    HashNavCacheFileSignature(hash, scenePath);
    HashNavCacheSettings(hash, settings);
    int includedSources = 0;
    for (int meshIndex = 0; meshIndex < instanceCount; ++meshIndex) {
      const PrimitiveInst& instance = instances[meshIndex];
      if (!instance.Visible || !instance.pBase || instance.GetSkinnedMesh()) {
        continue;
      }

      ++includedSources;
      HashNavCacheValue(hash, meshIndex);
      if (meshIndex >= 0 && meshIndex < static_cast<int>(meshPaths.size())) {
        HashNavCacheFileSignature(hash, meshPaths[static_cast<std::size_t>(meshIndex)]);
      } else if (const RenderMesh* mesh = dynamic_cast<const RenderMesh*>(instance.pBase)) {
        HashNavCacheFileSignature(hash, mesh->m_sourcePath);
      }
      HashNavCacheMatrix(hash, instance.Final);
    }
    HashNavCacheValue(hash, includedSources);
    return includedSources > 0 ? hash : 0;
  }

  uint32_t NextNavTestRandom(uint32_t& state, uint32_t salt) {
    state = state * 1664525u + 1013904223u + salt * 747796405u;
    return state;
  }

  XVECTOR3 FurthestNavTestPoint(const std::vector<XVECTOR3>& points, const XVECTOR3& origin) {
    XVECTOR3 best = origin;
    float bestDistanceSq = -1.0f;
    for (const XVECTOR3& point : points) {
      const float distanceSq = DistanceSquared(point, origin);
      if (distanceSq > bestDistanceSq) {
        bestDistanceSq = distanceSq;
        best = point;
      }
    }
    return best;
  }

  XVECTOR3 RandomNavTestPoint(const std::vector<XVECTOR3>& points,
                              XVECTOR3 fallback,
                              uint32_t& randomState,
                              uint32_t salt) {
    if (points.empty()) {
      return fallback;
    }
    for (std::size_t attempt = 0; attempt < points.size(); ++attempt) {
      const uint32_t value = NextNavTestRandom(randomState, salt + static_cast<uint32_t>(attempt));
      const XVECTOR3 candidate = points[static_cast<std::size_t>(value % points.size())];
      if (DistanceSquared(candidate, fallback) > 0.25f) {
        return candidate;
      }
    }
    return points[static_cast<std::size_t>(NextNavTestRandom(randomState, salt) % points.size())];
  }

  XVECTOR3 HorizontalOrFallback(XVECTOR3 value, const XVECTOR3& fallback) {
    value.y = 0.0f;
    value.w = 0.0f;
    if (value.Length() <= 0.0001f) {
      value = fallback;
    }
    value.Normalize();
    return value;
  }

  XVECTOR3 NavTestAgentProjectionExtents() {
    return XVECTOR3(3.0f, 16.0f, 3.0f, 0.0f);
  }

  XVECTOR3 NavTestFollowProjectionExtents() {
    return XVECTOR3(8.0f, 32.0f, 8.0f, 0.0f);
  }

  XVECTOR3 NavTestVisualPosition(const XVECTOR3& navPosition, const XVECTOR3& visualOffset) {
    XVECTOR3 position = navPosition + visualOffset;
    position.w = 1.0f;
    return position;
  }

  float HorizontalYawDeg(XVECTOR3 direction, const XVECTOR3& fallback = XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f)) {
    direction.y = 0.0f;
    direction.w = 0.0f;
    if (direction.Length() <= 0.0001f) {
      direction = fallback;
      direction.y = 0.0f;
      direction.w = 0.0f;
    }
    if (direction.Length() <= 0.0001f) {
      direction = XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
    }
    direction.Normalize();
    return Rad2Deg(std::atan2(direction.x, direction.z));
  }

  float NavTestAgentFacingYawDeg(const XVECTOR3& agentPosition,
                                 const XVECTOR3& targetPosition,
                                 float yawOffsetDegrees,
                                 float yawSign) {
    const float dx = targetPosition.x - agentPosition.x;
    const float dz = targetPosition.z - agentPosition.z;
    return Rad2Deg(std::atan2(dx, dz)) * (yawSign < 0.0f ? -1.0f : 1.0f) + yawOffsetDegrees;
  }

  bool RotateNavTestAgentToFace(PrimitiveInst& instance,
                                const XVECTOR3& agentPosition,
                                const XVECTOR3& targetPosition,
                                float yawOffsetDegrees,
                                float yawSign,
                                float* outYawDegrees = nullptr) {
    const float dx = targetPosition.x - agentPosition.x;
    const float dz = targetPosition.z - agentPosition.z;
    if (dx * dx + dz * dz <= 0.0001f) {
      return false;
    }

    const float yawDegrees = NavTestAgentFacingYawDeg(agentPosition, targetPosition, yawOffsetDegrees, yawSign);
    instance.RotateYAbsolute(yawDegrees);
    if (outYawDegrees) {
      *outYawDegrees = yawDegrees;
    }
    return true;
  }

  int NavAgentBehaviorModeFromScene(const std::string& mode) {
    if (mode == "random") return kNavTestModeRandom;
    if (mode == "furthest") return kNavTestModeFurthest;
    return kNavTestModeFollowPlayer;
  }

  bool NavAgentUsesFormationTarget(const std::string& mode) {
    return mode == "formation";
  }

  XVECTOR3 NavTestFollowSlotTargetAt(const Camera& camera,
                                     XVECTOR3 anchor,
                                     const XVECTOR3& agentPosition,
                                     int slotIndex,
                                     float playerRadius,
                                     float followDistance,
                                     float sideOffset,
                                     float formationDepthStep) {
    const XVECTOR3 forward = HorizontalOrFallback(camera.Look, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
    const XVECTOR3 right = HorizontalOrFallback(camera.Right, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));

    anchor.w = 1.0f;
    XVECTOR3 target = anchor;
    if (slotIndex < 0) {
      const float radius = (std::max)(0.0f, playerRadius);
      if (radius > 0.0001f) {
        XVECTOR3 away = agentPosition - anchor;
        away.y = 0.0f;
        away.w = 0.0f;
        if (away.Length() <= 0.0001f) {
          away = forward * -1.0f;
          away.y = 0.0f;
          away.w = 0.0f;
        }
        if (away.Length() > 0.0001f) {
          away.Normalize();
          target += away * radius;
          target.w = 1.0f;
        }
      }
      return target;
    }
    if (slotIndex == 0) {
      target -= forward * followDistance;
      return target;
    }

    const int pairIndex = (slotIndex + 1) / 2;
    const float sideSign = (slotIndex % 2) ? 1.0f : -1.0f;
    target -= forward * (followDistance + static_cast<float>(pairIndex) * formationDepthStep);
    target += right * (sideSign * sideOffset * static_cast<float>(pairIndex));
    return target;
  }

  XVECTOR3 NavTestFollowSlotTarget(const Camera& camera,
                                   const XVECTOR3& agentPosition,
                                   int slotIndex,
                                   float playerRadius,
                                   float followDistance,
                                   float sideOffset,
                                   float formationDepthStep) {
    return NavTestFollowSlotTargetAt(camera, camera.Eye, agentPosition, slotIndex, playerRadius, followDistance, sideOffset, formationDepthStep);
  }

  bool ResolveNavTestFollowTarget(const t850::navigation::NavMesh& navMesh,
                                  const Camera& camera,
                                  const XVECTOR3& agentPosition,
                                  int slotIndex,
                                  float playerRadius,
                                  float followDistance,
                                  float sideOffset,
                                  float formationDepthStep,
                                  XVECTOR3& desiredTarget,
                                  XVECTOR3& projectedTarget,
                                  std::string* error) {
    XVECTOR3 playerNavPoint;
    std::string projectionError;
    if (!navMesh.ProjectPoint(camera.Eye, playerNavPoint, NavTestFollowProjectionExtents(), &projectionError)) {
      desiredTarget = NavTestFollowSlotTarget(camera, agentPosition, slotIndex, playerRadius, followDistance, sideOffset, formationDepthStep);
      projectedTarget = desiredTarget;
      if (error) *error = "player projection failed: " + projectionError;
      return false;
    }

    desiredTarget = NavTestFollowSlotTargetAt(camera, playerNavPoint, agentPosition, slotIndex, playerRadius, followDistance, sideOffset, formationDepthStep);
    if (navMesh.ProjectPoint(desiredTarget, projectedTarget, NavTestFollowProjectionExtents(), &projectionError)) {
      return true;
    }

    projectedTarget = playerNavPoint;
    if (error) *error = "slot projection failed, using player nav point: " + projectionError;
    return true;
  }

  float Q3CharacterGroundOffsetY(const t850::KinematicCharacterSettings& settings) {
    return settings.capsuleHalfHeight + settings.capsuleRadius;
  }

  XVECTOR3 Q3CenterFromGroundPoint(XVECTOR3 groundPoint, const t850::KinematicCharacterSettings& settings) {
    groundPoint.y += Q3CharacterGroundOffsetY(settings);
    groundPoint.w = 1.0f;
    return groundPoint;
  }

  XVECTOR3 Q3GroundPointFromCenter(XVECTOR3 center, const t850::KinematicCharacterSettings& settings) {
    center.y -= Q3CharacterGroundOffsetY(settings);
    center.w = 1.0f;
    return center;
  }

  XVECTOR3 HorizontalDirectionTo(const XVECTOR3& from, const XVECTOR3& to, const XVECTOR3& fallback) {
    XVECTOR3 direction = to - from;
    direction.y = 0.0f;
    direction.w = 0.0f;
    if (direction.Length() <= 0.0001f) {
      direction = fallback;
    }
    direction.y = 0.0f;
    direction.w = 0.0f;
    if (direction.Length() <= 0.0001f) {
      direction = XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
    }
    direction.Normalize();
    return direction;
  }

  t850::KinematicCharacterInput BuildNavAgentPhysicsInput(const XVECTOR3& groundPosition,
                                                          const XVECTOR3& target,
                                                          bool jump) {
    t850::KinematicCharacterInput input;
    const XVECTOR3 forward = HorizontalDirectionTo(groundPosition, target, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
    input.forward = forward;
    input.right = XVECTOR3(forward.z, 0.0f, -forward.x, 0.0f);
    input.moveForward = true;
    input.moveForwardAmount = 1.0f;
    input.jump = jump;
    input.sprint = false;
    return input;
  }

  t850::KinematicCharacterInput BuildNavAgentAirControlInput(
      const XVECTOR3& groundPosition,
      const XVECTOR3& velocity,
      const XVECTOR3& target,
      bool jump,
      const t850::KinematicCharacterSettings& settings) {
    XVECTOR3 direction(0.0f, 0.0f, 0.0f, 0.0f);
    float moveAmount = 1.0f;

    XVECTOR3 org = groundPosition;
    XVECTOR3 vel = velocity * 0.1f;
    bool usedAirControl = false;
    for (int i = 0; i < 50; ++i) {
      vel.y -= settings.gravity * 0.01f;
      if (vel.y < 0.0f && org.y + vel.y < target.y) {
        const float scale = std::fabs(vel.y) > 0.000001f ? (target.y - org.y) / vel.y : 0.0f;
        org += vel * scale;
        direction = target - org;
        direction.y = 0.0f;
        direction.w = 0.0f;
        float dist = direction.Length();
        if (dist > 0.0001f) {
          direction /= dist;
          const float distQ3Units = (std::min)(32.0f, dist * 32.0f);
          const float speedQ3 = (std::max)(32.0f, (std::min)(400.0f, 13.0f * distQ3Units));
          moveAmount = std::clamp(speedQ3 / 400.0f, 0.08f, 1.0f);
          usedAirControl = true;
        }
        break;
      }
      org += vel;
    }

    if (!usedAirControl) {
      direction = HorizontalDirectionTo(groundPosition, target, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
      moveAmount = 1.0f;
    }

    t850::KinematicCharacterInput input;
    input.forward = direction;
    input.right = XVECTOR3(direction.z, 0.0f, -direction.x, 0.0f);
    input.moveForward = true;
    input.moveForwardAmount = moveAmount;
    input.jump = jump;
    input.sprint = false;
    return input;
  }

  struct SandboxConsoleLine {
    t850::Log::Level level = t850::Log::LVL_INFO;
    std::string text;
  };

  std::vector<SandboxConsoleLine> g_sandboxConsoleLines;
  std::mutex g_sandboxConsoleMutex;
  bool g_sandboxConsoleAutoScroll = true;
  bool g_sandboxConsoleOpen = true;
  bool g_sandboxConsoleCallbackInstalled = false;

  void SandboxConsoleLogCallback(t850::Log::Level level, const char* message) {
    std::lock_guard<std::mutex> lock(g_sandboxConsoleMutex);
    if (g_sandboxConsoleLines.size() >= kSandboxConsoleMaxLines) {
      g_sandboxConsoleLines.erase(g_sandboxConsoleLines.begin());
    }
    g_sandboxConsoleLines.push_back({level, message ? message : ""});
  }

  void InstallSandboxConsoleLogCapture() {
    {
      std::lock_guard<std::mutex> lock(g_sandboxConsoleMutex);
      g_sandboxConsoleLines.clear();
    }
    t850::Log::SetCallback(SandboxConsoleLogCallback);
    g_sandboxConsoleCallbackInstalled = true;
    T8_LOG_INFO("[SandboxConsole] Capturing log output in ImGui console panel");
  }

  void UninstallSandboxConsoleLogCapture() {
    if (g_sandboxConsoleCallbackInstalled) {
      t850::Log::SetCallback(nullptr);
      g_sandboxConsoleCallbackInstalled = false;
    }
  }

#ifdef OS_ANDROID
  struct SandboxAndroidVirtualControlsLayout {
    ImVec2 moveCenter;
    ImVec2 lookCenter;
    ImVec2 jumpCenter;
    ImVec2 runCenter;
    float stickRadius = 0.0f;
    float knobRadius = 0.0f;
    float buttonRadius = 0.0f;
  };

  float ClampFloat(float value, float minValue, float maxValue) {
    return (std::max)(minValue, (std::min)(maxValue, value));
  }

  SandboxAndroidVirtualControlsLayout BuildSandboxAndroidVirtualControlsLayout(float width, float height) {
    SandboxAndroidVirtualControlsLayout layout;
    width = (std::max)(width, 1.0f);
    height = (std::max)(height, 1.0f);
    const float shortest = (std::max)(1.0f, (std::min)(width, height));
    layout.stickRadius = ClampFloat(shortest * 0.12f, 64.0f, shortest * 0.18f);
    layout.knobRadius = layout.stickRadius * 0.38f;
    layout.buttonRadius = layout.stickRadius * 0.42f;
    const float margin = layout.stickRadius * 1.35f;
    const float centerY = height - margin;
    layout.moveCenter = ImVec2(margin, centerY);
    layout.lookCenter = ImVec2(width - margin, centerY);
    layout.jumpCenter = ImVec2(layout.moveCenter.x + layout.stickRadius * 1.55f,
                               layout.moveCenter.y - layout.stickRadius * 0.58f);
    layout.runCenter = ImVec2(layout.jumpCenter.x,
                              layout.moveCenter.y + layout.stickRadius * 0.58f);
    return layout;
  }

  bool PointInsideCircle(float x, float y, const ImVec2& center, float radius) {
    const float dx = x - center.x;
    const float dy = y - center.y;
    return (dx * dx + dy * dy) <= radius * radius;
  }

  XVECTOR2 StickAxisFromPoint(float x, float y, const ImVec2& center, float radius) {
    constexpr float kDeadZone = 0.12f;
    const float dx = x - center.x;
    const float dy = y - center.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= radius * kDeadZone) {
      return XVECTOR2(0.0f, 0.0f);
    }

    const float scale = 1.0f / (std::max)(radius, 1.0f);
    XVECTOR2 axis(dx * scale, dy * scale);
    const float axisLength = axis.Length();
    if (axisLength > 1.0f) {
      axis /= axisLength;
    }
    return axis;
  }

  int FindPointerIndexById(AInputEvent* event, int pointerId) {
    const size_t pointerCount = AMotionEvent_getPointerCount(event);
    for (size_t pointerIndex = 0; pointerIndex < pointerCount; ++pointerIndex) {
      if (AMotionEvent_getPointerId(event, pointerIndex) == pointerId) {
        return static_cast<int>(pointerIndex);
      }
    }
    return -1;
  }

  void DrawLabeledCircle(ImDrawList* drawList,
                         const ImVec2& center,
                         float radius,
                         const char* label,
                         ImU32 fillColor,
                         ImU32 lineColor,
                         ImU32 textColor) {
    drawList->AddCircleFilled(center, radius, fillColor, 32);
    drawList->AddCircle(center, radius, lineColor, 32, 2.0f);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                      textColor,
                      label);
  }
#endif

  ImVec4 SandboxConsoleLogColor(t850::Log::Level level) {
    switch (level) {
    case t850::Log::LVL_ERROR: return ImVec4(1.0f, 0.30f, 0.30f, 1.0f);
    case t850::Log::LVL_DEBUG: return ImVec4(0.45f, 0.95f, 0.45f, 1.0f);
    case t850::Log::LVL_VERBOSE: return ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
    case t850::Log::LVL_TRACE: return ImVec4(0.55f, 0.65f, 1.0f, 1.0f);
    case t850::Log::LVL_INFO:
    default: return ImVec4(0.92f, 0.92f, 0.92f, 1.0f);
    }
  }

  const char* LuminanceModeName(int mode) {
    return mode == 0 ? "Temporal HDR" : "Robust temporal";
  }

  void DrawSandboxConsolePanel(t850::CameraProfileType activeProfile,
                               const XVECTOR3& eye,
                               const SceneProps& sceneProps,
                               const RenderMesh* cullingMesh) {
    if (!g_sandboxConsoleOpen) {
      return;
    }

    ImGui::SetNextWindowSize(ImVec2(1120.0f, 360.0f), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = t850::DevGuiContext::PanelAllowsNavigationFocus("Sandbox Console")
        ? 0
        : (ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoNavInputs);
    if (!ImGui::Begin("Sandbox Console", &g_sandboxConsoleOpen, flags)) {
      ImGui::End();
      return;
    }

    const float blockHeight = ImGui::GetContentRegionAvail().y;
    ImGui::Columns(3, "SandboxConsoleBlocks", true);

    if (ImGui::BeginChild("SandboxConsoleCullingBlock", ImVec2(0.0f, blockHeight), true, ImGuiWindowFlags_HorizontalScrollbar)) {
      ImGui::TextUnformatted("Culling / Light Culling");
      ImGui::Separator();
      ImGui::Text("Frustum culling: %s", sceneProps.FrustumCullingEnabled ? "ON" : "OFF");
      ImGui::Text("Culling debug: %s", sceneProps.ShowCullingDebug ? "ON" : "OFF");
      if (cullingMesh) {
        ImGui::Spacing();
        ImGui::Text("Meshes: %d/%d visible, %d culled",
                    cullingMesh->m_visibleMeshes,
                    cullingMesh->m_totalMeshes,
                    cullingMesh->m_culledMeshes);
        ImGui::Text("Subsets: %d/%d visible, %d culled, %d drawn",
                    cullingMesh->m_visibleSubsets,
                    cullingMesh->m_totalSubsets,
                    cullingMesh->m_culledSubsets,
                    cullingMesh->m_drawnSubsets);
        ImGui::Text("Clusters: %d/%d visible, %d culled, %d drawn",
                    cullingMesh->m_visibleClusters,
                    cullingMesh->m_totalClusters,
                    cullingMesh->m_culledClusters,
                    cullingMesh->m_drawnClusters);
        ImGui::Text("Indices: %llu/%llu drawn, %llu culled",
                    cullingMesh->m_drawnIndices,
                    cullingMesh->m_totalIndices,
                    cullingMesh->m_culledIndices);
        ImGui::Text("Cull CPU: %.3f ms", cullingMesh->m_cullingCpuMs);
      } else {
        ImGui::TextUnformatted("Mesh culling stats: unavailable");
      }

      ImGui::Spacing();
      ImGui::Text("Point lights: %s", sceneProps.PointLightsEnabled ? "enabled" : "disabled");
      ImGui::Text("Deferred lights: %u packed (%u dir + %u tiled)",
                  sceneProps.DebugDeferredLightsPacked,
                  sceneProps.DebugDeferredLightsDirectional,
                  sceneProps.DebugDeferredLightsPointVolumes);
      ImGui::Text("Light set: %u considered / %u active / %u scene",
                  sceneProps.DebugDeferredLightsConsidered,
                  sceneProps.DebugDeferredLightsActiveLimit,
                  sceneProps.DebugDeferredLightsSceneTotal);
      ImGui::Text("Light skips: %u frustum, %u disabled, %u zero, %u capped",
                  sceneProps.DebugDeferredLightsFrustumCulled,
                  sceneProps.DebugDeferredLightsDisabled,
                  sceneProps.DebugDeferredLightsZeroIntensity,
                  sceneProps.DebugDeferredLightsMaxCapped);
      ImGui::Text("Projected work: %.1f%% of screen",
                  sceneProps.DebugDeferredLightVolumeScreenPercent);
      if (sceneProps.DebugDeferredLightTileCount > 0) {
        ImGui::Text("Tiles: %ux%u = %u, %u px",
                    sceneProps.DebugDeferredLightTilesX,
                    sceneProps.DebugDeferredLightTilesY,
                    sceneProps.DebugDeferredLightTileCount,
                    sceneProps.DebugDeferredLightTileSize);
        ImGui::Text("Tile usage: %u active, %u refs, avg %.2f",
                    sceneProps.DebugDeferredLightActiveTiles,
                    sceneProps.DebugDeferredLightTileLightRefs,
                    sceneProps.DebugDeferredLightAverageLightsPerTile);
        ImGui::Text("Tile max: %u lights, saturated %u",
                    sceneProps.DebugDeferredLightMaxLightsInTile,
                    sceneProps.DebugDeferredLightSaturatedTiles);
      } else {
        ImGui::TextUnformatted("Tiles: inactive");
      }
    }
    ImGui::EndChild();

    ImGui::NextColumn();
    if (ImGui::BeginChild("SandboxConsoleCameraBlock", ImVec2(0.0f, blockHeight), true, ImGuiWindowFlags_HorizontalScrollbar)) {
      ImGui::TextUnformatted("Camera / Luminance");
      ImGui::Separator();
      ImGui::Text("Profile: %s", t850::CameraProfileName(activeProfile));
      ImGui::Text("Position: %.2f, %.2f, %.2f", eye.x, eye.y, eye.z);
      ImGui::Text("Frame dt: %.3f ms", sceneProps.FrameDeltaSec * 1000.0f);
      ImGui::Spacing();
      if (!sceneProps.DebugLuminanceEnabled) {
        ImGui::TextUnformatted("Adapted luminance: debug disabled");
      } else if (sceneProps.DebugAdaptedLuminanceValid) {
        ImGui::Text("Adapted luminance: %.4f", sceneProps.DebugAdaptedLuminance);
      } else {
        ImGui::TextUnformatted("Adapted luminance: pending");
      }
      ImGui::Text("Mode: %s", LuminanceModeName(sceneProps.LuminanceMode));
      ImGui::Text("Tau: %.2f", sceneProps.LuminanceTau);
      ImGui::Text("Exposure: %.3f", sceneProps.Exposure);
      ImGui::Text("Tone white: %.3f", sceneProps.ToneMapWhiteLevel);
      ImGui::Text("Bloom: factor %.3f, threshold %.3f",
                  sceneProps.BloomFactor,
                  sceneProps.BloomThreshold);
      ImGui::Spacing();
      ImGui::TextUnformatted("F9 camera profiles | F10 frame dump");
      ImGui::TextUnformatted("Free Fly: WASD, Q/E, Shift, mouse");
      ImGui::TextUnformatted("FPS: WASD, Shift run, Space jump");
    }
    ImGui::EndChild();

    ImGui::NextColumn();
    if (ImGui::BeginChild("SandboxConsoleLogBlock", ImVec2(0.0f, blockHeight), true, ImGuiWindowFlags_HorizontalScrollbar)) {
      std::size_t logCount = 0;
      {
        std::lock_guard<std::mutex> lock(g_sandboxConsoleMutex);
        logCount = g_sandboxConsoleLines.size();
      }
      ImGui::Text("Events / Logs (%u)", static_cast<unsigned int>(logCount));
      ImGui::Separator();
      if (ImGui::SmallButton("Clear")) {
        std::lock_guard<std::mutex> lock(g_sandboxConsoleMutex);
        g_sandboxConsoleLines.clear();
      }
      ImGui::SameLine();
      ImGui::Checkbox("Auto-scroll", &g_sandboxConsoleAutoScroll);
      ImGui::BeginChild("SandboxConsoleLogScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
      {
        std::lock_guard<std::mutex> lock(g_sandboxConsoleMutex);
        for (std::size_t lineIndex = 0; lineIndex < g_sandboxConsoleLines.size(); ++lineIndex) {
          const SandboxConsoleLine& line = g_sandboxConsoleLines[lineIndex];
          ImGui::PushID(static_cast<int>(lineIndex));
          ImGui::PushStyleColor(ImGuiCol_Text, SandboxConsoleLogColor(line.level));
          if (ImGui::Selectable(line.text.c_str(), false)) {
            ImGui::SetClipboardText(line.text.c_str());
          }
          ImGui::PopStyleColor();
          ImGui::PopID();
        }
      }
      if (g_sandboxConsoleAutoScroll) {
        ImGui::SetScrollHereY(1.0f);
      }
      ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::Columns(1);
    ImGui::End();
  }

  int ClampRagdollSimulationSpeedIndex(int index) {
    return t850::ragdoll_editor::ClampSimulationSpeedIndex(index);
  }

  float RagdollSimulationSpeedScaleForIndex(int index) {
    return t850::ragdoll_editor::SimulationSpeedScaleForIndex(index);
  }

  const char* RagdollSimulationSpeedLabelForIndex(int index) {
    return t850::ragdoll_editor::SimulationSpeedLabelForIndex(index);
  }

  std::string NormalizeSceneResourcePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    const std::string embeddedAssetsMarker = "/assets/";
    const std::size_t embeddedAssets = lowerPath.rfind(embeddedAssetsMarker);
    if (embeddedAssets != std::string::npos) {
      path.erase(0, embeddedAssets + 1);
    }
    while (!path.empty() && path.front() == '/') {
      path.erase(path.begin());
    }
    const std::string assetsPrefix = "Assets/";
    if (path.size() >= assetsPrefix.size()) {
      std::string prefix = path.substr(0, assetsPrefix.size());
      std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      if (prefix == "assets/") {
        path.erase(0, assetsPrefix.size());
      }
    }
    return path;
  }

  std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return value;
  }

  XVECTOR3 SceneVecToVector(const t850::scene::Vec3f& value, float w = 1.0f) {
    return XVECTOR3(value.x, value.y, value.z, w);
  }

  bool InvertAffineNoExit(const XMATRIX44& matrix, XMATRIX44& out) {
    for (int i = 0; i < 16; ++i) {
      if (!std::isfinite(matrix.mat[i])) {
        return false;
      }
    }

    const float a00 = matrix.m11, a01 = matrix.m12, a02 = matrix.m13;
    const float a10 = matrix.m21, a11 = matrix.m22, a12 = matrix.m23;
    const float a20 = matrix.m31, a21 = matrix.m32, a22 = matrix.m33;

    const float det =
        a00 * (a11 * a22 - a12 * a21) -
        a01 * (a10 * a22 - a12 * a20) +
        a02 * (a10 * a21 - a11 * a20);
    if (!std::isfinite(det) || std::fabs(det) <= 0.000001f) {
      return false;
    }

    const float invDet = 1.0f / det;
    out.m11 =  (a11 * a22 - a12 * a21) * invDet;
    out.m12 =  (a02 * a21 - a01 * a22) * invDet;
    out.m13 =  (a01 * a12 - a02 * a11) * invDet;
    out.m14 = 0.0f;
    out.m21 =  (a12 * a20 - a10 * a22) * invDet;
    out.m22 =  (a00 * a22 - a02 * a20) * invDet;
    out.m23 =  (a02 * a10 - a00 * a12) * invDet;
    out.m24 = 0.0f;
    out.m31 =  (a10 * a21 - a11 * a20) * invDet;
    out.m32 =  (a01 * a20 - a00 * a21) * invDet;
    out.m33 =  (a00 * a11 - a01 * a10) * invDet;
    out.m34 = 0.0f;

    const float tx = matrix.m41;
    const float ty = matrix.m42;
    const float tz = matrix.m43;
    out.m41 = -(tx * out.m11 + ty * out.m21 + tz * out.m31);
    out.m42 = -(tx * out.m12 + ty * out.m22 + tz * out.m32);
    out.m43 = -(tx * out.m13 + ty * out.m23 + tz * out.m33);
    out.m44 = 1.0f;
    return true;
  }

  t850::Mat4Json MatrixToSnapshotJson(const XMATRIX44& mat) {
    t850::Mat4Json j;
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++)
        j[r][c] = mat.m[r][c];
    return j;
  }

  XMATRIX44 MatrixFromSnapshotJson(const t850::Mat4Json& j) {
    XMATRIX44 mat;
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++)
        mat.m[r][c] = j[r][c];
    return mat;
  }

  t850::SnapshotSkinnedJson CaptureSkinnedSnapshot(RenderSkinnedMesh* skinned,
                                                   bool wireframeVisible,
                                                   bool skeletonVisible) {
    t850::SnapshotSkinnedJson snap;
    if (!skinned || !skinned->HasSkinData()) return snap;

    snap.has_skin = true;
    snap.playing = skinned->IsPlaying();
    snap.looping = skinned->IsLooping();
    snap.use_slerp = skinned->GetUseSlerp();
    snap.use_quat_skinning = skinned->GetUseQuatSkinning();
    snap.keyframe_mode = skinned->GetKeyframeMode();
    snap.wireframe_visible = wireframeVisible;
    snap.skeleton_visible = skeletonVisible;
    snap.animation_speed = skinned->GetAnimSpeed();
    snap.local_time = skinned->GetAnimLocalTime();
    snap.tick_time = skinned->GetAnimTickTime();
    snap.ticks_per_second = skinned->GetAnimTicksPerSecond();
    snap.current_anim_set = skinned->GetCurrentAnimSet();
    snap.num_anim_sets = skinned->GetNumAnimSets();
    snap.current_keyframe = skinned->GetCurrentKeyframe();
    snap.total_keyframes = skinned->GetTotalKeyframes();
    snap.num_bones = skinned->GetNumBones();
    snap.bone_texture_width = skinned->GetBoneTextureWidth();
    snap.bone_texture_rgba32f = skinned->GetBoneTextureData();

    std::vector<XMATRIX44> bones;
    skinned->ExportBoneMatrices(bones);
    snap.bone_matrices.reserve(bones.size());
    for (const XMATRIX44& bone : bones) {
      snap.bone_matrices.push_back(MatrixToSnapshotJson(bone));
    }
    return snap;
  }

  void ApplySkinnedSnapshot(RenderSkinnedMesh* skinned,
                            const t850::SnapshotSkinnedJson& snap,
                            bool& wireframeVisible,
                            bool& skeletonVisible) {
    if (!skinned || !skinned->HasSkinData() || !snap.has_skin) return;

    skinned->SetAnimSpeed(snap.animation_speed);
    skinned->SetLooping(snap.looping);
    skinned->SetUseSlerp(snap.use_slerp);
    skinned->SetUseQuatSkinning(snap.use_quat_skinning);
    skinned->SetKeyframeMode(snap.keyframe_mode);
    if (snap.playing) skinned->PlayAnimation();
    else skinned->PauseAnimation();

    int targetSet = snap.current_anim_set;
    int numSets = skinned->GetNumAnimSets();
    if (numSets > 0 && targetSet >= 0 && targetSet < numSets) {
      int guard = 0;
      while (skinned->GetCurrentAnimSet() != targetSet && guard++ < numSets) {
        skinned->NextAnimation();
      }
    }

    std::vector<XMATRIX44> bones;
    bones.reserve(snap.bone_matrices.size());
    for (const auto& bone : snap.bone_matrices) {
      bones.push_back(MatrixFromSnapshotJson(bone));
    }
    if (!bones.empty()) {
      skinned->ApplySnapshotBoneMatrices(bones);
    } else {
      skinned->ClearSnapshotBoneMatrices();
    }

    wireframeVisible = snap.wireframe_visible;
    skeletonVisible = snap.skeleton_visible;
  }

  bool NearlyEqual(float lhs, float rhs, float epsilon = 0.0001f) {
    return std::fabs(lhs - rhs) <= epsilon;
  }

  bool VecNearlyEqual(const std::array<float, 3>& lhs, const std::array<float, 3>& rhs) {
    return NearlyEqual(lhs[0], rhs[0]) && NearlyEqual(lhs[1], rhs[1]) && NearlyEqual(lhs[2], rhs[2]);
  }

  bool OrbitCameraNearlyEqual(const t850::SandboxOrbitCameraDesc& lhs, const t850::SandboxOrbitCameraDesc& rhs) {
    return VecNearlyEqual(lhs.target, rhs.target) &&
           VecNearlyEqual(lhs.pan_offset, rhs.pan_offset) &&
           VecNearlyEqual(lhs.eye, rhs.eye) &&
           NearlyEqual(lhs.yaw, rhs.yaw) &&
           NearlyEqual(lhs.pitch, rhs.pitch) &&
           NearlyEqual(lhs.distance, rhs.distance);
  }

  bool CameraNearlyEqual(const t850::SandboxCameraDesc& lhs, const t850::SandboxCameraDesc& rhs) {
    const bool orbitMatches =
        (!lhs.orbit.has_value() && !rhs.orbit.has_value()) ||
        (lhs.orbit.has_value() && rhs.orbit.has_value() && OrbitCameraNearlyEqual(*lhs.orbit, *rhs.orbit));
    return lhs.profile == rhs.profile &&
           VecNearlyEqual(lhs.eye, rhs.eye) &&
           VecNearlyEqual(lhs.look, rhs.look) &&
           VecNearlyEqual(lhs.up, rhs.up) &&
           VecNearlyEqual(lhs.right, rhs.right) &&
           NearlyEqual(lhs.yaw, rhs.yaw) &&
           NearlyEqual(lhs.pitch, rhs.pitch) &&
           NearlyEqual(lhs.roll, rhs.roll) &&
           NearlyEqual(lhs.fov, rhs.fov) &&
           NearlyEqual(lhs.aspect_ratio, rhs.aspect_ratio) &&
           NearlyEqual(lhs.near_plane, rhs.near_plane) &&
           NearlyEqual(lhs.far_plane, rhs.far_plane) &&
           lhs.ortho == rhs.ortho &&
           NearlyEqual(lhs.width, rhs.width) &&
           NearlyEqual(lhs.height, rhs.height) &&
           lhs.left_handed == rhs.left_handed &&
           orbitMatches;
  }

  std::string SandboxProfileModelKey(const std::string& path) {
    std::string key = path;
    size_t slash = key.find_last_of("/\\");
    if (slash != std::string::npos)
      key = key.substr(slash + 1);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
      return (char)std::tolower(ch);
    });
    return key;
  }

  const t850::FloatOverrideDesc* FindFloatOverride(const std::vector<t850::FloatOverrideDesc>& values, const std::string& name) {
    for (const auto& value : values)
      if (value.name == name) return &value;
    return nullptr;
  }

  const t850::BoolOverrideDesc* FindBoolOverride(const std::vector<t850::BoolOverrideDesc>& values, const std::string& name) {
    for (const auto& value : values)
      if (value.name == name) return &value;
    return nullptr;
  }

  const t850::IntOverrideDesc* FindIntOverride(const std::vector<t850::IntOverrideDesc>& values, const std::string& name) {
    for (const auto& value : values)
      if (value.name == name) return &value;
    return nullptr;
  }

  const t850::SandboxLightOverrideDesc* FindLightOverride(const std::vector<t850::SandboxLightOverrideDesc>& values, int index) {
    for (const auto& value : values)
      if (value.index == index) return &value;
    return nullptr;
  }

  const t850::SandboxAnimationOverrideDesc* FindAnimationOverride(
      const std::vector<t850::SandboxAnimationOverrideDesc>& values,
      int index) {
    for (const auto& value : values)
      if (value.index == index) return &value;
    return nullptr;
  }

  bool AnimationOverrideNearlyEqual(const t850::SandboxAnimationOverrideDesc& lhs,
                                    const t850::SandboxAnimationOverrideDesc& rhs) {
    return lhs.index == rhs.index &&
           lhs.mesh == rhs.mesh &&
           NearlyEqual(lhs.anim_speed, rhs.anim_speed) &&
           lhs.anim_select == rhs.anim_select &&
           lhs.anim_mode == rhs.anim_mode &&
           lhs.current_keyframe == rhs.current_keyframe;
  }

  std::array<float, 3> ToArray(const XVECTOR3& value) {
    return {value.x, value.y, value.z};
  }

  XVECTOR3 FromArray(const std::array<float, 3>& value) {
    return XVECTOR3(value[0], value[1], value[2]);
  }

  XVECTOR3 TransformPoint(const XVECTOR3& point, const XMATRIX44& matrix) {
    return XVECTOR3(
        point.x * matrix.m11 + point.y * matrix.m21 + point.z * matrix.m31 + matrix.m41,
        point.x * matrix.m12 + point.y * matrix.m22 + point.z * matrix.m32 + matrix.m42,
        point.x * matrix.m13 + point.y * matrix.m23 + point.z * matrix.m33 + matrix.m43,
        1.0f);
  }

  XVECTOR3 TransformVectorNoTranslation(const XVECTOR3& vector, const XMATRIX44& matrix) {
    return XVECTOR3(
        vector.x * matrix.m11 + vector.y * matrix.m21 + vector.z * matrix.m31,
        vector.x * matrix.m12 + vector.y * matrix.m22 + vector.z * matrix.m32,
        vector.x * matrix.m13 + vector.y * matrix.m23 + vector.z * matrix.m33,
        0.0f);
  }

  float Dot3(const XVECTOR3& lhs, const XVECTOR3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
  }

  bool IsBlockingSweepHit(const t850::CharacterCollisionHit& hit, const XVECTOR3& displacement) {
    constexpr float kInitialHitEpsilon = 0.0005f;
    return hit.hit &&
           (hit.fraction > kInitialHitEpsilon || Dot3(displacement, hit.normal) < -0.00001f);
  }

  bool ConsiderSweepHit(const t850::CharacterCollisionHit& candidate,
                        const XVECTOR3& displacement,
                        t850::CharacterCollisionHit& best,
                        bool& hasHit,
                        bool& hasBlockingHit) {
    if (!candidate.hit) {
      return false;
    }

    const bool candidateBlocking = IsBlockingSweepHit(candidate, displacement);
    if (!hasHit ||
        (candidateBlocking && !hasBlockingHit) ||
        (candidateBlocking == hasBlockingHit && candidate.fraction < best.fraction)) {
      best = candidate;
      hasHit = true;
      hasBlockingHit = candidateBlocking;
      return true;
    }
    return false;
  }

  float Length3(const XVECTOR3& vector) {
    return std::sqrt((std::max)(0.0f, Dot3(vector, vector)));
  }

  constexpr float kMaxPhysicsAuthoringCoordinate = 1.0e12f;

  bool IsUsablePhysicsCoordinate(float value) {
    return std::isfinite(value) && std::fabs(value) <= kMaxPhysicsAuthoringCoordinate;
  }

  bool IsUsablePhysicsPoint(const XVECTOR3& point) {
    return IsUsablePhysicsCoordinate(point.x) &&
           IsUsablePhysicsCoordinate(point.y) &&
           IsUsablePhysicsCoordinate(point.z);
  }

  bool IsUsableRenderBounds(const RenderMesh::AABB& bounds) {
    return IsUsablePhysicsPoint(bounds.min) &&
           IsUsablePhysicsPoint(bounds.max) &&
           bounds.min.x <= bounds.max.x &&
           bounds.min.y <= bounds.max.y &&
           bounds.min.z <= bounds.max.z;
  }

  XVECTOR3 Cross3(const XVECTOR3& lhs, const XVECTOR3& rhs) {
    return XVECTOR3(
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
        0.0f);
  }

  XVECTOR3 Normalize3(const XVECTOR3& vector, const XVECTOR3& fallback = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f)) {
    const float length = Length3(vector);
    if (length <= 0.000001f) {
      return fallback;
    }
    return XVECTOR3(vector.x / length, vector.y / length, vector.z / length, 0.0f);
  }

  XVECTOR3 ClampVectorLength3(const XVECTOR3& vector, float maxLength) {
    const float length = Length3(vector);
    if (length <= 0.000001f || length <= maxLength) {
      return XVECTOR3(vector.x, vector.y, vector.z, 0.0f);
    }
    const float scale = maxLength / length;
    return XVECTOR3(vector.x * scale, vector.y * scale, vector.z * scale, 0.0f);
  }

  std::string LowerName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
      out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
  }

  bool NameContains(const std::string& name, const char* token) {
    return name.find(token) != std::string::npos;
  }

  bool NameContainsAny(const std::string& name, std::initializer_list<const char*> tokens) {
    for (const char* token : tokens) {
      if (NameContains(name, token)) {
        return true;
      }
    }
    return false;
  }

  bool HasCommandLineFlag(const char* flag) {
    for (const std::string& arg : ::g_args) {
      if (arg == flag) {
        return true;
      }
    }
    return false;
  }

  bool IsDeformationHelperBoneName(const std::string& lowerName) {
    return NameContains(lowerName, "roll") || NameContains(lowerName, "twist") || NameContains(lowerName, "_pin");
  }

  bool IsAttachmentBoneName(const std::string& lowerName) {
    return NameContainsAny(lowerName, {
        "armor", "weapon", "launcher", "blade", "serration", "guard",
        "thumb", "index", "middle", "ring", "pinky", "knuckle",
        "jaw", "tongue", "teeth", "lip", "brow", "nose", "nostril", "snarl",
        "cheek", "eye", "ear", "crease", "puff", "eyelid", "helmet"});
  }

  bool IsHumanoidDisplayBoneName(const std::string& lowerName) {
    if (lowerName.empty() || IsDeformationHelperBoneName(lowerName) || IsAttachmentBoneName(lowerName)) {
      return false;
    }
    if (NameContainsAny(lowerName, {"hips", "pelvis", "spine", "chest", "neck", "head", "clavicle"})) return true;
    if (NameContainsAny(lowerName, {"arm_upper", "upperarm", "upper_arm", "arm_lower", "lowerarm", "forearm", "lower_arm", "arm_hand"})) return true;
    if (NameContains(lowerName, "hand") && !NameContains(lowerName, "weapon")) return true;
    if (NameContainsAny(lowerName, {"leg_upper", "upperleg", "upper_leg", "thigh", "leg_lower", "lowerleg", "lower_leg", "calf", "shin", "leg_foot"})) return true;
    return NameContains(lowerName, "foot");
  }

  bool IsEndpointHelperForBone(const std::string& parentLowerName, const std::string& childLowerName) {
    if (NameContains(childLowerName, "end")) return true;
    if (NameContains(parentLowerName, "foot") && NameContainsAny(childLowerName, {"ball", "toe"})) return true;
    return false;
  }

  bool IsSpineLikeDisplayName(const std::string& lowerName) {
    return NameContainsAny(lowerName, {"hips", "pelvis", "spine", "chest", "neck", "head"});
  }

  bool IsUpperLegName(const std::string& lowerName) {
    return NameContainsAny(lowerName, {"leg_upper", "upperleg", "upper_leg", "thigh"});
  }

  bool IsLowerLegName(const std::string& lowerName) {
    return NameContainsAny(lowerName, {"leg_lower", "lowerleg", "lower_leg", "calf", "shin"});
  }

  bool IsFootName(const std::string& lowerName) {
    return NameContainsAny(lowerName, {"leg_foot", "foot"});
  }

  bool IsUpperArmName(const std::string& lowerName) {
    return NameContainsAny(lowerName, {"arm_upper", "upperarm", "upper_arm"});
  }

  bool IsLowerArmName(const std::string& lowerName) {
    return NameContainsAny(lowerName, {"arm_lower", "lowerarm", "forearm", "lower_arm"});
  }

  bool IsHandName(const std::string& lowerName) {
    return NameContains(lowerName, "hand") && !NameContains(lowerName, "weapon");
  }

  int DisplayChildPriority(const std::string& parentLowerName, const std::string& childLowerName) {
    int score = 0;
    if (IsAttachmentBoneName(childLowerName)) score -= 1000;
    if (IsDeformationHelperBoneName(childLowerName)) score -= 300;
    if (NameContainsAny(parentLowerName, {"hips", "pelvis"})) {
      if (NameContainsAny(childLowerName, {"spine", "chest", "neck", "head"})) score += 600;
      if (IsUpperLegName(childLowerName)) score += 200;
    } else if (NameContainsAny(parentLowerName, {"spine", "chest"})) {
      if (NameContainsAny(childLowerName, {"spine", "chest", "neck", "head"})) score += 600;
      if (NameContains(childLowerName, "clavicle")) score += 150;
    } else if (NameContains(parentLowerName, "neck")) {
      if (NameContains(childLowerName, "head")) score += 600;
    } else if (NameContains(parentLowerName, "clavicle")) {
      if (IsUpperArmName(childLowerName)) score += 600;
    } else if (IsUpperArmName(parentLowerName)) {
      if (IsLowerArmName(childLowerName)) score += 600;
    } else if (IsLowerArmName(parentLowerName)) {
      if (IsHandName(childLowerName)) score += 600;
    } else if (IsUpperLegName(parentLowerName)) {
      if (IsLowerLegName(childLowerName)) score += 600;
    } else if (IsLowerLegName(parentLowerName)) {
      if (IsFootName(childLowerName)) score += 600;
    } else if (IsFootName(parentLowerName)) {
      if (NameContainsAny(childLowerName, {"ball", "toe"})) score += 600;
    }
    if (IsHumanoidDisplayBoneName(childLowerName)) score += 100;
    return score;
  }

  XMATRIX44 FlipMatrixZ(const XMATRIX44& matrix) {
    XMATRIX44 out = matrix;
    for (int i = 0; i < 4; ++i) {
      out.m[2][i] = -out.m[2][i];
      out.m[i][2] = -out.m[i][2];
    }
    out.m[2][2] = matrix.m[2][2];
    return out;
  }

  XMATRIX44 MakeCapsuleBodyTransform(const XVECTOR3& position, const XVECTOR3& localY) {
    const XVECTOR3 up = Normalize3(localY);
    const XVECTOR3 reference = std::fabs(Dot3(up, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f))) > 0.92f
        ? XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f)
        : XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
    const XVECTOR3 right = Normalize3(Cross3(up, reference), XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
    const XVECTOR3 forward = Normalize3(Cross3(right, up), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));

    XMATRIX44 out;
    out.m11 = right.x;   out.m12 = right.y;   out.m13 = right.z;   out.m14 = 0.0f;
    out.m21 = up.x;      out.m22 = up.y;      out.m23 = up.z;      out.m24 = 0.0f;
    out.m31 = forward.x; out.m32 = forward.y; out.m33 = forward.z; out.m34 = 0.0f;
    out.m41 = position.x; out.m42 = position.y; out.m43 = position.z; out.m44 = 1.0f;
    return out;
  }

  float AxisCoord(const XVECTOR3& value, int axis) {
    if (axis == 0) return value.x;
    if (axis == 1) return value.y;
    return value.z;
  }

  void SetAxisCoord(XVECTOR3& value, int axis, float coord) {
    if (axis == 0) value.x = coord;
    else if (axis == 1) value.y = coord;
    else value.z = coord;
  }

  XVECTOR3 MatrixAxisX(const XMATRIX44& matrix) {
    return Normalize3(XVECTOR3(matrix.m11, matrix.m12, matrix.m13, 0.0f),
                      XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  }

  XVECTOR3 MatrixAxisY(const XMATRIX44& matrix) {
    return Normalize3(XVECTOR3(matrix.m21, matrix.m22, matrix.m23, 0.0f),
                      XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  }

  XVECTOR3 MatrixAxisZ(const XMATRIX44& matrix) {
    return Normalize3(XVECTOR3(matrix.m31, matrix.m32, matrix.m33, 0.0f),
                      XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
  }

  bool IsValidRagdollAxis(const XVECTOR3& axis) {
    return IsUsablePhysicsCoordinate(axis.x) &&
           IsUsablePhysicsCoordinate(axis.y) &&
           IsUsablePhysicsCoordinate(axis.z) &&
           Length3(axis) > 0.000001f;
  }

  XVECTOR3 RejectFromAxis(const XVECTOR3& vector, const XVECTOR3& axis) {
    const float dot = Dot3(vector, axis);
    return XVECTOR3(vector.x - axis.x * dot,
                    vector.y - axis.y * dot,
                    vector.z - axis.z * dot,
                    0.0f);
  }

  void NormalizeRagdollJointFrameAxes(XVECTOR3& twist,
                                      XVECTOR3& plane,
                                      const XVECTOR3& fallbackTwist,
                                      const XVECTOR3& fallbackPlane) {
    twist = Normalize3(IsValidRagdollAxis(twist) ? twist : fallbackTwist, fallbackTwist);
    plane = RejectFromAxis(IsValidRagdollAxis(plane) ? plane : fallbackPlane, twist);
    if (!IsValidRagdollAxis(plane)) {
      plane = RejectFromAxis(std::fabs(twist.x) < 0.8f
                                 ? XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f)
                                 : XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f),
                             twist);
    }
    plane = Normalize3(plane, fallbackPlane);
  }

  XVECTOR3 RotateVectorAroundAxis(const XVECTOR3& vector, const XVECTOR3& axisWorld, float angleRadians) {
    XMATRIX44 rotation;
    XMatRotationAxis(rotation, Normalize3(axisWorld, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f)), angleRadians);
    return Normalize3(TransformVectorNoTranslation(vector, rotation), vector);
  }

  constexpr float kRagdollMinShapeExtent = 0.001f;

  bool IsEditableRagdollShape(const t850::PhysicsShapeDesc& shape) {
    return shape.type == t850::PhysicsShapeType::Capsule ||
           shape.type == t850::PhysicsShapeType::Box;
  }

  const char* RagdollShapeTypeName(t850::PhysicsShapeType type) {
    return t850::ragdoll_editor::ShapeTypeName(type);
  }

  const char* RagdollShapeTypeSaveName(t850::PhysicsShapeType type) {
    return type == t850::PhysicsShapeType::Box ? "box" : "capsule";
  }

  t850::PhysicsShapeType RagdollShapeTypeFromSaveName(const std::string& name) {
    const std::string lower = LowerName(name);
    return lower == "box" ? t850::PhysicsShapeType::Box : t850::PhysicsShapeType::Capsule;
  }

  XVECTOR3 ClampRagdollBoxHalfExtents(const XVECTOR3& halfExtents) {
    return XVECTOR3(
        (std::max)(kRagdollMinShapeExtent, halfExtents.x),
        (std::max)(kRagdollMinShapeExtent, halfExtents.y),
        (std::max)(kRagdollMinShapeExtent, halfExtents.z),
        0.0f);
  }

  float RagdollCapsuleVolume(float radius, float halfHeight) {
    radius = (std::max)(kRagdollMinShapeExtent, radius);
    halfHeight = (std::max)(0.0f, halfHeight);
    return 2.0f * xPI * radius * radius * halfHeight +
           (4.0f / 3.0f) * xPI * radius * radius * radius;
  }

  float RagdollBoxVolume(const XVECTOR3& halfExtents) {
    const XVECTOR3 extents = ClampRagdollBoxHalfExtents(halfExtents);
    return 8.0f * extents.x * extents.y * extents.z;
  }

  XVECTOR3 EquivalentBoxHalfExtentsFromCapsule(const t850::PhysicsShapeDesc& shape) {
    const float radius = (std::max)(kRagdollMinShapeExtent, shape.radius);
    const float halfHeight = (std::max)(0.0f, shape.halfHeight);
    const float halfLength = (std::max)(radius + kRagdollMinShapeExtent, halfHeight + radius);
    const float volume = RagdollCapsuleVolume(radius, halfHeight);
    const float sideHalfExtent = std::sqrt((std::max)(kRagdollMinShapeExtent * kRagdollMinShapeExtent,
                                                     volume / (8.0f * halfLength)));
    return XVECTOR3(sideHalfExtent, halfLength, sideHalfExtent, 0.0f);
  }

  void MorphShapeToBox(t850::PhysicsShapeDesc& shape) {
    if (shape.type == t850::PhysicsShapeType::Box) {
      shape.halfExtents = ClampRagdollBoxHalfExtents(shape.halfExtents);
      return;
    }
    shape.halfExtents = EquivalentBoxHalfExtentsFromCapsule(shape);
    shape.type = t850::PhysicsShapeType::Box;
  }

  void MorphShapeToCapsule(t850::PhysicsShapeDesc& shape) {
    if (shape.type == t850::PhysicsShapeType::Capsule) {
      shape.radius = (std::max)(kRagdollMinShapeExtent, shape.radius);
      shape.halfHeight = (std::max)(kRagdollMinShapeExtent, shape.halfHeight);
      return;
    }

    const XVECTOR3 boxHalfExtents = ClampRagdollBoxHalfExtents(shape.halfExtents);
    const float targetVolume = RagdollBoxVolume(boxHalfExtents);
    const float halfLength = (std::max)(boxHalfExtents.y, kRagdollMinShapeExtent * 2.0f);
    float low = kRagdollMinShapeExtent;
    float high = (std::max)(low, halfLength - kRagdollMinShapeExtent);
    if (RagdollCapsuleVolume(high, (std::max)(kRagdollMinShapeExtent, halfLength - high)) < targetVolume) {
      shape.radius = high;
    } else {
      for (int i = 0; i < 24; ++i) {
        const float mid = (low + high) * 0.5f;
        const float midVolume = RagdollCapsuleVolume(mid, (std::max)(0.0f, halfLength - mid));
        if (midVolume < targetVolume) {
          low = mid;
        } else {
          high = mid;
        }
      }
      shape.radius = (low + high) * 0.5f;
    }
    shape.halfHeight = (std::max)(kRagdollMinShapeExtent, halfLength - shape.radius);
    shape.type = t850::PhysicsShapeType::Capsule;
  }

  float RagdollShapeSupportRadius(const t850::PhysicsShapeDesc& shape,
                                  const XMATRIX44& world,
                                  const XVECTOR3& normalWorld) {
    const XVECTOR3 normal = Normalize3(normalWorld, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
    if (shape.type == t850::PhysicsShapeType::Box) {
      const XVECTOR3 extents = ClampRagdollBoxHalfExtents(shape.halfExtents);
      return std::fabs(Dot3(MatrixAxisX(world), normal)) * extents.x +
             std::fabs(Dot3(MatrixAxisY(world), normal)) * extents.y +
             std::fabs(Dot3(MatrixAxisZ(world), normal)) * extents.z;
    }
    const float radius = (std::max)(kRagdollMinShapeExtent, shape.radius);
    const float halfHeight = (std::max)(0.0f, shape.halfHeight);
    return radius + std::fabs(Dot3(MatrixAxisY(world), normal)) * halfHeight;
  }

  void ClosestPointsOnSegments(const XVECTOR3& p1,
                               const XVECTOR3& q1,
                               const XVECTOR3& p2,
                               const XVECTOR3& q2,
                               XVECTOR3& outPoint1,
                               XVECTOR3& outPoint2);

  bool ComputeCapsuleCapsuleContactAnchor(const t850::PhysicsShapeDesc& childShape,
                                          const XMATRIX44& childWorld,
                                          const t850::PhysicsShapeDesc& parentShape,
                                          const XMATRIX44& parentWorld,
                                          XVECTOR3& outAnchor,
                                          XVECTOR3* outChildDeltaToContact) {
    auto getCapsuleSegment = [](const t850::PhysicsShapeDesc& shape,
                                const XMATRIX44& bodyWorld,
                                XVECTOR3& outStart,
                                XVECTOR3& outEnd,
                                XVECTOR3& outCenter,
                                XVECTOR3& outAxis,
                                float& outRadius) {
      if (shape.type != t850::PhysicsShapeType::Capsule) {
        return false;
      }
      outCenter = XVECTOR3(bodyWorld.m41, bodyWorld.m42, bodyWorld.m43, 1.0f);
      outAxis = MatrixAxisY(bodyWorld);
      outRadius = (std::max)(kRagdollMinShapeExtent, shape.radius);
      const float halfHeight = (std::max)(0.0f, shape.halfHeight);
      outStart = outCenter - outAxis * halfHeight;
      outEnd = outCenter + outAxis * halfHeight;
      outStart.w = 1.0f;
      outEnd.w = 1.0f;
      return true;
    };

    XVECTOR3 childStart;
    XVECTOR3 childEnd;
    XVECTOR3 childCenter;
    XVECTOR3 childAxis;
    float childRadius = 0.0f;
    XVECTOR3 parentStart;
    XVECTOR3 parentEnd;
    XVECTOR3 parentCenter;
    XVECTOR3 parentAxis;
    float parentRadius = 0.0f;
    if (!getCapsuleSegment(childShape, childWorld, childStart, childEnd, childCenter, childAxis, childRadius) ||
        !getCapsuleSegment(parentShape, parentWorld, parentStart, parentEnd, parentCenter, parentAxis, parentRadius)) {
      return false;
    }

    XVECTOR3 movedChildStart = childStart;
    XVECTOR3 movedChildEnd = childEnd;
    XVECTOR3 totalDelta(0.0f, 0.0f, 0.0f, 0.0f);
    XVECTOR3 normal = Normalize3(parentCenter - childCenter, parentAxis);
    if (outChildDeltaToContact) {
      constexpr float kContactTolerance = 0.0001f;
      for (int iteration = 0; iteration < 4; ++iteration) {
        XVECTOR3 childAxisPoint;
        XVECTOR3 parentAxisPoint;
        ClosestPointsOnSegments(movedChildStart, movedChildEnd, parentStart, parentEnd, childAxisPoint, parentAxisPoint);
        normal = Normalize3(parentAxisPoint - childAxisPoint, normal);
        const float centerlineDistance = Length3(parentAxisPoint - childAxisPoint);
        const float surfaceGap = centerlineDistance - (childRadius + parentRadius);
        if (std::fabs(surfaceGap) <= kContactTolerance) {
          break;
        }

        const XVECTOR3 delta = normal * surfaceGap;
        movedChildStart += delta;
        movedChildEnd += delta;
        totalDelta += delta;
      }
    }

    XVECTOR3 childAxisPoint;
    XVECTOR3 parentAxisPoint;
    ClosestPointsOnSegments(movedChildStart, movedChildEnd, parentStart, parentEnd, childAxisPoint, parentAxisPoint);
    normal = Normalize3(parentAxisPoint - childAxisPoint, normal);
    const XVECTOR3 childSurface = childAxisPoint + normal * childRadius;
    const XVECTOR3 parentSurface = parentAxisPoint - normal * parentRadius;
    outAnchor = (childSurface + parentSurface) * 0.5f;
    outAnchor.w = 1.0f;
    if (outChildDeltaToContact) {
      *outChildDeltaToContact = totalDelta;
      outChildDeltaToContact->w = 0.0f;
    }
    return true;
  }

  bool ComputeRagdollShapeContactAnchor(const t850::PhysicsShapeDesc& childShape,
                                        const XMATRIX44& childWorld,
                                        const t850::PhysicsShapeDesc& parentShape,
                                        const XMATRIX44& parentWorld,
                                        XVECTOR3& outAnchor,
                                        XVECTOR3* outChildDeltaToContact = nullptr) {
    if (!IsEditableRagdollShape(childShape) || !IsEditableRagdollShape(parentShape)) {
      return false;
    }
    if (childShape.type == t850::PhysicsShapeType::Capsule &&
        parentShape.type == t850::PhysicsShapeType::Capsule) {
      return ComputeCapsuleCapsuleContactAnchor(
          childShape, childWorld, parentShape, parentWorld, outAnchor, outChildDeltaToContact);
    }
    const XVECTOR3 childCenter(childWorld.m41, childWorld.m42, childWorld.m43, 1.0f);
    const XVECTOR3 parentCenter(parentWorld.m41, parentWorld.m42, parentWorld.m43, 1.0f);
    const XVECTOR3 centerDelta = parentCenter - childCenter;
    const float centerDistance = Length3(centerDelta);
    const XVECTOR3 normal = Normalize3(centerDelta, MatrixAxisY(parentWorld));
    const float childSupport = RagdollShapeSupportRadius(childShape, childWorld, normal);
    const float parentSupport = RagdollShapeSupportRadius(parentShape, parentWorld, normal);
    const XVECTOR3 childSurface = childCenter + normal * childSupport;
    const XVECTOR3 parentSurface = parentCenter - normal * parentSupport;
    outAnchor = (childSurface + parentSurface) * 0.5f;
    outAnchor.w = 1.0f;
    if (outChildDeltaToContact) {
      *outChildDeltaToContact = normal * (centerDistance - childSupport - parentSupport);
      outChildDeltaToContact->w = 0.0f;
    }
    return true;
  }

  std::array<float, 3> RagdollShapeComparableExtents(const t850::PhysicsShapeDesc& shape) {
    if (shape.type == t850::PhysicsShapeType::Box) {
      const XVECTOR3 extents = ClampRagdollBoxHalfExtents(shape.halfExtents);
      return {extents.x, extents.y, extents.z};
    }
    const float radius = (std::max)(kRagdollMinShapeExtent, shape.radius);
    return {radius, (std::max)(kRagdollMinShapeExtent, shape.halfHeight + radius), radius};
  }

  XMATRIX44 MakeCapsuleBodyTransform(const XVECTOR3& position, const XVECTOR3& localY, const XVECTOR3& localXHint) {
    const XVECTOR3 up = Normalize3(localY);
    XVECTOR3 rightHint = localXHint - up * Dot3(localXHint, up);
    const XVECTOR3 reference = std::fabs(Dot3(up, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f))) > 0.92f
        ? XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f)
        : XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
    const XVECTOR3 fallbackRight = Normalize3(Cross3(up, reference), XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
    const XVECTOR3 right = Normalize3(rightHint, fallbackRight);
    const XVECTOR3 forward = Normalize3(Cross3(right, up), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
    const XVECTOR3 correctedRight = Normalize3(Cross3(up, forward), right);

    XMATRIX44 out;
    out.m11 = correctedRight.x; out.m12 = correctedRight.y; out.m13 = correctedRight.z; out.m14 = 0.0f;
    out.m21 = up.x;             out.m22 = up.y;             out.m23 = up.z;             out.m24 = 0.0f;
    out.m31 = forward.x;        out.m32 = forward.y;        out.m33 = forward.z;        out.m34 = 0.0f;
    out.m41 = position.x;       out.m42 = position.y;       out.m43 = position.z;       out.m44 = 1.0f;
    return out;
  }

  XVECTOR3 MirrorPointAcrossAxisPlane(const XVECTOR3& point, int axis, float plane) {
    XVECTOR3 mirrored = point;
    SetAxisCoord(mirrored, axis, plane * 2.0f - AxisCoord(point, axis));
    mirrored.w = 1.0f;
    return mirrored;
  }

  XVECTOR3 MirrorVectorAcrossAxis(const XVECTOR3& vector, int axis) {
    XVECTOR3 mirrored = vector;
    SetAxisCoord(mirrored, axis, -AxisCoord(vector, axis));
    mirrored.w = 0.0f;
    return mirrored;
  }

  XMATRIX44 MirrorCapsuleTransformAcrossAxisPlane(const XMATRIX44& source, int axis, float plane) {
    const XVECTOR3 center = MirrorPointAcrossAxisPlane(
        XVECTOR3(source.m41, source.m42, source.m43, 1.0f), axis, plane);
    const XVECTOR3 localY = MirrorVectorAcrossAxis(MatrixAxisY(source), axis);
    const XVECTOR3 localX = MirrorVectorAcrossAxis(MatrixAxisX(source), axis);
    return MakeCapsuleBodyTransform(center, localY, localX);
  }

  bool IsNameBoundary(char ch) {
    return !std::isalnum(static_cast<unsigned char>(ch));
  }

  bool HasShortSideToken(const std::string& lowerName, char side) {
    for (std::size_t i = 0; i < lowerName.size(); ++i) {
      if (lowerName[i] != side) {
        continue;
      }
      const bool before = i == 0 || IsNameBoundary(lowerName[i - 1]);
      const bool after = i + 1 >= lowerName.size() || IsNameBoundary(lowerName[i + 1]);
      if (before && after) {
        return true;
      }
    }
    return false;
  }

  bool HasSideToken(const std::string& lowerName, const char* token) {
    const std::size_t tokenLength = std::strlen(token);
    if (tokenLength == 0) {
      return false;
    }
    std::size_t pos = lowerName.find(token);
    while (pos != std::string::npos) {
      const bool before = pos == 0 || IsNameBoundary(lowerName[pos - 1]);
      const bool after = pos + tokenLength >= lowerName.size() || IsNameBoundary(lowerName[pos + tokenLength]);
      if (before && after) {
        return true;
      }
      pos = lowerName.find(token, pos + 1);
    }
    return false;
  }

  bool HasLongSideToken(const std::string& lowerName, const char* token) {
    const std::size_t tokenLength = std::strlen(token);
    std::size_t pos = lowerName.find(token);
    while (pos != std::string::npos) {
      const bool before = pos == 0 || IsNameBoundary(lowerName[pos - 1]);
      const bool after = pos + tokenLength >= lowerName.size() || IsNameBoundary(lowerName[pos + tokenLength]);
      if (before || after || pos == 0) {
        return true;
      }
      pos = lowerName.find(token, pos + 1);
    }
    return false;
  }

  int DetectSymmetrySideFromLowerName(const std::string& lowerName) {
    const bool left = HasLongSideToken(lowerName, "left") ||
        HasSideToken(lowerName, "lf") ||
        HasSideToken(lowerName, "lt") ||
        HasSideToken(lowerName, "lft") ||
        HasShortSideToken(lowerName, 'l');
    const bool right = HasLongSideToken(lowerName, "right") ||
        HasSideToken(lowerName, "rt") ||
        HasSideToken(lowerName, "rgt") ||
        HasShortSideToken(lowerName, 'r');
    if (left == right) {
      return 0;
    }
    return left ? -1 : 1;
  }

  std::string NormalizeRagdollSymmetryKey(const std::string& name) {
    const std::string lowerName = LowerName(name);
    std::string out;
    out.reserve(lowerName.size());
    for (std::size_t i = 0; i < lowerName.size();) {
      if (lowerName.compare(i, 4, "left") == 0 &&
          (i == 0 || IsNameBoundary(lowerName[i - 1]))) {
        i += 4;
        continue;
      }
      if (lowerName.compare(i, 5, "right") == 0 &&
          (i == 0 || IsNameBoundary(lowerName[i - 1]))) {
        i += 5;
        continue;
      }
      bool removedSideToken = false;
      for (const char* token : {"lf", "lt", "lft", "rt", "rgt"}) {
        const std::size_t tokenLength = std::strlen(token);
        if (tokenLength == 0 || lowerName.compare(i, tokenLength, token) != 0) {
          continue;
        }
        const bool before = i == 0 || IsNameBoundary(lowerName[i - 1]);
        const bool after = i + tokenLength >= lowerName.size() || IsNameBoundary(lowerName[i + tokenLength]);
        if (before && after) {
          i += tokenLength;
          removedSideToken = true;
          break;
        }
      }
      if (removedSideToken) {
        continue;
      }
      if ((lowerName[i] == 'l' || lowerName[i] == 'r') &&
          (i == 0 || IsNameBoundary(lowerName[i - 1])) &&
          (i + 1 >= lowerName.size() || IsNameBoundary(lowerName[i + 1]))) {
        ++i;
        continue;
      }
      if (std::isalnum(static_cast<unsigned char>(lowerName[i]))) {
        out.push_back(lowerName[i]);
      }
      ++i;
    }
    return out;
  }

  bool IsCoreSymmetricCapsuleName(const std::string& lowerName) {
    return NameContainsAny(lowerName, {
        "root", "hips", "pelvis", "spine", "abdomen", "torso", "chest", "neck", "head"});
  }

  void BuildOctahedralBonePoints(const XVECTOR3& root,
                                 const XVECTOR3& tip,
                                 float widthScale,
                                 float minWidth,
                                 std::array<XVECTOR3, 6>& outPoints) {
    XVECTOR3 axis = tip - root;
    float length = Length3(axis);
    if (length <= 0.0001f) {
      axis = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
      length = 0.02f;
    } else {
      axis = axis / length;
    }

    const XVECTOR3 ref = std::fabs(axis.y) < 0.85f
        ? XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f)
        : XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
    const XVECTOR3 sideA = Normalize3(Cross3(ref, axis), XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
    const XVECTOR3 sideB = Normalize3(Cross3(axis, sideA), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
    const XVECTOR3 center = root + axis * (length * 0.38f);
    const float width = (std::max)(minWidth, length * widthScale);

    outPoints[0] = root;
    outPoints[1] = tip;
    outPoints[2] = center + sideA * width;
    outPoints[3] = center - sideA * width;
    outPoints[4] = center + sideB * width;
    outPoints[5] = center - sideB * width;
  }

  float MatrixMaxAbsDiff(const XMATRIX44& a, const XMATRIX44& b) {
    float maxDiff = 0.0f;
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c)
        maxDiff = (std::max)(maxDiff, std::fabs(a.m[r][c] - b.m[r][c]));
    return maxDiff;
  }

  float MatrixTranslationDistance(const XMATRIX44& a, const XMATRIX44& b) {
    const float dx = a.m41 - b.m41;
    const float dy = a.m42 - b.m42;
    const float dz = a.m43 - b.m43;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  void WriteMatrixCsv(std::ofstream& file, const XMATRIX44& matrix) {
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 4; ++c) {
        file << ',' << matrix.m[r][c];
      }
    }
  }

  const char* BoneNameOrEmpty(const xF::xSkeleton* skeleton, int boneIndex) {
    if (!skeleton || boneIndex < 0 || boneIndex >= static_cast<int>(skeleton->Bones.size())) {
      return "";
    }
    return skeleton->Bones[boneIndex].Name.c_str();
  }

  constexpr int kRagdollJointDisabled = -2;
  constexpr int kRagdollJointInheritParent = -1;
  constexpr int kRagdollSelectCapsules = 0;
  constexpr int kRagdollSelectJoints = 1;
  constexpr int kRagdollSelectBones = 2;
  constexpr int kRagdollToolSelect = 0;
  constexpr int kRagdollToolEditCapsule = 1;
  constexpr int kRagdollToolMove = 2;
  constexpr int kRagdollToolRotate = 3;
  constexpr int kRagdollTransformSpaceLocal = 0;
  constexpr int kRagdollTransformSpaceGlobal = 1;

  const char* RagdollToolName(int toolMode) {
    return t850::ragdoll_editor::ToolModeName(toolMode);
  }

  t850::PhysicsRagdollJointType RagdollJointTypeFromInt(int value) {
    return value == static_cast<int>(t850::PhysicsRagdollJointType::Fixed)
        ? t850::PhysicsRagdollJointType::Fixed
        : t850::PhysicsRagdollJointType::SwingTwist;
  }

  int RagdollJointTypeToInt(t850::PhysicsRagdollJointType type) {
    return type == t850::PhysicsRagdollJointType::Fixed ? 1 : 0;
  }

  const char* RagdollJointTypeName(t850::PhysicsRagdollJointType type) {
    return t850::ragdoll_editor::JointTypeName(type);
  }

  struct SkeletonEditBoneJson {
    int index = -1;
    std::string name;
    std::array<float, 16> combined{};
  };

  struct SkeletonEditJson {
    std::string model;
    std::vector<SkeletonEditBoneJson> bones;
  };

  std::string JsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
      if (ch == '\\' || ch == '"') {
        out.push_back('\\');
      }
      out.push_back(ch);
    }
    return out;
  }

  bool ParseJsonStringAt(const std::string& json, std::size_t keyPos, std::string& out) {
    const std::size_t colon = json.find(':', keyPos);
    if (colon == std::string::npos) return false;
    std::size_t quote = json.find('"', colon + 1);
    if (quote == std::string::npos) return false;
    ++quote;
    out.clear();
    bool escaped = false;
    for (std::size_t i = quote; i < json.size(); ++i) {
      const char ch = json[i];
      if (escaped) {
        out.push_back(ch);
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        return true;
      } else {
        out.push_back(ch);
      }
    }
    return false;
  }

  bool ParseJsonIntAt(const std::string& json, std::size_t keyPos, int& out) {
    const std::size_t colon = json.find(':', keyPos);
    if (colon == std::string::npos) return false;
    char* end = nullptr;
    const long value = std::strtol(json.c_str() + colon + 1, &end, 10);
    if (end == json.c_str() + colon + 1) return false;
    out = static_cast<int>(value);
    return true;
  }

  bool ParseJsonFloatAt(const std::string& json, std::size_t keyPos, float& out) {
    const std::size_t colon = json.find(':', keyPos);
    if (colon == std::string::npos) return false;
    char* end = nullptr;
    const float value = std::strtof(json.c_str() + colon + 1, &end);
    if (end == json.c_str() + colon + 1) return false;
    out = value;
    return true;
  }

  bool ParseJsonBoolAt(const std::string& json, std::size_t keyPos, bool& out) {
    const std::size_t colon = json.find(':', keyPos);
    if (colon == std::string::npos) return false;
    const char* cursor = json.c_str() + colon + 1;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
    if (std::strncmp(cursor, "true", 4) == 0) {
      out = true;
      return true;
    }
    if (std::strncmp(cursor, "false", 5) == 0) {
      out = false;
      return true;
    }
    return false;
  }

  bool ParseFloatArray16At(const std::string& json, std::size_t keyPos, std::array<float, 16>& out) {
    const std::size_t start = json.find('[', keyPos);
    if (start == std::string::npos) return false;
    const char* cursor = json.c_str() + start + 1;
    char* end = nullptr;
    for (std::size_t i = 0; i < out.size(); ++i) {
      while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' || *cursor == ',') ++cursor;
      out[i] = std::strtof(cursor, &end);
      if (end == cursor) return false;
      cursor = end;
    }
    return true;
  }

  bool ParseFloatArray3At(const std::string& json, std::size_t keyPos, std::array<float, 3>& out) {
    const std::size_t start = json.find('[', keyPos);
    if (start == std::string::npos) return false;
    const char* cursor = json.c_str() + start + 1;
    char* end = nullptr;
    for (std::size_t i = 0; i < out.size(); ++i) {
      while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' || *cursor == ',') ++cursor;
      out[i] = std::strtof(cursor, &end);
      if (end == cursor) return false;
      cursor = end;
    }
    return true;
  }

  bool ParseIntArrayAt(const std::string& json, std::size_t keyPos, std::vector<int>& out) {
    out.clear();
    const std::size_t start = json.find('[', keyPos);
    if (start == std::string::npos) return false;
    const std::size_t endArray = json.find(']', start + 1);
    if (endArray == std::string::npos) return false;

    const char* cursor = json.c_str() + start + 1;
    const char* endCursor = json.c_str() + endArray;
    while (cursor < endCursor) {
      while (cursor < endCursor && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' || *cursor == ',')) {
        ++cursor;
      }
      if (cursor >= endCursor) break;
      char* parsedEnd = nullptr;
      const long value = std::strtol(cursor, &parsedEnd, 10);
      if (parsedEnd == cursor) return false;
      out.push_back(static_cast<int>(value));
      cursor = parsedEnd;
    }
    return true;
  }

  bool ParseSkeletonEditJson(const std::string& json, SkeletonEditJson& out) {
    out = SkeletonEditJson{};
    if (const std::size_t modelKey = json.find("\"model\""); modelKey != std::string::npos) {
      ParseJsonStringAt(json, modelKey, out.model);
    }

    std::size_t pos = json.find("\"bones\"");
    if (pos == std::string::npos) return true;
    while ((pos = json.find("\"index\"", pos)) != std::string::npos) {
      SkeletonEditBoneJson bone;
      const std::size_t colon = json.find(':', pos);
      if (colon == std::string::npos) break;
      bone.index = std::atoi(json.c_str() + colon + 1);

      const std::size_t nameKey = json.find("\"name\"", colon);
      const std::size_t combinedKey = json.find("\"combined\"", colon);
      if (nameKey == std::string::npos || combinedKey == std::string::npos) break;
      ParseJsonStringAt(json, nameKey, bone.name);
      if (!ParseFloatArray16At(json, combinedKey, bone.combined)) break;
      out.bones.push_back(std::move(bone));
      pos = combinedKey + 10;
    }
    return true;
  }

  std::array<float, 16> MatrixToArray16(const XMATRIX44& matrix) {
    std::array<float, 16> out{};
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c)
        out[static_cast<std::size_t>(r * 4 + c)] = matrix.m[r][c];
    return out;
  }

  XMATRIX44 MatrixFromArray16(const std::array<float, 16>& values) {
    XMATRIX44 out;
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c)
        out.m[r][c] = values[static_cast<std::size_t>(r * 4 + c)];
    return out;
  }

  std::array<float, 3> MatrixTranslation(const XMATRIX44& matrix) {
    return {matrix.m41, matrix.m42, matrix.m43};
  }

  std::array<float, 3> MatrixEulerDegreesXYZ(const XMATRIX44& matrix) {
    const float y = std::asin((std::max)(-1.0f, (std::min)(1.0f, -matrix.m13)));
    const float cy = std::cos(y);
    float x = 0.0f;
    float z = 0.0f;
    if (std::fabs(cy) > 0.00001f) {
      x = std::atan2(matrix.m23, matrix.m33);
      z = std::atan2(matrix.m12, matrix.m11);
    } else {
      x = std::atan2(-matrix.m32, matrix.m22);
    }
    return {Rad2Deg(x), Rad2Deg(y), Rad2Deg(z)};
  }

  XMATRIX44 MatrixFromTranslationEulerDegreesXYZ(const std::array<float, 3>& translation,
                                                 const std::array<float, 3>& eulerDegrees) {
    XMATRIX44 rx, ry, rz;
    XMatIdentity(rx);
    XMatIdentity(ry);
    XMatIdentity(rz);
    XMatRotationX(rx, Deg2Rad(eulerDegrees[0]));
    XMatRotationY(ry, Deg2Rad(eulerDegrees[1]));
    XMatRotationZ(rz, Deg2Rad(eulerDegrees[2]));
    XMATRIX44 matrix = rx * ry * rz;
    matrix.m41 = translation[0];
    matrix.m42 = translation[1];
    matrix.m43 = translation[2];
    matrix.m44 = 1.0f;
    return matrix;
  }

  const char* RagdollCapsuleHandleName(int handleIndex) {
    switch (handleIndex) {
    case 0: return "center";
    case 1: return "+Y extent";
    case 2: return "-Y extent";
    case 3: return "+X extent";
    case 4: return "-X extent";
    case 5: return "+Z extent";
    case 6: return "-Z extent";
    default: return "none";
    }
  }

  std::string FileSafeModelKey(std::string key) {
    if (key.empty()) key = "model";
    for (char& ch : key) {
      const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                      (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.';
      if (!ok) ch = '_';
    }
    return key;
  }

  ImVec2 ProjectWorldToScreen(const XVECTOR3& point, const XMATRIX44& vp, int width, int height, bool& visible) {
    const float cx = point.x * vp.m11 + point.y * vp.m21 + point.z * vp.m31 + vp.m41;
    const float cy = point.x * vp.m12 + point.y * vp.m22 + point.z * vp.m32 + vp.m42;
    const float cw = point.x * vp.m14 + point.y * vp.m24 + point.z * vp.m34 + vp.m44;
    visible = std::fabs(cw) > 0.000001f;
    if (!visible) return ImVec2(-1.0f, -1.0f);
    const float ndcX = cx / cw;
    const float ndcY = cy / cw;
    visible = ndcX >= -1.15f && ndcX <= 1.15f && ndcY >= -1.15f && ndcY <= 1.15f;
    return ImVec2((ndcX * 0.5f + 0.5f) * width,
                  (1.0f - (ndcY * 0.5f + 0.5f)) * height);
  }

  float DistancePointToSegmentSq(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
    const float abX = b.x - a.x;
    const float abY = b.y - a.y;
    const float abLenSq = abX * abX + abY * abY;
    float t = 0.0f;
    if (abLenSq > 0.000001f) {
      t = ((p.x - a.x) * abX + (p.y - a.y) * abY) / abLenSq;
      t = (std::max)(0.0f, (std::min)(1.0f, t));
    }
    const float closestX = a.x + abX * t;
    const float closestY = a.y + abY * t;
    const float dx = p.x - closestX;
    const float dy = p.y - closestY;
    return dx * dx + dy * dy;
  }

  float Clamp01(float value) {
    return (std::max)(0.0f, (std::min)(1.0f, value));
  }

  void ClosestPointsOnSegments(const XVECTOR3& p1,
                               const XVECTOR3& q1,
                               const XVECTOR3& p2,
                               const XVECTOR3& q2,
                               XVECTOR3& outPoint1,
                               XVECTOR3& outPoint2) {
    constexpr float kEpsilon = 0.000001f;
    const XVECTOR3 d1 = q1 - p1;
    const XVECTOR3 d2 = q2 - p2;
    const XVECTOR3 r = p1 - p2;
    const float a = Dot3(d1, d1);
    const float e = Dot3(d2, d2);
    const float f = Dot3(d2, r);

    float s = 0.0f;
    float t = 0.0f;
    if (a <= kEpsilon && e <= kEpsilon) {
      outPoint1 = p1;
      outPoint2 = p2;
      return;
    }

    if (a <= kEpsilon) {
      t = e > kEpsilon ? Clamp01(f / e) : 0.0f;
    } else {
      const float c = Dot3(d1, r);
      if (e <= kEpsilon) {
        s = Clamp01(-c / a);
      } else {
        const float b = Dot3(d1, d2);
        const float denom = a * e - b * b;
        if (std::fabs(denom) > kEpsilon) {
          s = Clamp01((b * f - c * e) / denom);
        }

        const float tNumerator = b * s + f;
        if (tNumerator < 0.0f) {
          t = 0.0f;
          s = Clamp01(-c / a);
        } else if (tNumerator > e) {
          t = 1.0f;
          s = Clamp01((b - c) / a);
        } else {
          t = tNumerator / e;
        }
      }
    }

    outPoint1 = p1 + d1 * s;
    outPoint2 = p2 + d2 * t;
    outPoint1.w = 1.0f;
    outPoint2.w = 1.0f;
  }

  bool RayPlaneIntersection(const t850::Ray& ray,
                            const XVECTOR3& planePoint,
                            const XVECTOR3& planeNormal,
                            XVECTOR3& outPoint) {
    const float denom = Dot3(ray.direction, planeNormal);
    if (std::fabs(denom) < 0.000001f) {
      return false;
    }
    const float t = Dot3(planePoint - ray.origin, planeNormal) / denom;
    if (t < 0.0f) {
      return false;
    }
    outPoint = ray.origin + ray.direction * t;
    outPoint.w = 1.0f;
    return true;
  }

  bool ClosestRaySegment(const t850::Ray& ray,
                         const XVECTOR3& segmentStart,
                         const XVECTOR3& segmentEnd,
                         float& outRayT,
                         XVECTOR3& outRayPoint,
                         XVECTOR3& outSegmentPoint) {
    constexpr float kEpsilon = 0.000001f;
    const XVECTOR3 rayDirection = Normalize3(ray.direction, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
    const XVECTOR3 segmentDirection = segmentEnd - segmentStart;
    const XVECTOR3 r = ray.origin - segmentStart;
    const float a = Dot3(rayDirection, rayDirection);
    const float e = Dot3(segmentDirection, segmentDirection);
    const float c = Dot3(rayDirection, r);

    if (e <= kEpsilon) {
      outRayT = (std::max)(0.0f, -c / a);
      outRayPoint = ray.origin + rayDirection * outRayT;
      outSegmentPoint = segmentStart;
      outRayPoint.w = 1.0f;
      outSegmentPoint.w = 1.0f;
      return true;
    }

    const float b = Dot3(rayDirection, segmentDirection);
    const float f = Dot3(segmentDirection, r);
    const float denom = a * e - b * b;
    float rayT = std::fabs(denom) > kEpsilon ? (b * f - c * e) / denom : 0.0f;
    rayT = (std::max)(0.0f, rayT);

    float segmentT = (b * rayT + f) / e;
    if (segmentT < 0.0f) {
      segmentT = 0.0f;
      rayT = (std::max)(0.0f, -c / a);
    } else if (segmentT > 1.0f) {
      segmentT = 1.0f;
      rayT = (std::max)(0.0f, (b - c) / a);
    }

    outRayT = rayT;
    outRayPoint = ray.origin + rayDirection * rayT;
    outSegmentPoint = segmentStart + segmentDirection * segmentT;
    outRayPoint.w = 1.0f;
    outSegmentPoint.w = 1.0f;
    return true;
  }

  bool RayIntersectsRagdollShape(const t850::Ray& ray,
                                 const t850::PhysicsShapeDesc& shape,
                                 const XMATRIX44& bodyWorld,
                                 float& outDistance,
                                 XVECTOR3& outPoint) {
    if (shape.type == t850::PhysicsShapeType::Box) {
      XMATRIX44 inverseWorld;
      if (!InvertAffineNoExit(bodyWorld, inverseWorld)) {
        return false;
      }
      t850::Ray localRay;
      localRay.origin = TransformPoint(ray.origin, inverseWorld);
      localRay.direction = Normalize3(TransformVectorNoTranslation(ray.direction, inverseWorld),
                                      XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
      const XVECTOR3 extents = ClampRagdollBoxHalfExtents(shape.halfExtents);
      const t850::AABB localBox(
          XVECTOR3(-extents.x, -extents.y, -extents.z, 1.0f),
          XVECTOR3( extents.x,  extents.y,  extents.z, 1.0f));
      float localT = 0.0f;
      if (!t850::RayIntersectsAABB(localRay, localBox, localT)) {
        return false;
      }
      const XVECTOR3 localHit = localRay.origin + localRay.direction * localT;
      outPoint = TransformPoint(localHit, bodyWorld);
      outPoint.w = 1.0f;
      outDistance = (std::max)(0.0f, Dot3(outPoint - ray.origin, Normalize3(ray.direction)));
      return true;
    }

    if (shape.type != t850::PhysicsShapeType::Capsule) {
      return false;
    }

    const XVECTOR3 center(bodyWorld.m41, bodyWorld.m42, bodyWorld.m43, 1.0f);
    const XVECTOR3 axis = MatrixAxisY(bodyWorld);
    const float radius = (std::max)(kRagdollMinShapeExtent, shape.radius);
    const float halfHeight = (std::max)(0.0f, shape.halfHeight);
    const XVECTOR3 start = center - axis * halfHeight;
    const XVECTOR3 end = center + axis * halfHeight;

    if (halfHeight <= 0.000001f) {
      t850::BoundingSphere sphere;
      sphere.center = center;
      sphere.radius = radius;
      if (!t850::RayIntersectsSphere(ray, sphere, outDistance)) {
        return false;
      }
      outPoint = ray.origin + Normalize3(ray.direction) * outDistance;
      outPoint.w = 1.0f;
      return true;
    }

    float rayT = 0.0f;
    XVECTOR3 rayPoint;
    XVECTOR3 segmentPoint;
    if (!ClosestRaySegment(ray, start, end, rayT, rayPoint, segmentPoint)) {
      return false;
    }
    const float distanceSq = Dot3(rayPoint - segmentPoint, rayPoint - segmentPoint);
    if (distanceSq > radius * radius) {
      return false;
    }

    const float surfaceOffset = std::sqrt((std::max)(0.0f, radius * radius - distanceSq));
    outDistance = (std::max)(0.0f, rayT - surfaceOffset);
    outPoint = ray.origin + Normalize3(ray.direction) * outDistance;
    outPoint.w = 1.0f;
    return true;
  }

  bool ClosestRayAxisParameter(const t850::Ray& ray,
                               const XVECTOR3& axisOrigin,
                               const XVECTOR3& axisDirection,
                               float& outParameter) {
    const XVECTOR3 axis = Normalize3(axisDirection, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
    const XVECTOR3 rayDirection = Normalize3(ray.direction, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
    const XVECTOR3 w0 = axisOrigin - ray.origin;
    const float a = Dot3(axis, axis);
    const float b = Dot3(axis, rayDirection);
    const float c = Dot3(rayDirection, rayDirection);
    const float d = Dot3(axis, w0);
    const float e = Dot3(rayDirection, w0);
    const float denom = a * c - b * b;
    if (std::fabs(denom) < 0.000001f) {
      return false;
    }
    outParameter = (b * e - c * d) / denom;
    return true;
  }

  ImU32 RagdollGizmoAxisColor(int axis, bool active) {
    if (active) return IM_COL32(255, 255, 255, 255);
    switch (axis) {
    case 0: return IM_COL32(245, 70, 70, 235);
    case 1: return IM_COL32(70, 230, 90, 235);
    case 2: return IM_COL32(80, 130, 255, 235);
    default: return IM_COL32(255, 255, 255, 235);
    }
  }

  bool DumpRagdollF5MatrixComparison(const RenderSkinnedMesh& skinned,
                                     const t850::PhysicsRagdollDesc& expectedPose,
                                     const std::vector<t850::PhysicsBodyState>& physicsStates,
                                     const std::vector<int>& physicsBoneIndices,
                                     const std::vector<XMATRIX44>& physicsCombinedMatrices,
                                     const std::vector<XMATRIX44>& animationShaderMatrices,
                                     const std::vector<XMATRIX44>& physicsShaderMatrices) {
    std::error_code ec;
    std::filesystem::create_directories("Logs", ec);
    if (ec) {
      T8_LOG_ERROR("[SceneTemplate] Failed to create Logs directory for ragdoll matrix dump");
      return false;
    }

    const xF::xSkeleton* skeleton = skinned.GetAnimController().GetAnimSkeleton();
    const std::filesystem::path shaderPath = std::filesystem::path("Logs") / "ragdoll_f5_shader_compare.csv";
    const std::filesystem::path combinedPath = std::filesystem::path("Logs") / "ragdoll_f5_combined_overrides.csv";
    const std::filesystem::path bodyPath = std::filesystem::path("Logs") / "ragdoll_f5_physics_bodies.csv";

    std::ofstream shaderFile(shaderPath, std::ios::out | std::ios::trunc);
    if (!shaderFile.is_open()) {
      T8_LOG_ERROR("[SceneTemplate] Failed to open ragdoll shader matrix dump '%s'", shaderPath.string().c_str());
      return false;
    }
    shaderFile << std::fixed << std::setprecision(8);
    shaderFile << "bone_index,bone_name,has_physics_override,max_abs_diff,translation_diff";
    for (int i = 0; i < 16; ++i) shaderFile << ",anim_m" << i;
    for (int i = 0; i < 16; ++i) shaderFile << ",phys_m" << i;
    shaderFile << '\n';

    float maxShaderDiff = 0.0f;
    float maxShaderTranslationDiff = 0.0f;
    int maxShaderBone = -1;
    const std::size_t shaderCount = (std::min)(animationShaderMatrices.size(), physicsShaderMatrices.size());
    for (std::size_t boneIndex = 0; boneIndex < shaderCount; ++boneIndex) {
      const bool hasOverride = std::find(physicsBoneIndices.begin(), physicsBoneIndices.end(), static_cast<int>(boneIndex)) != physicsBoneIndices.end();
      const float maxDiff = MatrixMaxAbsDiff(animationShaderMatrices[boneIndex], physicsShaderMatrices[boneIndex]);
      const float translationDiff = MatrixTranslationDistance(animationShaderMatrices[boneIndex], physicsShaderMatrices[boneIndex]);
      if (maxDiff > maxShaderDiff) {
        maxShaderDiff = maxDiff;
        maxShaderTranslationDiff = translationDiff;
        maxShaderBone = static_cast<int>(boneIndex);
      }
      shaderFile << boneIndex << ",\"" << BoneNameOrEmpty(skeleton, static_cast<int>(boneIndex)) << "\","
                 << (hasOverride ? 1 : 0) << ',' << maxDiff << ',' << translationDiff;
      WriteMatrixCsv(shaderFile, animationShaderMatrices[boneIndex]);
      WriteMatrixCsv(shaderFile, physicsShaderMatrices[boneIndex]);
      shaderFile << '\n';
    }

    std::ofstream combinedFile(combinedPath, std::ios::out | std::ios::trunc);
    if (!combinedFile.is_open()) {
      T8_LOG_ERROR("[SceneTemplate] Failed to open ragdoll combined matrix dump '%s'", combinedPath.string().c_str());
      return false;
    }
    combinedFile << std::fixed << std::setprecision(8);
    combinedFile << "bone_index,bone_name";
    for (int i = 0; i < 16; ++i) combinedFile << ",combined_m" << i;
    combinedFile << '\n';
    for (std::size_t i = 0; i < physicsBoneIndices.size() && i < physicsCombinedMatrices.size(); ++i) {
      const int boneIndex = physicsBoneIndices[i];
      combinedFile << boneIndex << ",\"" << BoneNameOrEmpty(skeleton, boneIndex) << "\"";
      WriteMatrixCsv(combinedFile, physicsCombinedMatrices[i]);
      combinedFile << '\n';
    }

    std::ofstream bodyFile(bodyPath, std::ios::out | std::ios::trunc);
    if (!bodyFile.is_open()) {
      T8_LOG_ERROR("[SceneTemplate] Failed to open ragdoll body matrix dump '%s'", bodyPath.string().c_str());
      return false;
    }
    bodyFile << std::fixed << std::setprecision(8);
    bodyFile << "bone_index,bone_name,body_max_abs_diff,body_translation_diff";
    for (int i = 0; i < 16; ++i) bodyFile << ",expected_body_world_m" << i;
    for (int i = 0; i < 16; ++i) bodyFile << ",actual_body_world_m" << i;
    bodyFile << ",linear_vx,linear_vy,linear_vz,angular_vx,angular_vy,angular_vz\n";

    float maxBodyDiff = 0.0f;
    float maxBodyTranslationDiff = 0.0f;
    int maxBodyBone = -1;
    for (const t850::PhysicsBodyState& state : physicsStates) {
      const XMATRIX44* expectedBodyWorld = nullptr;
      for (const t850::PhysicsRagdollBoneDesc& bone : expectedPose.bones) {
        if (bone.body.boneIndex == state.boneIndex) {
          expectedBodyWorld = &bone.body.worldTransform;
          break;
        }
      }
      const float bodyDiff = expectedBodyWorld ? MatrixMaxAbsDiff(*expectedBodyWorld, state.worldTransform) : 0.0f;
      const float bodyTranslationDiff = expectedBodyWorld ? MatrixTranslationDistance(*expectedBodyWorld, state.worldTransform) : 0.0f;
      if (bodyDiff > maxBodyDiff) {
        maxBodyDiff = bodyDiff;
        maxBodyTranslationDiff = bodyTranslationDiff;
        maxBodyBone = state.boneIndex;
      }

      bodyFile << state.boneIndex << ",\"" << BoneNameOrEmpty(skeleton, state.boneIndex) << "\","
               << bodyDiff << ',' << bodyTranslationDiff;
      if (expectedBodyWorld) {
        WriteMatrixCsv(bodyFile, *expectedBodyWorld);
      } else {
        XMATRIX44 identity;
        identity.Identity();
        WriteMatrixCsv(bodyFile, identity);
      }
      WriteMatrixCsv(bodyFile, state.worldTransform);
      bodyFile << ',' << state.linearVelocity.x << ',' << state.linearVelocity.y << ',' << state.linearVelocity.z
               << ',' << state.angularVelocity.x << ',' << state.angularVelocity.y << ',' << state.angularVelocity.z
               << '\n';
    }

    T8_LOG_INFO("[SceneTemplate] F5 ragdoll matrix dump: shader='%s' combined='%s' bodies='%s' maxShaderDiff=%.6f bone=%d('%s') transDiff=%.6f maxBodyDiff=%.6f bone=%d('%s') bodyTransDiff=%.6f",
                shaderPath.string().c_str(),
                combinedPath.string().c_str(),
                bodyPath.string().c_str(),
                maxShaderDiff,
                maxShaderBone,
                BoneNameOrEmpty(skeleton, maxShaderBone),
                maxShaderTranslationDiff,
                maxBodyDiff,
                maxBodyBone,
                BoneNameOrEmpty(skeleton, maxBodyBone),
                maxBodyTranslationDiff);
    return true;
  }

  bool BuildWorldBounds(RenderMesh* mesh, const XMATRIX44& worldFromMesh, RenderMesh::AABB& outBounds) {
    if (!mesh) {
      return false;
    }

    outBounds.Reset();
    bool expanded = false;
    for (const RenderMesh::MeshInfo& meshInfo : mesh->Info) {
      const RenderMesh::AABB& bounds = meshInfo.bounds;
      if (!IsUsableRenderBounds(bounds)) {
        continue;
      }

      const float xs[2] = { bounds.min.x, bounds.max.x };
      const float ys[2] = { bounds.min.y, bounds.max.y };
      const float zs[2] = { bounds.min.z, bounds.max.z };
      for (float x : xs) {
        for (float y : ys) {
          for (float z : zs) {
            const XVECTOR3 point = TransformPoint(XVECTOR3(x, y, z, 1.0f), worldFromMesh);
            if (!IsUsablePhysicsPoint(point)) {
              continue;
            }
            outBounds.Expand(point.x, point.y, point.z);
            expanded = true;
          }
        }
      }
    }
    return expanded && IsUsableRenderBounds(outBounds);
  }

  void ExpandBounds(RenderMesh::AABB& bounds, const RenderMesh::AABB& other) {
    if (!IsUsableRenderBounds(other)) {
      return;
    }
    bounds.Expand(other.min.x, other.min.y, other.min.z);
    bounds.Expand(other.max.x, other.max.y, other.max.z);
  }

  float VectorComponent(const XVECTOR3& value, int component) {
    switch (component) {
      case 0: return value.x;
      case 1: return value.y;
      case 2: return value.z;
      case 3: return value.w;
      default: return 0.0f;
    }
  }

  bool BuildSkinnedWorldBounds(RenderSkinnedMesh* skinned,
                               const XMATRIX44& worldFromMesh,
                               RenderMesh::AABB& outBounds) {
    if (!skinned || !skinned->HasSkinData() || !skinned->xFile ||
        skinned->xFile->XMeshDataBase.empty() || !skinned->xFile->XMeshDataBase[0]) {
      return false;
    }

    std::vector<XMATRIX44> boneMatrices;
    skinned->ExportBoneMatrices(boneMatrices);
    if (boneMatrices.empty()) {
      return false;
    }

    const xF::xMeshContainer* meshContainer = skinned->xFile->XMeshDataBase[0];
    const std::size_t geometryCount = (std::min)(meshContainer->Geometry.size(), skinned->xFile->MeshInfo.size());
    outBounds.Reset();
    bool expanded = false;

    for (std::size_t geometryIndex = 0; geometryIndex < geometryCount; ++geometryIndex) {
      const xF::xMeshGeometry& sourceGeometry = meshContainer->Geometry[geometryIndex];
      const xF::xFinalGeometry& finalGeometry = skinned->xFile->MeshInfo[geometryIndex];
      const bool hasSkin =
          (sourceGeometry.VertexAttributes & xF::xMeshGeometry::HAS_SKINWEIGHTS0) != 0 &&
          (sourceGeometry.VertexAttributes & xF::xMeshGeometry::HAS_SKININDEXES0) != 0 &&
          !sourceGeometry.SkinWeights.empty() &&
          !sourceGeometry.SkinIndices.empty();
      if (!hasSkin) {
        continue;
      }

      std::size_t vertexCount = (std::min)(sourceGeometry.SkinWeights.size(), sourceGeometry.SkinIndices.size());
      if (!sourceGeometry.Positions.empty()) {
        vertexCount = (std::min)(vertexCount, sourceGeometry.Positions.size());
      }
      const uint32_t strideFloats = finalGeometry.VertexSize / sizeof(float);
      if (finalGeometry.pData && strideFloats >= 3u) {
        vertexCount = (std::min)(vertexCount, static_cast<std::size_t>(finalGeometry.NumVertex));
      } else if (sourceGeometry.Positions.empty()) {
        continue;
      }

      for (std::size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
        XVECTOR3 localPosition;
        if (finalGeometry.pData && strideFloats >= 3u) {
          const float* vertex = finalGeometry.pData + vertexIndex * strideFloats;
          localPosition = XVECTOR3(vertex[0], vertex[1], vertex[2], 1.0f);
        } else {
          localPosition = sourceGeometry.Positions[vertexIndex];
          localPosition.w = 1.0f;
        }

        const XVECTOR3& weights = sourceGeometry.SkinWeights[vertexIndex];
        const XVECTOR3& indices = sourceGeometry.SkinIndices[vertexIndex];
        XVECTOR3 skinnedPosition(0.0f, 0.0f, 0.0f, 1.0f);
        bool hasWeightedBone = false;
        for (int component = 0; component < 4; ++component) {
          const float weight = VectorComponent(weights, component);
          if (weight <= 0.0f) {
            continue;
          }

          const int boneIndex = static_cast<int>(std::floor(VectorComponent(indices, component) + 0.5f));
          if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= boneMatrices.size()) {
            continue;
          }

          const XVECTOR3 bonePosition = TransformPoint(localPosition, boneMatrices[static_cast<std::size_t>(boneIndex)]);
          skinnedPosition.x += bonePosition.x * weight;
          skinnedPosition.y += bonePosition.y * weight;
          skinnedPosition.z += bonePosition.z * weight;
          hasWeightedBone = true;
        }

        if (!hasWeightedBone) {
          continue;
        }

        const XVECTOR3 worldPosition = TransformPoint(skinnedPosition, worldFromMesh);
        if (!IsUsablePhysicsPoint(worldPosition)) {
          continue;
        }
        outBounds.Expand(worldPosition.x, worldPosition.y, worldPosition.z);
        expanded = true;
      }
    }

    return expanded && IsUsableRenderBounds(outBounds);
  }

  bool BuildRagdollCapsuleBounds(const t850::PhysicsRagdollDesc& pose, RenderMesh::AABB& outBounds) {
    outBounds.Reset();
    bool expanded = false;
    for (const t850::PhysicsRagdollBoneDesc& bone : pose.bones) {
      if (!IsEditableRagdollShape(bone.body.shape) ||
          !IsUsablePhysicsPoint(XVECTOR3(
              bone.body.worldTransform.m41,
              bone.body.worldTransform.m42,
              bone.body.worldTransform.m43,
              1.0f))) {
        continue;
      }

      float extentX = 0.0f;
      float extentY = 0.0f;
      float extentZ = 0.0f;
      if (bone.body.shape.type == t850::PhysicsShapeType::Box) {
        const XVECTOR3 extents = ClampRagdollBoxHalfExtents(bone.body.shape.halfExtents);
        const XVECTOR3 axisX = MatrixAxisX(bone.body.worldTransform);
        const XVECTOR3 axisY = MatrixAxisY(bone.body.worldTransform);
        const XVECTOR3 axisZ = MatrixAxisZ(bone.body.worldTransform);
        extentX = std::fabs(axisX.x) * extents.x + std::fabs(axisY.x) * extents.y + std::fabs(axisZ.x) * extents.z;
        extentY = std::fabs(axisX.y) * extents.x + std::fabs(axisY.y) * extents.y + std::fabs(axisZ.y) * extents.z;
        extentZ = std::fabs(axisX.z) * extents.x + std::fabs(axisY.z) * extents.y + std::fabs(axisZ.z) * extents.z;
      } else {
        const XVECTOR3 axisY = MatrixAxisY(bone.body.worldTransform);
        const float radius = (std::max)(0.0f, bone.body.shape.radius);
        const float halfHeight = (std::max)(0.0f, bone.body.shape.halfHeight);
        extentX = radius + std::fabs(axisY.x) * halfHeight;
        extentY = radius + std::fabs(axisY.y) * halfHeight;
        extentZ = radius + std::fabs(axisY.z) * halfHeight;
      }
      const XVECTOR3 center(
          bone.body.worldTransform.m41,
          bone.body.worldTransform.m42,
          bone.body.worldTransform.m43,
          1.0f);
      outBounds.Expand(center.x - extentX, center.y - extentY, center.z - extentZ);
      outBounds.Expand(center.x + extentX, center.y + extentY, center.z + extentZ);
      expanded = true;
    }
    return expanded && IsUsableRenderBounds(outBounds);
  }

  const t850::SelectorDesc* FindSelectorDesc(const std::vector<t850::SelectorDesc>& selectors, const std::string& name) {
    for (const auto& selector : selectors)
      if (selector.name == name) return &selector;
    return nullptr;
  }

  struct CubemapSelection {
    std::string path;
    int index = -1;
  };

  std::string CubemapPathForSelectorIndex(const t850::SelectorDesc& selector, int selectedIndex) {
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(selector.options.size())) {
      return {};
    }
    return "sky/" + selector.options[static_cast<std::size_t>(selectedIndex)];
  }

  bool ResourcePathEquals(std::string lhs, std::string rhs) {
    return ToLowerAscii(NormalizeSceneResourcePath(lhs)) ==
           ToLowerAscii(NormalizeSceneResourcePath(rhs));
  }

  t850::PhysicsMeshBuildQuality PhysicsBuildQualityFromScene(const std::string& quality) {
    return quality == "build_speed"
        ? t850::PhysicsMeshBuildQuality::FavorBuildSpeed
        : t850::PhysicsMeshBuildQuality::FavorRuntimePerformance;
  }

  t850::PhysicsTriangleMeshCookSettings PhysicsCookSettingsFromScene(const t850::scene::ScenePhysicsCookSettingsDesc& desc) {
    t850::PhysicsTriangleMeshCookSettings settings;
    settings.maxTrianglesPerLeaf = desc.max_triangles_per_leaf;
    settings.buildQuality = PhysicsBuildQualityFromScene(desc.build_quality);
    settings.activeEdgeCosThresholdAngle = desc.active_edge_cos_threshold_angle;
    settings.perTriangleUserData = desc.per_triangle_user_data;
    settings.useDiskCache = desc.use_disk_cache;
    return settings;
  }

  t850::navigation::NavMeshBuildSettings DefaultAuthoredNavMeshBuildSettings() {
    t850::navigation::NavMeshBuildSettings settings;
    settings.enableAutoDropLinks = true;
    settings.enableAutoJumpLinks = true;
    settings.enableHybridJumpLinks = true;
    settings.hybridJumpMaxLinks = 192;
    return settings;
  }

  t850::navigation::NavMeshBuildSettings NavMeshBuildSettingsFromScene(
      const t850::scene::SceneNavMeshBuildSettingsDesc& desc) {
    t850::navigation::NavMeshBuildSettings settings = DefaultAuthoredNavMeshBuildSettings();
    settings.cellSize = desc.cell_size;
    settings.cellHeight = desc.cell_height;
    settings.agentHeight = desc.agent_height;
    settings.agentRadius = desc.agent_radius;
    settings.agentMaxClimb = desc.agent_max_climb;
    settings.agentMaxSlope = desc.agent_max_slope;
    settings.regionMinSize = desc.region_min_size;
    settings.regionMergeSize = desc.region_merge_size;
    settings.edgeMaxLen = desc.edge_max_len;
    settings.edgeMaxError = desc.edge_max_error;
    settings.vertsPerPoly = desc.verts_per_poly;
    settings.detailSampleDist = desc.detail_sample_dist;
    settings.detailSampleMaxError = desc.detail_sample_max_error;
    settings.queryExtents = XVECTOR3(desc.query_extents.x, desc.query_extents.y, desc.query_extents.z, 0.0f);
    settings.enableAutoDropLinks = desc.auto_drop_links;
    settings.dropLinkMinHeight = desc.drop_min_height;
    settings.dropLinkMaxHeight = desc.drop_max_height;
    settings.dropLinkMaxHorizontalDistance = desc.drop_max_horizontal;
    settings.dropLinkSampleSpacing = desc.drop_sample_spacing;
    settings.dropLinkRadius = desc.drop_link_radius;
    settings.enableAutoJumpLinks = desc.auto_jump_links;
    settings.jumpLinkMaxHorizontalDistance = desc.jump_max_horizontal;
    settings.jumpLinkSampleSpacing = desc.jump_sample_spacing;
    settings.jumpLinkRadius = desc.jump_link_radius;
    settings.enableHybridJumpLinks = desc.hybrid_jump_links;
    settings.hybridJumpMaxLinks = desc.hybrid_max_links;
    settings.offMeshLinkValidationKey = desc.off_mesh_link_validation_key;
    return settings;
  }

  t850::navigation::NavTraversalType NavLinkTypeFromSceneName(const std::string& name) {
    if (name == "drop") return t850::navigation::NavTraversalType::Drop;
    if (name == "jump_pad") return t850::navigation::NavTraversalType::JumpPad;
    if (name == "jump_intent") return t850::navigation::NavTraversalType::JumpIntent;
    return t850::navigation::NavTraversalType::Jump;
  }

  t850::navigation::NavOffMeshLink NavOffMeshLinkFromScene(const t850::scene::SceneNavMeshLinkDesc& desc) {
    t850::navigation::NavOffMeshLink link;
    link.start = XVECTOR3(desc.start.x, desc.start.y, desc.start.z, 1.0f);
    link.end = XVECTOR3(desc.end.x, desc.end.y, desc.end.z, 1.0f);
    link.radius = (std::max)(0.05f, desc.radius);
    link.bidirectional = desc.bidirectional;
    link.type = NavLinkTypeFromSceneName(desc.type);
    return link;
  }

  bool IsFiniteNavPoint(const t850::scene::Vec3f& point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
  }

  bool IsUsableAuthoredNavLink(const t850::scene::SceneNavMeshLinkDesc& link) {
    if (!link.enabled || !IsFiniteNavPoint(link.start) || !IsFiniteNavPoint(link.end)) {
      return false;
    }
    const float dx = link.end.x - link.start.x;
    const float dy = link.end.y - link.start.y;
    const float dz = link.end.z - link.start.z;
    return dx * dx + dy * dy + dz * dz > 0.0001f && link.radius > 0.0f;
  }

  bool SceneHasStaticPhysicsEntityForObject(const t850::scene::EditorSceneFile& scene, const std::string& objectName) {
    for (const t850::scene::ScenePhysicsEntityDesc& entity : scene.physics_entities) {
      if (entity.type == "static_triangle_mesh" && entity.source_object == objectName) {
        return true;
      }
    }
    return false;
  }

  t850::KinematicCharacterSettings CharacterSettingsFromPhysicsEntity(const t850::scene::ScenePhysicsEntityDesc& player) {
    t850::KinematicCharacterSettings settings = t850::MakeQuake3CharacterSettings();
    const bool capsule = player.shape == "capsule" || player.shape == "sphere" || player.shape == "cylinder";
    settings.collisionShape = capsule
        ? t850::KinematicCharacterSettings::CollisionShape::Capsule
        : t850::KinematicCharacterSettings::CollisionShape::Box;
    if (capsule) {
      settings.capsuleRadius = (std::max)(0.001f, player.radius);
      settings.capsuleHalfHeight = player.shape == "sphere"
          ? (std::max)(0.001f, player.radius)
          : (std::max)(0.001f, player.half_height);
      const float totalHeight = 2.0f * (settings.capsuleHalfHeight + settings.capsuleRadius);
      settings.eyeHeight = (std::max)(0.001f, totalHeight * 0.88f);
    } else {
      const float horizontalRadius = (std::max)(0.001f, (std::max)(player.half_extents.x, player.half_extents.z));
      settings.capsuleRadius = horizontalRadius;
      settings.capsuleHalfHeight = (std::max)(0.001f, player.half_extents.y - horizontalRadius);
      settings.eyeHeight = (std::max)(0.001f, player.half_extents.y * 2.0f * 0.88f);
    }
    settings.minWalkNormalY = std::cos(Deg2Rad(std::clamp(player.character.max_slope_angle_deg, 0.0f, 89.0f)));
    settings.groundProbeDistance = (std::max)(0.05f, settings.capsuleRadius * 0.35f);
    settings.stepHeight = (std::max)(0.05f, settings.capsuleRadius * 1.2f);
    return settings;
  }

  int CharacterRuntimePathFromPhysicsEntity(const t850::scene::ScenePhysicsEntityDesc& entity) {
    return entity.character.runtime_path == "jolt" ? 1 : 0;
  }

  float PlayerBotRadiusFromEntity(const t850::scene::ScenePhysicsEntityDesc& player) {
    return (std::max)(0.0f, player.character.bot_radius);
  }

  XVECTOR3 PlayerEyeFromEntity(const t850::scene::ScenePhysicsEntityDesc& player,
                               const t850::KinematicCharacterSettings& settings) {
    const float verticalHalfExtent = settings.capsuleHalfHeight + settings.capsuleRadius;
    return XVECTOR3(
        player.position.x,
        player.position.y + settings.eyeHeight - verticalHalfExtent,
        player.position.z,
        1.0f);
  }

  int CubemapSelectorIndexForPath(const t850::SelectorDesc& selector, const std::string& resourcePath) {
    for (int index = 0; index < static_cast<int>(selector.options.size()); ++index) {
      if (ResourcePathEquals(CubemapPathForSelectorIndex(selector, index), resourcePath)) {
        return index;
      }
    }
    return -1;
  }

  int CubemapSelectorIndexFromProfile(const t850::SandboxProfileDesc& profile) {
    for (const t850::IntOverrideDesc& selector : profile.selectors) {
      if (selector.name == "cubemap") {
        return selector.value;
      }
    }
    return -1;
  }

  CubemapSelection CubemapSelectionFromProfile(const t850::SelectorDesc& cubemapDesc,
                                               const t850::SandboxProfileDesc& profile) {
    CubemapSelection selection;
    if (profile.cubemap_path.has_value()) {
      selection.path = NormalizeSceneResourcePath(*profile.cubemap_path);
      if (!selection.path.empty()) {
        selection.index = CubemapSelectorIndexForPath(cubemapDesc, selection.path);
        return selection;
      }
    }

    selection.index = CubemapSelectorIndexFromProfile(profile);
    if (selection.index >= 0 && selection.index < static_cast<int>(cubemapDesc.options.size())) {
      selection.path = CubemapPathForSelectorIndex(cubemapDesc, selection.index);
      return selection;
    }

    selection.index = -1;
    return selection;
  }

  CubemapSelection ResolveStartupCubemapSelection(const std::vector<t850::SelectorDesc>& selectors,
                                                  const std::vector<t850::SandboxProfileDesc>& profiles,
                                                  bool embeddedInScene,
                                                  const std::string& modelKey) {
    const t850::SelectorDesc* cubemapDesc = FindSelectorDesc(selectors, "cubemap");
    if (!cubemapDesc) {
      return {};
    }

    const t850::SandboxProfileDesc* baseProfile = nullptr;
    const t850::SandboxProfileDesc* runtimeProfile = nullptr;
    int bestRuntimeScore = -1;
    for (const t850::SandboxProfileDesc& profile : profiles) {
      const bool modelSpecific = !profile.model.empty();
      const bool modelMatches = embeddedInScene
          ? !modelSpecific
          : (!modelSpecific || SandboxProfileModelKey(profile.model) == modelKey);
      if (!modelMatches) {
        continue;
      }

      const bool hasTarget = !profile.name.empty() ||
                             !profile.platform.empty() ||
                             !profile.architecture.empty() ||
                             !profile.gpu_family.empty() ||
                             !profile.gpu_name_contains.empty();
      if (!hasTarget && (embeddedInScene || modelSpecific)) {
        baseProfile = &profile;
        continue;
      }

      const int score = t850::ScoreSceneProfileMatch(profile, modelKey);
      if (score > bestRuntimeScore) {
        bestRuntimeScore = score;
        runtimeProfile = &profile;
      }
    }

    CubemapSelection selection;
    if (baseProfile) {
      selection = CubemapSelectionFromProfile(*cubemapDesc, *baseProfile);
    }
    if (runtimeProfile && runtimeProfile != baseProfile) {
      const CubemapSelection runtimeSelection = CubemapSelectionFromProfile(*cubemapDesc, *runtimeProfile);
      if (!runtimeSelection.path.empty()) {
        selection = runtimeSelection;
      }
    }
    return selection;
  }
}

void SceneTemplate::SetRenderSize(int width, int height) {
  m_renderWidth = width;
  m_renderHeight = height;
  UpdateCameraProjectionForRenderViewport();
}

void SceneTemplate::SetLaunchDesc(const SceneTemplateLaunchDesc& desc) {
  m_launchDesc = desc;
  m_hasLaunchDesc = true;
}

const std::string& SceneTemplate::ActiveSceneFilePath() const {
  if (m_hasLaunchDesc && !m_launchDesc.sceneFilePath.empty()) {
    return m_launchDesc.sceneFilePath;
  }
  if (!g_config.sceneFilePath.empty()) {
    return g_config.sceneFilePath;
  }
  return DefaultSceneTemplateSceneFilePath();
}

const std::string& SceneTemplate::ActiveModelPath() const {
  return (m_hasLaunchDesc && !m_launchDesc.modelPath.empty())
      ? m_launchDesc.modelPath
      : g_config.modelPath;
}

int SceneTemplate::ActiveStartScene() const {
  return m_hasLaunchDesc ? m_launchDesc.startScene : g_config.startScene;
}

void SceneTemplate::ResizeRenderTargets(int width, int height, int finalOutputRT) {
  m_renderWidth = width;
  m_renderHeight = height;
  m_finalOutputRT = finalOutputRT;
  UpdateCameraProjectionForRenderViewport();
  if (!pFramework || !pFramework->pVideoDriver || width <= 0 || height <= 0) {
    return;
  }
  m_renderGraph.DestroyRenderTargets(pFramework->pVideoDriver);
  m_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, SceneProp, width, height);
  t850::sandbox::RefreshDeferredPassHandles(
      m_renderGraph,
      GBufferPass,
      DeferredPass,
      Extra16FPass,
      DepthPass,
      ShadowAccumPass,
      ExtraHelperPass,
      BloomAccumPass,
      AdaptedLumCurrentPass,
      AdaptedLumPrevPass);
}

int SceneTemplate::RenderViewportWidth() const {
  if (m_renderWidth > 0) {
    return m_renderWidth;
  }
  return (std::max)(1, g_pBaseDriver ? g_pBaseDriver->width : 1);
}

int SceneTemplate::RenderViewportHeight() const {
  if (m_renderHeight > 0) {
    return m_renderHeight;
  }
  return (std::max)(1, g_pBaseDriver ? g_pBaseDriver->height : 1);
}

void SceneTemplate::UpdateCameraProjectionForRenderViewport() {
  const int width = RenderViewportWidth();
  const int height = RenderViewportHeight();
  if (width <= 0 || height <= 0) {
    return;
  }
  Cam.AspectRatio = static_cast<float>(width) / static_cast<float>(height);
  if (Cam.Ortho) {
    Cam.Height = Cam.Width > 0.0f ? Cam.Width / Cam.AspectRatio : static_cast<float>(height);
  }
  Cam.CreatePojection();
  Cam.Update(0.0f);
  VP = Cam.VP;
}

void SceneTemplate::InitVars() {



  // Free camera
  Cam.InitPerspective(XVECTOR3(0.0f, 1.0f, 10.0f), Deg2Rad(46.8f), 1280.0f / 720.0f, 0.1f, 5000.0f);
  Cam.Speed = 10.0f;
  Cam.Eye = XVECTOR3(0.0f, 5.0f, -15.0f);
  Cam.Pitch = 0.2f;
  Cam.Roll = 0.0f;
  Cam.Yaw = 0.0f;
  Cam.m_externalControl = false;
  Cam.Update(0.0f);
  UpdateCameraProjectionForRenderViewport();

  // Initialize orbit camera defaults
  m_orbitTarget = XVECTOR3(0, 0, 0);
  m_panOffset   = XVECTOR3(0, 0, 0);
  m_orbitYaw    = 0.0f;
  m_orbitPitch  = 0.0f;
  m_orbitDist   = 5.0f;
  m_modelRadius = 1.0f;

  LightCam.InitPerspective(XVECTOR3(0.0f, 100.0f, 10.0f), Deg2Rad(45.0f), 1.0f, 10.0f, 500.0f);
  LightCam.Speed = 10.0f;
  LightCam.Eye = XVECTOR3(50.0f, 150.0f, -50.0f);
  LightCam.Pitch = 1.0f;
  LightCam.Roll = 0.0f;
  LightCam.Yaw = -1.57f;
  LightCam.Update(0.0f);

  ActiveCam = &Cam;
  m_cameraController.AttachCamera(&Cam);
  m_cameraProfileSelection = t850::CameraProfileIndex(t850::CameraProfileType::Orbit);
#ifdef OS_ANDROID
  ResetAndroidVirtualControls();
#endif
  SyncOrbitProfileFromSandbox();
  SetCameraProfile(t850::CameraProfileType::Orbit);

  SceneProp.AddCamera(ActiveCam);
  SceneProp.AddLightCamera(&LightCam);

  SceneProp.AddDirectionalLight(XVECTOR3(-0.2f, -1.0f, 0.1f), XVECTOR3(1, 1, 1), 5.0f, true);
  SceneProp.AddLight(XVECTOR3(10.0f, 10.0f, -10.0f), XVECTOR3(1.0, 0.9, 0.8), 100.0f, 1.0f, LIGHT_POINT, true);
  SceneProp.ActiveLights = 2;
  SceneProp.AmbientColor = XVECTOR3(0.3f, 0.3f, 0.3f);
  EnsureLightRuntimeState();
  if (!SceneProp.Lights.empty() && SceneProp.Lights[0].Type == LIGHT_DIRECTIONAL) {
    SceneProp.Lights[0].Position = LightCam.Eye;
    SceneProp.Lights[0].Direction = LightCam.Look;
  }

  ShadowFilter.kernelSize = 4;
  ShadowFilter.radius = 1.f;
  ShadowFilter.sigma = 1.0f;
  ShadowFilter.Update();

  BloomFilter.kernelSize = 11;
  BloomFilter.radius = 2.5f;
  BloomFilter.sigma = 4.5f;
  BloomFilter.Update();

  NearDOFFilter.kernelSize = 23;
  NearDOFFilter.radius = 3.0f;
  NearDOFFilter.sigma = 6.f;
  NearDOFFilter.Update();

  SceneProp.AddGaussKernel(&ShadowFilter);
  SceneProp.AddGaussKernel(&BloomFilter);
  SceneProp.AddGaussKernel(&NearDOFFilter);
  SceneProp.ActiveGaussKernel = 0;

  SceneProp.ShadowMapResolution = 1024.0f;
  SceneProp.PCFScale = 1.7f;
  SceneProp.PCFSamples = 1.0f;
  SceneProp.SSAOKernel.Radius = 1.5f;
  SceneProp.SSAOKernel.KernelSize = 8;
  SceneProp.SSAOKernel.Update();

  SceneProp.ToogleShadow = true;
  SceneProp.ToogleSSAO = true;
  m_showWireframe = false;
  m_showSkeleton = false;
  m_showPhysics = false;
  m_showNavMesh = false;
  m_navMeshDebugOffset = 0.01f;
  m_navMeshDebugShapeMode = 0;
  m_navMeshBuildAttempted = false;
  m_navMeshBuildSettings = t850::navigation::NavMeshBuildSettings();
  m_navMeshBuildSettings.enableAutoDropLinks = true;
  m_navMeshBuildSettings.enableAutoJumpLinks = true;
  m_navMeshBuildSettings.enableHybridJumpLinks = true;
  m_navMeshBuildSettings.hybridJumpMaxLinks = 192;
  m_navMeshLastBuildMs = 0.0f;
  m_navMeshLastBuildFromCache = false;
  m_showLightVolumes = false;
  m_drawLightDirection = false;
  m_meshCount = 0;
  m_loadedEditorScene = false;
  m_loadedEditorScenePath.clear();
  m_primaryRagdollResourcePath.clear();
  m_sceneObjectNames.clear();
  m_sceneMeshPaths.clear();
  m_sceneRagdollPaths.clear();
  m_sceneObjectYawDegrees.clear();
  m_sceneNavAgentFrontYawOffsets.clear();
  m_sceneNavAgentFaceYawSigns.clear();
  m_sceneNavAgentTargetModes.clear();
  m_sceneNavAgentFollowDistances.clear();
  m_sceneNavAgentSideOffsets.clear();
  m_sceneNavAgentFormationDepthSteps.clear();
  m_sceneNavAgentSlots.clear();
  m_sceneRagdolls.clear();
  m_scenePhysicsEntities.clear();
  m_hasAuthoredNavMesh = false;
  m_authoredNavMesh = t850::scene::SceneNavigationMeshDesc{};
  m_hasAuthoredPlayer = false;
  m_authoredPlayer = t850::scene::ScenePhysicsEntityDesc{};
  m_navTestAgents.clear();
  m_navTestCandidatePoints.clear();
  m_navTestInitialized = false;
  m_navTestMode = kNavTestModeFollowPlayer;
  m_navTestAppliedMode = m_navTestMode;
  m_navTestRandomState = 0x6d2b79f5u;
  m_navTestSpeed = 3.0f;
  m_selectedSkinningMeshIndex = 0;
  m_selectedAnimationMeshIndex = 0;
  m_lightAttachToCamera.clear();
  m_profileModelKey.clear();
  m_driveRagdollFromAnimation = false;
  m_ragdollPhysicsDriven = false;
  m_ragdollDriveLogEmitted = false;
  m_ragdollPhysicsLogEmitted = false;
  m_sceneSplines.clear();
  m_runtimeSplineActive = false;
  m_runtimeSplineCameraIndex = -1;
  m_runtimeSpline.m_points.clear();
  m_runtimeSpline.m_totalLength = 0.0f;
  m_runtimeSpline.m_looped = false;
  m_runtimeSplineAgent = t850::SplineAgent{};
  m_hasAuthoredLightCamera = false;
  m_authoredLightCameraAttachedLight = -1;
  m_authoredLightCameraLinearVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  m_authoredLightCameraTargetVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  m_authoredLightCameraAngularVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  m_ragdollEditDirty = false;
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditSelectedCapsule = -1;
  m_ragdollEditSelectedJoint = -1;
  m_ragdollEditSelectedParentCapsule = -1;
  m_ragdollEditSelectedJointParentCapsule = -1;
  m_ragdollEditSelectedHandle = -1;
  m_ragdollEditRenamingCapsule = -1;
  m_ragdollEditRenameFocusPending = false;
  m_ragdollEditSavePath.clear();
  m_skeletonEditMode = false;
  m_skeletonEditWasPlaying = false;
  m_skeletonEditDragging = false;
  m_skeletonEditDirty = false;
  m_skeletonEditSelectedBone = -1;
  m_skeletonPreviewBoneActive = false;
  m_skeletonPreviewBoneIndex = -1;
  m_skeletonPreviewOriginalCombined.clear();
  m_skeletonEditBindCombined.clear();
  m_skeletonEditCombined.clear();
  m_skeletonEditSavePath.clear();
  m_ragdollAnimationBinding = t850::PhysicsRagdollAnimationBinding{};
  m_ragdollAnimationPose = t850::PhysicsRagdollDesc{};
  m_ragdollParentCapsules.clear();
  m_ragdollJointParentCapsules.clear();
  m_ragdollPhysicsStates.clear();
  m_ragdollPhysicsBoneIndices.clear();
  m_ragdollPhysicsCombinedMatrices.clear();
  m_floorBody.Reset();
  m_ragdollGeneratedBinding = t850::PhysicsRagdollAnimationBinding{};

  SceneProp.Exposure = 1.0f;
  SceneProp.BloomFactor = 0.35f;
  SceneProp.BloomThreshold = 1.5f;
  SceneProp.ToneMapWhiteLevel = 5.5f;
  SceneProp.LuminanceTau = 1.1f;
  SceneProp.IBLMipCount = 4.0f;
  SceneProp.IBLBRDFLUTEnabled = 0.0f;

  if (m_controlSetup.Load("Scenes/SceneTemplate.json")) {
    m_controlSetup.ApplyQualityAndSettings(SceneProp);
  } else {
    T8_LOG_ERROR("[SceneTemplate] Failed to load Scenes/SceneTemplate.json");
  }
  SceneProp.FrustumCullingToggleAllowed = g_config.cullingLoadMode != t850::Config::CullingLoadMode::Disabled;
  SceneProp.FrustumCullingEnabled = g_config.cullingLoadMode == t850::Config::CullingLoadMode::FullOnLoad;

  t850::FrameDumperConfig dumpCfg;
  dumpCfg.dumpEnabled        = g_config.flags.dumpEnabled;
  dumpCfg.dumpByFrame        = g_config.flags.dumpByFrame;
  dumpCfg.dumpFrame          = g_config.dumpFrame;
  dumpCfg.dumpSeconds        = g_config.dumpSeconds;
  dumpCfg.debugFrames        = g_config.flags.debugFrames;
  dumpCfg.keepRunning        = g_config.flags.keepRunning;
  dumpCfg.replaySnapshotPath = g_config.replaySnapshotPath;
  dumpCfg.sceneIndex         = ActiveStartScene();
  m_dumper.Init(dumpCfg);
}

void SceneTemplate::ApplyEditorSceneCameraAndLights(const t850::scene::EditorSceneFile& scene) {
  const bool hasSceneCamera = !scene.cameras.empty();
  const bool useQ3CameraDefaults = !hasSceneCamera;
  XVECTOR3 eye(0.0f, 0.0f, 0.0f, 1.0f);
  XVECTOR3 target(-1.0f, 0.0f, 0.0f, 1.0f);
  float nearPlane = (std::max)(0.0001f, Cam.NPlane);
  float farPlane = (std::max)(nearPlane + 0.01f, Cam.FPlane);
  float fov = Cam.Fov > 0.0f ? Cam.Fov : Deg2Rad(46.8f);
  if (hasSceneCamera) {
    const auto& camera = scene.cameras.front();
    eye = SceneVecToVector(camera.position);
    target = SceneVecToVector(camera.target);
    nearPlane = (std::max)(0.0001f, camera.near_plane);
    farPlane = (std::max)(nearPlane + 0.01f, camera.far_plane);
    fov = Deg2Rad((std::max)(1.0f, camera.fov_deg));
  } else if (useQ3CameraDefaults) {
    eye = XVECTOR3(-18.524239f, 9.683158f, 2.7011344f, 1.0f);
    target = eye + XVECTOR3(-0.96796095f, -0.2035667f, 0.14701098f, 0.0f);
    nearPlane = 0.125f;
    farPlane = 6198.7783f;
    fov = Deg2Rad(100.0f);
  } else {
    target = eye + XVECTOR3(-1.0f, 0.0f, 0.0f, 0.0f);
  }
  const float aspect = static_cast<float>(RenderViewportWidth()) / static_cast<float>(RenderViewportHeight());
  Cam.InitPerspective(eye, fov, aspect, nearPlane, farPlane);
  Cam.Speed = 10.0f;
  Cam.Velocity = XVECTOR3(0.0f, 0.0f, 0.0f);
  Cam.SetLookAt(target);
  m_orbitTarget = target;
  m_panOffset = XVECTOR3(0.0f, 0.0f, 0.0f);
  m_orbitDist = 1.0f;
  m_orbitPitch = 0.0f;
  m_orbitYaw = -1.57079632679f;
  SyncOrbitProfileFromSandbox();
  SetCameraProfile(t850::CameraProfileType::Quake3Fps);
  VP = Cam.VP;
  T8_LOG_INFO("[SceneTemplate] Using %s camera eye=(%.3f,%.3f,%.3f) look=(%.3f,%.3f,%.3f) fov=%.1f near=%.3f far=%.3f",
              hasSceneCamera ? "scene" : "SceneTemplate FPS",
              Cam.Eye.x, Cam.Eye.y, Cam.Eye.z,
              Cam.Look.x, Cam.Look.y, Cam.Look.z,
              Rad2Deg(Cam.Fov),
              Cam.NPlane,
              Cam.FPlane);

  if (!scene.lights.empty()) {
    SceneProp.Lights.clear();
    for (const auto& light : scene.lights) {
      const bool enabled = light.enabled && light.visible;
      if (light.type == 0) {
        const XVECTOR3 dir = SceneVecToVector(light.direction, 0.0f);
        SceneProp.AddDirectionalLight(dir, SceneVecToVector(light.color, 0.0f), light.intensity, enabled);
        if (!SceneProp.Lights.empty()) {
          SceneProp.Lights.back().Position = SceneVecToVector(light.position);
        }
      } else {
        SceneProp.AddLight(SceneVecToVector(light.position),
                           SceneVecToVector(light.color, 0.0f),
                           light.radius,
                           light.intensity,
                           LIGHT_POINT,
                           enabled);
      }
    }
    SceneProp.ActiveLights = static_cast<int>(SceneProp.Lights.size());
    m_selectedLightIndex = 0;
    EnsureLightRuntimeState();
    for (const Light& light : SceneProp.Lights) {
      if (light.Type == LIGHT_DIRECTIONAL) {
        LightCam.Eye = light.Position;
        break;
      }
    }
    m_hasAuthoredLightCamera = false;
    m_authoredLightCameraAttachedLight = -1;
    m_authoredLightCameraLinearVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
    m_authoredLightCameraTargetVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
    m_authoredLightCameraAngularVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
    for (const t850::scene::SceneLightCameraDesc& lightCamera : scene.light_cameras) {
      if (!lightCamera.enabled) continue;
      const XVECTOR3 lightCameraPosition = SceneVecToVector(lightCamera.position);
      const float nearPlane = (std::max)(0.0001f, lightCamera.near_plane);
      const float farPlane = (std::max)(nearPlane + 0.01f, lightCamera.far_plane);
      if (lightCamera.type == 1) {
        LightCam.InitOrtho(lightCameraPosition,
                           (std::max)(0.01f, lightCamera.ortho_w),
                           (std::max)(0.01f, lightCamera.ortho_h),
                           nearPlane,
                           farPlane);
      } else {
        LightCam.InitPerspective(lightCameraPosition,
                                 Deg2Rad((std::max)(1.0f, lightCamera.fov_deg)),
                                 1.0f,
                                 nearPlane,
                                 farPlane);
      }
      LightCam.SetLookAt(SceneVecToVector(lightCamera.target));
      m_hasAuthoredLightCamera = true;
      m_authoredLightCameraAttachedLight = lightCamera.attached_light;
      m_authoredLightCameraAngularVelocity = XVECTOR3(0.0f, lightCamera.yaw_rate, 0.0f, 0.0f);
      for (const t850::scene::SceneCameraAnimationDesc& animation : scene.camera_animations) {
        std::string target = animation.target;
        std::transform(target.begin(), target.end(), target.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool targetsLightCamera = target == "light_camera" || target == "light-camera" || target == "lightcamera";
        if (animation.enabled && targetsLightCamera && animation.camera == 0) {
          m_authoredLightCameraLinearVelocity = SceneVecToVector(animation.linear_velocity, 0.0f);
          m_authoredLightCameraTargetVelocity = SceneVecToVector(animation.target_velocity, 0.0f);
          m_authoredLightCameraAngularVelocity = SceneVecToVector(animation.angular_velocity, 0.0f);
          break;
        }
      }
      if (m_authoredLightCameraAttachedLight >= 0 &&
          m_authoredLightCameraAttachedLight < static_cast<int>(SceneProp.Lights.size())) {
        Light& attachedLight = SceneProp.Lights[static_cast<std::size_t>(m_authoredLightCameraAttachedLight)];
        attachedLight.Position = LightCam.Eye;
        attachedLight.Direction = LightCam.Look;
      }
      T8_LOG_INFO("[SceneTemplate] Applied authored light camera '%s' type=%s eye=(%.3f,%.3f,%.3f) look=(%.3f,%.3f,%.3f) attachedLight=%d",
                  lightCamera.name.c_str(),
                  lightCamera.type == 1 ? "ortho" : "perspective",
                  LightCam.Eye.x, LightCam.Eye.y, LightCam.Eye.z,
                  LightCam.Look.x, LightCam.Look.y, LightCam.Look.z,
                  m_authoredLightCameraAttachedLight);
      break;
    }
    if (!m_hasAuthoredLightCamera) {
      SyncLightCameraFromDirectionalLight();
    }
    if (scene.god_rays_volume) {
      const auto& volume = *scene.god_rays_volume;
      const bool volumeAuthored = volume.authored || volume.enabled || volume.visible || volume.clip_enabled;
      const int volumeLightCamera = std::clamp(
          volume.light_camera,
          0,
          SceneProp.pLightCameras.empty() ? 0 : static_cast<int>(SceneProp.pLightCameras.size()) - 1);
      const bool validVolumeLightCamera = !SceneProp.pLightCameras.empty() &&
          volumeLightCamera >= 0 &&
          volumeLightCamera < static_cast<int>(SceneProp.pLightCameras.size());
      SceneProp.GodRaysVolumeEnabled = (volumeAuthored && validVolumeLightCamera && volume.enabled && volume.clip_enabled) ? 1 : 0;
      if (validVolumeLightCamera) {
        SceneProp.ActiveLightCamera = volumeLightCamera;
      }
      SceneProp.GodRaysVolumeCenter = XVECTOR3(volume.position.x, volume.position.y, volume.position.z, 1.0f);
      SceneProp.GodRaysVolumeHalfExtents = XVECTOR3((std::max)(0.001f, std::abs(volume.half_extents.x)),
                                                    (std::max)(0.001f, std::abs(volume.half_extents.y)),
                                                    (std::max)(0.001f, std::abs(volume.half_extents.z)),
                                                    0.0f);
    }
    T8_LOG_INFO("[SceneTemplate] Applied %zu scene lights; dynamic point lights %s",
                SceneProp.Lights.size(),
                SceneProp.PointLightsEnabled ? "enabled" : "disabled");
  }
}

void SceneTemplate::InitializeSceneSplinePlayback(const t850::scene::EditorSceneFile& scene) {
  m_sceneSplines = scene.splines;
  m_runtimeSplineActive = false;
  m_runtimeSplineCameraIndex = -1;
  m_runtimeSpline.m_points.clear();
  m_runtimeSpline.m_totalLength = 0.0f;
  m_runtimeSpline.m_looped = false;
  m_runtimeSplineAgent = t850::SplineAgent{};
  if (m_sceneSplines.empty()) {
    return;
  }

  const t850::scene::SceneSplineDesc* selectedSpline = nullptr;
  for (const t850::scene::SceneSplineDesc& spline : m_sceneSplines) {
    if (spline.play_on_start && spline.attached_camera >= 0 && spline.points.size() >= 4) {
      selectedSpline = &spline;
      break;
    }
  }
  if (!selectedSpline) {
    return;
  }
  if (selectedSpline->attached_camera < 0 ||
      selectedSpline->attached_camera >= static_cast<int>(scene.cameras.size())) {
    T8_LOG_ERROR("[SceneTemplate] Spline '%s' references missing camera index %d",
                 selectedSpline->name.c_str(),
                 selectedSpline->attached_camera);
    return;
  }

  std::vector<t850::SplinePoint> points;
  points.reserve(selectedSpline->points.size());
  for (const t850::scene::SceneSplinePointDesc& point : selectedSpline->points) {
    t850::SplinePoint splinePoint(point.position.x, point.position.y, point.position.z);
    splinePoint.m_velocity = point.velocity;
    splinePoint.m_rotation = XVECTOR3(point.rotation.x, point.rotation.y, point.rotation.z, 0.0f);
    splinePoint.m_LookAtCenter = point.look_at_center;
    points.push_back(splinePoint);
  }

  m_runtimeSpline.m_points = std::move(points);
  m_runtimeSpline.m_totalLength = 0.0f;
  m_runtimeSpline.m_looped = selectedSpline->looped;
  m_runtimeSpline.Init();
  if (m_runtimeSpline.m_totalLength <= 0.0f) {
    return;
  }

  m_runtimeSplineAgent = t850::SplineAgent{};
  m_runtimeSplineAgent.m_pSpline = &m_runtimeSpline;
  m_runtimeSplineAgent.m_moving = true;
  m_runtimeSplineAgent.m_velocity = selectedSpline->agent_velocity;
  float safeOffset = (std::max)(0.0f, selectedSpline->agent_offset);
  if (m_runtimeSpline.m_totalLength > 0.0f) {
    safeOffset = std::fmod(safeOffset, m_runtimeSpline.m_totalLength);
  }
  m_runtimeSplineAgent.SetOffset(safeOffset);
  m_runtimeSplineAgent.m_actualPoint = m_runtimeSpline.GetPoint(m_runtimeSpline.GetNormalizedOffset(m_runtimeSplineAgent.GetOffset()));
  const t850::scene::SceneCameraDesc& cameraDesc = scene.cameras[static_cast<std::size_t>(selectedSpline->attached_camera)];
  const XVECTOR3 cameraPosition = SceneVecToVector(cameraDesc.position);
  const float nearPlane = (std::max)(0.0001f, cameraDesc.near_plane);
  const float farPlane = (std::max)(nearPlane + 0.01f, cameraDesc.far_plane);
  if (cameraDesc.type == 1) {
    Cam.InitOrtho(cameraPosition,
                  (std::max)(0.01f, cameraDesc.ortho_w),
                  (std::max)(0.01f, cameraDesc.ortho_h),
                  nearPlane,
                  farPlane);
  } else {
    const float aspect = static_cast<float>(RenderViewportWidth()) / static_cast<float>(RenderViewportHeight());
    Cam.InitPerspective(cameraPosition, Deg2Rad((std::max)(1.0f, cameraDesc.fov_deg)), aspect, nearPlane, farPlane);
  }
  Cam.AttachAgent(m_runtimeSplineAgent);
  Cam.m_lookAtCenter = false;
  Cam.Update(0.0f);
  VP = Cam.VP;
  m_runtimeSplineActive = true;
  m_runtimeSplineCameraIndex = selectedSpline->attached_camera;
  m_cameraController.ClearInput();
  T8_LOG_INFO("[SceneTemplate] Spline playback attached camera=%d points=%zu length=%.2f velocity=%.2f",
              m_runtimeSplineCameraIndex,
              selectedSpline->points.size(),
              m_runtimeSpline.m_totalLength,
              selectedSpline->agent_velocity);
}

bool SceneTemplate::UpdateSceneSplinePlayback(float deltaSeconds) {
  if (!m_runtimeSplineActive || !Cam.m_externalControl) {
    return false;
  }
  m_runtimeSplineAgent.Update(deltaSeconds);
  Cam.Update(deltaSeconds);
  VP = Cam.VP;
  return true;
}

int SceneTemplate::GetRuntimeMeshCount() const {
  return (std::max)(m_meshCount, Meshes[0].pBase ? 1 : 0);
}

RenderSkinnedMesh* SceneTemplate::GetSkinnedMeshForIndex(int meshIndex) const {
  if (meshIndex < 0 || meshIndex >= kMaxSandboxMeshes || meshIndex >= GetRuntimeMeshCount() || !Meshes[meshIndex].pBase) {
    return nullptr;
  }
  RenderSkinnedMesh* skinned = Meshes[meshIndex].GetSkinnedMesh();
  return (skinned && skinned->HasSkinData()) ? skinned : nullptr;
}

std::vector<std::string> SceneTemplate::BuildSkinnedMeshOptions(std::vector<int>* outMeshIndices) const {
  if (outMeshIndices) {
    outMeshIndices->clear();
  }

  std::vector<std::string> options;
  const int meshCount = GetRuntimeMeshCount();
  for (int meshIndex = 0; meshIndex < meshCount && meshIndex < kMaxSandboxMeshes; ++meshIndex) {
    RenderSkinnedMesh* skinned = GetSkinnedMeshForIndex(meshIndex);
    if (!skinned) {
      continue;
    }

    std::string label;
    if (meshIndex >= 0 && meshIndex < static_cast<int>(m_sceneMeshPaths.size())) {
      label = m_sceneMeshPaths[static_cast<std::size_t>(meshIndex)];
    } else if (!ActiveModelPath().empty()) {
      label = ActiveModelPath();
    }
    if (label.empty()) {
      label = "Skinned Mesh";
    }

    std::replace(label.begin(), label.end(), '\\', '/');
    std::filesystem::path labelPath(label);
    const std::string filename = labelPath.filename().string();
    if (!filename.empty()) {
      label = filename;
    }

    label = std::to_string(meshIndex) + ": " + label;
    if (Meshes[meshIndex].HasPhysicsRagdoll()) {
      const SceneRagdollRuntime* sceneRagdoll = FindSceneRagdollRuntime(meshIndex);
      label += sceneRagdoll ? " (scene ragdoll)" : " (ragdoll)";
    }

    if (outMeshIndices) {
      outMeshIndices->push_back(meshIndex);
    }
    options.push_back(label);
  }

  if (options.empty()) {
    options.push_back("No skinned models");
  }
  return options;
}

int SceneTemplate::ClampSkinnedMeshSelection(int preferredMeshIndex) const {
  std::vector<int> meshIndices;
  BuildSkinnedMeshOptions(&meshIndices);
  if (meshIndices.empty()) {
    return -1;
  }
  for (int meshIndex : meshIndices) {
    if (meshIndex == preferredMeshIndex) {
      return preferredMeshIndex;
    }
  }
  return meshIndices.front();
}

RenderSkinnedMesh* SceneTemplate::GetSelectedSkinningMesh() const {
  return GetSkinnedMeshForIndex(ClampSkinnedMeshSelection(m_selectedSkinningMeshIndex));
}

RenderSkinnedMesh* SceneTemplate::GetSelectedAnimationMesh() const {
  return GetSkinnedMeshForIndex(ClampSkinnedMeshSelection(m_selectedAnimationMeshIndex));
}

bool SceneTemplate::AttachSceneObjectRagdoll(int meshIndex,
                                            const std::string& meshPath,
                                            const std::string& ragdollPath) {
  if (meshIndex < 0 || meshIndex >= kMaxSandboxMeshes || !Meshes[meshIndex].pBase) {
    return false;
  }

  RenderSkinnedMesh* skinned = dynamic_cast<RenderSkinnedMesh*>(Meshes[meshIndex].pBase);
  if (!skinned || ragdollPath.empty()) {
    return false;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  t850::JoltPhysicsSystem* physics = engineContext ? engineContext->physics : nullptr;
  if (!physics || !physics->IsInitialized()) {
    T8_LOG_INFO("[SceneTemplate] Cannot attach ragdoll '%s' for scene object '%s' because physics is not initialized",
                ragdollPath.c_str(),
                meshPath.c_str());
    return false;
  }

  RenderMesh::AABB bounds;
  float objectRadius = 1.0f;
  if (BuildSkinnedWorldBounds(skinned, Meshes[meshIndex].Final, bounds)) {
    const float ex = 0.5f * (bounds.max.x - bounds.min.x);
    const float ey = 0.5f * (bounds.max.y - bounds.min.y);
    const float ez = 0.5f * (bounds.max.z - bounds.min.z);
    objectRadius = (std::max)(0.01f, std::sqrt(ex * ex + ey * ey + ez * ez));
  }

  skinned->UpdateAnimationPose();

  t850::PhysicsRagdollBuildSettings settings;
  settings.fitToSkinnedGeometry = false;
  settings.preferHumanoidBones = false;
  settings.forceCapsuleForEveryBone = true;
  settings.minBoneLength = (std::max)(0.0002f, objectRadius * 0.0002f);
  settings.syntheticBoneLength = (std::max)(0.001f, objectRadius * 0.001f);
  settings.minRadius = (std::max)(0.0006f, objectRadius * 0.0008f);
  settings.maxRadius = (std::max)(0.02f, objectRadius * 0.035f);
  settings.radiusScale = 0.12f;
  settings.minSkinWeight = 0.08f;
  settings.radiusPercentile = 0.86f;
  settings.jointTrimFraction = 0.0f;

  t850::PhysicsRagdollAuthoringDesc generatedAuthoring;
  if (!t850::BuildRagdollAuthoringFromSkeleton(*skinned,
                                               Meshes[meshIndex].Final,
                                               Meshes[meshIndex].GetEntityId(),
                                               settings,
                                               generatedAuthoring)) {
    T8_LOG_INFO("[SceneTemplate] Failed to generate ragdoll binding for scene object '%s' using '%s'",
                meshPath.c_str(),
                ragdollPath.c_str());
    return false;
  }

  t850::PhysicsRagdollAuthoringDesc authoring = generatedAuthoring;
  int appliedBodies = 0;
  if (!t850::LoadRagdollAuthoringAsset(ragdollPath,
                                       *skinned,
                                       Meshes[meshIndex].Final,
                                       generatedAuthoring.binding,
                                       authoring,
                                       &appliedBodies)) {
    T8_LOG_INFO("[SceneTemplate] Failed to load authored ragdoll '%s' for scene object '%s'",
                ragdollPath.c_str(),
                meshPath.c_str());
    return false;
  }

  t850::PhysicsRagdollDesc pose = authoring.binding.referencePose;
  if (pose.bones.empty()) {
    T8_LOG_INFO("[SceneTemplate] Authored ragdoll '%s' for scene object '%s' produced no bodies",
                ragdollPath.c_str(),
                meshPath.c_str());
    return false;
  }

  const t850::PhysicsRagdollHandle handle = physics->CreateRagdoll(pose, t850::PhysicsBodyMotion::Kinematic);
  if (!handle.IsValid()) {
    T8_LOG_INFO("[SceneTemplate] Failed to create runtime ragdoll '%s' for scene object '%s'",
                ragdollPath.c_str(),
                meshPath.c_str());
    return false;
  }

  Meshes[meshIndex].AttachPhysicsRagdoll(handle);

  SceneRagdollRuntime runtime;
  runtime.meshIndex = meshIndex;
  runtime.resourcePath = ragdollPath;
  runtime.binding = authoring.binding;
  runtime.pose = std::move(pose);
  m_sceneRagdolls.push_back(std::move(runtime));

  T8_LOG_INFO("[SceneTemplate] Loaded scene ragdoll '%s' for mesh '%s' bodies=%d appliedEdits=%d",
              ragdollPath.c_str(),
              meshPath.c_str(),
              static_cast<int>(authoring.binding.referencePose.bones.size()),
              appliedBodies);
  return true;
}

SceneTemplate::SceneRagdollRuntime* SceneTemplate::FindSceneRagdollRuntime(int meshIndex) {
  for (SceneRagdollRuntime& runtime : m_sceneRagdolls) {
    if (runtime.meshIndex == meshIndex) {
      return &runtime;
    }
  }
  return nullptr;
}

const SceneTemplate::SceneRagdollRuntime* SceneTemplate::FindSceneRagdollRuntime(int meshIndex) const {
  for (const SceneRagdollRuntime& runtime : m_sceneRagdolls) {
    if (runtime.meshIndex == meshIndex) {
      return &runtime;
    }
  }
  return nullptr;
}

bool SceneTemplate::IsSceneRagdollPhysicsDriven(int meshIndex) const {
  const SceneRagdollRuntime* runtime = FindSceneRagdollRuntime(meshIndex);
  return runtime && runtime->physicsDriven;
}

void SceneTemplate::DriveSceneRagdollsFromAnimation(float deltaSeconds) {
  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  t850::JoltPhysicsSystem* physics = engineContext ? engineContext->physics : nullptr;
  if (!physics || !physics->IsInitialized() || m_sceneRagdolls.empty()) {
    return;
  }

  for (SceneRagdollRuntime& runtime : m_sceneRagdolls) {
    if (runtime.physicsDriven) {
      continue;
    }
    if (runtime.meshIndex < 0 || runtime.meshIndex >= static_cast<int>(m_meshCount) ||
        runtime.meshIndex >= kMaxSandboxMeshes) {
      continue;
    }

    const t850::PhysicsRagdollHandle handle = Meshes[runtime.meshIndex].GetPhysicsRagdoll();
    if (!handle.IsValid()) {
      continue;
    }

    RenderSkinnedMesh* skinned = dynamic_cast<RenderSkinnedMesh*>(Meshes[runtime.meshIndex].pBase);
    if (!skinned) {
      continue;
    }

    t850::PhysicsRagdollDesc pose;
    if (!t850::BuildRagdollPoseFromAnimation(*skinned,
                                             Meshes[runtime.meshIndex].Final,
                                             runtime.binding,
                                             pose)) {
      if (!runtime.driveLogEmitted) {
        T8_LOG_INFO("[SceneTemplate] Failed to drive scene ragdoll '%s' from animation",
                    runtime.resourcePath.c_str());
        runtime.driveLogEmitted = true;
      }
      continue;
    }

    if (physics->DriveRagdollFromPose(handle, pose, deltaSeconds)) {
      runtime.pose = std::move(pose);
      if (!runtime.driveLogEmitted) {
        T8_LOG_INFO("[SceneTemplate] Driving scene ragdoll '%s' from animation", runtime.resourcePath.c_str());
        runtime.driveLogEmitted = true;
      }
    }
  }
}

bool SceneTemplate::SwitchSceneRagdollsToPhysics(int meshIndexFilter) {
  if (m_sceneRagdolls.empty()) {
    return false;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (!engineContext || !engineContext->physics || !engineContext->physics->IsInitialized()) {
    return false;
  }

  int switchedCount = 0;
  for (SceneRagdollRuntime& runtime : m_sceneRagdolls) {
    if (meshIndexFilter >= 0 && runtime.meshIndex != meshIndexFilter) {
      continue;
    }
    if (runtime.physicsDriven ||
        runtime.meshIndex < 0 ||
        runtime.meshIndex >= static_cast<int>(m_meshCount) ||
        runtime.meshIndex >= kMaxSandboxMeshes ||
        !Meshes[runtime.meshIndex].HasPhysicsRagdoll()) {
      continue;
    }

    RenderSkinnedMesh* skinned = Meshes[runtime.meshIndex].GetSkinnedMesh();
    if (!skinned || !skinned->HasSkinData()) {
      continue;
    }

    t850::PhysicsRagdollDesc pose;
    if (t850::BuildRagdollPoseFromAnimation(*skinned, Meshes[runtime.meshIndex].Final, runtime.binding, pose)) {
      engineContext->physics->DriveRagdollFromPose(Meshes[runtime.meshIndex].GetPhysicsRagdoll(), pose, 0.0f);
      runtime.pose = std::move(pose);
    }

    if (!engineContext->physics->SetRagdollMotion(Meshes[runtime.meshIndex].GetPhysicsRagdoll(),
                                                   t850::PhysicsBodyMotion::Dynamic)) {
      T8_LOG_ERROR("[SceneTemplate] Failed to switch scene ragdoll '%s' to dynamic physics",
                   runtime.resourcePath.c_str());
      continue;
    }

    engineContext->physics->SetRagdollVelocity(
        Meshes[runtime.meshIndex].GetPhysicsRagdoll(),
        XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f),
        XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f));
    runtime.physicsDriven = true;
    runtime.physicsLogEmitted = false;
    skinned->PauseAnimation();
    skinned->ClearSnapshotBoneMatrices();
    ++switchedCount;
  }

  if (switchedCount > 0) {
    T8_LOG_INFO("[SceneTemplate] F5: %d scene-file ragdoll(s) switched to dynamic physics", switchedCount);
  }
  return switchedCount > 0;
}

bool SceneTemplate::ResetSceneRagdollPhysicsAndAnimation(int meshIndex) {
  SceneRagdollRuntime* runtime = FindSceneRagdollRuntime(meshIndex);
  if (!runtime ||
      runtime->meshIndex < 0 ||
      runtime->meshIndex >= static_cast<int>(m_meshCount) ||
      runtime->meshIndex >= kMaxSandboxMeshes ||
      !Meshes[runtime->meshIndex].HasPhysicsRagdoll()) {
    return false;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  RenderSkinnedMesh* skinned = Meshes[runtime->meshIndex].GetSkinnedMesh();
  if (!engineContext || !engineContext->physics || !skinned || !skinned->HasSkinData()) {
    return false;
  }

  skinned->ResetAnimation();
  skinned->PlayAnimation();
  skinned->ClearSnapshotBoneMatrices();
  skinned->UpdateAnimationPose();

  t850::PhysicsRagdollDesc pose;
  if (!t850::BuildRagdollPoseFromAnimation(*skinned, Meshes[runtime->meshIndex].Final, runtime->binding, pose)) {
    T8_LOG_ERROR("[SceneTemplate] Failed to reset scene ragdoll '%s' from animation",
                 runtime->resourcePath.c_str());
    return false;
  }

  const t850::PhysicsRagdollHandle handle = Meshes[runtime->meshIndex].GetPhysicsRagdoll();
  if (!engineContext->physics->SetRagdollMotion(handle, t850::PhysicsBodyMotion::Kinematic) ||
      !engineContext->physics->DriveRagdollFromPose(handle, pose, 0.0f)) {
    T8_LOG_ERROR("[SceneTemplate] Failed to reset scene ragdoll physics for '%s'",
                 runtime->resourcePath.c_str());
    return false;
  }

  engineContext->physics->SetRagdollVelocity(
      handle,
      XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f),
      XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f));
  runtime->pose = std::move(pose);
  runtime->physicsDriven = false;
  runtime->driveLogEmitted = false;
  runtime->physicsLogEmitted = false;
  runtime->physicsStates.clear();
  runtime->physicsBoneIndices.clear();
  runtime->physicsCombinedMatrices.clear();
  T8_LOG_INFO("[SceneTemplate] Reset scene ragdoll '%s' to animation drive", runtime->resourcePath.c_str());
  return true;
}

bool SceneTemplate::LoadEditorSceneAssets(const std::string& scenePath) {
  t850::scene::EditorSceneFile scene;
  std::string error;
  if (!t850::scene::LoadEditorSceneFile(scenePath, scene, &error)) {
    T8_LOG_ERROR("[SceneTemplate] Failed to load editor scene '%s': %s", scenePath.c_str(), error.c_str());
    return false;
  }

  std::string migrationLog;
  if (t850::scene::MigrateEditorSceneGameLogic(scene, &migrationLog)) {
    T8_LOG_INFO("[GameLogic] Migrated runtime scene copy '%s': %s",
                scenePath.c_str(), migrationLog.c_str());
  }

  m_loadedEditorScene = true;
  m_loadedEditorScenePath = scenePath;
  m_meshCount = 0;
  m_sceneObjectNames.clear();
  m_sceneMeshPaths.clear();
  m_sceneRagdollPaths.clear();
  m_sceneObjectYawDegrees.clear();
  m_sceneNavAgentFrontYawOffsets.clear();
  m_sceneNavAgentFaceYawSigns.clear();
  m_sceneNavAgentTargetModes.clear();
  m_sceneNavAgentFollowDistances.clear();
  m_sceneNavAgentSideOffsets.clear();
  m_sceneNavAgentFormationDepthSteps.clear();
  m_sceneNavAgentSlots.clear();
  m_sceneRagdolls.clear();
  m_scenePhysicsAuthoring.clear();
  m_scenePhysicsEntities = scene.physics_entities;
  m_sceneNavigationAuthoring.clear();
  m_sceneRagdollAuthoring.clear();
  m_sceneSplines = scene.splines;
  m_runtimeSplineActive = false;
  m_runtimeSplineCameraIndex = -1;
  m_runtimeSpline.m_points.clear();
  m_runtimeSpline.m_totalLength = 0.0f;
  m_runtimeSpline.m_looped = false;
  m_runtimeSplineAgent = t850::SplineAgent{};
  m_hasAuthoredLightCamera = false;
  m_authoredLightCameraAttachedLight = -1;
  m_authoredLightCameraLinearVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  m_authoredLightCameraTargetVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  m_authoredLightCameraAngularVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  m_primaryRagdollResourcePath.clear();
  m_hasAuthoredNavMesh = false;
  m_authoredNavMesh = t850::scene::SceneNavigationMeshDesc{};
  if (scene.navigation_mesh && scene.navigation_mesh->enabled) {
    m_authoredNavMesh = *scene.navigation_mesh;
    m_hasAuthoredNavMesh = true;
    m_navMeshBuildSettings = NavMeshBuildSettingsFromScene(m_authoredNavMesh.build_settings);
    m_navMeshDebugOffset = m_authoredNavMesh.debug_offset;
    m_navMeshDebugShapeMode = std::clamp(m_authoredNavMesh.debug_shape_mode, 0, 1);
    m_navMeshBuildAttempted = false;
    m_gameLogic.Navigation().PrepareForNavMeshMutation();
    m_navMesh.Clear();
  }
  m_hasAuthoredPlayer = false;
  m_authoredPlayer = t850::scene::ScenePhysicsEntityDesc{};

  std::vector<std::pair<std::string, int>> loadedObjectSlots;
  loadedObjectSlots.reserve(scene.objects.size());

  for (const auto& object : scene.objects) {
    if (!object.visible) continue;
#if defined(OS_ANDROID)
    if (object.mobile_visible && !*object.mobile_visible) {
      T8_LOG_INFO("[SceneTemplate] Android skipped mobile-hidden scene object '%s'", object.name.c_str());
      continue;
    }
#endif
    const std::string meshPath = NormalizeSceneResourcePath(object.mesh);
    if (meshPath.empty()) {
      T8_LOG_ERROR("[SceneTemplate] Scene object '%s' has no mesh path; skipping", object.name.c_str());
      continue;
    }
    if (m_meshCount >= kMaxSandboxMeshes) {
      T8_LOG_ERROR("[SceneTemplate] Scene '%s' has more than %d visible meshes; remaining objects skipped",
                   scenePath.c_str(), kMaxSandboxMeshes);
      break;
    }

    T8_LOG_INFO("[SceneTemplate] Loading scene object '%s' mesh='%s'", object.name.c_str(), meshPath.c_str());
    const int primitiveIndex = PrimitiveMgr.CreateMesh(meshPath.c_str());
    if (primitiveIndex < 0) {
      T8_LOG_ERROR("[SceneTemplate] Failed to load scene object '%s' mesh '%s'", object.name.c_str(), meshPath.c_str());
      continue;
    }

    PrimitiveInst& instance = Meshes[m_meshCount];
    instance.CreateInstance(PrimitiveMgr.GetPrimitive(primitiveIndex), &VP);
    instance.ScaleAbsolute(object.scale.x, object.scale.y, object.scale.z);
    instance.RotateXAbsolute(object.rotation.x);
    instance.RotateYAbsolute(object.rotation.y);
    instance.RotateZAbsolute(object.rotation.z);
    instance.TranslateAbsolute(object.position.x, object.position.y, object.position.z);
    instance.Visible = object.visible;
    instance.Update();

    t850::scene::SceneObjectPhysicsDesc physicsMeta = object.physics.value_or(t850::scene::SceneObjectPhysicsDesc{});
    const bool hasExplicitNavigation = object.navigation.has_value();
    t850::scene::SceneObjectNavigationDesc navigationMeta = object.navigation.value_or(t850::scene::SceneObjectNavigationDesc{});
    if (!hasExplicitNavigation && !object.visible) {
      navigationMeta.include = false;
    }
    t850::scene::SceneObjectRagdollDesc ragdollMeta = object.ragdoll_authoring.value_or(t850::scene::SceneObjectRagdollDesc{});
    const std::string legacyRagdollPath = NormalizeSceneResourcePath(object.ragdoll);
    if (ragdollMeta.asset.empty()) {
      ragdollMeta.asset = legacyRagdollPath;
    } else {
      ragdollMeta.asset = NormalizeSceneResourcePath(ragdollMeta.asset);
    }
    const std::string ragdollPath = ragdollMeta.enabled ? ragdollMeta.asset : legacyRagdollPath;
    const bool isSkinnedObject = (instance.GetSkinnedMesh() != nullptr);
    if (!ragdollPath.empty() && isSkinnedObject && (ragdollMeta.enabled || !object.ragdoll_authoring.has_value())) {
      AttachSceneObjectRagdoll(static_cast<int>(m_meshCount), meshPath, ragdollPath);
    } else if (!isSkinnedObject && physicsMeta.enabled) {
      if (!ragdollPath.empty()) {
        T8_LOG_INFO("[SceneTemplate] Ignoring ragdoll '%s' on non-skinned scene object '%s'",
                    ragdollPath.c_str(),
                    object.name.c_str());
      }
      t850::EngineContext* engineContext = GetEngineContext();
      if (!engineContext) engineContext = &t850::GetEngineContext();
      const bool staticMeshOwnedByPhysicsEntity = SceneHasStaticPhysicsEntityForObject(scene, object.name);
      if (!staticMeshOwnedByPhysicsEntity && engineContext && engineContext->physics && engineContext->physics->IsInitialized()) {
        t850::PhysicsTriangleMeshCookSettings cookSettings;
        cookSettings.maxTrianglesPerLeaf = 8;
        cookSettings.buildQuality = t850::PhysicsMeshBuildQuality::FavorRuntimePerformance;
        cookSettings.useDiskCache = true;
        t850::PhysicsCookStats cookStats;
        RenderMesh* renderMesh = dynamic_cast<RenderMesh*>(instance.pBase);
        const bool wantsStaticTriangle =
            physicsMeta.body_type == "static_triangle_mesh" && physicsMeta.motion == "static";
        if (wantsStaticTriangle && renderMesh && t850::AttachStaticTriangleMeshBody(
          *engineContext->physics,
          instance,
          *renderMesh,
          cookSettings,
          &cookStats,
          t850::GameplayLayerFromString(physicsMeta.collision_layer, t850::GameplayLayer::WorldStatic))) {
          T8_LOG_INFO("[SceneTemplate] Scene collision mesh ready for '%s': cache=%s vertices=%u triangles=%u total=%.2fms",
                      object.name.c_str(),
                      cookStats.cacheHit ? "hit" : "miss",
                      cookStats.vertexCount,
                      cookStats.triangleCount,
                      cookStats.totalMs);
        } else {
          T8_LOG_ERROR("[SceneTemplate] Failed to create scene collision mesh for '%s'", object.name.c_str());
        }
      }
    }
    if (m_meshCount == 0) {
      m_profileModelKey = meshPath;
      m_primaryRagdollResourcePath = ragdollPath;
    }
    m_sceneMeshPaths.push_back(meshPath);
    m_sceneObjectNames.push_back(object.name);
    m_sceneRagdollPaths.push_back(ragdollPath);
    m_sceneObjectYawDegrees.push_back(object.rotation.y);
    m_scenePhysicsAuthoring.push_back(physicsMeta);
    m_sceneNavigationAuthoring.push_back(navigationMeta);
    m_sceneRagdollAuthoring.push_back(ragdollMeta);
    const float navAgentFrontYawOffset = object.nav_agent_front_yaw_offset_deg.value_or(0.0f);
    m_sceneNavAgentFrontYawOffsets.push_back(navAgentFrontYawOffset);
    const float navAgentFaceYawSign = object.nav_agent_face_yaw_sign.value_or(1.0f) < 0.0f ? -1.0f : 1.0f;
    m_sceneNavAgentFaceYawSigns.push_back(navAgentFaceYawSign);
    m_sceneNavAgentTargetModes.push_back(object.nav_agent_target_mode.empty() ? "direct" : object.nav_agent_target_mode);
    m_sceneNavAgentFollowDistances.push_back(object.nav_agent_follow_distance);
    m_sceneNavAgentSideOffsets.push_back(object.nav_agent_side_offset);
    m_sceneNavAgentFormationDepthSteps.push_back(object.nav_agent_formation_depth_step);
    m_sceneNavAgentSlots.push_back(object.nav_agent_slot);
    loadedObjectSlots.emplace_back(object.name, m_meshCount);
    T8_LOG_INFO("[SceneTemplate] Loaded scene object '%s' mesh='%s' ragdoll='%s' slot=%d navFrontYawOffset=%.1f navFaceYawSign=%.1f",
                object.name.c_str(), meshPath.c_str(), ragdollPath.c_str(), m_meshCount,
                navAgentFrontYawOffset, navAgentFaceYawSign);
    ++m_meshCount;
  }

  auto findLoadedObjectSlot = [&](const std::string& name) -> int {
    for (const auto& item : loadedObjectSlots) {
      if (item.first == name) {
        return item.second;
      }
    }
    return -1;
  };

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (engineContext && engineContext->physics && engineContext->physics->IsInitialized()) {
    for (const t850::scene::ScenePhysicsEntityDesc& entity : scene.physics_entities) {
      if (entity.type == "player") {
        m_authoredPlayer = entity;
        m_hasAuthoredPlayer = true;
        continue;
      }
      if (entity.type == "character") {
        continue;
      }
      if (entity.type != "static_triangle_mesh") {
        continue;
      }
      const int meshSlot = findLoadedObjectSlot(entity.source_object);
      if (meshSlot < 0 || meshSlot >= m_meshCount) {
        T8_LOG_ERROR("[SceneTemplate] Physics entity '%s' source object '%s' was not loaded",
                     entity.name.c_str(),
                     entity.source_object.c_str());
        continue;
      }
      RenderMesh* renderMesh = dynamic_cast<RenderMesh*>(Meshes[meshSlot].pBase);
      if (!renderMesh) {
        T8_LOG_ERROR("[SceneTemplate] Physics entity '%s' source object '%s' has no render mesh",
                     entity.name.c_str(),
                     entity.source_object.c_str());
        continue;
      }
      t850::PhysicsCookStats cookStats;
      if (t850::AttachStaticTriangleMeshBody(
              *engineContext->physics,
              Meshes[meshSlot],
              *renderMesh,
              PhysicsCookSettingsFromScene(entity.cook_settings),
              &cookStats)) {
        T8_LOG_INFO("[SceneTemplate] Authored physics static mesh '%s' ready from '%s': cache=%s vertices=%u triangles=%u total=%.2fms",
                    entity.name.c_str(),
                    entity.source_object.c_str(),
                    cookStats.cacheHit ? "hit" : "miss",
                    cookStats.vertexCount,
                    cookStats.triangleCount,
                    cookStats.totalMs);
      } else {
        T8_LOG_ERROR("[SceneTemplate] Failed to create authored physics static mesh '%s' from '%s'",
                     entity.name.c_str(),
                     entity.source_object.c_str());
      }
    }
  }

  if (m_meshCount <= 0) {
    T8_LOG_ERROR("[SceneTemplate] Editor scene '%s' did not load any visible meshes", scenePath.c_str());
    return false;
  }

  m_selectedSkinningMeshIndex = ClampSkinnedMeshSelection(m_selectedSkinningMeshIndex);
  m_selectedAnimationMeshIndex = ClampSkinnedMeshSelection(m_selectedAnimationMeshIndex);
  FitModelToView();
  ApplyEditorSceneCameraAndLights(scene);
  m_controlSetup.descriptor.profiles = scene.profiles;
  LoadSandboxProfile(true);
  if (m_hasAuthoredNavMesh) {
    m_navMeshBuildSettings = NavMeshBuildSettingsFromScene(m_authoredNavMesh.build_settings);
    m_navMeshDebugOffset = m_authoredNavMesh.debug_offset;
    m_navMeshDebugShapeMode = std::clamp(m_authoredNavMesh.debug_shape_mode, 0, 1);
    m_navMeshBuildAttempted = false;
  }
  if (m_hasAuthoredPlayer) {
    const t850::KinematicCharacterSettings playerSettings = CharacterSettingsFromPhysicsEntity(m_authoredPlayer);
    const t850::CameraProfileType profileType =
        m_authoredPlayer.character.implementation == "character"
            ? t850::CameraProfileType::GroundedFps
            : t850::CameraProfileType::Quake3Fps;
    m_cameraController.SetKinematicProfileSettings(profileType, playerSettings);
    Cam.Eye = PlayerEyeFromEntity(m_authoredPlayer, playerSettings);
    Cam.Pitch = Deg2Rad(m_authoredPlayer.rotation.x);
    Cam.Yaw = Deg2Rad(m_authoredPlayer.rotation.y);
    Cam.Roll = Deg2Rad(m_authoredPlayer.rotation.z);
    Cam.Update(0.0f);
    SetCameraProfile(profileType);
    VP = Cam.VP;
    T8_LOG_INFO("[SceneTemplate] Runtime player from scene '%s': profile=%s eye=(%.3f, %.3f, %.3f) shape=%s radius=%.3f halfHeight=%.3f",
                m_authoredPlayer.name.c_str(),
                t850::CameraProfileName(profileType),
                Cam.Eye.x,
                Cam.Eye.y,
                Cam.Eye.z,
                m_authoredPlayer.shape.c_str(),
                playerSettings.capsuleRadius,
                playerSettings.capsuleHalfHeight);
  }
  InitializeSceneSplinePlayback(scene);

  t850::game::GameLogicSettings gameSettings;
  if (scene.game_logic_settings.has_value()) {
    gameSettings.fixedDeltaSeconds = scene.game_logic_settings->fixed_delta_seconds;
    gameSettings.maxStepsPerFrame = scene.game_logic_settings->max_steps_per_frame;
  }
  t850::EngineContext* gameContext = GetEngineContext();
  if (!gameContext) gameContext = &t850::GetEngineContext();
  m_gameLogic.Initialize(*gameContext, gameSettings);
  m_gameLogic.SetGroupSystem(&m_groupManager);
  t850::game::examples::RegisterHealthComponent(m_gameLogic.Factories());
  t850::game::examples::RegisterPathFollowComponent(m_gameLogic.Factories());
  t850::game::examples::RegisterWeaponComponent(m_gameLogic.Factories());

  t850::game::GameSceneRuntimeLinks gameLinks;
  gameLinks.resolveMeshSlot = [this](std::string_view objectName) {
    for (int meshIndex = 0; meshIndex < static_cast<int>(m_sceneObjectNames.size()); ++meshIndex) {
      if (m_sceneObjectNames[static_cast<std::size_t>(meshIndex)] == objectName) return meshIndex;
    }
    return -1;
  };
  gameLinks.primitiveForSlot = [this](int meshSlot) -> t850::PrimitiveInst* {
    return meshSlot >= 0 && meshSlot < m_meshCount ? &Meshes[meshSlot] : nullptr;
  };
  gameLinks.resolveBody = [this](std::string_view physicsEntityName) {
    for (const t850::scene::ScenePhysicsEntityDesc& physicsEntity : m_scenePhysicsEntities) {
      if (physicsEntity.name != physicsEntityName) continue;
      for (int meshIndex = 0; meshIndex < static_cast<int>(m_sceneObjectNames.size()); ++meshIndex) {
        if (m_sceneObjectNames[static_cast<std::size_t>(meshIndex)] == physicsEntity.source_object) {
          return Meshes[meshIndex].GetPhysicsBody();
        }
      }
      break;
    }
    return t850::PhysicsBodyHandle{};
  };
  gameLinks.resolveCamera = [&scene](std::string_view cameraName) {
    for (int cameraIndex = 0; cameraIndex < static_cast<int>(scene.cameras.size()); ++cameraIndex) {
      if (scene.cameras[static_cast<std::size_t>(cameraIndex)].name == cameraName) return cameraIndex;
    }
    return -1;
  };
  gameLinks.navigationAgentForSlot = [this](int meshSlot) {
    t850::game::GameNavigationAgentSettings settings;
    if (meshSlot < 0 || meshSlot >= static_cast<int>(m_sceneObjectNames.size())) return settings;
    const std::size_t index = static_cast<std::size_t>(meshSlot);
    if (index < m_sceneNavAgentTargetModes.size()) settings.targetMode = m_sceneNavAgentTargetModes[index];
    if (index < m_sceneNavAgentFollowDistances.size()) settings.followDistance = m_sceneNavAgentFollowDistances[index];
    if (index < m_sceneNavAgentSideOffsets.size()) settings.sideOffset = m_sceneNavAgentSideOffsets[index];
    if (index < m_sceneNavAgentFormationDepthSteps.size()) settings.formationDepthStep = m_sceneNavAgentFormationDepthSteps[index];
    if (index < m_sceneNavAgentSlots.size()) settings.formationSlot = m_sceneNavAgentSlots[index];
    return settings;
  };
  gameLinks.navMesh = &m_navMesh;

  t850::scene::SceneValidationReport gameReport;
  if (!m_gameLogic.LoadFromScene(scene, gameLinks, &gameReport)) {
    T8_LOG_ERROR("[GameLogic] Scene '%s' contains game-logic validation errors", scenePath.c_str());
  }
  m_rtsCommandController.Bind(&m_groupManager, &m_gameLogic);
  for (const t850::scene::SceneValidationIssue& issue : gameReport.issues) {
    if (issue.severity == t850::scene::SceneValidationSeverity::Error) {
      T8_LOG_ERROR("[GameLogic] %s: %s", issue.code.c_str(), issue.message.c_str());
    } else {
      T8_LOG_INFO("[GameLogic] %s: %s", issue.code.c_str(), issue.message.c_str());
    }
  }
  auto runtimeObject = m_gameLogic.Registry().Objects().begin();
  for (std::size_t entityIndex = 0;
       entityIndex < scene.game_entities.size() && runtimeObject != m_gameLogic.Registry().Objects().end();
       ++entityIndex, ++runtimeObject) {
    const std::string& ragdollObject = scene.game_entities[entityIndex].ragdoll_object;
    if (ragdollObject.empty()) continue;
    t850::game::GameObject& object = *runtimeObject;
    for (int ragdollIndex = 0; ragdollIndex < static_cast<int>(m_sceneRagdolls.size()); ++ragdollIndex) {
      const int meshIndex = m_sceneRagdolls[static_cast<std::size_t>(ragdollIndex)].meshIndex;
      if (meshIndex >= 0 && meshIndex < static_cast<int>(m_sceneObjectNames.size()) &&
          m_sceneObjectNames[static_cast<std::size_t>(meshIndex)] == ragdollObject) {
        object.links.ragdollIndex = ragdollIndex;
        break;
      }
    }
  }
  T8_LOG_INFO("[GameLogic] Loaded %zu runtime object(s) from '%s'",
              m_gameLogic.Registry().Count(), scenePath.c_str());
  T8_LOG_INFO("[SceneTemplate] Loaded editor scene '%s' with %d mesh instances", scenePath.c_str(), m_meshCount);
  return true;
}

bool SceneTemplate::EnsureNavMeshBuilt() {
  if (m_navMesh.IsReady()) {
    return true;
  }
  if (!m_hasAuthoredNavMesh) {
    if (!m_navMeshBuildAttempted) {
      T8_LOG_INFO("[Navigation] SceneTemplate NavMesh build skipped: no authored NavMesh in scene");
    }
    m_navMeshBuildAttempted = true;
    return false;
  }
  if (m_navMeshBuildAttempted) {
    return false;
  }
  m_navMeshBuildAttempted = true;

  const int meshCount = (std::min)(kMaxSandboxMeshes, (std::max)(m_meshCount, Meshes[0].pBase ? 1 : 0));
  t850::navigation::NavMeshBuildSettings navBuildSettings = m_navMeshBuildSettings;
  uint64_t authoredLinksHash = 0xcbf29ce484222325ull;
  HashNavCacheValue(authoredLinksHash, static_cast<uint64_t>(m_authoredNavMesh.authored_links.size()));
  for (const t850::scene::SceneNavMeshLinkDesc& link : m_authoredNavMesh.authored_links) {
    HashNavCacheString(authoredLinksHash, link.name);
    HashNavCacheString(authoredLinksHash, link.type);
    HashNavCacheValue(authoredLinksHash, link.start_node);
    HashNavCacheValue(authoredLinksHash, link.end_node);
    HashNavCacheValue(authoredLinksHash, link.start.x);
    HashNavCacheValue(authoredLinksHash, link.start.y);
    HashNavCacheValue(authoredLinksHash, link.start.z);
    HashNavCacheValue(authoredLinksHash, link.end.x);
    HashNavCacheValue(authoredLinksHash, link.end.y);
    HashNavCacheValue(authoredLinksHash, link.end.z);
    HashNavCacheValue(authoredLinksHash, link.radius);
    HashNavCacheValue(authoredLinksHash, link.bidirectional);
    HashNavCacheValue(authoredLinksHash, link.cost);
    HashNavCacheValue(authoredLinksHash, link.enabled);
  }
  HashNavMeshVolumes(authoredLinksHash, m_authoredNavMesh.volumes);
  navBuildSettings.offMeshLinkValidationKey ^= authoredLinksHash;
  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  t850::JoltPhysicsSystem* linkValidationPhysics =
      (engineContext && engineContext->physics && engineContext->physics->IsInitialized())
          ? engineContext->physics
          : nullptr;
  if (linkValidationPhysics) {
    navBuildSettings.offMeshLinkValidationKey ^= kJoltNavLinkValidationCacheKey;
  }
  const uint64_t navCacheKey =
      ComputeSandboxNavMeshCacheKey(
          Meshes,
          meshCount,
          m_sceneMeshPaths,
          m_loadedEditorScenePath,
          navBuildSettings);
  const auto navBuildStart = std::chrono::steady_clock::now();
  const std::string runtimeMode = m_authoredNavMesh.runtime_mode.empty()
      ? "build_cached"
      : m_authoredNavMesh.runtime_mode;
  if (runtimeMode == "baked_asset" && !m_authoredNavMesh.baked_asset.empty()) {
    std::string bakedError;
    if (m_navMesh.LoadBaked(m_authoredNavMesh.baked_asset, navBuildSettings, &bakedError)) {
      m_navMeshLastBuildMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - navBuildStart).count();
      m_navMeshLastBuildFromCache = true;
      m_navMeshDebugRenderer.Invalidate();
      const t850::navigation::NavMeshBuildStats& stats = m_navMesh.GetStats();
      T8_LOG_INFO("[Navigation] Loaded baked NavMesh asset '%s': %.2fms verts=%d tris=%d polys=%d offMesh=%d",
                  m_authoredNavMesh.baked_asset.c_str(),
                  m_navMeshLastBuildMs,
                  stats.vertexCount,
                  stats.triangleCount,
                  stats.polygonCount,
                  stats.offMeshLinkCount);
      return true;
    }
    T8_LOG_ERROR("[Navigation] Failed to load baked NavMesh asset '%s': %s; falling back to cached build.",
                 m_authoredNavMesh.baked_asset.c_str(),
                 bakedError.c_str());
  }
  const bool allowCache = runtimeMode != "build";
  if (allowCache && navCacheKey != 0 && m_navMesh.LoadCached(navCacheKey, navBuildSettings, nullptr)) {
    m_navMeshLastBuildMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - navBuildStart).count();
    m_navMeshLastBuildFromCache = true;
    m_navMeshDebugRenderer.Invalidate();
    const t850::navigation::NavMeshBuildStats& stats = m_navMesh.GetStats();
    T8_LOG_INFO("[Navigation] Sandbox navmesh ready from cache: %.2fms verts=%d tris=%d polys=%d offMesh=%d drop=%d jump=%d jumpPad=%d",
                m_navMeshLastBuildMs,
                stats.vertexCount, stats.triangleCount, stats.polygonCount,
                stats.offMeshLinkCount, stats.dropLinkCount, stats.jumpLinkCount, stats.jumpPadLinkCount);
    return true;
  }

  t850::navigation::NavMeshGeometry geometry;
  t850::navigation::NavSourceBuildStats sourceStats;
  std::string error;
  std::vector<t850::navigation::NavSourceInstance> navSources;
  navSources.reserve(static_cast<std::size_t>(meshCount));
  for (int meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
    t850::navigation::NavSourceInstance source;
    source.entityId = Meshes[meshIndex].GetEntityId();
    source.instance = &Meshes[meshIndex];
    source.worldTransform = Meshes[meshIndex].Final;
    source.visible = Meshes[meshIndex].Visible;
    if (meshIndex < static_cast<int>(m_sceneNavigationAuthoring.size())) {
      const t850::scene::SceneObjectNavigationDesc& nav = m_sceneNavigationAuthoring[static_cast<std::size_t>(meshIndex)];
      source.includeInNavigation = nav.include;
      source.visible = source.visible || nav.include;
      source.navigationStatic = nav.static_object;
      source.navigationWalkable = nav.walkable;
      source.area = nav.walkable ? 0 : -1;
    }
    navSources.push_back(source);
  }
  if (!t850::navigation::BuildGeometryFromNavSources(navSources, geometry, &sourceStats, &error)) {
    T8_LOG_ERROR("[Navigation] Sandbox navmesh build skipped: %s (considered=%d included=%d skippedInvisible=%d skippedSkinned=%d skippedInvalid=%d)",
                 error.c_str(),
                 sourceStats.considered,
                 sourceStats.included,
                 sourceStats.skippedInvisible,
                 sourceStats.skippedSkinned,
                 sourceStats.skippedInvalid);
    return false;
  }
  if (linkValidationPhysics) {
    geometry.offMeshLinkValidator = [linkValidationPhysics, navBuildSettings](const t850::navigation::NavOffMeshLink& link) {
      return t850::ValidateNavOffMeshLinkWithPhysics(*linkValidationPhysics, navBuildSettings, link);
    };
    geometry.offMeshHybridLinkValidator = geometry.offMeshLinkValidator;
  }
  for (const t850::scene::SceneNavMeshVolumeDesc& volumeDesc : m_authoredNavMesh.volumes) {
    t850::navigation::NavMeshVolumeModifier modifier = NavVolumeModifierFromScene(volumeDesc);
    if (!modifier.enabled) {
      continue;
    }
    geometry.volumeModifiers.push_back(modifier);
    if (modifier.mode == t850::navigation::NavMeshModifierMode::Area) {
      geometry.areaCosts.push_back({modifier.area, modifier.cost});
    }
  }
  for (const t850::scene::SceneNavMeshLinkDesc& linkDesc : m_authoredNavMesh.authored_links) {
    if (IsUsableAuthoredNavLink(linkDesc)) {
      geometry.offMeshLinks.push_back(NavOffMeshLinkFromScene(linkDesc));
    }
  }
  const uint64_t buildCacheKey = allowCache ? navCacheKey : 0;
  if (!m_navMesh.BuildCached(geometry, navBuildSettings, buildCacheKey, &error)) {
    T8_LOG_ERROR("[Navigation] Sandbox navmesh build failed: %s", error.c_str());
    return false;
  }
  m_navMeshLastBuildMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - navBuildStart).count();
  m_navMeshLastBuildFromCache = false;

  m_navMeshDebugRenderer.Invalidate();
  const t850::navigation::NavMeshBuildStats& stats = m_navMesh.GetStats();
  T8_LOG_INFO("[Navigation] Sandbox navmesh ready: %.2fms sources=%d skippedSkinned=%d skippedInvalid=%d verts=%d tris=%d polys=%d offMesh=%d drop=%d jump=%d jumpPad=%d",
              m_navMeshLastBuildMs,
              sourceStats.included, sourceStats.skippedSkinned, sourceStats.skippedInvalid,
              stats.vertexCount, stats.triangleCount, stats.polygonCount,
              stats.offMeshLinkCount, stats.dropLinkCount, stats.jumpLinkCount, stats.jumpPadLinkCount);
  return true;
}

void SceneTemplate::InitializeNavTestAgents() {
  if (m_navTestInitialized) {
    return;
  }
  m_navTestInitialized = true;
  m_navTestAgents.clear();
  m_navTestCandidatePoints.clear();

  if (!EnsureNavMeshBuilt()) {
    return;
  }

  std::vector<unsigned int> graphIndices;
  m_navMesh.GetDebugGraphEdges(m_navTestCandidatePoints, graphIndices, 0.0f);
  if (m_navTestCandidatePoints.empty()) {
    std::vector<unsigned int> wireIndices;
    m_navMesh.GetDebugWireframe(m_navTestCandidatePoints, wireIndices, 0.0f);
  }
  if (m_navTestCandidatePoints.empty()) {
    T8_LOG_ERROR("[NavigationTest] No navmesh candidate points available for skinned mesh path test");
    return;
  }

  const int meshCount = (std::min)(kMaxSandboxMeshes, (std::max)(m_meshCount, Meshes[0].pBase ? 1 : 0));
  auto findAuthoredCharacterForMesh = [&](int meshIndex) -> const t850::scene::ScenePhysicsEntityDesc* {
    if (meshIndex < 0 || meshIndex >= static_cast<int>(m_sceneObjectNames.size())) {
      return nullptr;
    }
    const std::string& objectName = m_sceneObjectNames[static_cast<std::size_t>(meshIndex)];
    for (const t850::scene::ScenePhysicsEntityDesc& entity : m_scenePhysicsEntities) {
      if (entity.type == "character" && entity.source_object == objectName) {
        return &entity;
      }
    }
    return nullptr;
  };

  int followSlot = 0;
  int authoredCharacterCount = 0;
  for (int meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
    PrimitiveInst& instance = Meshes[meshIndex];
    RenderSkinnedMesh* skinned = instance.GetSkinnedMesh();
    if (!instance.Visible || !skinned || !skinned->HasSkinData()) {
      continue;
    }
    const t850::scene::ScenePhysicsEntityDesc* authoredCharacter = findAuthoredCharacterForMesh(meshIndex);
    if (m_loadedEditorScene && !authoredCharacter) {
      continue;
    }

    NavTestAgentRuntime agent;
    agent.meshIndex = meshIndex;
    agent.characterSettings = t850::MakeQuake3CharacterSettings();
    if (authoredCharacter) {
      agent.characterSettings = CharacterSettingsFromPhysicsEntity(*authoredCharacter);
      agent.characterRuntimePath = CharacterRuntimePathFromPhysicsEntity(*authoredCharacter);
      agent.authoredCharacterPosition = XVECTOR3(
          authoredCharacter->position.x,
          authoredCharacter->position.y,
          authoredCharacter->position.z,
          1.0f);
      agent.authoredCharacterRotationDeg = XVECTOR3(
          authoredCharacter->rotation.x,
          authoredCharacter->rotation.y,
          authoredCharacter->rotation.z,
          0.0f);
      agent.hasAuthoredCharacter = true;
      ++authoredCharacterCount;
      const float meshYaw = meshIndex < static_cast<int>(m_sceneObjectYawDegrees.size())
          ? m_sceneObjectYawDegrees[static_cast<std::size_t>(meshIndex)]
          : 0.0f;
      T8_LOG_INFO("[NavigationTest] Mesh %d uses authored character '%s' source='%s' path=%s implementation=%s pos=(%.2f,%.2f,%.2f) rot=(%.2f,%.2f,%.2f) meshYaw=%.2f",
                  meshIndex,
                  authoredCharacter->name.c_str(),
                  authoredCharacter->source_object.c_str(),
                  authoredCharacter->character.runtime_path.c_str(),
                  authoredCharacter->character.implementation.c_str(),
                  authoredCharacter->position.x,
                  authoredCharacter->position.y,
                  authoredCharacter->position.z,
                  authoredCharacter->rotation.x,
                  authoredCharacter->rotation.y,
                  authoredCharacter->rotation.z,
                  meshYaw);
    }
    agent.physicsController.SetSettings(agent.characterSettings);
    if (meshIndex < static_cast<int>(m_sceneNavAgentFrontYawOffsets.size())) {
      agent.frontYawOffsetDeg = m_sceneNavAgentFrontYawOffsets[static_cast<std::size_t>(meshIndex)];
    }
    if (authoredCharacter && meshIndex < static_cast<int>(m_sceneObjectYawDegrees.size())) {
      agent.frontYawOffsetDeg += m_sceneObjectYawDegrees[static_cast<std::size_t>(meshIndex)];
    }
    if (meshIndex < static_cast<int>(m_sceneNavAgentFaceYawSigns.size())) {
      agent.faceYawSign = m_sceneNavAgentFaceYawSigns[static_cast<std::size_t>(meshIndex)];
    }
    std::string targetMode = "direct";
    if (meshIndex < static_cast<int>(m_sceneNavAgentTargetModes.size()) &&
        !m_sceneNavAgentTargetModes[static_cast<std::size_t>(meshIndex)].empty()) {
      targetMode = m_sceneNavAgentTargetModes[static_cast<std::size_t>(meshIndex)];
    }
    agent.behaviorMode = NavAgentBehaviorModeFromScene(targetMode);
    agent.followSlot = NavAgentUsesFormationTarget(targetMode)
        ? (meshIndex < static_cast<int>(m_sceneNavAgentSlots.size()) &&
           m_sceneNavAgentSlots[static_cast<std::size_t>(meshIndex)] >= 0
              ? m_sceneNavAgentSlots[static_cast<std::size_t>(meshIndex)]
              : followSlot++)
        : -1;
    if (meshIndex < static_cast<int>(m_sceneNavAgentFollowDistances.size())) {
      agent.followDistance = (std::max)(0.0f, m_sceneNavAgentFollowDistances[static_cast<std::size_t>(meshIndex)]);
    }
    if (meshIndex < static_cast<int>(m_sceneNavAgentSideOffsets.size())) {
      agent.sideOffset = m_sceneNavAgentSideOffsets[static_cast<std::size_t>(meshIndex)];
    }
    if (meshIndex < static_cast<int>(m_sceneNavAgentFormationDepthSteps.size())) {
      agent.formationDepthStep = m_sceneNavAgentFormationDepthSteps[static_cast<std::size_t>(meshIndex)];
    }
    const XVECTOR3 visualPosition(instance.Final.m41, instance.Final.m42, instance.Final.m43, 1.0f);
    std::string projectionError;
    if (!m_navMesh.ProjectPoint(visualPosition, agent.navPosition, NavTestAgentProjectionExtents(), &projectionError)) {
      T8_LOG_ERROR("[NavigationTest] Skipping mesh %d nav agent; cannot project initial position (%.2f,%.2f,%.2f): %s",
                   meshIndex,
                   visualPosition.x, visualPosition.y, visualPosition.z,
                   projectionError.c_str());
      continue;
    }
    agent.home = agent.navPosition;
    agent.visualOffset = visualPosition - agent.navPosition;
    agent.visualOffset.x = 0.0f;
    agent.visualOffset.z = 0.0f;
    agent.visualOffset.w = 0.0f;
    agent.navToOriginOffset = agent.visualOffset;
    agent.physicsController.SetPosition(Q3CenterFromGroundPoint(agent.navPosition, agent.characterSettings));
    if (agent.behaviorMode == kNavTestModeFollowPlayer) {
      if (!ResolveNavTestFollowTarget(m_navMesh, Cam,
                                      agent.navPosition,
                                      agent.followSlot,
                                      m_hasAuthoredPlayer ? PlayerBotRadiusFromEntity(m_authoredPlayer) : 0.0f,
                                      agent.followDistance,
                                      agent.sideOffset,
                                      agent.formationDepthStep,
                                      agent.desiredTarget, agent.target, &agent.lastPathError)) {
        agent.target = agent.navPosition;
        agent.repathCooldownSec = kNavTestFailedPathRetrySec;
      }
    } else if (agent.behaviorMode == kNavTestModeRandom) {
      agent.target = RandomNavTestPoint(m_navTestCandidatePoints,
                                        agent.home,
                                        m_navTestRandomState,
                                        static_cast<uint32_t>(meshIndex + 1));
      agent.desiredTarget = agent.target;
    } else {
      agent.target = FurthestNavTestPoint(m_navTestCandidatePoints, agent.home);
      agent.desiredTarget = agent.target;
    }
    agent.targetInitialized = true;
    agent.active = DistanceSquared(agent.target, agent.home) > 0.25f || agent.behaviorMode == kNavTestModeFollowPlayer;
    agent.needsPath = agent.active && agent.repathCooldownSec <= 0.0f;
    m_navTestAgents.push_back(std::move(agent));
  }

  if (!m_navTestAgents.empty()) {
    T8_LOG_INFO("[NavigationTest] Initialized %zu skinned mesh nav agents mode=%d speed=%.2f q3 units/sec authoredCharacters=%d",
                m_navTestAgents.size(), m_navTestMode, m_navTestSpeed, authoredCharacterCount);
  } else if (m_loadedEditorScene) {
    T8_LOG_INFO("[NavigationTest] No authored mesh characters found; skinned scene meshes remain visible but are not nav agents.");
  }
}

void SceneTemplate::PlanNavTestAgentPaths() {
  T8_TELEMETRY_SCOPE("navigation.agents.plan_paths");
  if (!m_navMesh.IsReady() || m_navTestAgents.empty()) {
    return;
  }

  std::vector<int> agentIndices;
  std::vector<unsigned int> requestGenerations;
  std::vector<t850::navigation::NavPathRequest> requests;
  for (int i = 0; i < static_cast<int>(m_navTestAgents.size()); ++i) {
    NavTestAgentRuntime& agent = m_navTestAgents[static_cast<std::size_t>(i)];
    if (!agent.active || agent.physicsTraversalActive || !agent.needsPath || agent.repathCooldownSec > 0.0f ||
        agent.meshIndex < 0 || agent.meshIndex >= kMaxSandboxMeshes ||
        !Meshes[agent.meshIndex].pBase) {
      continue;
    }

    t850::navigation::NavPathRequest request;
    request.start = agent.navPosition;
    request.end = (agent.behaviorMode == kNavTestModeFurthest && agent.returning) ? agent.home : agent.target;
    request.queryExtents = NavTestAgentProjectionExtents();
    agent.lastPathStart = request.start;
    agent.lastPathEnd = request.end;
    ++agent.pathGeneration;
    requests.push_back(request);
    agentIndices.push_back(i);
    requestGenerations.push_back(agent.pathGeneration);
  }

  if (requests.empty()) {
    if (t850::RuntimeTelemetry::IsFrameActive()) {
      t850::RuntimeTelemetry::SetCounter("navigation.agents.path_requests", 0.0);
    }
    return;
  }
  if (t850::RuntimeTelemetry::IsFrameActive()) {
    t850::RuntimeTelemetry::SetCounter("navigation.agents.path_requests", static_cast<double>(requests.size()));
  }

  std::vector<t850::navigation::NavPathResult> results;
  {
    T8_TELEMETRY_SCOPE("navigation.agents.find_paths");
    m_navMesh.FindPaths(requests, results);
  }
  int successfulPaths = 0;
  int failedPaths = 0;
  int totalPathPoints = 0;
  int dropSegments = 0;
  int jumpSegments = 0;
  int jumpPadSegments = 0;
  for (std::size_t i = 0; i < agentIndices.size(); ++i) {
    NavTestAgentRuntime& agent = m_navTestAgents[static_cast<std::size_t>(agentIndices[i])];
    if (i >= requestGenerations.size() || agent.pathGeneration != requestGenerations[i]) {
      continue;
    }
    agent.needsPath = false;
    if (i >= results.size() || !results[i].success || results[i].points.empty()) {
      ++failedPaths;
      const PrimitiveInst& instance = Meshes[agent.meshIndex];
      const XVECTOR3 current(instance.Final.m41, instance.Final.m42, instance.Final.m43, 1.0f);
      agent.lastPathSuccess = false;
      agent.lastPathError = i < results.size() ? results[i].error : "missing result";
      agent.repathCooldownSec = kNavTestFailedPathRetrySec;
      agent.path.clear();
      agent.pathSegmentTypes.clear();
      agent.waypointIndex = 0;
      if (agent.behaviorMode == kNavTestModeRandom) {
        agent.target = RandomNavTestPoint(m_navTestCandidatePoints,
                                          agent.navPosition,
                                          m_navTestRandomState,
                                          static_cast<uint32_t>(agent.meshIndex + 43));
        agent.desiredTarget = agent.target;
      } else if (agent.behaviorMode == kNavTestModeFollowPlayer) {
        XVECTOR3 desiredTarget;
        XVECTOR3 projectedTarget;
        if (ResolveNavTestFollowTarget(m_navMesh, Cam,
                                       agent.navPosition,
                                       agent.followSlot,
                                       m_hasAuthoredPlayer ? PlayerBotRadiusFromEntity(m_authoredPlayer) : 0.0f,
                                       agent.followDistance,
                                       agent.sideOffset,
                                       agent.formationDepthStep,
                                       desiredTarget, projectedTarget, nullptr)) {
          agent.desiredTarget = desiredTarget;
          agent.target = projectedTarget;
        } else {
          agent.desiredTarget = desiredTarget;
          agent.target = agent.navPosition;
        }
      } else {
        agent.active = false;
      }
      T8_LOG_ERROR("[NavigationTest] Agent mesh %d failed to find path gen=%u start=(%.2f,%.2f,%.2f) end=(%.2f,%.2f,%.2f) desired=(%.2f,%.2f,%.2f) player=(%.2f,%.2f,%.2f) nav=(%.2f,%.2f,%.2f) visual=(%.2f,%.2f,%.2f) offset=(%.2f,%.2f,%.2f): %s",
                   agent.meshIndex,
                   agent.pathGeneration,
                   agent.lastPathStart.x, agent.lastPathStart.y, agent.lastPathStart.z,
                   agent.lastPathEnd.x, agent.lastPathEnd.y, agent.lastPathEnd.z,
                   agent.desiredTarget.x, agent.desiredTarget.y, agent.desiredTarget.z,
                   Cam.Eye.x, Cam.Eye.y, Cam.Eye.z,
                   agent.navPosition.x, agent.navPosition.y, agent.navPosition.z,
                   current.x, current.y, current.z,
                   agent.visualOffset.x, agent.visualOffset.y, agent.visualOffset.z,
                   agent.lastPathError.c_str());
      continue;
    }

    ++successfulPaths;
    totalPathPoints += static_cast<int>(results[i].points.size());
    agent.navPosition = results[i].points.front();
    agent.navToOriginOffset = agent.visualOffset;
    agent.lastPathFirst = results[i].points.front();
    agent.lastPathSuccess = true;
    agent.lastPathError.clear();
    agent.repathCooldownSec = 0.0f;
    agent.path = results[i].points;
    agent.pathSegmentTypes.assign(agent.path.size() > 1 ? agent.path.size() - 1 : 0,
                                  t850::navigation::NavTraversalType::Walk);
    for (const t850::navigation::NavPathResult::Segment& segment : results[i].segments) {
      if (segment.startPointIndex < 0 ||
          segment.endPointIndex <= segment.startPointIndex ||
          segment.endPointIndex > static_cast<int>(agent.path.size())) {
        continue;
      }
      for (int pointIndex = segment.startPointIndex; pointIndex < segment.endPointIndex; ++pointIndex) {
        if (pointIndex >= 0 && pointIndex < static_cast<int>(agent.pathSegmentTypes.size())) {
          agent.pathSegmentTypes[static_cast<std::size_t>(pointIndex)] = segment.type;
        }
      }
      if (segment.type == t850::navigation::NavTraversalType::Drop) {
        ++dropSegments;
      } else if (segment.type == t850::navigation::NavTraversalType::Jump) {
        ++jumpSegments;
      } else if (segment.type == t850::navigation::NavTraversalType::JumpPad) {
        ++jumpPadSegments;
      }
    }
    agent.waypointIndex = agent.path.size() > 1 ? 1 : 0;
  }
  if (t850::RuntimeTelemetry::IsFrameActive()) {
    t850::RuntimeTelemetry::SetCounter("navigation.agents.path_success", static_cast<double>(successfulPaths));
    t850::RuntimeTelemetry::SetCounter("navigation.agents.path_fail", static_cast<double>(failedPaths));
    t850::RuntimeTelemetry::SetCounter("navigation.agents.path_points", static_cast<double>(totalPathPoints));
    t850::RuntimeTelemetry::SetCounter("navigation.agents.path_segments.drop", static_cast<double>(dropSegments));
    t850::RuntimeTelemetry::SetCounter("navigation.agents.path_segments.jump", static_cast<double>(jumpSegments));
    t850::RuntimeTelemetry::SetCounter("navigation.agents.path_segments.jump_pad", static_cast<double>(jumpPadSegments));
  }
}

void SceneTemplate::UpdateNavTestAgents(float dtSecs) {
  T8_TELEMETRY_SCOPE("navigation.agents.update");
  if (!m_loadedEditorScene) {
    return;
  }
  m_navTestMode = ClampNavTestMode(m_navTestMode);
  m_navTestSpeed = (std::max)(0.0f, (std::min)(10.0f, m_navTestSpeed));
  if (m_navTestMode != m_navTestAppliedMode) {
    m_navTestAppliedMode = m_navTestMode;
    m_navTestInitialized = false;
    m_navTestAgents.clear();
    m_navTestRandomState = 0x6d2b79f5u;
    m_navTestDiagAccumSec = 0.0f;
  }
  InitializeNavTestAgents();
  if (m_navTestAgents.empty()) {
    return;
  }
  if (t850::RuntimeTelemetry::IsFrameActive()) {
    int activeAgents = 0;
    int physicsAgents = 0;
    int needsPathAgents = 0;
    for (const NavTestAgentRuntime& agent : m_navTestAgents) {
      if (agent.active) {
        ++activeAgents;
      }
      if (agent.physicsTraversalActive) {
        ++physicsAgents;
      }
      if (agent.needsPath) {
        ++needsPathAgents;
      }
    }
    t850::RuntimeTelemetry::SetCounter("navigation.agents.count", static_cast<double>(m_navTestAgents.size()));
    t850::RuntimeTelemetry::SetCounter("navigation.agents.active", static_cast<double>(activeAgents));
    t850::RuntimeTelemetry::SetCounter("navigation.agents.physics_active", static_cast<double>(physicsAgents));
    t850::RuntimeTelemetry::SetCounter("navigation.agents.needs_path", static_cast<double>(needsPathAgents));
  }

  {
    T8_TELEMETRY_SCOPE("navigation.agents.target_update");
    for (NavTestAgentRuntime& agent : m_navTestAgents) {
      if (!agent.active ||
          agent.meshIndex < 0 || agent.meshIndex >= kMaxSandboxMeshes ||
          !Meshes[agent.meshIndex].pBase) {
        continue;
      }

      agent.repathCooldownSec = (std::max)(0.0f, agent.repathCooldownSec - (std::max)(0.0f, dtSecs));
      if (agent.physicsTraversalActive) {
        continue;
      }
      if (agent.behaviorMode == kNavTestModeFollowPlayer) {
        XVECTOR3 desiredTarget;
        XVECTOR3 projectedTarget;
        std::string targetError;
        if (ResolveNavTestFollowTarget(m_navMesh, Cam,
                                       agent.navPosition,
                                       agent.followSlot,
                                       m_hasAuthoredPlayer ? PlayerBotRadiusFromEntity(m_authoredPlayer) : 0.0f,
                                       agent.followDistance,
                                       agent.sideOffset,
                                       agent.formationDepthStep,
                                       desiredTarget, projectedTarget, &targetError)) {
          agent.desiredTarget = desiredTarget;
          if ((!agent.targetInitialized || agent.path.empty() || DistanceSquared(agent.target, projectedTarget) > 1.0f) &&
              agent.repathCooldownSec <= 0.0f) {
            agent.target = projectedTarget;
            agent.returning = false;
            agent.needsPath = true;
            agent.targetInitialized = true;
          }
        } else {
          agent.desiredTarget = desiredTarget;
          agent.lastPathSuccess = false;
          agent.lastPathError = targetError;
          agent.repathCooldownSec = kNavTestFailedPathRetrySec;
          agent.needsPath = false;
        }
      } else if (agent.behaviorMode == kNavTestModeRandom) {
        if (!agent.targetInitialized || (agent.path.empty() && !agent.needsPath)) {
          agent.target = RandomNavTestPoint(m_navTestCandidatePoints,
                                            agent.navPosition,
                                            m_navTestRandomState,
                                            static_cast<uint32_t>(agent.meshIndex + 17));
          agent.desiredTarget = agent.target;
          agent.returning = false;
          agent.needsPath = true;
          agent.targetInitialized = true;
        }
      } else if (!agent.targetInitialized) {
        agent.target = FurthestNavTestPoint(m_navTestCandidatePoints, agent.home);
        agent.desiredTarget = agent.target;
        agent.returning = false;
        agent.needsPath = true;
        agent.targetInitialized = true;
      }
    }
  }

  PlanNavTestAgentPaths();

  const float maxStep = (std::max)(0.0f, dtSecs) * m_navTestSpeed;
  std::vector<XVECTOR3> proposedPositions(m_navTestAgents.size());
  std::vector<unsigned char> movedAgents(m_navTestAgents.size(), 0);
  std::vector<unsigned char> physicsMovedAgents(m_navTestAgents.size(), 0);
  std::vector<unsigned char> suppressProjection(m_navTestAgents.size(), 0);
  static std::vector<float> s_navProjectionDebugCooldownSec;
  if (s_navProjectionDebugCooldownSec.size() != m_navTestAgents.size()) {
    s_navProjectionDebugCooldownSec.assign(m_navTestAgents.size(), 0.0f);
  }
  for (float& cooldown : s_navProjectionDebugCooldownSec) {
    cooldown = (std::max)(0.0f, cooldown - (std::max)(0.0f, dtSecs));
  }

  auto traversalName = [](t850::navigation::NavTraversalType type) {
    switch (type) {
      case t850::navigation::NavTraversalType::Drop: return "Drop";
      case t850::navigation::NavTraversalType::Jump: return "Jump";
      case t850::navigation::NavTraversalType::JumpPad: return "JumpPad";
      case t850::navigation::NavTraversalType::JumpIntent: return "JumpIntent";
      case t850::navigation::NavTraversalType::Walk:
      default: return "Walk";
    }
  };

  auto authoredTraversalDuration = [&](t850::navigation::NavTraversalType type,
                                       const XVECTOR3& start,
                                       const XVECTOR3& end) {
    const float horizontal = std::sqrt(HorizontalDistanceSq3(start, end));
    const float vertical = std::fabs(end.y - start.y);
    const float speed = (std::max)(3.5f, m_navTestSpeed * 1.5f);
    float duration = horizontal / speed;
    if (type == t850::navigation::NavTraversalType::Drop) {
      duration = (std::max)(duration, vertical > 2.0f ? 0.65f : 0.35f);
    } else {
      duration = (std::max)(duration, 0.45f);
    }
    return std::clamp(duration, 0.25f, 1.35f);
  };

  auto authoredTraversalPosition = [&](t850::navigation::NavTraversalType type,
                                       const XVECTOR3& start,
                                       const XVECTOR3& end,
                                       float fraction) {
    const float t = std::clamp(fraction, 0.0f, 1.0f);
    XVECTOR3 position = start + (end - start) * t;
    if (type == t850::navigation::NavTraversalType::Jump) {
      const float horizontal = std::sqrt(HorizontalDistanceSq3(start, end));
      const float arcHeight = (std::max)(0.45f, (std::min)(2.0f, horizontal * 0.25f));
      position.y += std::sin(t * xPI) * arcHeight;
    } else if (type == t850::navigation::NavTraversalType::Drop && end.y < start.y) {
      position.y = start.y + (end.y - start.y) * (t * t);
    }
    position.w = 1.0f;
    return position;
  };

  auto isPhysicsTraversalProtected = [&](std::size_t agentIndex) {
    if (agentIndex >= m_navTestAgents.size()) {
      return false;
    }
    const NavTestAgentRuntime& agent = m_navTestAgents[agentIndex];
    return agent.active && agent.physicsTraversalActive;
  };

  auto refreshAgentTargetAfterPathEnd = [&](NavTestAgentRuntime& agent, const XVECTOR3& current) {
    XVECTOR3 resolvedCurrent = current;
    if (agent.waypointIndex < static_cast<int>(agent.path.size())) {
      return resolvedCurrent;
    }
    if (agent.behaviorMode == kNavTestModeFurthest && agent.returning) {
      resolvedCurrent = agent.home;
      agent.returning = false;
    } else if (agent.behaviorMode == kNavTestModeFurthest) {
      agent.returning = true;
    } else if (agent.behaviorMode == kNavTestModeRandom) {
      agent.target = RandomNavTestPoint(m_navTestCandidatePoints,
                                        resolvedCurrent,
                                        m_navTestRandomState,
                                        static_cast<uint32_t>(agent.meshIndex + 31));
      agent.desiredTarget = agent.target;
      agent.returning = false;
    } else {
      XVECTOR3 desiredTarget;
      XVECTOR3 projectedTarget;
      if (ResolveNavTestFollowTarget(m_navMesh, Cam,
                                     agent.navPosition,
                                     agent.followSlot,
                                     m_hasAuthoredPlayer ? PlayerBotRadiusFromEntity(m_authoredPlayer) : 0.0f,
                                     agent.followDistance,
                                     agent.sideOffset,
                                     agent.formationDepthStep,
                                     desiredTarget, projectedTarget, nullptr)) {
        agent.desiredTarget = desiredTarget;
        agent.target = projectedTarget;
      } else {
        agent.desiredTarget = desiredTarget;
        agent.target = resolvedCurrent;
      }
      agent.returning = false;
    }
    agent.needsPath = true;
    agent.path.clear();
    agent.pathSegmentTypes.clear();
    agent.waypointIndex = 0;
    return resolvedCurrent;
  };

  for (std::size_t agentIndex = 0; agentIndex < m_navTestAgents.size(); ++agentIndex) {
    NavTestAgentRuntime& agent = m_navTestAgents[agentIndex];
    if (!agent.active ||
        agent.meshIndex < 0 || agent.meshIndex >= kMaxSandboxMeshes ||
        !Meshes[agent.meshIndex].pBase) {
      continue;
    }

    if (agent.physicsTraversalActive) {
      T8_TELEMETRY_SCOPE("navigation.agents.physics_traversal");
      if (t850::RuntimeTelemetry::IsFrameActive()) {
        t850::RuntimeTelemetry::AddCounter("navigation.agents.physics_traversal.count", 1.0);
        if (agent.physicsTraversalType == t850::navigation::NavTraversalType::Drop) {
          t850::RuntimeTelemetry::AddCounter("navigation.agents.physics_traversal.drop", 1.0);
        } else if (agent.physicsTraversalType == t850::navigation::NavTraversalType::Jump) {
          t850::RuntimeTelemetry::AddCounter("navigation.agents.physics_traversal.jump", 1.0);
        } else if (agent.physicsTraversalType == t850::navigation::NavTraversalType::JumpPad) {
          t850::RuntimeTelemetry::AddCounter("navigation.agents.physics_traversal.jump_pad", 1.0);
        }
      }

      if (agent.physicsTraversalType == t850::navigation::NavTraversalType::Jump ||
          agent.physicsTraversalType == t850::navigation::NavTraversalType::Drop) {
        agent.physicsTraversalTimeSec += (std::max)(0.0f, dtSecs);
        const float duration = (std::max)(0.01f, agent.physicsTraversalDurationSec);
        const float fraction = std::clamp(agent.physicsTraversalTimeSec / duration, 0.0f, 1.0f);
        XVECTOR3 navPosition = authoredTraversalPosition(
            agent.physicsTraversalType,
            agent.physicsTraversalStart,
            agent.physicsTarget,
            fraction);
        agent.navPosition = navPosition;
        proposedPositions[agentIndex] = navPosition;
        movedAgents[agentIndex] = 1;
        physicsMovedAgents[agentIndex] = 1;

        if (fraction >= 1.0f) {
          agent.navPosition = agent.physicsTarget;
          proposedPositions[agentIndex] = agent.physicsTarget;
          agent.physicsTraversalActive = false;
          agent.physicsWasAirborne = false;
          agent.physicsTraversalType = t850::navigation::NavTraversalType::Walk;
          agent.physicsTraversalTimeSec = 0.0f;
          agent.physicsTraversalDurationSec = 0.0f;
          agent.physicsStuckTimeSec = 0.0f;
          agent.needsPath = true;
          agent.repathCooldownSec = 0.0f;
          agent.path.clear();
          agent.pathSegmentTypes.clear();
          agent.waypointIndex = 0;
        }
        continue;
      }

      const t850::KinematicCharacterSettings& q3Settings = agent.characterSettings;
      agent.physicsController.SetSettings(q3Settings);
      const XVECTOR3 physicsGround = Q3GroundPointFromCenter(agent.physicsController.GetPosition(), q3Settings);
      const bool jumpInput =
          agent.physicsTraversalType == t850::navigation::NavTraversalType::Jump &&
          agent.physicsTraversalTimeSec < 0.18f;
      agent.physicsController.UpdateQuake3(
          (std::max)(0.0f, dtSecs),
          BuildNavAgentAirControlInput(
              physicsGround,
              agent.physicsController.GetVelocity(),
              agent.physicsTarget,
              jumpInput,
              q3Settings),
          t850::CharacterControllerContext{this});
      agent.physicsTraversalTimeSec += (std::max)(0.0f, dtSecs);
      if (!agent.physicsController.IsGrounded()) {
        agent.physicsWasAirborne = true;
      }

      XVECTOR3 navPosition = Q3GroundPointFromCenter(agent.physicsController.GetPosition(), q3Settings);
      const float traversalMoveSq = DistanceSquared(navPosition, agent.physicsLastNavPosition);
      if (!agent.physicsController.IsGrounded() &&
          agent.physicsTraversalTimeSec > 0.50f &&
          traversalMoveSq < 0.0004f) {
        agent.physicsStuckTimeSec += (std::max)(0.0f, dtSecs);
      } else {
        agent.physicsStuckTimeSec = 0.0f;
        agent.physicsLastNavPosition = navPosition;
      }
      agent.navPosition = navPosition;
      proposedPositions[agentIndex] = navPosition;
      movedAgents[agentIndex] = 1;
      physicsMovedAgents[agentIndex] = 1;

      const float minTraversalTime = agent.physicsTraversalType == t850::navigation::NavTraversalType::JumpPad
          ? 0.20f
          : 0.35f;
      const bool canReattach =
          agent.physicsWasAirborne &&
          agent.physicsTraversalTimeSec >= minTraversalTime &&
          agent.physicsController.IsGrounded();
      if (canReattach) {
        XVECTOR3 projected;
        if (m_navMesh.ProjectPoint(navPosition, projected, NavTestAgentProjectionExtents(), nullptr)) {
          agent.navPosition = projected;
          proposedPositions[agentIndex] = projected;
        }
        agent.physicsTraversalActive = false;
        agent.physicsWasAirborne = false;
        agent.physicsTraversalType = t850::navigation::NavTraversalType::Walk;
        agent.physicsTraversalTimeSec = 0.0f;
        agent.physicsTraversalDurationSec = 0.0f;
        agent.needsPath = true;
        agent.repathCooldownSec = 0.0f;
        agent.path.clear();
        agent.pathSegmentTypes.clear();
        agent.waypointIndex = 0;
      } else if (agent.physicsTraversalTimeSec > 6.0f || navPosition.y < -64.0f) {
        agent.physicsTraversalActive = false;
        agent.physicsWasAirborne = false;
        agent.physicsTraversalType = t850::navigation::NavTraversalType::Walk;
        agent.physicsTraversalTimeSec = 0.0f;
        agent.physicsTraversalDurationSec = 0.0f;
        agent.needsPath = true;
        agent.repathCooldownSec = kNavTestFailedPathRetrySec;
        agent.path.clear();
        agent.pathSegmentTypes.clear();
        agent.waypointIndex = 0;
      } else if (agent.physicsStuckTimeSec > 0.75f) {
        T8_LOG_INFO("[NavTraversalDebug] physics_stuck_repath mesh=%d agent=%zu type=%s pos=(%.2f,%.2f,%.2f) target=(%.2f,%.2f,%.2f) velocity=(%.2f,%.2f,%.2f) time=%.2f stuck=%.2f",
                    agent.meshIndex,
                    agentIndex,
                    traversalName(agent.physicsTraversalType),
                    navPosition.x, navPosition.y, navPosition.z,
                    agent.physicsTarget.x, agent.physicsTarget.y, agent.physicsTarget.z,
                    agent.physicsController.GetVelocity().x,
                    agent.physicsController.GetVelocity().y,
                    agent.physicsController.GetVelocity().z,
                    agent.physicsTraversalTimeSec,
                    agent.physicsStuckTimeSec);
        agent.physicsTraversalActive = false;
        agent.physicsWasAirborne = false;
        agent.physicsTraversalType = t850::navigation::NavTraversalType::Walk;
        agent.physicsTraversalTimeSec = 0.0f;
        agent.physicsTraversalDurationSec = 0.0f;
        agent.physicsStuckTimeSec = 0.0f;
        agent.needsPath = true;
        agent.repathCooldownSec = 0.0f;
        agent.path.clear();
        agent.pathSegmentTypes.clear();
        agent.waypointIndex = 0;
      }
      continue;
    }

    if (agent.needsPath || agent.path.empty()) {
      continue;
    }

    T8_TELEMETRY_SCOPE("navigation.agents.walk_follow");
    if (t850::RuntimeTelemetry::IsFrameActive()) {
      t850::RuntimeTelemetry::AddCounter("navigation.agents.walk_follow.count", 1.0);
    }
    XVECTOR3 current = agent.navPosition;
    float remaining = maxStep;
    while (remaining > 0.0f && agent.waypointIndex < static_cast<int>(agent.path.size())) {
      const int segmentIndex = agent.waypointIndex - 1;
      const t850::navigation::NavTraversalType segmentType =
          (segmentIndex >= 0 && segmentIndex < static_cast<int>(agent.pathSegmentTypes.size()))
              ? agent.pathSegmentTypes[static_cast<std::size_t>(segmentIndex)]
              : t850::navigation::NavTraversalType::Walk;
      const XVECTOR3 segmentStart =
          (segmentIndex >= 0 && segmentIndex < static_cast<int>(agent.path.size()))
              ? agent.path[static_cast<std::size_t>(segmentIndex)]
              : current;
      const XVECTOR3 target = agent.path[static_cast<std::size_t>(agent.waypointIndex)];
      XVECTOR3 delta = target - current;
      const float distance = delta.Length();
      t850::navigation::NavTraversalType effectiveSegmentType = segmentType;
      if (effectiveSegmentType != t850::navigation::NavTraversalType::Walk) {
        const t850::KinematicCharacterSettings& q3Settings = agent.characterSettings;
        agent.physicsController.SetSettings(q3Settings);
        agent.physicsController.Reset();
        agent.physicsController.SetPosition(Q3CenterFromGroundPoint(current, q3Settings));
        if (effectiveSegmentType == t850::navigation::NavTraversalType::Jump ||
            effectiveSegmentType == t850::navigation::NavTraversalType::Drop) {
          agent.physicsController.SetVelocity(XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f));
        }
        agent.physicsTraversalStart = current;
        agent.physicsTarget = target;
        agent.physicsLastNavPosition = current;
        agent.physicsTargetWaypointIndex = agent.waypointIndex;
        agent.physicsTraversalType = effectiveSegmentType;
        agent.physicsTraversalTimeSec = 0.0f;
        agent.physicsTraversalDurationSec = authoredTraversalDuration(effectiveSegmentType, current, target);
        agent.physicsStuckTimeSec = 0.0f;
        agent.physicsTraversalActive = true;
        agent.physicsWasAirborne = false;
        if (t850::RuntimeTelemetry::IsFrameActive()) {
          t850::RuntimeTelemetry::AddCounter("navigation.agents.physics_traversal.start", 1.0);
        }
        proposedPositions[agentIndex] = current;
        movedAgents[agentIndex] = 1;
        physicsMovedAgents[agentIndex] = 1;
        remaining = 0.0f;
        break;
      }
      if (agent.characterRuntimePath == 1) {
        const t850::KinematicCharacterSettings& q3Settings = agent.characterSettings;
        agent.physicsController.SetSettings(q3Settings);
        XVECTOR3 controllerGround = Q3GroundPointFromCenter(agent.physicsController.GetPosition(), q3Settings);
        if (DistanceSquared(controllerGround, agent.navPosition) > 4.0f) {
          agent.physicsController.SetPosition(Q3CenterFromGroundPoint(agent.navPosition, q3Settings));
          controllerGround = agent.navPosition;
        }
        agent.physicsController.UpdateQuake3(
            (std::max)(0.0f, dtSecs),
            BuildNavAgentPhysicsInput(controllerGround, target, false),
            t850::CharacterControllerContext{this});
        current = Q3GroundPointFromCenter(agent.physicsController.GetPosition(), q3Settings);
        if (HorizontalDistanceSq3(current, target) <= 0.04f &&
            std::fabs(current.y - target.y) <= (std::max)(0.50f, q3Settings.stepHeight * 2.0f)) {
          ++agent.waypointIndex;
        }
        physicsMovedAgents[agentIndex] = 1;
        remaining = 0.0f;
        break;
      }
      if (distance <= 0.0001f) {
        current = target;
        ++agent.waypointIndex;
        continue;
      }
      if (distance <= remaining) {
        current = target;
        remaining -= distance;
        ++agent.waypointIndex;
      } else {
        delta /= distance;
        current += delta * remaining;
        remaining = 0.0f;
      }
    }

    current = refreshAgentTargetAfterPathEnd(agent, current);

      proposedPositions[agentIndex] = current;
      movedAgents[agentIndex] = 1;
  }

  {
    T8_TELEMETRY_SCOPE("navigation.agents.separation");
    const t850::KinematicCharacterSettings q3Settings = t850::MakeQuake3CharacterSettings();
    const float agentHalfX = q3Settings.capsuleRadius;
    const float agentHalfZ = q3Settings.capsuleRadius;
    const float playerHalfX = q3Settings.capsuleRadius;
    const float playerHalfZ = q3Settings.capsuleRadius;
    XVECTOR3 playerGround = Cam.Eye;
    playerGround.y -= q3Settings.eyeHeight;
    playerGround.w = 1.0f;

    auto resolveAabbOverlap = [](XVECTOR3& first,
                                 float firstHalfX,
                                 float firstHalfZ,
                                 XVECTOR3& second,
                                 float secondHalfX,
                                 float secondHalfZ,
                                 bool moveFirst,
                                 bool moveSecond) {
      const float dx = second.x - first.x;
      const float dz = second.z - first.z;
      const float overlapX = firstHalfX + secondHalfX - std::fabs(dx);
      const float overlapZ = firstHalfZ + secondHalfZ - std::fabs(dz);
      if (overlapX <= 0.0f || overlapZ <= 0.0f) {
        return false;
      }

      const bool pushX = overlapX < overlapZ;
      const float direction = pushX
          ? (dx >= 0.0f ? 1.0f : -1.0f)
          : (dz >= 0.0f ? 1.0f : -1.0f);
      const float amount = (pushX ? overlapX : overlapZ) + 0.01f;
      const float firstShare = moveFirst && moveSecond ? 0.5f : (moveFirst ? 1.0f : 0.0f);
      const float secondShare = moveFirst && moveSecond ? 0.5f : (moveSecond ? 1.0f : 0.0f);
      if (pushX) {
        first.x -= direction * amount * firstShare;
        second.x += direction * amount * secondShare;
      } else {
        first.z -= direction * amount * firstShare;
        second.z += direction * amount * secondShare;
      }
      return true;
    };

    for (std::size_t iteration = 0; iteration < 3; ++iteration) {
      for (std::size_t agentIndex = 0; agentIndex < m_navTestAgents.size(); ++agentIndex) {
        NavTestAgentRuntime& agent = m_navTestAgents[agentIndex];
        if (!agent.active ||
            agent.meshIndex < 0 || agent.meshIndex >= kMaxSandboxMeshes ||
            !Meshes[agent.meshIndex].pBase) {
          continue;
        }
        if (!movedAgents[agentIndex]) {
          proposedPositions[agentIndex] = agent.navPosition;
          movedAgents[agentIndex] = 1;
        }
        const bool agentProtected = isPhysicsTraversalProtected(agentIndex);
        if (resolveAabbOverlap(playerGround,
                               playerHalfX,
                               playerHalfZ,
                               proposedPositions[agentIndex],
                               agentHalfX,
                               agentHalfZ,
                               false,
                               !agentProtected)) {
          if (t850::RuntimeTelemetry::IsFrameActive()) {
            t850::RuntimeTelemetry::AddCounter("navigation.agents.aabb.player_overlaps", 1.0);
          }
        }
      }

    for (std::size_t a = 0; a < m_navTestAgents.size(); ++a) {
        if (!movedAgents[a]) continue;
      for (std::size_t b = a + 1; b < m_navTestAgents.size(); ++b) {
          if (!movedAgents[b]) continue;
          const bool protectA = isPhysicsTraversalProtected(a);
          const bool protectB = isPhysicsTraversalProtected(b);
          if (protectA && protectB) {
            continue;
          }
          if (resolveAabbOverlap(proposedPositions[a],
                                 agentHalfX,
                                 agentHalfZ,
                                 proposedPositions[b],
                                 agentHalfX,
                                 agentHalfZ,
                                 !protectA,
                                 !protectB)) {
            if (t850::RuntimeTelemetry::IsFrameActive()) {
              t850::RuntimeTelemetry::AddCounter("navigation.agents.aabb.agent_overlaps", 1.0);
            }
          }
        }

      }
    }
  }

  {
    T8_TELEMETRY_SCOPE("navigation.agents.apply_transforms");
    for (std::size_t agentIndex = 0; agentIndex < m_navTestAgents.size(); ++agentIndex) {
      NavTestAgentRuntime& agent = m_navTestAgents[agentIndex];
      if (!agent.active ||
          agent.meshIndex < 0 || agent.meshIndex >= kMaxSandboxMeshes ||
          !Meshes[agent.meshIndex].pBase) {
        continue;
      }

      PrimitiveInst& instance = Meshes[agent.meshIndex];
      if (movedAgents[agentIndex]) {
        XVECTOR3 navPosition = proposedPositions[agentIndex];
        if (!physicsMovedAgents[agentIndex] && !suppressProjection[agentIndex]) {
          const XVECTOR3 requestedNavPosition = navPosition;
          XVECTOR3 projectedNavPosition = navPosition;
          std::string projectionError;
          const bool projected = m_navMesh.ProjectPoint(
              requestedNavPosition,
              projectedNavPosition,
              NavTestAgentProjectionExtents(),
              &projectionError);

          const float correctionSq = projected
              ? DistanceSquared(requestedNavPosition, projectedNavPosition)
              : 0.0f;
          const bool largeCorrection = projected && correctionSq > 0.25f;
          const bool shouldLogProjection =
              (!projected || largeCorrection) &&
              agentIndex < s_navProjectionDebugCooldownSec.size() &&
              s_navProjectionDebugCooldownSec[agentIndex] <= 0.0f;

          if (shouldLogProjection) {
            s_navProjectionDebugCooldownSec[agentIndex] = 0.50f;
            const int segmentIndex = agent.waypointIndex - 1;
            const t850::navigation::NavTraversalType segmentType =
                (segmentIndex >= 0 && segmentIndex < static_cast<int>(agent.pathSegmentTypes.size()))
                    ? agent.pathSegmentTypes[static_cast<std::size_t>(segmentIndex)]
                    : t850::navigation::NavTraversalType::Walk;
            const XVECTOR3 segmentBase =
                (segmentIndex >= 0 && segmentIndex < static_cast<int>(agent.path.size()))
                    ? agent.path[static_cast<std::size_t>(segmentIndex)]
                    : agent.navPosition;
            const XVECTOR3 nextPoint =
                (agent.waypointIndex >= 0 && agent.waypointIndex < static_cast<int>(agent.path.size()))
                    ? agent.path[static_cast<std::size_t>(agent.waypointIndex)]
                    : agent.target;
            const float requestedMove = std::sqrt(HorizontalDistanceSq3(agent.navPosition, requestedNavPosition));
            const float distToNext = std::sqrt(HorizontalDistanceSq3(agent.navPosition, nextPoint));
            const float correction = projected ? std::sqrt(correctionSq) : 0.0f;
#ifdef NAV_MESH_TRACE_LOGS
            T8_LOG_INFO("[NavProjectionDebug] mesh=%d agent=%zu projected=%d largeCorrection=%d old=(%.2f,%.2f,%.2f) requested=(%.2f,%.2f,%.2f) projectedPos=(%.2f,%.2f,%.2f) requestedMoveXZ=%.3f correction=%.3f segment=%s wp=%d path=%zu base=(%.2f,%.2f,%.2f) next=(%.2f,%.2f,%.2f) distNext=%.3f target=(%.2f,%.2f,%.2f) desired=(%.2f,%.2f,%.2f) physics=%d queued=%d suppressed=%d err='%s'",
                        agent.meshIndex,
                        agentIndex,
                        projected ? 1 : 0,
                        largeCorrection ? 1 : 0,
                        agent.navPosition.x, agent.navPosition.y, agent.navPosition.z,
                        requestedNavPosition.x, requestedNavPosition.y, requestedNavPosition.z,
                        projected ? projectedNavPosition.x : requestedNavPosition.x,
                        projected ? projectedNavPosition.y : requestedNavPosition.y,
                        projected ? projectedNavPosition.z : requestedNavPosition.z,
                        requestedMove,
                        correction,
                        traversalName(segmentType),
                        agent.waypointIndex,
                        agent.path.size(),
                        segmentBase.x, segmentBase.y, segmentBase.z,
                        nextPoint.x, nextPoint.y, nextPoint.z,
                        distToNext,
                        agent.target.x, agent.target.y, agent.target.z,
                        agent.desiredTarget.x, agent.desiredTarget.y, agent.desiredTarget.z,
                        agent.physicsTraversalActive ? 1 : 0,
                        0,
                        suppressProjection[agentIndex] ? 1 : 0,
                        projectionError.c_str());
#endif
          }

          if (projected) {
            navPosition = projectedNavPosition;
          } else {
            navPosition = agent.navPosition;
          }
        }
        agent.navPosition = navPosition;
        agent.navToOriginOffset = agent.visualOffset;
        if (agent.physicsTraversalActive || agent.characterRuntimePath == 1) {
          const t850::KinematicCharacterSettings& q3Settings = agent.characterSettings;
          agent.physicsController.SetPosition(Q3CenterFromGroundPoint(agent.navPosition, q3Settings));
        }
      }

      const XVECTOR3 visualPosition = NavTestVisualPosition(agent.navPosition, agent.visualOffset);
      instance.TranslateAbsolute(visualPosition.x, visualPosition.y, visualPosition.z);
      RotateNavTestAgentToFace(instance, visualPosition, Cam.Eye, agent.frontYawOffsetDeg, agent.faceYawSign, &agent.visualYawDeg);
      instance.Update();
    }
  }

  if (m_navTestMode == kNavTestModeFollowPlayer) {
    m_navTestDiagAccumSec += (std::max)(0.0f, dtSecs);
    if (m_navTestDiagAccumSec >= kNavTestDiagIntervalSec) {
      m_navTestDiagAccumSec = 0.0f;
      for (const NavTestAgentRuntime& agent : m_navTestAgents) {
        if (!agent.active ||
            agent.meshIndex < 0 || agent.meshIndex >= kMaxSandboxMeshes ||
            !Meshes[agent.meshIndex].pBase) {
          continue;
        }

        const PrimitiveInst& instance = Meshes[agent.meshIndex];
        const XVECTOR3 position(instance.Final.m41, instance.Final.m42, instance.Final.m43, 1.0f);
        const bool hasNextWaypoint =
            !agent.path.empty() &&
            agent.waypointIndex >= 0 &&
            agent.waypointIndex < static_cast<int>(agent.path.size());
        const XVECTOR3 nextNav = hasNextWaypoint
            ? agent.path[static_cast<std::size_t>(agent.waypointIndex)]
            : agent.lastPathFirst;
        const XVECTOR3 nextWorld = NavTestVisualPosition(nextNav, agent.visualOffset);
        const float playerYawDeg = HorizontalYawDeg(Cam.Look);
        const float playerRightYawDeg = HorizontalYawDeg(Cam.Right, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
        const float toPlayerYawDeg = HorizontalYawDeg(Cam.Eye - position);
        const float toDesiredYawDeg = HorizontalYawDeg(agent.desiredTarget - position);
        const float desiredBehindDistance = Dot3(agent.desiredTarget - Cam.Eye, HorizontalOrFallback(Cam.Look, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f)));
        const float targetBehindDistance = Dot3(agent.target - Cam.Eye, HorizontalOrFallback(Cam.Look, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f)));
        const float expectedFaceYawDeg = NavTestAgentFacingYawDeg(position, Cam.Eye, agent.frontYawOffsetDeg, agent.faceYawSign);
        const float meshAuthoredYawDeg = agent.meshIndex >= 0 && agent.meshIndex < static_cast<int>(m_sceneObjectYawDegrees.size())
            ? m_sceneObjectYawDegrees[static_cast<std::size_t>(agent.meshIndex)]
            : 0.0f;
        const float playerBotRadius = m_hasAuthoredPlayer ? PlayerBotRadiusFromEntity(m_authoredPlayer) : 0.0f;
        const char* meshName = (agent.meshIndex >= 0 && agent.meshIndex < static_cast<int>(m_sceneObjectNames.size()))
            ? m_sceneObjectNames[static_cast<std::size_t>(agent.meshIndex)].c_str()
            : "";
        T8_LOG_TRACE("[DoomOrientationTrace] mesh=%d name='%s' slot=%d playerPos=(%.2f,%.2f,%.2f) playerLook=(%.3f,%.3f,%.3f) playerYaw=%.2f playerRightYaw=%.2f playerBotRadius=%.2f agentVisual=(%.2f,%.2f,%.2f) agentNav=(%.2f,%.2f,%.2f) meshAuthoredYaw=%.2f authoredCapsulePos=(%.2f,%.2f,%.2f) authoredCapsuleRotDeg=(%.2f,%.2f,%.2f) frontYawOffset=%.2f faceYawSign=%.1f meshVisualYaw=%.2f expectedFaceYaw=%.2f toPlayerYaw=%.2f toDesiredYaw=%.2f desired=(%.2f,%.2f,%.2f) target=(%.2f,%.2f,%.2f) desiredDotPlayerForward=%.2f targetDotPlayerForward=%.2f pathCount=%zu wp=%d physicsPath=%d traversal=%d",
                     agent.meshIndex,
                     meshName,
                     agent.followSlot,
                     Cam.Eye.x, Cam.Eye.y, Cam.Eye.z,
                     Cam.Look.x, Cam.Look.y, Cam.Look.z,
                     playerYawDeg,
                     playerRightYawDeg,
                     playerBotRadius,
                     position.x, position.y, position.z,
                     agent.navPosition.x, agent.navPosition.y, agent.navPosition.z,
                     meshAuthoredYawDeg,
                     agent.authoredCharacterPosition.x, agent.authoredCharacterPosition.y, agent.authoredCharacterPosition.z,
                     agent.authoredCharacterRotationDeg.x, agent.authoredCharacterRotationDeg.y, agent.authoredCharacterRotationDeg.z,
                     agent.frontYawOffsetDeg,
                     agent.faceYawSign,
                     agent.visualYawDeg,
                     expectedFaceYawDeg,
                     toPlayerYawDeg,
                     toDesiredYawDeg,
                     agent.desiredTarget.x, agent.desiredTarget.y, agent.desiredTarget.z,
                     agent.target.x, agent.target.y, agent.target.z,
                     desiredBehindDistance,
                     targetBehindDistance,
                     agent.path.size(),
                     agent.waypointIndex,
                     agent.characterRuntimePath,
                     static_cast<int>(agent.physicsTraversalType));
#ifdef NAV_MESH_TRACE_LOGS
        T8_LOG_INFO("[NavigationTestPos] mesh=%d slot=%d active=%d needsPath=%d cooldown=%.3f pathOk=%d pathCount=%zu wp=%d player=(%.2f,%.2f,%.2f) nav=(%.2f,%.2f,%.2f) visual=(%.2f,%.2f,%.2f) dyPlayer=%.2f desired=(%.2f,%.2f,%.2f) target=(%.2f,%.2f,%.2f) lastStart=(%.2f,%.2f,%.2f) lastEnd=(%.2f,%.2f,%.2f) pathFirst=(%.2f,%.2f,%.2f) nextNav=(%.2f,%.2f,%.2f) nextWorld=(%.2f,%.2f,%.2f) offset=(%.2f,%.2f,%.2f) err='%s'",
                    agent.meshIndex,
                    agent.followSlot,
                    agent.active ? 1 : 0,
                    agent.needsPath ? 1 : 0,
                    agent.repathCooldownSec,
                    agent.lastPathSuccess ? 1 : 0,
                    agent.path.size(),
                    agent.waypointIndex,
                    Cam.Eye.x, Cam.Eye.y, Cam.Eye.z,
                    agent.navPosition.x, agent.navPosition.y, agent.navPosition.z,
                    position.x, position.y, position.z,
                    position.y - Cam.Eye.y,
                    agent.desiredTarget.x, agent.desiredTarget.y, agent.desiredTarget.z,
                    agent.target.x, agent.target.y, agent.target.z,
                    agent.lastPathStart.x, agent.lastPathStart.y, agent.lastPathStart.z,
                    agent.lastPathEnd.x, agent.lastPathEnd.y, agent.lastPathEnd.z,
                    agent.lastPathFirst.x, agent.lastPathFirst.y, agent.lastPathFirst.z,
                    nextNav.x, nextNav.y, nextNav.z,
                    nextWorld.x, nextWorld.y, nextWorld.z,
                    agent.visualOffset.x, agent.visualOffset.y, agent.visualOffset.z,
                    agent.lastPathError.c_str());
#endif
      }
    }
  }
}

void SceneTemplate::CreateAssets() {
  const std::string& activeSceneFilePath = ActiveSceneFilePath();
  const std::string& activeModelPath = ActiveModelPath();
  const bool embeddedSceneProfile = !activeSceneFilePath.empty();
  const std::string startupModelKey = embeddedSceneProfile ? std::string{} : SandboxProfileModelKey(activeModelPath);
  std::vector<t850::SandboxProfileDesc> startupSceneProfiles;
  const std::vector<t850::SandboxProfileDesc>* startupProfiles = nullptr;
  t850::scene::EditorSceneFile startupScene;
  std::string renderGraphPath = "Scenes/SceneTemplate_RenderGraph.json";

  if (m_controlSetup.descriptor.name.empty()) {
    m_controlSetup.Load("Scenes/SceneTemplate.json");
  }
  startupProfiles = &m_controlSetup.descriptor.profiles;
  if (embeddedSceneProfile) {
    std::string startupSceneError;
    if (t850::scene::LoadEditorSceneFile(activeSceneFilePath, startupScene, &startupSceneError)) {
      startupSceneProfiles = startupScene.profiles;
      startupProfiles = &startupSceneProfiles;
      if (!startupScene.render_graph.empty()) {
        renderGraphPath = startupScene.render_graph;
      }
    } else {
      T8_LOG_ERROR("[SceneTemplate] Could not pre-read scene file from '%s': %s",
                   activeSceneFilePath.c_str(), startupSceneError.c_str());
    }
  }

  auto applyStartupProfilesForRenderTargets = [&]() {
    if (!startupProfiles) return;
    const t850::SandboxProfileDesc* baseProfile = nullptr;
    const t850::SandboxProfileDesc* runtimeProfile = nullptr;
    int bestRuntimeScore = -1;
    for (const auto& profile : *startupProfiles) {
      const bool modelSpecific = !profile.model.empty();
      const bool modelMatches = embeddedSceneProfile
          ? !modelSpecific
          : (!modelSpecific || SandboxProfileModelKey(profile.model) == startupModelKey);
      if (!modelMatches) continue;

      const bool hasTarget = !profile.name.empty() || !profile.platform.empty() || !profile.architecture.empty() ||
                             !profile.gpu_family.empty() || !profile.gpu_name_contains.empty();
      if (!hasTarget && (embeddedSceneProfile || modelSpecific)) {
        baseProfile = &profile;
        continue;
      }

      const int score = t850::ScoreSceneProfileMatch(profile, startupModelKey);
      if (score > bestRuntimeScore) {
        bestRuntimeScore = score;
        runtimeProfile = &profile;
      }
    }
    if (baseProfile) ApplySandboxProfileState(*baseProfile);
    if (runtimeProfile && runtimeProfile != baseProfile) ApplySandboxProfileState(*runtimeProfile);
  };
  applyStartupProfilesForRenderTargets();

  if (!m_renderGraph.Load(renderGraphPath)) {
    T8_LOG_ERROR("[SceneTemplate] Failed to load render graph");
    return;
  }
  T8_LOG_INFO("[SceneTemplate] Loaded render graph '%s'", renderGraphPath.c_str());
  if (renderGraphPath == "Scenes/SceneTemplate_RenderGraph.json") {
    m_renderGraph.DisablePass("Light Add");
  }
  if (m_renderWidth > 0 && m_renderHeight > 0) {
    m_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, SceneProp, m_renderWidth, m_renderHeight);
  } else {
    m_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, SceneProp);
  }
  const bool dofOn = SceneProp.ToogleDOF != 0;
  m_renderGraph.SetPassEnabled("CoC", dofOn);
  m_renderGraph.SetPassEnabled("Combine CoC", dofOn);
  m_renderGraph.SetPassEnabled("DOF", dofOn);
  m_renderGraph.SetPassEnabled("DOF 2", dofOn);

  t850::sandbox::RefreshDeferredPassHandles(
      m_renderGraph,
      GBufferPass,
      DeferredPass,
      Extra16FPass,
      DepthPass,
      ShadowAccumPass,
      ExtraHelperPass,
      BloomAccumPass,
      AdaptedLumCurrentPass,
      AdaptedLumPrevPass);

  PrimitiveMgr.SetEngineContext(pEngineContext);
  PrimitiveMgr.Init();
  PrimitiveMgr.SetVP(&VP);

  SceneProp.SSAOKernel.InitTexture();

  const t850::SelectorDesc* cubemapDesc = FindSelectorDesc(m_controlSetup.descriptor.selectors, "cubemap");
  const CubemapSelection startupProfileCubemap =
      ResolveStartupCubemapSelection(
          m_controlSetup.descriptor.selectors,
          *startupProfiles,
          embeddedSceneProfile,
          startupModelKey);
  std::string startupCubemapPath = NormalizeSceneResourcePath(m_controlSetup.environmentMap);
  if (startupCubemapPath.empty()) {
    startupCubemapPath = "sky/Ennis.dds";
  }
  if (cubemapDesc && !startupProfileCubemap.path.empty()) {
    startupCubemapPath = startupProfileCubemap.path;
    const int startupCubemapIndex = startupProfileCubemap.index >= 0
        ? startupProfileCubemap.index
        : CubemapSelectorIndexForPath(*cubemapDesc, startupCubemapPath);
    if (startupCubemapIndex >= 0) {
      m_currentCubemapIndex = startupCubemapIndex;
    }
  } else if (cubemapDesc) {
    const int environmentIndex = CubemapSelectorIndexForPath(*cubemapDesc, startupCubemapPath);
    m_currentCubemapIndex = environmentIndex >= 0
        ? environmentIndex
        : (std::max)(0, (std::min)(cubemapDesc->default_index, static_cast<int>(cubemapDesc->options.size()) - 1));
  }
  EnvMapTexIndex = g_pBaseDriver->CreateTexture(startupCubemapPath);
  m_currentCubemapPath = startupCubemapPath;
  EnvMaps.SetFallback(EnvMapTexIndex);
  LoadEnvironmentIBLResources(
    g_pBaseDriver,
    {m_controlSetup.environmentDiffuseIBL, m_controlSetup.environmentSpecularIBL, m_controlSetup.environmentBrdfLUT,
     m_controlSetup.environmentSheenIBL, m_controlSetup.environmentCharlieLUT, m_controlSetup.environmentSheenELUT},
    EnvMaps,
    DiffuseIBLTexIndex,
    SpecularIBLTexIndex,
    BrdfLUTTexIndex,
    SheenIBLTexIndex,
    CharlieLUTTexIndex,
    SheenELUTTexIndex);
  UpdateSceneIBLSettings(SceneProp, g_pBaseDriver, EnvMaps);

  if (!activeSceneFilePath.empty()) {
    if (!LoadEditorSceneAssets(activeSceneFilePath)) {
      T8_LOG_ERROR("[SceneTemplate] Cannot continue without scene assets for '%s'", activeSceneFilePath.c_str());
    }
  } else {
    // Load the glTF model
    int index = PrimitiveMgr.CreateMesh(activeModelPath.c_str());
    if (index < 0) {
      T8_LOG_ERROR("[SceneTemplate] Failed to load '%s'", activeModelPath.c_str());
    } else {
      T8_LOG_INFO("[SceneTemplate] Loaded model '%s', primitive index=%d", activeModelPath.c_str(), index);
      Meshes[0].CreateInstance(PrimitiveMgr.GetPrimitive(index), &VP);
      Meshes[0].Update();
      m_meshCount = 1;
      m_selectedSkinningMeshIndex = 0;
      m_selectedAnimationMeshIndex = 0;
      m_profileModelKey.clear();
      FitModelToView();
      LoadSandboxProfile();
    }
  }

  // No SkyBox mesh needed — cleared GBuffer pixels (MatId=0) sample
  // the environment cubemap directly in the deferred pass using the
  // interpolated view ray (PosCorner). This avoids cull-face issues
  // and works across all APIs.

  // Fullscreen quad setup
  m.Identity();
  Quads[0].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[0], 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[1], 1);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[2], 2);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[3], 3);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->pDepthTexture, 4);
  Quads[0].SetEnvironmentMap(g_pBaseDriver->GetTexture(EnvMapTexIndex));

  for (int i = 1; i <= 7; i++)
    Quads[i].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);

  PrimitiveMgr.SetSceneProps(&SceneProp);

  Quads[0].TranslateAbsolute(0.0f, 0.0f, 0.0f);
  Quads[0].Update();

  // Debug visualization
  m_debugText.LoadFromFile(24, "Fonts/Martius-LV9L4.ttf", 512.0f);
  m_debugSphere.Create(6, 12);
  m_lightArrowRenderer.Create();
  m_physicsDebugRenderer.Create();
  m_navMeshDebugRenderer.Create();
  m_gameLogic.Navigation().PrepareForNavMeshMutation();
  m_navMesh.Clear();
  m_navMeshBuildAttempted = false;
  float arrowVerts[10 * 4] = {};
  unsigned short arrowIndices[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  m_lightArrowVB = t850::LineRenderer::CreatePositionVB(arrowVerts, 10, BufferUsage::DINAMIC);
  m_lightArrowIB = t850::LineRenderer::CreateIndexBuffer16(arrowIndices, 10);
  m_lightArrowIndexCount = 10;

  if (false &&
      ActiveSceneFilePath().empty() &&
      Meshes[0].pBase &&
      !Meshes[0].HasPhysicsBody() &&
      !Meshes[0].HasPhysicsRagdoll()) {
    t850::EngineContext* engineContext = GetEngineContext();
    if (!engineContext) engineContext = &t850::GetEngineContext();
    if (engineContext && engineContext->physics && engineContext->physics->IsInitialized()) {
      bool attachedPhysics = false;
      RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
      if (skinned) {
        skinned->UpdateAnimationPose();
        t850::PhysicsRagdollBuildSettings settings;
        settings.fitToSkinnedGeometry = false;
        settings.preferHumanoidBones = false;
        settings.forceCapsuleForEveryBone = true;
        settings.minBoneLength = (std::max)(0.0002f, m_modelRadius * 0.0002f);
        settings.syntheticBoneLength = (std::max)(0.001f, m_modelRadius * 0.001f);
        settings.minRadius = (std::max)(0.0006f, m_modelRadius * 0.0008f);
        settings.maxRadius = (std::max)(0.02f, m_modelRadius * 0.035f);
        settings.radiusScale = 0.12f;
        settings.minSkinWeight = 0.08f;
        settings.radiusPercentile = 0.86f;
        settings.jointTrimFraction = 0.0f;
        t850::PhysicsRagdollAuthoringDesc generatedAuthoring;
        attachedPhysics = t850::BuildRagdollAuthoringFromSkeleton(
            *skinned,
            Meshes[0].Final,
            Meshes[0].GetEntityId(),
            settings,
            generatedAuthoring);
        if (attachedPhysics) {
          t850::PhysicsRagdollHandle handle =
              engineContext->physics->CreateRagdoll(
                  generatedAuthoring.binding.referencePose,
                  t850::PhysicsBodyMotion::Kinematic);
          attachedPhysics = handle.IsValid();
          if (attachedPhysics) {
            Meshes[0].AttachPhysicsRagdoll(handle);
          }
        }
        if (attachedPhysics) {
          m_ragdollAnimationBinding = generatedAuthoring.binding;
          m_ragdollParentCapsules = generatedAuthoring.parentBodyIndices;
          m_ragdollJointParentCapsules = generatedAuthoring.jointParentBodyIndices;
          m_ragdollFrozenCapsules = generatedAuthoring.frozenBodies;
          m_ragdollFrozenJoints = generatedAuthoring.frozenJoints;
          m_ragdollContactJoints = generatedAuthoring.contactJoints;
          m_driveRagdollFromAnimation = true;
          m_ragdollPhysicsDriven = false;
          m_ragdollPhysicsLogEmitted = false;
          m_ragdollDriveLogEmitted = false;
          if (m_driveRagdollFromAnimation) {
            m_ragdollGeneratedBinding = m_ragdollAnimationBinding;
            m_ragdollEditSavePath = BuildRagdollEditSavePath();
            m_ragdollEditSelectedCapsule = m_ragdollAnimationBinding.referencePose.bones.empty() ? -1 : 0;
            m_ragdollEditSelectedHandle = 0;
            LoadRagdollEditPose();
          }
          T8_LOG_INFO("[SceneTemplate] Attached full-skeleton ragdoll physics for '%s'", ActiveModelPath().c_str());
          if (!m_driveRagdollFromAnimation) {
            T8_LOG_ERROR("[SceneTemplate] Failed to bind full-skeleton ragdoll to animation pose for '%s'", ActiveModelPath().c_str());
          }
          CreatePhysicsFloor(*engineContext->physics);
        } else {
          T8_LOG_ERROR("[SceneTemplate] Failed to attach full-skeleton ragdoll physics for '%s'", ActiveModelPath().c_str());
        }
      }

      if (!attachedPhysics && !skinned) {
        CreatePhysicsFloor(*engineContext->physics);
        RenderMesh* mesh = static_cast<RenderMesh*>(Meshes[0].pBase);
        attachedPhysics = t850::AttachMeshBoxBody(*engineContext->physics, Meshes[0], *mesh, t850::PhysicsBodyMotion::Static);
        if (attachedPhysics) {
          T8_LOG_INFO("[SceneTemplate] Attached static mesh-box physics for '%s'", ActiveModelPath().c_str());
        }
      }
    }
  }
}

void SceneTemplate::OnLoadScene() {
  InstallSandboxConsoleLogCapture();
  InitVars();
  CreateAssets();
}

void SceneTemplate::OnDestoryScene() {
  DestroyAssets();
  UninstallSandboxConsoleLogCapture();
}

void SceneTemplate::ResetViewInput() {
  m_cameraController.ClearInput();
}

void SceneTemplate::DestroyAssets() {
  m_gameLogic.Shutdown();
  SceneProp.SSAOKernel.Destroy();
  bool hasPhysicsLinks = m_floorBody.IsValid();
  const int meshCount = (std::max)(m_meshCount, Meshes[0].pBase ? 1 : 0);
  for (int meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
    hasPhysicsLinks = hasPhysicsLinks || Meshes[meshIndex].HasPhysicsRagdoll() || Meshes[meshIndex].HasPhysicsBody();
  }
  if (hasPhysicsLinks) {
    t850::EngineContext* engineContext = GetEngineContext();
    if (!engineContext) engineContext = &t850::GetEngineContext();
    if (engineContext && engineContext->physics) {
      for (int meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
        if (Meshes[meshIndex].HasPhysicsRagdoll()) {
          engineContext->physics->DestroyRagdoll(Meshes[meshIndex].GetPhysicsRagdoll());
        }
        if (Meshes[meshIndex].HasPhysicsBody()) {
          engineContext->physics->DestroyBody(Meshes[meshIndex].GetPhysicsBody());
        }
      }
      if (m_floorBody.IsValid()) {
        engineContext->physics->DestroyBody(m_floorBody);
      }
    }
    for (int meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
      Meshes[meshIndex].ClearPhysicsLinks();
    }
    m_floorBody.Reset();
  }
  m_driveRagdollFromAnimation = false;
  m_ragdollPhysicsDriven = false;
  m_ragdollDriveLogEmitted = false;
  m_ragdollPhysicsLogEmitted = false;
  m_ragdollEditDirty = false;
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditSelectedCapsule = -1;
  m_ragdollEditSelectedJoint = -1;
  m_ragdollEditSelectedHandle = -1;
  m_ragdollEditRenamingCapsule = -1;
  m_ragdollEditRenameFocusPending = false;
  m_ragdollEditSavePath.clear();
  m_skeletonEditMode = false;
  m_skeletonEditWasPlaying = false;
  m_skeletonEditDragging = false;
  m_skeletonEditDirty = false;
  m_skeletonEditSelectedBone = -1;
  m_skeletonPreviewBoneActive = false;
  m_skeletonPreviewBoneIndex = -1;
  m_skeletonPreviewOriginalCombined.clear();
  m_skeletonEditBindCombined.clear();
  m_skeletonEditCombined.clear();
  m_skeletonEditSavePath.clear();
  m_ragdollAnimationBinding = t850::PhysicsRagdollAnimationBinding{};
  m_ragdollAnimationPose = t850::PhysicsRagdollDesc{};
  m_ragdollParentCapsules.clear();
  m_ragdollJointParentCapsules.clear();
  m_ragdollPhysicsStates.clear();
  m_ragdollPhysicsBoneIndices.clear();
  m_ragdollPhysicsCombinedMatrices.clear();
  m_ragdollGeneratedBinding = t850::PhysicsRagdollAnimationBinding{};
  m_meshCount = 0;
  m_loadedEditorScene = false;
  m_loadedEditorScenePath.clear();
  m_primaryRagdollResourcePath.clear();
  m_sceneObjectNames.clear();
  m_sceneMeshPaths.clear();
  m_sceneRagdollPaths.clear();
  m_sceneObjectYawDegrees.clear();
  m_sceneNavAgentFrontYawOffsets.clear();
  m_sceneNavAgentFaceYawSigns.clear();
  m_sceneNavAgentTargetModes.clear();
  m_sceneNavAgentFollowDistances.clear();
  m_sceneNavAgentSideOffsets.clear();
  m_sceneNavAgentFormationDepthSteps.clear();
  m_sceneNavAgentSlots.clear();
  m_sceneRagdolls.clear();
  m_scenePhysicsEntities.clear();
  m_hasAuthoredPlayer = false;
  m_authoredPlayer = t850::scene::ScenePhysicsEntityDesc{};
  m_navTestAgents.clear();
  m_navTestCandidatePoints.clear();
  m_navTestInitialized = false;
  m_selectedSkinningMeshIndex = 0;
  m_selectedAnimationMeshIndex = 0;
  m_currentCubemapIndex = 0;
  m_currentCubemapPath.clear();
  m_pendingCubemap.clear();
  m_debugText.Destroy();
  m_debugSphere.Destroy();
  if (m_lightArrowVB) m_lightArrowVB->release();
  if (m_lightArrowIB) m_lightArrowIB->release();
  m_lightArrowVB = nullptr;
  m_lightArrowIB = nullptr;
  m_lightArrowIndexCount = 0;
  m_lightArrowRenderer.Destroy();
  m_physicsDebugRenderer.Destroy();
  m_navMeshDebugRenderer.Destroy();
  m_navMesh.Clear();
  m_navMeshBuildAttempted = false;
  PrimitiveMgr.DestroyPrimitives();
  if (pFramework && pFramework->pVideoDriver) {
    m_renderGraph.DestroyRenderTargets(pFramework->pVideoDriver);
  }
}

void SceneTemplate::OnUpdate(float _DtSecs) {
  T8_TELEMETRY_SCOPE("sandbox.update");
  DtSecs = _DtSecs;
  SceneProp.FrameDeltaSec = DtSecs;

  // Apply deferred cubemap change BEFORE any rendering begins.
  // D3D12 texture upload submits a temp command list + fence wait, which
  // conflicts with the main command list if done mid-frame.
  if (!m_pendingCubemap.empty()) {
    T8_TELEMETRY_SCOPE("sandbox.update.pending_cubemap");
    T8_LOG_INFO("[SceneTemplate] Loading cubemap '%s' (old slot=%d)",
                m_pendingCubemap.c_str(), EnvMapTexIndex);
    // Flush GPU before destroying — D3D12 may still reference the old
    // texture from the previous frame's command list.
    g_pBaseDriver->WaitForGPU();
    int newEnvMapTexIndex = g_pBaseDriver->CreateTexture(m_pendingCubemap);
    if (newEnvMapTexIndex >= 0) {
      if (EnvMapTexIndex >= 0 && EnvMapTexIndex != newEnvMapTexIndex)
        g_pBaseDriver->DestroyTexture(EnvMapTexIndex);
      EnvMapTexIndex = newEnvMapTexIndex;
      m_currentCubemapPath = m_pendingCubemap;
      if (m_controlSetup.environmentDiffuseIBL.empty() && DiffuseIBLTexIndex >= 0) {
        g_pBaseDriver->DestroyTexture(DiffuseIBLTexIndex);
        DiffuseIBLTexIndex = -1;
      }
      if (m_controlSetup.environmentSpecularIBL.empty() && SpecularIBLTexIndex >= 0) {
        g_pBaseDriver->DestroyTexture(SpecularIBLTexIndex);
        SpecularIBLTexIndex = -1;
      }
      if (m_controlSetup.environmentSheenIBL.empty() && SheenIBLTexIndex >= 0) {
        g_pBaseDriver->DestroyTexture(SheenIBLTexIndex);
        SheenIBLTexIndex = -1;
      }
      EnvMaps.SetFallback(EnvMapTexIndex);
      LoadEnvironmentIBLResources(
        g_pBaseDriver,
        {m_controlSetup.environmentDiffuseIBL, m_controlSetup.environmentSpecularIBL, m_controlSetup.environmentBrdfLUT,
         m_controlSetup.environmentSheenIBL, m_controlSetup.environmentCharlieLUT, m_controlSetup.environmentSheenELUT},
        EnvMaps,
        DiffuseIBLTexIndex,
        SpecularIBLTexIndex,
        BrdfLUTTexIndex,
        SheenIBLTexIndex,
        CharlieLUTTexIndex,
        SheenELUTTexIndex);
      EnvMaps.BrdfLUT = BrdfLUTTexIndex;
      EnvMaps.CharlieIBL = SheenIBLTexIndex;
      EnvMaps.CharlieLUT = CharlieLUTTexIndex;
      EnvMaps.SheenELUT = SheenELUTTexIndex;
      UpdateSceneIBLSettings(SceneProp, g_pBaseDriver, EnvMaps);
      Texture* newTex = g_pBaseDriver->GetTexture(EnvMapTexIndex);
      T8_LOG_INFO("[SceneTemplate] Cubemap loaded: slot=%d tex=%p (%dx%d)",
                  EnvMapTexIndex, newTex, newTex ? newTex->x : 0, newTex ? newTex->y : 0);
      Quads[0].SetEnvironmentMap(newTex);
      const int meshCount = (std::max)(m_meshCount, Meshes[0].pBase ? 1 : 0);
      for (int meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
        if (Meshes[meshIndex].pBase) {
          Meshes[meshIndex].SetEnvironmentMap(newTex);
        }
      }
    } else {
      T8_LOG_ERROR("[SceneTemplate] Failed to load cubemap '%s'; keeping previous cubemap", m_pendingCubemap.c_str());
    }
    m_pendingCubemap.clear();
  }

  // Replay snapshot: load and apply (one-time)
  if (m_dumper.HasPendingReplay()) {
    T8_TELEMETRY_SCOPE("sandbox.update.replay_snapshot");
    if (m_dumper.LoadReplaySnapshot()) {
      m_dumper.ApplySnapshot(Cam, LightCam, SceneProp);
      if (const t850::SnapshotSkinnedJson* skinnedSnap = m_dumper.GetReplaySkinnedState()) {
        if (Meshes[0].pBase) {
          RenderSkinnedMesh* skinned = dynamic_cast<RenderSkinnedMesh*>(Meshes[0].pBase);
          ApplySkinnedSnapshot(skinned, *skinnedSnap, m_showWireframe, m_showSkeleton);
        }
      }
      VP = Cam.VP;
    }
  }
  {
    T8_TELEMETRY_SCOPE("sandbox.update.dumper_state");
    m_dumper.UpdateReplayState();
  }

  if (!m_dumper.SkipCameraUpdates()) {
    T8_TELEMETRY_SCOPE("sandbox.update.camera_and_lights");
    if (!UpdateSceneSplinePlayback(DtSecs)) {
      if (m_cameraController.GetActiveProfileType() == t850::CameraProfileType::Orbit) {
        T8_TELEMETRY_SCOPE("sandbox.update.camera.sync_orbit_profile");
        SyncOrbitProfileFromSandbox();
      }
      {
        T8_TELEMETRY_SCOPE("sandbox.update.camera_controller");
        t850::CameraUpdateContext cameraContext;
        cameraContext.collisionWorld = this;
        m_cameraController.Update(DtSecs, cameraContext);
      }
      if (m_cameraController.GetActiveProfileType() == t850::CameraProfileType::Orbit) {
        T8_TELEMETRY_SCOPE("sandbox.update.camera.sync_sandbox_orbit");
        SyncSandboxOrbitFromProfile();
      }
    }
    VP = Cam.VP;
    {
      T8_TELEMETRY_SCOPE("sandbox.update.attached_lights");
      UpdateAttachedLights();
      if (m_hasAuthoredLightCamera &&
          m_authoredLightCameraAttachedLight >= 0 &&
          m_authoredLightCameraAttachedLight < static_cast<int>(SceneProp.Lights.size())) {
        const bool hasLinearVelocity = m_authoredLightCameraLinearVelocity.Length() > 0.000001f;
        const bool hasTargetVelocity = m_authoredLightCameraTargetVelocity.Length() > 0.000001f;
        const bool hasAngularVelocity = m_authoredLightCameraAngularVelocity.Length() > 0.000001f;
        if (hasLinearVelocity || hasTargetVelocity || hasAngularVelocity) {
          XVECTOR3 target = LightCam.Eye + LightCam.Look;
          if (hasLinearVelocity) {
            LightCam.Eye += m_authoredLightCameraLinearVelocity * DtSecs;
            target += m_authoredLightCameraLinearVelocity * DtSecs;
          }
          if (hasTargetVelocity) {
            target += m_authoredLightCameraTargetVelocity * DtSecs;
          }
          LightCam.SetLookAt(target);
          LightCam.Pitch += m_authoredLightCameraAngularVelocity.x * DtSecs;
          LightCam.Yaw += m_authoredLightCameraAngularVelocity.y * DtSecs;
          LightCam.Roll += m_authoredLightCameraAngularVelocity.z * DtSecs;
          LightCam.Update(DtSecs);
        }
        Light& attachedLight = SceneProp.Lights[static_cast<std::size_t>(m_authoredLightCameraAttachedLight)];
        attachedLight.Position = LightCam.Eye;
        attachedLight.Direction = LightCam.Look;
      } else {
        SyncLightCameraFromDirectionalLight();
      }
    }
  }
  {
    T8_TELEMETRY_SCOPE("sandbox.update.nav_agents");
    UpdateNavTestAgents(DtSecs);
  }

  // --dumpMatrices: log all camera matrices per frame, then exit
  if (g_config.flags.dumpMatrices) {
    T8_TELEMETRY_SCOPE("sandbox.update.dump_matrices");
    static int s_matDumpFrame = 0;
    static std::ofstream s_matFile;
    if (s_matDumpFrame == 0) {
      s_matFile.open("matrix_dump.csv", std::ios::out | std::ios::trunc);
      s_matFile << "frame,";
      s_matFile << "cam_eye_x,cam_eye_y,cam_eye_z,";
      s_matFile << "cam_pitch,cam_roll,cam_yaw,";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "camView_" << r << c << ",";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "camProj_" << r << c << ",";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "camVP_" << r << c << ",";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "lightView_" << r << c << ",";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "lightProj_" << r << c << ",";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "lightVP_" << r << c << (r == 3 && c == 3 ? "" : ",");
      s_matFile << "\n";
    }
    s_matFile << s_matDumpFrame << ",";
    s_matFile << Cam.Eye.x << "," << Cam.Eye.y << "," << Cam.Eye.z << ",";
    s_matFile << Cam.Pitch << "," << Cam.Roll << "," << Cam.Yaw << ",";
    auto writeM = [&](const XMATRIX44& M) {
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << M.m[r][c] << ",";
    };
    writeM(Cam.View);
    writeM(Cam.Projection);
    writeM(Cam.VP);
    writeM(LightCam.View);
    writeM(LightCam.Projection);
    auto& LVP = LightCam.VP;
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++)
        s_matFile << LVP.m[r][c] << (r == 3 && c == 3 ? "" : ",");
    s_matFile << "\n";
    s_matFile.flush();
    s_matDumpFrame++;
    if (s_matDumpFrame >= g_config.dumpMatricesFrames) {
      s_matFile.close();
      T8_LOG_INFO("[dumpMatrices] Wrote %d frames to matrix_dump.csv", s_matDumpFrame);
      exit(0);
    }
  }

  {
    T8_TELEMETRY_SCOPE("sandbox.update.skinned_animation");
    const int updateMeshCount = (std::max)(m_meshCount, Meshes[0].pBase ? 1 : 0);
    for (int meshIndex = 0; meshIndex < updateMeshCount; ++meshIndex) {
      if (!Meshes[meshIndex].pBase) continue;
      RenderSkinnedMesh* skinned = Meshes[meshIndex].GetSkinnedMesh();
      if (!skinned || !skinned->HasSkinData()) continue;
      T8_TELEMETRY_SCOPE("sandbox.update.skinned_animation.pose");
      skinned->UpdateAnimationPose();
    }
  }
  m_gameLogic.Update(DtSecs);
}

void SceneTemplate::DriveRagdollFromAnimation(float deltaSeconds) {
  if (!m_driveRagdollFromAnimation || !Meshes[0].HasPhysicsRagdoll()) {
    return;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  RenderSkinnedMesh* skinned = GetSelectedAnimationMesh();
  if (!engineContext || !engineContext->physics || !skinned || skinned->HasSnapshotBoneMatrices()) {
    return;
  }

  if (!t850::BuildRagdollPoseFromAnimation(*skinned, Meshes[0].Final, m_ragdollAnimationBinding, m_ragdollAnimationPose)) {
    if (!m_ragdollDriveLogEmitted) {
      T8_LOG_ERROR("[SceneTemplate] Failed to build animation-driven ragdoll pose for '%s'", ActiveModelPath().c_str());
      m_ragdollDriveLogEmitted = true;
    }
    return;
  }

  if (!engineContext->physics->DriveRagdollFromPose(Meshes[0].GetPhysicsRagdoll(), m_ragdollAnimationPose, deltaSeconds)) {
    if (!m_ragdollDriveLogEmitted) {
      T8_LOG_ERROR("[SceneTemplate] Failed to drive ragdoll from animation pose for '%s'", ActiveModelPath().c_str());
      m_ragdollDriveLogEmitted = true;
    }
    return;
  }

  if (!m_ragdollDriveLogEmitted) {
    T8_LOG_INFO("[SceneTemplate] Driving humanoid ragdoll from animation pose: bodies=%zu", m_ragdollAnimationPose.bones.size());
    LogRagdollFloorDiagnostics("animation driven");
    m_ragdollDriveLogEmitted = true;
  }
}

void SceneTemplate::UpdateSkeletonFromRagdollPhysics() {
  if (!m_ragdollPhysicsDriven || !Meshes[0].HasPhysicsRagdoll()) {
    return;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  RenderSkinnedMesh* skinned = GetSelectedAnimationMesh();
  if (!engineContext || !engineContext->physics || !skinned || !skinned->HasSkinData()) {
    return;
  }

  if (!engineContext->physics->GetRagdollState(Meshes[0].GetPhysicsRagdoll(), m_ragdollPhysicsStates) ||
      !t850::BuildSkeletonPoseFromRagdollState(
          *skinned,
          Meshes[0].Final,
          m_ragdollAnimationBinding,
          m_ragdollPhysicsStates,
          m_ragdollPhysicsBoneIndices,
          m_ragdollPhysicsCombinedMatrices) ||
      !skinned->GetAnimController().ApplyCombinedPoseOverrides(
          m_ragdollPhysicsBoneIndices,
          m_ragdollPhysicsCombinedMatrices)) {
    if (!m_ragdollPhysicsLogEmitted) {
      T8_LOG_ERROR("[SceneTemplate] Failed to drive skinned skeleton from physics for '%s'", ActiveModelPath().c_str());
      m_ragdollPhysicsLogEmitted = true;
    }
    return;
  }

  if (!m_ragdollPhysicsLogEmitted) {
    T8_LOG_INFO("[SceneTemplate] Driving skinned skeleton from dynamic ragdoll physics: bodies=%zu", m_ragdollPhysicsStates.size());
    m_ragdollPhysicsLogEmitted = true;
  }
  if (!m_ragdollFloorRuntimeDiagEmitted) {
    LogRagdollFloorDiagnostics("first dynamic frame");
    m_ragdollFloorRuntimeDiagEmitted = true;
  }
}

void SceneTemplate::UpdateSceneSkeletonsFromRagdollPhysics() {
  if (m_sceneRagdolls.empty()) {
    return;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (!engineContext || !engineContext->physics) {
    return;
  }

  for (SceneRagdollRuntime& runtime : m_sceneRagdolls) {
    if (!runtime.physicsDriven ||
        runtime.meshIndex < 0 ||
        runtime.meshIndex >= static_cast<int>(m_meshCount) ||
        runtime.meshIndex >= kMaxSandboxMeshes ||
        !Meshes[runtime.meshIndex].HasPhysicsRagdoll()) {
      continue;
    }

    RenderSkinnedMesh* skinned = Meshes[runtime.meshIndex].GetSkinnedMesh();
    if (!skinned || !skinned->HasSkinData()) {
      continue;
    }

    const bool gotState =
        engineContext->physics->GetRagdollState(
            Meshes[runtime.meshIndex].GetPhysicsRagdoll(),
            runtime.physicsStates);
    const bool builtPose =
        gotState &&
        t850::BuildSkeletonPoseFromRagdollState(
            *skinned,
            Meshes[runtime.meshIndex].Final,
            runtime.binding,
            runtime.physicsStates,
            runtime.physicsBoneIndices,
            runtime.physicsCombinedMatrices);
    const bool appliedPose =
        builtPose &&
        skinned->GetAnimController().ApplyCombinedPoseOverrides(
            runtime.physicsBoneIndices,
            runtime.physicsCombinedMatrices);
    if (!gotState || !builtPose || !appliedPose) {
      if (!runtime.physicsLogEmitted) {
        T8_LOG_ERROR("[SceneTemplate] Failed to drive scene object skeleton from ragdoll '%s' state=%d bodies=%zu pose=%d overrides=%d bones=%zu",
                     runtime.resourcePath.c_str(),
                     gotState ? 1 : 0,
                     runtime.physicsStates.size(),
                     builtPose ? 1 : 0,
                     appliedPose ? 1 : 0,
                     runtime.physicsBoneIndices.size());
        runtime.physicsLogEmitted = true;
      }
      continue;
    }

    if (!runtime.physicsLogEmitted) {
      T8_LOG_INFO("[SceneTemplate] Driving scene object skeleton from dynamic ragdoll '%s': bodies=%zu",
                  runtime.resourcePath.c_str(),
                  runtime.physicsStates.size());
      runtime.physicsLogEmitted = true;
    }
  }
}

void SceneTemplate::SwitchRagdollToPhysics() {
  if (m_ragdollPhysicsDriven) {
    T8_LOG_INFO("[SceneTemplate] Ragdoll is already physics-driven");
    return;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  RenderSkinnedMesh* skinned = GetSelectedAnimationMesh();
  if (!engineContext || !engineContext->physics || !Meshes[0].HasPhysicsRagdoll() || !skinned || !skinned->HasSkinData()) {
    if (SwitchSceneRagdollsToPhysics()) {
      return;
    }
    T8_LOG_ERROR("[SceneTemplate] Cannot switch to ragdoll physics: no skinned ragdoll is attached");
    return;
  }

  std::vector<XMATRIX44> animationShaderMatrices;
  skinned->ExportBoneMatrices(animationShaderMatrices);

  if (m_driveRagdollFromAnimation &&
      t850::BuildRagdollPoseFromAnimation(*skinned, Meshes[0].Final, m_ragdollAnimationBinding, m_ragdollAnimationPose)) {
    engineContext->physics->DriveRagdollFromPose(Meshes[0].GetPhysicsRagdoll(), m_ragdollAnimationPose, 0.0f);
  }

  std::vector<t850::PhysicsBodyState> handoffPhysicsStates;
  std::vector<int> handoffBoneIndices;
  std::vector<XMATRIX44> handoffCombinedMatrices;
  std::vector<XMATRIX44> handoffShaderMatrices;
  if (engineContext->physics->GetRagdollState(Meshes[0].GetPhysicsRagdoll(), handoffPhysicsStates) &&
      t850::BuildSkeletonPoseFromRagdollState(
          *skinned,
          Meshes[0].Final,
          m_ragdollAnimationBinding,
          handoffPhysicsStates,
          handoffBoneIndices,
          handoffCombinedMatrices) &&
      skinned->GetAnimController().ApplyCombinedPoseOverrides(handoffBoneIndices, handoffCombinedMatrices)) {
    skinned->ExportBoneMatrices(handoffShaderMatrices);
    DumpRagdollF5MatrixComparison(
        *skinned,
        m_ragdollAnimationPose,
        handoffPhysicsStates,
        handoffBoneIndices,
        handoffCombinedMatrices,
        animationShaderMatrices,
        handoffShaderMatrices);
    m_ragdollPhysicsStates = std::move(handoffPhysicsStates);
    m_ragdollPhysicsBoneIndices = std::move(handoffBoneIndices);
    m_ragdollPhysicsCombinedMatrices = std::move(handoffCombinedMatrices);
  } else {
    T8_LOG_ERROR("[SceneTemplate] Failed to dump F5 ragdoll matrix comparison for '%s'", ActiveModelPath().c_str());
  }

  if (m_floorBody.IsValid()) {
    engineContext->physics->DestroyBody(m_floorBody);
    m_floorBody.Reset();
  }
  CreatePhysicsFloor(*engineContext->physics);
  LogRagdollFloorDiagnostics("F5 pre-dynamic");
  if (!engineContext->physics->SetRagdollMotion(Meshes[0].GetPhysicsRagdoll(), t850::PhysicsBodyMotion::Dynamic)) {
    T8_LOG_ERROR("[SceneTemplate] Failed to switch ragdoll bodies to dynamic physics");
    return;
  }
  engineContext->physics->SetRagdollVelocity(
      Meshes[0].GetPhysicsRagdoll(),
      XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f),
      XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f));

  m_driveRagdollFromAnimation = false;
  m_ragdollPhysicsDriven = true;
  m_ragdollPhysicsLogEmitted = false;
  m_ragdollFloorRuntimeDiagEmitted = false;
  skinned->PauseAnimation();
  skinned->ClearSnapshotBoneMatrices();
  LogRagdollFloorDiagnostics("F5 post-dynamic");
  SwitchSceneRagdollsToPhysics();
  T8_LOG_INFO("[SceneTemplate] F5: animation-to-physics ragdoll transition started");
}

bool SceneTemplate::PickRagdollSimulationBody(float mouseX,
                                             float mouseY,
                                             int& outBodyIndex,
                                             t850::PhysicsBodyState& outState,
                                             XVECTOR3& outHitPoint,
                                             float& outHitDistance) {
  outBodyIndex = -1;
  outHitDistance = FLT_MAX;
  if (!m_ragdollPhysicsDriven ||
      !Meshes[0].HasPhysicsRagdoll() ||
      !g_pBaseDriver ||
      m_ragdollAnimationBinding.referencePose.bones.empty()) {
    return false;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (!engineContext || !engineContext->physics) {
    return false;
  }

  std::vector<t850::PhysicsBodyState> states;
  if (!engineContext->physics->GetRagdollState(Meshes[0].GetPhysicsRagdoll(), states)) {
    return false;
  }

  XMATRIX44 invVP;
  VP.Inverse(&invVP);
  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  const t850::Ray ray = t850::ScreenPointToRay(mouseX, mouseY, 0, 0, width, height, invVP);
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  const int count = (std::min)(static_cast<int>(states.size()), static_cast<int>(bones.size()));
  for (int bodyIndex = 0; bodyIndex < count; ++bodyIndex) {
    const auto& shape = bones[static_cast<std::size_t>(bodyIndex)].body.shape;
    if (!IsEditableRagdollShape(shape)) {
      continue;
    }

    float hitDistance = FLT_MAX;
    XVECTOR3 hitPoint;
    if (!RayIntersectsRagdollShape(
            ray,
            shape,
            states[static_cast<std::size_t>(bodyIndex)].worldTransform,
            hitDistance,
            hitPoint)) {
      continue;
    }

    if (hitDistance < outHitDistance) {
      outBodyIndex = bodyIndex;
      outState = states[static_cast<std::size_t>(bodyIndex)];
      outHitPoint = hitPoint;
      outHitDistance = hitDistance;
    }
  }

  if (outBodyIndex >= 0) {
    m_ragdollPhysicsStates = std::move(states);
    return true;
  }
  return false;
}

bool SceneTemplate::BeginRagdollSimulationGrab(float mouseX, float mouseY) {
  t850::PhysicsBodyState pickedState;
  XVECTOR3 hitPoint;
  float hitDistance = 0.0f;
  int bodyIndex = -1;
  if (!PickRagdollSimulationBody(mouseX, mouseY, bodyIndex, pickedState, hitPoint, hitDistance) ||
      !pickedState.handle.IsValid()) {
    return false;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (!engineContext || !engineContext->physics) {
    return false;
  }

  if (!engineContext->physics->SetBodyMotion(pickedState.handle, t850::PhysicsBodyMotion::Dynamic)) {
    return false;
  }
  engineContext->physics->SetBodyVelocity(
      pickedState.handle,
      XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f),
      pickedState.angularVelocity);

  const XVECTOR3 center(pickedState.worldTransform.m41,
                        pickedState.worldTransform.m42,
                        pickedState.worldTransform.m43,
                        1.0f);
  m_ragdollSimulationGrabActive = true;
  m_ragdollSimulationGrabBodyIndex = bodyIndex;
  m_ragdollSimulationGrabHandle = pickedState.handle;
  m_ragdollSimulationGrabDepth = hitDistance;
  m_ragdollSimulationGrabCenterOffset = center - hitPoint;
  m_ragdollSimulationGrabCenterOffset.w = 0.0f;
  m_ragdollSimulationGrabPreviousTarget = center;
  m_ragdollSimulationGrabReleaseVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  m_ragdollEditSelectedCapsule = bodyIndex;
  if (bodyIndex >= 0 && bodyIndex < static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size())) {
    const auto& bone = m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(bodyIndex)];
    T8_LOG_INFO("[SceneTemplate] Grabbed ragdoll body %d (%s)", bodyIndex, bone.body.debugName.c_str());
  }
  return UpdateRagdollSimulationGrab(mouseX, mouseY);
}

bool SceneTemplate::UpdateRagdollSimulationGrab(float mouseX, float mouseY) {
  if (!m_ragdollSimulationGrabActive || !m_ragdollSimulationGrabHandle.IsValid() || !g_pBaseDriver) {
    return false;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (!engineContext || !engineContext->physics) {
    EndRagdollSimulationGrab(false);
    return false;
  }

  XMATRIX44 invVP;
  VP.Inverse(&invVP);
  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  const t850::Ray ray = t850::ScreenPointToRay(mouseX, mouseY, 0, 0, width, height, invVP);
  const XVECTOR3 targetOnRay = ray.origin + Normalize3(ray.direction) * m_ragdollSimulationGrabDepth;
  XVECTOR3 targetCenter = targetOnRay + m_ragdollSimulationGrabCenterOffset;
  targetCenter.w = 1.0f;

  t850::PhysicsBodyState currentState;
  if (!engineContext->physics->GetBodyState(m_ragdollSimulationGrabHandle, currentState)) {
    EndRagdollSimulationGrab(false);
    return false;
  }

  const float dt = (std::max)(0.0001f, DtSecs);
  const XVECTOR3 currentCenter(currentState.worldTransform.m41,
                               currentState.worldTransform.m42,
                               currentState.worldTransform.m43,
                               1.0f);
  const XVECTOR3 cursorVelocity = (targetCenter - m_ragdollSimulationGrabPreviousTarget) * (1.0f / dt);
  const XVECTOR3 pullError = targetCenter - currentCenter;
  constexpr float kGrabSpring = 12.0f;
  const XVECTOR3 desiredVelocity = cursorVelocity + pullError * kGrabSpring;
  const float maxThrowSpeed = (std::max)(1.0f, m_modelRadius * 8.0f);
  const float maxPullSpeed = (std::max)(maxThrowSpeed, m_modelRadius * 12.0f);
  const XVECTOR3 pullVelocity = ClampVectorLength3(desiredVelocity, maxPullSpeed);
  m_ragdollSimulationGrabReleaseVelocity = ClampVectorLength3(cursorVelocity, maxThrowSpeed);
  m_ragdollSimulationGrabPreviousTarget = targetCenter;
  return engineContext->physics->SetBodyVelocity(
      m_ragdollSimulationGrabHandle,
      pullVelocity,
      currentState.angularVelocity);
}

void SceneTemplate::EndRagdollSimulationGrab(bool applyThrow) {
  if (!m_ragdollSimulationGrabActive) {
    return;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (engineContext && engineContext->physics && m_ragdollSimulationGrabHandle.IsValid()) {
    engineContext->physics->SetBodyMotion(m_ragdollSimulationGrabHandle, t850::PhysicsBodyMotion::Dynamic);
    if (applyThrow) {
      t850::PhysicsBodyState currentState;
      const XVECTOR3 angularVelocity =
          engineContext->physics->GetBodyState(m_ragdollSimulationGrabHandle, currentState)
              ? currentState.angularVelocity
              : XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
      engineContext->physics->SetBodyVelocity(
          m_ragdollSimulationGrabHandle,
          m_ragdollSimulationGrabReleaseVelocity,
          angularVelocity);
    }
  }

  m_ragdollSimulationGrabActive = false;
  m_ragdollSimulationGrabBodyIndex = -1;
  m_ragdollSimulationGrabHandle.Reset();
  m_ragdollSimulationGrabDepth = 0.0f;
  m_ragdollSimulationGrabCenterOffset = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  m_ragdollSimulationGrabPreviousTarget = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  m_ragdollSimulationGrabReleaseVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
}

bool SceneTemplate::HandleRagdollSimulationGrabInput(InputManager* input, bool imguiWantsMouse) {
  if (!input) {
    return false;
  }

  if (!m_ragdollPhysicsDriven || m_skeletonEditMode) {
    if (m_ragdollSimulationGrabActive) {
      EndRagdollSimulationGrab(false);
    }
    return false;
  }

  if (m_ragdollSimulationGrabActive) {
    if (input->PressedMouseButton(0)) {
      UpdateRagdollSimulationGrab(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY));
      return true;
    }
    EndRagdollSimulationGrab(true);
    return true;
  }

  if (imguiWantsMouse || !input->PressedOnceMouseButton(0)) {
    return false;
  }

  return BeginRagdollSimulationGrab(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY));
}

bool SceneTemplate::ResetRagdollPhysicsAndAnimation() {
  EndRagdollSimulationGrab(false);

  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (!skinned || !skinned->HasSkinData()) {
    T8_LOG_ERROR("[SceneTemplate] Cannot reset ragdoll: no skinned model is loaded");
    return false;
  }

  if (m_skeletonEditMode) {
    ExitSkeletonEditMode();
  }

  skinned->ClearSnapshotBoneMatrices();
  skinned->ResetAnimation();
  skinned->PlayAnimation();
  skinned->GetAnimController().Update(0.0f);

  m_ragdollPhysicsDriven = false;
  m_driveRagdollFromAnimation = true;
  m_ragdollDriveLogEmitted = false;
  m_ragdollPhysicsLogEmitted = false;
  m_ragdollPhysicsStates.clear();
  m_ragdollPhysicsBoneIndices.clear();
  m_ragdollPhysicsCombinedMatrices.clear();

  if (!engineContext || !engineContext->physics || m_ragdollAnimationBinding.referencePose.bones.empty()) {
    T8_LOG_INFO("[SceneTemplate] F7: animation state reset");
    return true;
  }

  t850::PhysicsRagdollDesc pose;
  if (!t850::BuildRagdollPoseFromAnimation(*skinned, Meshes[0].Final, m_ragdollAnimationBinding, pose)) {
    return ApplyRagdollEditPose(true);
  }

  if (!Meshes[0].HasPhysicsRagdoll()) {
    const bool recreated = RecreateRagdollFromPose(pose);
    if (recreated) {
      T8_LOG_INFO("[SceneTemplate] F7: animation and ragdoll reset");
    }
    return recreated;
  }

  engineContext->physics->SetRagdollMotion(Meshes[0].GetPhysicsRagdoll(), t850::PhysicsBodyMotion::Kinematic);
  engineContext->physics->SetRagdollVelocity(
      Meshes[0].GetPhysicsRagdoll(),
      XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f),
      XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f));
  const bool driven = engineContext->physics->DriveRagdollFromPose(Meshes[0].GetPhysicsRagdoll(), pose, 0.0f);
  if (driven) {
    m_ragdollAnimationPose = std::move(pose);
    T8_LOG_INFO("[SceneTemplate] F7: animation and ragdoll reset");
  }
  return driven;
}

void SceneTemplate::LogRagdollFloorDiagnostics(const char* stage) {
  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (!engineContext || !engineContext->physics || !engineContext->physics->IsInitialized()) {
    return;
  }

  std::vector<t850::PhysicsDebugBody> bodies;
  if (!engineContext->physics->GetDebugBodies(bodies)) {
    return;
  }

  auto boxVerticalExtent = [](const XMATRIX44& transform, const XVECTOR3& halfExtents) {
    return std::fabs(transform.m12) * halfExtents.x +
           std::fabs(transform.m22) * halfExtents.y +
           std::fabs(transform.m32) * halfExtents.z;
  };
  auto capsuleVerticalExtent = [](const XMATRIX44& transform, const t850::PhysicsShapeDesc& shape) {
    return shape.radius + std::fabs(transform.m22) * shape.halfHeight;
  };
  auto shapeVerticalExtent = [&](const XMATRIX44& transform, const t850::PhysicsShapeDesc& shape) {
    if (shape.type == t850::PhysicsShapeType::Box) {
      return boxVerticalExtent(transform, ClampRagdollBoxHalfExtents(shape.halfExtents));
    }
    return capsuleVerticalExtent(transform, shape);
  };

  bool hasFloor = false;
  float floorTop = 0.0f;
  float floorCenterY = 0.0f;
  float floorHalfHeight = 0.0f;
  for (const t850::PhysicsDebugBody& body : bodies) {
    if (body.debugName == "Sandbox ragdoll floor" && body.shape.type == t850::PhysicsShapeType::Box) {
      floorCenterY = body.state.worldTransform.m42;
      floorHalfHeight = boxVerticalExtent(body.state.worldTransform, body.shape.halfExtents);
      floorTop = floorCenterY + floorHalfHeight;
      hasFloor = true;
      break;
    }
  }
  if (!hasFloor) {
    T8_LOG_INFO("[RagdollFloor] %s: no static ragdoll floor body is present", stage ? stage : "unknown");
    return;
  }

  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;
  const uint32_t entityId = Meshes[0].GetEntityId();
  int bodyCount = 0;
  int lowestBone = -1;
  float lowestMinY = (std::numeric_limits<float>::max)();
  float lowestCenterY = 0.0f;
  float lowestRadius = 0.0f;
  float lowestHalfHeight = 0.0f;
  float highestMaxY = -(std::numeric_limits<float>::max)();

  for (const t850::PhysicsDebugBody& body : bodies) {
    if (body.state.entityId != entityId ||
        body.state.boneIndex < 0 ||
        !IsEditableRagdollShape(body.shape)) {
      continue;
    }

    const float extentY = shapeVerticalExtent(body.state.worldTransform, body.shape);
    const float minY = body.state.worldTransform.m42 - extentY;
    const float maxY = body.state.worldTransform.m42 + extentY;
    ++bodyCount;
    if (minY < lowestMinY) {
      lowestMinY = minY;
      lowestCenterY = body.state.worldTransform.m42;
      const auto comparableExtents = RagdollShapeComparableExtents(body.shape);
      lowestRadius = (comparableExtents[0] + comparableExtents[2]) * 0.5f;
      lowestHalfHeight = comparableExtents[1];
      lowestBone = body.state.boneIndex;
    }
    highestMaxY = (std::max)(highestMaxY, maxY);

  }

  const char* lowestBoneName =
      skeleton && lowestBone >= 0 && lowestBone < static_cast<int>(skeleton->Bones.size())
          ? skeleton->Bones[static_cast<std::size_t>(lowestBone)].Name.c_str()
          : "<unknown>";
  T8_LOG_INFO("[RagdollFloor] %s summary: floorTop=%.3f floorCenterY=%.3f floorHalfHeight=%.3f bodies=%d minBodyY=%.3f maxBodyY=%.3f lowestBone=%d '%s' lowestCenterY=%.3f clearance=%.3f lowestRadius=%.3f lowestHalfExtentY=%.3f",
              stage ? stage : "unknown",
              floorTop,
              floorCenterY,
              floorHalfHeight,
              bodyCount,
              lowestMinY,
              highestMaxY,
              lowestBone,
              lowestBoneName,
              lowestCenterY,
              lowestMinY - floorTop,
              lowestRadius,
              lowestHalfHeight);
}

void SceneTemplate::CreatePhysicsFloor(t850::JoltPhysicsSystem& physics) {
  if (m_floorBody.IsValid() || !Meshes[0].pBase) {
    return;
  }

  RenderMesh* mesh = static_cast<RenderMesh*>(Meshes[0].pBase);
  RenderMesh::AABB worldBounds;
  if (!BuildWorldBounds(mesh, Meshes[0].Final, worldBounds)) {
    T8_LOG_ERROR("[SceneTemplate] Failed to build physics floor: model bounds are unavailable");
    return;
  }
  RenderMesh::AABB modelFloorBounds = worldBounds;
  const bool hasSkinnedBounds = BuildSkinnedWorldBounds(Meshes[0].GetSkinnedMesh(), Meshes[0].Final, modelFloorBounds);

  RenderMesh::AABB floorBounds;
  floorBounds.Reset();
  ExpandBounds(floorBounds, modelFloorBounds);
  RenderMesh::AABB ragdollBounds;
  const bool hasRagdollBounds =
      BuildRagdollCapsuleBounds(m_ragdollAnimationPose, ragdollBounds) ||
      BuildRagdollCapsuleBounds(m_ragdollAnimationBinding.referencePose, ragdollBounds);
  if (hasRagdollBounds) {
    ExpandBounds(floorBounds, ragdollBounds);
  }

  const float extentX = (floorBounds.max.x - floorBounds.min.x) * 0.5f;
  const float extentZ = (floorBounds.max.z - floorBounds.min.z) * 0.5f;
  constexpr float kRagdollFloorAreaScale = 3.0f;
  const float baseHalfSize = (std::max)((std::max)(extentX, extentZ) * 2.0f, (std::max)(1.0f, m_modelRadius * 2.0f));
  const float halfSize = baseHalfSize * std::sqrt(kRagdollFloorAreaScale);
  const float halfHeight = (std::max)(0.05f, m_modelRadius * 0.04f);
  const float floorSourceMinY = modelFloorBounds.min.y;

  XMATRIX44 floorTransform;
  floorTransform.Identity();
  floorTransform.m41 = (floorBounds.min.x + floorBounds.max.x) * 0.5f;
  floorTransform.m42 = floorSourceMinY - halfHeight;
  floorTransform.m43 = (floorBounds.min.z + floorBounds.max.z) * 0.5f;

  t850::PhysicsBodyDesc floorDesc;
  floorDesc.entityId = Meshes[0].GetEntityId();
  floorDesc.debugName = "Sandbox ragdoll floor";
  floorDesc.shape = t850::PhysicsShapeDesc::Box(XVECTOR3(halfSize, halfHeight, halfSize, 0.0f));
  floorDesc.worldTransform = floorTransform;
  floorDesc.motion = t850::PhysicsBodyMotion::Static;
  floorDesc.friction = 0.85f;
  floorDesc.restitution = 0.05f;

  m_floorBody = physics.CreateBody(floorDesc);
  if (m_floorBody.IsValid()) {
    T8_LOG_INFO("[SceneTemplate] Added static ragdoll floor top y=%.3f source=%s sourceMinY=%.3f meshMinY=%.3f skinnedMinY=%.3f ragdollMinY=%.3f halfSize=%.3f halfHeight=%.3f areaScale=%.1f",
                floorTransform.m42 + halfHeight,
                hasSkinnedBounds ? "skinned-mesh" : "mesh",
                floorSourceMinY,
                worldBounds.min.y,
                hasSkinnedBounds ? modelFloorBounds.min.y : worldBounds.min.y,
                hasRagdollBounds ? ragdollBounds.min.y : worldBounds.min.y,
                halfSize,
                halfHeight,
                kRagdollFloorAreaScale);
  } else {
    T8_LOG_ERROR("[SceneTemplate] Failed to create static ragdoll floor");
  }
}

std::string SceneTemplate::BuildSkeletonEditSavePath() const {
  const std::string key = FileSafeModelKey(m_profileModelKey.empty() ? SandboxProfileModelKey(ActiveModelPath()) : m_profileModelKey);
  return t850::ResourceLocator::Instance().ResolveCachePath("SkeletonEdits/" + key + ".json").string();
}

bool SceneTemplate::EnterSkeletonEditMode() {
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  if (!skinned || !skinned->HasSkinData()) {
    T8_LOG_ERROR("[SkeletonEdit] Cannot enter edit mode: active model has no skinned skeleton");
    return false;
  }

  if (m_ragdollPhysicsDriven) {
    t850::EngineContext* engineContext = GetEngineContext();
    if (!engineContext) engineContext = &t850::GetEngineContext();
    if (engineContext && engineContext->physics && Meshes[0].HasPhysicsRagdoll()) {
      engineContext->physics->SetRagdollMotion(Meshes[0].GetPhysicsRagdoll(), t850::PhysicsBodyMotion::Kinematic);
      engineContext->physics->SetRagdollVelocity(
          Meshes[0].GetPhysicsRagdoll(),
          XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f),
          XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f));
    }
    m_ragdollPhysicsDriven = false;
  }

  m_skeletonEditWasPlaying = skinned->IsPlaying();
  skinned->PauseAnimation();
  skinned->ClearSnapshotBoneMatrices();
  T8_LOG_INFO("[SkeletonEdit] Moving '%s' to bind pose", ActiveModelPath().c_str());
  if (!skinned->GetAnimController().ApplyBindPose() ||
      !skinned->GetAnimController().ExportCombinedPose(m_skeletonEditBindCombined)) {
    T8_LOG_ERROR("[SkeletonEdit] Failed to move '%s' to bind pose", ActiveModelPath().c_str());
    return false;
  }
  T8_LOG_INFO("[SkeletonEdit] Captured bind pose: bones=%zu", m_skeletonEditBindCombined.size());

  m_skeletonEditCombined = m_skeletonEditBindCombined;
  m_skeletonEditSavePath = BuildSkeletonEditSavePath();
  m_skeletonPreviewBoneActive = false;
  m_skeletonPreviewBoneIndex = -1;
  m_skeletonPreviewOriginalCombined.clear();
  SelectSkeletonEditBone(m_skeletonEditCombined.empty() ? -1 : 0);
  m_skeletonEditDragging = false;
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  m_skeletonEditDirty = false;
  m_skeletonEditPrevShowSkeleton = m_showSkeleton;
  m_skeletonEditPrevShowPhysics = m_showPhysics;
  m_skeletonEditMode = true;
  m_showSkeleton = true;
  m_showPhysics = true;
  T8_LOG_INFO("[SkeletonEdit] Applying edit pose");
  LoadSkeletonEditPose();
  ApplySkeletonEditPose();
  if (!m_ragdollEditDirty) {
    LoadRagdollEditPose();
  } else if (!m_ragdollAnimationBinding.referencePose.bones.empty()) {
    for (int capsuleIndex = 0; capsuleIndex < static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size()); ++capsuleIndex) {
      UpdateRagdollReferenceBodyFromLocal(capsuleIndex);
    }
    ApplyRagdollEditPose(true);
  }
  T8_LOG_INFO("[SkeletonEdit] Entered bind-pose edit mode for '%s'", ActiveModelPath().c_str());
  return true;
}

void SceneTemplate::ExitSkeletonEditMode() {
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  RestoreSkeletonPreviewBone();
  m_skeletonEditMode = false;
  m_skeletonEditDragging = false;
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  m_showSkeleton = m_skeletonEditPrevShowSkeleton;
  m_showPhysics = m_skeletonEditPrevShowPhysics;
  if (skinned && m_skeletonEditWasPlaying) {
    skinned->PlayAnimation();
  }
  T8_LOG_INFO("[SkeletonEdit] Exited edit mode");
}

bool SceneTemplate::ApplySkeletonEditPose() {
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  if (!skinned || !skinned->HasSkinData() || m_skeletonEditCombined.empty()) {
    return false;
  }

  std::vector<int> boneIndices;
  boneIndices.reserve(m_skeletonEditCombined.size());
  for (std::size_t i = 0; i < m_skeletonEditCombined.size(); ++i) {
    boneIndices.push_back(static_cast<int>(i));
  }
  if (!skinned->GetAnimController().ApplyCombinedPoseOverrides(boneIndices, m_skeletonEditCombined)) {
    return false;
  }
  m_ragdollDriveLogEmitted = false;
  return true;
}

bool SceneTemplate::ResetSkeletonEditPose() {
  if (m_skeletonEditBindCombined.empty()) {
    return false;
  }
  m_skeletonPreviewBoneActive = false;
  m_skeletonPreviewBoneIndex = -1;
  m_skeletonPreviewOriginalCombined.clear();
  m_skeletonEditCombined = m_skeletonEditBindCombined;
  m_skeletonEditDirty = true;
  return ApplySkeletonEditPose();
}

bool SceneTemplate::LoadSkeletonEditPose() {
  if (m_skeletonEditSavePath.empty()) {
    m_skeletonEditSavePath = BuildSkeletonEditSavePath();
  }

  std::ifstream file(m_skeletonEditSavePath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return false;
  }

  const std::streamsize size = file.tellg();
  if (size < 0) {
    T8_LOG_ERROR("[SkeletonEdit] Failed to read '%s'", m_skeletonEditSavePath.c_str());
    return false;
  }
  file.seekg(0, std::ios::beg);
  std::string json(static_cast<std::size_t>(size), '\0');
  if (size > 0 && !file.read(json.data(), size)) {
    T8_LOG_ERROR("[SkeletonEdit] Failed to read '%s'", m_skeletonEditSavePath.c_str());
    return false;
  }

  SkeletonEditJson data;
  if (!ParseSkeletonEditJson(json, data)) {
    T8_LOG_ERROR("[SkeletonEdit] Failed to parse '%s'", m_skeletonEditSavePath.c_str());
    return false;
  }

  const xF::xSkeleton* skeleton = Meshes[0].GetSkinnedMesh()
      ? Meshes[0].GetSkinnedMesh()->GetAnimController().GetAnimSkeleton()
      : nullptr;
  if (!skeleton || m_skeletonEditCombined.empty()) {
    return false;
  }

  RestoreSkeletonPreviewBone();
  int applied = 0;
  for (const SkeletonEditBoneJson& bone : data.bones) {
    int target = -1;
    if (bone.index >= 0 && bone.index < static_cast<int>(m_skeletonEditCombined.size()) &&
        bone.index < static_cast<int>(skeleton->Bones.size()) &&
        (bone.name.empty() || skeleton->Bones[bone.index].Name == bone.name)) {
      target = bone.index;
    } else if (!bone.name.empty()) {
      for (int i = 0; i < static_cast<int>(skeleton->Bones.size()) && i < static_cast<int>(m_skeletonEditCombined.size()); ++i) {
        if (skeleton->Bones[i].Name == bone.name) {
          target = i;
          break;
        }
      }
    }
    if (target >= 0) {
      m_skeletonEditCombined[static_cast<std::size_t>(target)] = MatrixFromArray16(bone.combined);
      ++applied;
    }
  }

  m_skeletonEditDirty = false;
  if (applied > 0) {
    ApplySkeletonEditPose();
    T8_LOG_INFO("[SkeletonEdit] Loaded %d edited bones from '%s'", applied, m_skeletonEditSavePath.c_str());
  }
  return applied > 0;
}

bool SceneTemplate::SaveSkeletonEditPose() {
  if (m_skeletonEditSavePath.empty()) {
    m_skeletonEditSavePath = BuildSkeletonEditSavePath();
  }
  if (m_skeletonEditCombined.empty() || m_skeletonEditBindCombined.size() != m_skeletonEditCombined.size()) {
    return false;
  }
  RestoreSkeletonPreviewBone();

  const xF::xSkeleton* skeleton = Meshes[0].GetSkinnedMesh()
      ? Meshes[0].GetSkinnedMesh()->GetAnimController().GetAnimSkeleton()
      : nullptr;

  SkeletonEditJson data;
  data.model = m_profileModelKey.empty() ? SandboxProfileModelKey(ActiveModelPath()) : m_profileModelKey;
  for (std::size_t i = 0; i < m_skeletonEditCombined.size(); ++i) {
    const bool usePreviewOriginal =
        m_skeletonPreviewBoneActive &&
        i < m_skeletonPreviewOriginalCombined.size();
    const XMATRIX44& combined = usePreviewOriginal
        ? m_skeletonPreviewOriginalCombined[i]
        : m_skeletonEditCombined[i];
    if (MatrixMaxAbsDiff(combined, m_skeletonEditBindCombined[i]) <= 0.00001f) {
      continue;
    }
    SkeletonEditBoneJson bone;
    bone.index = static_cast<int>(i);
    if (skeleton && i < skeleton->Bones.size()) {
      bone.name = skeleton->Bones[i].Name;
    }
    bone.combined = MatrixToArray16(combined);
    data.bones.push_back(std::move(bone));
  }

  const std::filesystem::path path(m_skeletonEditSavePath);
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    T8_LOG_ERROR("[SkeletonEdit] Failed to create '%s'", path.parent_path().string().c_str());
    return false;
  }

  std::ofstream file(path, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    T8_LOG_ERROR("[SkeletonEdit] Failed to open '%s' for writing", m_skeletonEditSavePath.c_str());
    return false;
  }
  file << "{\n";
  file << "  \"model\": \"" << JsonEscape(data.model) << "\",\n";
  file << "  \"bones\": [\n";
  file << std::fixed << std::setprecision(8);
  for (std::size_t boneIndex = 0; boneIndex < data.bones.size(); ++boneIndex) {
    const SkeletonEditBoneJson& bone = data.bones[boneIndex];
    file << "    {\n";
    file << "      \"index\": " << bone.index << ",\n";
    file << "      \"name\": \"" << JsonEscape(bone.name) << "\",\n";
    file << "      \"combined\": [";
    for (std::size_t valueIndex = 0; valueIndex < bone.combined.size(); ++valueIndex) {
      if (valueIndex > 0) file << ", ";
      file << bone.combined[valueIndex];
    }
    file << "]\n";
    file << "    }" << (boneIndex + 1 < data.bones.size() ? "," : "") << "\n";
  }
  file << "  ]\n";
  file << "}\n";
  m_skeletonEditDirty = false;
  T8_LOG_INFO("[SkeletonEdit] Saved %zu edited bones to '%s'", data.bones.size(), m_skeletonEditSavePath.c_str());
  return true;
}

std::string SceneTemplate::BuildRagdollEditSavePath() const {
  if (!m_primaryRagdollResourcePath.empty()) {
    return m_primaryRagdollResourcePath;
  }
  return t850::BuildRagdollEditResourcePath(
      m_profileModelKey.empty() ? ActiveModelPath() : m_profileModelKey);
}

int SceneTemplate::FindRagdollCapsuleForBone(int boneIndex) const {
  return t850::ragdoll_editor::FindBodyForBone(m_ragdollAnimationBinding, boneIndex);
}

int SceneTemplate::FindRagdollCapsuleControllingBone(int boneIndex) const {
  return t850::ragdoll_editor::FindBodyControllingBone(m_ragdollAnimationBinding, boneIndex);
}

void SceneTemplate::EnsureRagdollControlledBones() {
  t850::ragdoll_editor::EnsureControlledBones(m_ragdollAnimationBinding);
}

void SceneTemplate::SelectRagdollEditCapsule(int capsuleIndex, bool syncBoneSelection) {
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (capsuleIndex < 0 || capsuleIndex >= static_cast<int>(bones.size())) {
    m_ragdollEditSelectedCapsule = -1;
    m_ragdollEditSelectedJoint = -1;
    m_ragdollEditSelectedParentCapsule = -1;
    m_ragdollEditSelectedJointParentCapsule = -1;
    m_ragdollEditSelectedHandle = -1;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    m_skeletonEditDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_ragdollEditJointAxis = -1;
    m_ragdollEditRenamingCapsule = -1;
    m_ragdollEditRenameFocusPending = false;
    m_ragdollEditSelectedUnassignedBone = -1;
    m_ragdollEditSelectedAffectedBone = -1;
    if (m_ragdollBoneSelectionActive) {
      if (m_ragdollBoneSelectionPreviousSelectionMode != kRagdollSelectBones) {
        RestoreSkeletonPreviewBone();
      }
      m_ragdollEditSelectionMode = m_ragdollBoneSelectionPreviousSelectionMode;
      m_ragdollEditGizmoMode = m_ragdollBoneSelectionPreviousGizmoMode;
    }
    m_ragdollBoneSelectionActive = false;
    m_ragdollBoneMarqueeDragging = false;
    m_ragdollBoneSelectionPending.clear();
    return;
  }

  m_ragdollEditSelectedCapsule = capsuleIndex;
  const int parentCapsule =
      capsuleIndex < static_cast<int>(m_ragdollParentCapsules.size())
          ? m_ragdollParentCapsules[static_cast<std::size_t>(capsuleIndex)]
          : -1;
  const int jointParentCapsule = GetRagdollEffectiveJointParentCapsule(capsuleIndex);
  m_ragdollEditSelectedParentCapsule = parentCapsule;
  m_ragdollEditSelectedJointParentCapsule = jointParentCapsule >= 0 ? jointParentCapsule : parentCapsule;
  m_ragdollEditSelectedHandle = 0;
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_skeletonEditDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  m_ragdollEditRenamingCapsule = -1;
  m_ragdollEditRenameFocusPending = false;
  m_ragdollEditSelectedUnassignedBone = -1;
    m_ragdollEditSelectedAffectedBone = -1;
    if (m_ragdollBoneSelectionActive) {
      if (m_ragdollBoneSelectionPreviousSelectionMode != kRagdollSelectBones) {
        RestoreSkeletonPreviewBone();
      }
      m_ragdollEditSelectionMode = m_ragdollBoneSelectionPreviousSelectionMode;
      m_ragdollEditGizmoMode = m_ragdollBoneSelectionPreviousGizmoMode;
  }
  m_ragdollBoneSelectionActive = false;
  m_ragdollBoneMarqueeDragging = false;
  m_ragdollBoneSelectionPending.clear();
  if (GetRagdollEffectiveJointParentCapsule(capsuleIndex) >= 0) {
    m_ragdollEditSelectedJoint = capsuleIndex;
  } else {
    m_ragdollEditSelectedJoint = -1;
  }
  const int boneIndex = bones[static_cast<std::size_t>(capsuleIndex)].body.boneIndex;
  if (syncBoneSelection &&
      boneIndex >= 0 &&
      boneIndex < static_cast<int>(m_skeletonEditCombined.size())) {
    SelectSkeletonEditBone(boneIndex);
  }
}

void SceneTemplate::SyncRagdollParentCapsulesFromBoneLinks() {
  t850::ragdoll_editor::SyncParentBodiesFromBoneLinks(m_ragdollAnimationBinding, m_ragdollParentCapsules);
}

void SceneTemplate::EnsureRagdollParentCapsules() {
  t850::ragdoll_editor::EnsureParentBodies(m_ragdollAnimationBinding, m_ragdollParentCapsules);
}

void SceneTemplate::EnsureRagdollJointState() {
  const std::vector<int> newJointOffsets = t850::ragdoll_editor::EnsureJointState(
      m_ragdollAnimationBinding,
      m_ragdollParentCapsules,
      m_ragdollJointParentCapsules,
      m_ragdollContactJoints);
  for (int capsuleIndex : newJointOffsets) {
    UpdateRagdollJointOffsetFromWorld(capsuleIndex);
  }
}

void SceneTemplate::EnsureRagdollFreezeState() {
  t850::ragdoll_editor::EnsureFreezeState(
      m_ragdollAnimationBinding.referencePose.bones.size(),
      m_ragdollFrozenCapsules,
      m_ragdollFrozenJoints);
}

bool SceneTemplate::IsRagdollCapsuleFrozen(int capsuleIndex) const {
  return t850::ragdoll_editor::IsFrozen(m_ragdollFrozenCapsules, capsuleIndex);
}

bool SceneTemplate::IsRagdollJointFrozen(int childCapsule) const {
  return t850::ragdoll_editor::IsFrozen(m_ragdollFrozenJoints, childCapsule);
}

void SceneTemplate::SetRagdollCapsuleFrozen(int capsuleIndex, bool frozen) {
  EnsureRagdollFreezeState();
  if (!t850::ragdoll_editor::SetFrozen(m_ragdollFrozenCapsules, capsuleIndex, frozen)) {
    return;
  }
  if (frozen && m_ragdollEditSelectedCapsule == capsuleIndex) {
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditGizmoAxis = -1;
  }
  m_ragdollEditDirty = true;
}

void SceneTemplate::SetRagdollJointFrozen(int childCapsule, bool frozen) {
  EnsureRagdollFreezeState();
  if (!t850::ragdoll_editor::SetFrozen(m_ragdollFrozenJoints, childCapsule, frozen)) {
    return;
  }
  if (frozen && m_ragdollEditSelectedJoint == childCapsule) {
    m_ragdollEditJointDragging = false;
    m_ragdollEditJointAxis = -1;
  }
  m_ragdollEditDirty = true;
}

int SceneTemplate::GetRagdollEffectiveJointParentCapsule(int childCapsule) const {
  return t850::ragdoll_editor::EffectiveJointParent(
      childCapsule,
      m_ragdollAnimationBinding.referencePose.bones.size(),
      m_ragdollParentCapsules,
      m_ragdollJointParentCapsules);
}

bool SceneTemplate::UpdateRagdollJointOffsetFromWorld(int childCapsule) {
  auto& binding = m_ragdollAnimationBinding;
  if (childCapsule < 0 ||
      childCapsule >= static_cast<int>(binding.referencePose.bones.size())) {
    return false;
  }
  if (binding.jointFromBone.size() != binding.referencePose.bones.size()) {
    binding.jointFromBone.resize(binding.referencePose.bones.size(), XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
  }

  XMATRIX44 boneWorld;
  if (!GetRagdollAuthoringBoneWorldTransform(binding.referencePose.bones[static_cast<std::size_t>(childCapsule)].body.boneIndex, boneWorld)) {
    binding.jointFromBone[static_cast<std::size_t>(childCapsule)] = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    return false;
  }
  XMATRIX44 inverseBoneWorld;
  if (!InvertAffineNoExit(boneWorld, inverseBoneWorld)) {
    T8_LOG_ERROR("[RagdollEdit] Cannot update joint offset for capsule %d: bone frame is singular", childCapsule);
    return false;
  }
  binding.jointFromBone[static_cast<std::size_t>(childCapsule)] =
      t850::TransformPoint(binding.referencePose.bones[static_cast<std::size_t>(childCapsule)].jointWorldPosition,
                           inverseBoneWorld);
  return true;
}

bool SceneTemplate::UpdateRagdollJointFrameOffsetsFromWorld(int childCapsule) {
  auto& binding = m_ragdollAnimationBinding;
  auto& bones = binding.referencePose.bones;
  if (childCapsule < 0 || childCapsule >= static_cast<int>(bones.size())) {
    return false;
  }
  EnsureRagdollJointState();

  const int parentCapsule = GetRagdollEffectiveJointParentCapsule(childCapsule);
  const XMATRIX44& childWorld = bones[static_cast<std::size_t>(childCapsule)].body.worldTransform;
  const XMATRIX44& parentWorld =
      parentCapsule >= 0 && parentCapsule < static_cast<int>(bones.size())
          ? bones[static_cast<std::size_t>(parentCapsule)].body.worldTransform
          : childWorld;

  auto& bone = bones[static_cast<std::size_t>(childCapsule)];
  XVECTOR3 parentTwist = bone.parentJointTwistAxis;
  XVECTOR3 parentPlane = bone.parentJointPlaneAxis;
  XVECTOR3 childTwist = bone.childJointTwistAxis;
  XVECTOR3 childPlane = bone.childJointPlaneAxis;
  NormalizeRagdollJointFrameAxes(parentTwist, parentPlane, MatrixAxisY(parentWorld), MatrixAxisX(parentWorld));
  NormalizeRagdollJointFrameAxes(childTwist, childPlane, MatrixAxisY(childWorld), MatrixAxisX(childWorld));
  bone.parentJointTwistAxis = parentTwist;
  bone.parentJointPlaneAxis = parentPlane;
  bone.childJointTwistAxis = childTwist;
  bone.childJointPlaneAxis = childPlane;

  XMATRIX44 inverseParentWorld;
  XMATRIX44 inverseChildWorld;
  if (!InvertAffineNoExit(parentWorld, inverseParentWorld) ||
      !InvertAffineNoExit(childWorld, inverseChildWorld)) {
    T8_LOG_ERROR("[RagdollEdit] Cannot update joint frame offsets for capsule %d: body frame is singular", childCapsule);
    return false;
  }

  binding.parentJointTwistFromBody[static_cast<std::size_t>(childCapsule)] =
      TransformVectorNoTranslation(parentTwist, inverseParentWorld);
  binding.parentJointPlaneFromBody[static_cast<std::size_t>(childCapsule)] =
      TransformVectorNoTranslation(parentPlane, inverseParentWorld);
  binding.childJointTwistFromBody[static_cast<std::size_t>(childCapsule)] =
      TransformVectorNoTranslation(childTwist, inverseChildWorld);
  binding.childJointPlaneFromBody[static_cast<std::size_t>(childCapsule)] =
      TransformVectorNoTranslation(childPlane, inverseChildWorld);
  return true;
}

void SceneTemplate::UpdateRagdollJointFrameOffsetsForBody(int capsuleIndex) {
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (capsuleIndex < 0 || capsuleIndex >= static_cast<int>(bones.size())) {
    return;
  }
  EnsureRagdollJointState();
  for (int childCapsule = 0; childCapsule < static_cast<int>(bones.size()); ++childCapsule) {
    const int parentCapsule = GetRagdollEffectiveJointParentCapsule(childCapsule);
    if (childCapsule == capsuleIndex || parentCapsule == capsuleIndex) {
      UpdateRagdollJointFrameOffsetsFromWorld(childCapsule);
    }
  }
}

bool SceneTemplate::ResetRagdollJointFrameToBodyAxes(int childCapsule) {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollJointState();
  if (childCapsule < 0 || childCapsule >= static_cast<int>(bones.size())) {
    return false;
  }
  const int parentCapsule = GetRagdollEffectiveJointParentCapsule(childCapsule);
  const XMATRIX44& childWorld = bones[static_cast<std::size_t>(childCapsule)].body.worldTransform;
  const XMATRIX44& parentWorld =
      parentCapsule >= 0 && parentCapsule < static_cast<int>(bones.size())
          ? bones[static_cast<std::size_t>(parentCapsule)].body.worldTransform
          : childWorld;

  auto& bone = bones[static_cast<std::size_t>(childCapsule)];
  bone.parentJointTwistAxis = MatrixAxisY(parentWorld);
  bone.parentJointPlaneAxis = MatrixAxisX(parentWorld);
  bone.childJointTwistAxis = MatrixAxisY(childWorld);
  bone.childJointPlaneAxis = MatrixAxisX(childWorld);
  return UpdateRagdollJointFrameOffsetsFromWorld(childCapsule);
}

void SceneTemplate::EnsureRagdollJointFrames() {
  auto& binding = m_ragdollAnimationBinding;
  auto& bones = binding.referencePose.bones;
  EnsureRagdollJointState();
  for (int childCapsule = 0; childCapsule < static_cast<int>(bones.size()); ++childCapsule) {
    const int parentCapsule = GetRagdollEffectiveJointParentCapsule(childCapsule);
    const XMATRIX44& childWorld = bones[static_cast<std::size_t>(childCapsule)].body.worldTransform;
    const XMATRIX44& parentWorld =
        parentCapsule >= 0 && parentCapsule < static_cast<int>(bones.size())
            ? bones[static_cast<std::size_t>(parentCapsule)].body.worldTransform
            : childWorld;
    auto& bone = bones[static_cast<std::size_t>(childCapsule)];
    if (!IsValidRagdollAxis(bone.parentJointTwistAxis)) bone.parentJointTwistAxis = MatrixAxisY(parentWorld);
    if (!IsValidRagdollAxis(bone.parentJointPlaneAxis)) bone.parentJointPlaneAxis = MatrixAxisX(parentWorld);
    if (!IsValidRagdollAxis(bone.childJointTwistAxis)) bone.childJointTwistAxis = MatrixAxisY(childWorld);
    if (!IsValidRagdollAxis(bone.childJointPlaneAxis)) bone.childJointPlaneAxis = MatrixAxisX(childWorld);
    UpdateRagdollJointFrameOffsetsFromWorld(childCapsule);
  }
}

bool SceneTemplate::ApplyRagdollParentCapsuleLinks() {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollJointState();
  if (m_ragdollParentCapsules.size() != bones.size()) {
    return false;
  }

  for (std::size_t childIndex = 0; childIndex < bones.size(); ++childIndex) {
    int parentCapsule = GetRagdollEffectiveJointParentCapsule(static_cast<int>(childIndex));
    bool invalidParent = parentCapsule < 0 ||
        parentCapsule >= static_cast<int>(bones.size()) ||
        parentCapsule == static_cast<int>(childIndex);
    int current = parentCapsule;
    for (std::size_t depth = 0; !invalidParent && depth < bones.size(); ++depth) {
      if (current == static_cast<int>(childIndex)) {
        invalidParent = true;
        break;
      }
      if (current < 0 || current >= static_cast<int>(m_ragdollParentCapsules.size())) {
        break;
      }
      current = GetRagdollEffectiveJointParentCapsule(current);
    }

    if (invalidParent) {
      bones[childIndex].parentBoneIndex = -1;
      continue;
    }
    bones[childIndex].parentBoneIndex = bones[static_cast<std::size_t>(parentCapsule)].body.boneIndex;
  }
  return true;
}

bool SceneTemplate::SetRagdollCapsuleParent(int childCapsule, int parentCapsule) {
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollJointState();
  EnsureRagdollFreezeState();
  if (childCapsule < 0 || childCapsule >= static_cast<int>(bones.size()) ||
      parentCapsule < 0 || parentCapsule >= static_cast<int>(bones.size()) ||
      childCapsule == parentCapsule ||
      m_ragdollParentCapsules.size() != bones.size()) {
    return false;
  }
  if (IsRagdollCapsuleFrozen(childCapsule)) {
    T8_LOG_INFO("[RagdollEdit] Capsule %d is frozen; unfreeze it before changing its parent", childCapsule);
    return false;
  }

  int current = parentCapsule;
  for (std::size_t depth = 0; depth < m_ragdollParentCapsules.size(); ++depth) {
    if (current == childCapsule) {
      T8_LOG_ERROR("[RagdollEdit] Refusing cyclic parent link: capsule %d -> %d", childCapsule, parentCapsule);
      return false;
    }
    if (current < 0 || current >= static_cast<int>(m_ragdollParentCapsules.size())) {
      break;
    }
    current = m_ragdollParentCapsules[static_cast<std::size_t>(current)];
  }

  if (m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)] == parentCapsule) {
    return true;
  }

  m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)] = parentCapsule;
  if (childCapsule < static_cast<int>(m_ragdollContactJoints.size())) {
    m_ragdollContactJoints[static_cast<std::size_t>(childCapsule)] = 0u;
  }
  if (childCapsule < static_cast<int>(m_ragdollJointParentCapsules.size()) &&
      m_ragdollJointParentCapsules[static_cast<std::size_t>(childCapsule)] == kRagdollJointDisabled) {
    m_ragdollJointParentCapsules[static_cast<std::size_t>(childCapsule)] = kRagdollJointInheritParent;
  }
  if (!ApplyRagdollParentCapsuleLinks()) {
    return false;
  }
  ResetRagdollJointFrameToBodyAxes(childCapsule);
  m_ragdollEditDirty = true;
  m_ragdollEditSelectedJoint = childCapsule;
  T8_LOG_INFO("[RagdollEdit] Capsule %d parent set to capsule %d", childCapsule, parentCapsule);
  return ApplyRagdollEditPose(true);
}

bool SceneTemplate::ClearRagdollCapsuleParent(int childCapsule) {
  EnsureRagdollJointState();
  EnsureRagdollFreezeState();
  if (childCapsule < 0 ||
      childCapsule >= static_cast<int>(m_ragdollParentCapsules.size())) {
    return false;
  }
  if (IsRagdollCapsuleFrozen(childCapsule)) {
    T8_LOG_INFO("[RagdollEdit] Capsule %d is frozen; unfreeze it before clearing its parent", childCapsule);
    return false;
  }
  if (m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)] < 0) {
    return true;
  }

  m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)] = -1;
  if (childCapsule < static_cast<int>(m_ragdollContactJoints.size())) {
    m_ragdollContactJoints[static_cast<std::size_t>(childCapsule)] = 0u;
  }
  if (!ApplyRagdollParentCapsuleLinks()) {
    return false;
  }
  if (m_ragdollEditSelectedJoint == childCapsule) {
    m_ragdollEditSelectedJoint = -1;
  }
  m_ragdollEditDirty = true;
  T8_LOG_INFO("[RagdollEdit] Capsule %d parent cleared", childCapsule);
  return ApplyRagdollEditPose(true);
}

bool SceneTemplate::SetRagdollCapsuleJoint(int childCapsule, int parentCapsule) {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollJointState();
  EnsureRagdollFreezeState();
  if (childCapsule < 0 || childCapsule >= static_cast<int>(bones.size()) ||
      parentCapsule < 0 || parentCapsule >= static_cast<int>(bones.size()) ||
      childCapsule == parentCapsule ||
      m_ragdollJointParentCapsules.size() != bones.size()) {
    return false;
  }
  if (IsRagdollCapsuleFrozen(childCapsule) || IsRagdollJointFrozen(childCapsule)) {
    T8_LOG_INFO("[RagdollEdit] Capsule/joint %d is frozen; unfreeze it before changing its joint", childCapsule);
    return false;
  }

  int current = parentCapsule;
  for (std::size_t depth = 0; depth < bones.size(); ++depth) {
    if (current == childCapsule) {
      T8_LOG_ERROR("[RagdollEdit] Refusing cyclic joint link: capsule %d -> %d", childCapsule, parentCapsule);
      return false;
    }
    if (current < 0 || current >= static_cast<int>(bones.size())) {
      break;
    }
    current = GetRagdollEffectiveJointParentCapsule(current);
  }

  XMATRIX44 childWorld;
  XMATRIX44 parentWorld;
  if (GetCurrentRagdollEditCapsuleWorld(childCapsule, childWorld) &&
      GetCurrentRagdollEditCapsuleWorld(parentCapsule, parentWorld)) {
    bones[static_cast<std::size_t>(childCapsule)].jointWorldPosition =
        XVECTOR3((childWorld.m41 + parentWorld.m41) * 0.5f,
                 (childWorld.m42 + parentWorld.m42) * 0.5f,
                 (childWorld.m43 + parentWorld.m43) * 0.5f,
                 1.0f);
    UpdateRagdollJointOffsetFromWorld(childCapsule);
  }

  m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)] = parentCapsule;
  m_ragdollJointParentCapsules[static_cast<std::size_t>(childCapsule)] = parentCapsule;
  if (childCapsule < static_cast<int>(m_ragdollContactJoints.size())) {
    m_ragdollContactJoints[static_cast<std::size_t>(childCapsule)] = 0u;
  }
  if (!ApplyRagdollParentCapsuleLinks()) {
    return false;
  }
  ResetRagdollJointFrameToBodyAxes(childCapsule);
  m_ragdollEditDirty = true;
  m_ragdollEditSelectedJoint = childCapsule;
  T8_LOG_INFO("[RagdollEdit] Capsule %d parent/joint set to capsule %d", childCapsule, parentCapsule);
  return ApplyRagdollEditPose(true);
}

bool SceneTemplate::ComputeRagdollCapsuleContactAnchor(int childCapsule, int parentCapsule, XVECTOR3& outAnchor) {
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (childCapsule < 0 || childCapsule >= static_cast<int>(bones.size()) ||
      parentCapsule < 0 || parentCapsule >= static_cast<int>(bones.size()) ||
      childCapsule == parentCapsule) {
    return false;
  }

  XMATRIX44 childWorld = bones[static_cast<std::size_t>(childCapsule)].body.worldTransform;
  XMATRIX44 parentWorld = bones[static_cast<std::size_t>(parentCapsule)].body.worldTransform;
  GetCurrentRagdollEditCapsuleWorld(childCapsule, childWorld);
  GetCurrentRagdollEditCapsuleWorld(parentCapsule, parentWorld);
  return ComputeRagdollShapeContactAnchor(
      bones[static_cast<std::size_t>(childCapsule)].body.shape,
      childWorld,
      bones[static_cast<std::size_t>(parentCapsule)].body.shape,
      parentWorld,
      outAnchor);
}

bool SceneTemplate::SetRagdollCapsuleJointAtContact(int childCapsule, int parentCapsule) {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollJointState();
  EnsureRagdollFreezeState();
  if (childCapsule < 0 || childCapsule >= static_cast<int>(bones.size()) ||
      parentCapsule < 0 || parentCapsule >= static_cast<int>(bones.size()) ||
      childCapsule == parentCapsule ||
      m_ragdollParentCapsules.size() != bones.size() ||
      m_ragdollJointParentCapsules.size() != bones.size()) {
    return false;
  }
  if (IsRagdollCapsuleFrozen(childCapsule) || IsRagdollJointFrozen(childCapsule)) {
    T8_LOG_INFO("[RagdollEdit] Capsule/joint %d is frozen; unfreeze it before changing its joint", childCapsule);
    return false;
  }

  int current = parentCapsule;
  for (std::size_t depth = 0; depth < bones.size(); ++depth) {
    if (current == childCapsule) {
      T8_LOG_ERROR("[RagdollEdit] Refusing cyclic contact joint link: capsule %d -> %d", childCapsule, parentCapsule);
      return false;
    }
    if (current < 0 || current >= static_cast<int>(bones.size())) {
      break;
    }
    current = GetRagdollEffectiveJointParentCapsule(current);
  }

  XMATRIX44 childWorld;
  XMATRIX44 parentWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(childCapsule, childWorld) ||
      !GetCurrentRagdollEditCapsuleWorld(parentCapsule, parentWorld)) {
    return false;
  }

  XVECTOR3 jointAnchor;
  XVECTOR3 totalDelta;
  const auto& childShape = bones[static_cast<std::size_t>(childCapsule)].body.shape;
  const auto& parentShape = bones[static_cast<std::size_t>(parentCapsule)].body.shape;
  if (!ComputeRagdollShapeContactAnchor(childShape, childWorld, parentShape, parentWorld, jointAnchor, &totalDelta)) {
    return false;
  }

  childWorld.m41 += totalDelta.x;
  childWorld.m42 += totalDelta.y;
  childWorld.m43 += totalDelta.z;
  if ((std::fabs(totalDelta.x) > 0.000001f ||
       std::fabs(totalDelta.y) > 0.000001f ||
       std::fabs(totalDelta.z) > 0.000001f) &&
      !SetRagdollEditCapsuleWorldTransform(childCapsule, childWorld, false)) {
    return false;
  }
  ComputeRagdollShapeContactAnchor(childShape, childWorld, parentShape, parentWorld, jointAnchor);

  bones[static_cast<std::size_t>(childCapsule)].jointWorldPosition = jointAnchor;
  UpdateRagdollJointOffsetFromWorld(childCapsule);
  m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)] = parentCapsule;
  m_ragdollJointParentCapsules[static_cast<std::size_t>(childCapsule)] = parentCapsule;
  if (childCapsule < static_cast<int>(m_ragdollContactJoints.size())) {
    m_ragdollContactJoints[static_cast<std::size_t>(childCapsule)] = 1u;
  }
  if (!ApplyRagdollParentCapsuleLinks()) {
    return false;
  }
  ResetRagdollJointFrameToBodyAxes(childCapsule);
  m_ragdollEditDirty = true;
  m_ragdollEditSelectedJoint = childCapsule;
  T8_LOG_INFO("[RagdollEdit] Capsule %d contact-snapped to parent/joint capsule %d at %.3f, %.3f, %.3f",
              childCapsule, parentCapsule, jointAnchor.x, jointAnchor.y, jointAnchor.z);
  return ApplyRagdollEditPose(true);
}

bool SceneTemplate::ClearRagdollCapsuleJoint(int childCapsule) {
  EnsureRagdollJointState();
  EnsureRagdollFreezeState();
  if (childCapsule < 0 ||
      childCapsule >= static_cast<int>(m_ragdollJointParentCapsules.size())) {
    return false;
  }
  if (IsRagdollJointFrozen(childCapsule)) {
    T8_LOG_INFO("[RagdollEdit] Joint %d is frozen; unfreeze it before deleting", childCapsule);
    return false;
  }

  const int jointParent = GetRagdollEffectiveJointParentCapsule(childCapsule);
  if (jointParent < 0) {
    return true;
  }

  const int logicalParent =
      childCapsule < static_cast<int>(m_ragdollParentCapsules.size())
          ? m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)]
          : -1;
  m_ragdollJointParentCapsules[static_cast<std::size_t>(childCapsule)] =
      logicalParent >= 0 ? kRagdollJointDisabled : kRagdollJointInheritParent;
  if (childCapsule < static_cast<int>(m_ragdollContactJoints.size())) {
    m_ragdollContactJoints[static_cast<std::size_t>(childCapsule)] = 0u;
  }
  if (!ApplyRagdollParentCapsuleLinks()) {
    return false;
  }
  if (m_ragdollEditSelectedJoint == childCapsule) {
    m_ragdollEditSelectedJoint = -1;
  }
  m_ragdollEditDirty = true;
  T8_LOG_INFO("[RagdollEdit] Capsule %d joint cleared", childCapsule);
  return ApplyRagdollEditPose(true);
}

bool SceneTemplate::ClearRagdollCapsuleJointBetween(int capsuleA, int capsuleB) {
  EnsureRagdollJointState();
  if (capsuleA < 0 || capsuleB < 0 || capsuleA == capsuleB) {
    return false;
  }
  if (GetRagdollEffectiveJointParentCapsule(capsuleA) == capsuleB) {
    return ClearRagdollCapsuleJoint(capsuleA);
  }
  if (GetRagdollEffectiveJointParentCapsule(capsuleB) == capsuleA) {
    return ClearRagdollCapsuleJoint(capsuleB);
  }
  return false;
}

bool SceneTemplate::AddControlledBoneToSelectedCapsule(int boneIndex) {
  EnsureRagdollControlledBones();
  EnsureRagdollFreezeState();
  if (m_ragdollEditSelectedCapsule < 0 ||
      m_ragdollEditSelectedCapsule >= static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size()) ||
      boneIndex < 0) {
    return false;
  }
  if (IsRagdollCapsuleFrozen(m_ragdollEditSelectedCapsule)) {
    T8_LOG_INFO("[RagdollEdit] Capsule %d is frozen; unfreeze it before adding bones", m_ragdollEditSelectedCapsule);
    return false;
  }

  const std::size_t capsuleIndex = static_cast<std::size_t>(m_ragdollEditSelectedCapsule);
  auto& controlledBones = m_ragdollAnimationBinding.controlledBoneIndices[capsuleIndex];
  auto& controlledFrames = m_ragdollAnimationBinding.controlledBodyFromBone[capsuleIndex];
  const bool firstControlledBone = controlledBones.empty();
  if (std::find(controlledBones.begin(), controlledBones.end(), boneIndex) != controlledBones.end()) {
    return false;
  }

  XMATRIX44 bodyWorld;
  XMATRIX44 boneWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(m_ragdollEditSelectedCapsule, bodyWorld) ||
      !GetSkeletonEditBoneWorldTransform(boneIndex, boneWorld)) {
    return false;
  }

  for (std::size_t otherCapsule = 0; otherCapsule < m_ragdollAnimationBinding.controlledBoneIndices.size(); ++otherCapsule) {
    if (otherCapsule == capsuleIndex) {
      continue;
    }
    auto& otherBones = m_ragdollAnimationBinding.controlledBoneIndices[otherCapsule];
    auto& otherFrames = m_ragdollAnimationBinding.controlledBodyFromBone[otherCapsule];
    for (std::size_t i = 0; i < otherBones.size(); ++i) {
      if (otherBones[i] == boneIndex) {
        otherBones.erase(otherBones.begin() + static_cast<std::ptrdiff_t>(i));
        if (i < otherFrames.size()) {
          otherFrames.erase(otherFrames.begin() + static_cast<std::ptrdiff_t>(i));
        }
        break;
      }
    }
  }

  XMATRIX44 inverseBoneWorld;
  if (!InvertAffineNoExit(boneWorld, inverseBoneWorld)) {
    T8_LOG_ERROR("[RagdollEdit] Cannot add bone %d to capsule %d: bone transform is singular",
                 boneIndex, m_ragdollEditSelectedCapsule);
    return false;
  }
  if (firstControlledBone &&
      m_ragdollAnimationBinding.referencePose.bones[capsuleIndex].body.boneIndex != boneIndex) {
    bool ownedByOtherCapsule = false;
    const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
    for (std::size_t otherCapsule = 0; otherCapsule < bones.size(); ++otherCapsule) {
      if (otherCapsule != capsuleIndex && bones[otherCapsule].body.boneIndex == boneIndex) {
        ownedByOtherCapsule = true;
        break;
      }
    }
    if (!ownedByOtherCapsule) {
      const int oldOwnerBone = m_ragdollAnimationBinding.referencePose.bones[capsuleIndex].body.boneIndex;
      m_ragdollAnimationBinding.referencePose.bones[capsuleIndex].body.boneIndex = boneIndex;
      m_ragdollAnimationBinding.bodyFromBone[capsuleIndex] = bodyWorld * inverseBoneWorld;
      UpdateRagdollJointOffsetFromWorld(m_ragdollEditSelectedCapsule);
      UpdateRagdollJointFrameOffsetsForBody(m_ragdollEditSelectedCapsule);
      ApplyRagdollParentCapsuleLinks();
      m_ragdollEditRebuildRequested = true;
      T8_LOG_INFO("[RagdollEdit] Capsule %d owner bone changed from %d to first affected bone %d",
                  m_ragdollEditSelectedCapsule,
                  oldOwnerBone,
                  boneIndex);
    }
  }
  controlledBones.push_back(boneIndex);
  controlledFrames.push_back(m_ragdollAnimationBinding.referencePose.bones[capsuleIndex].body.boneIndex == boneIndex
      ? m_ragdollAnimationBinding.bodyFromBone[capsuleIndex]
      : bodyWorld * inverseBoneWorld);
  m_ragdollEditSelectedUnassignedBone = -1;
  m_ragdollEditSelectedAffectedBone = boneIndex;
  m_ragdollEditDirty = true;
  T8_LOG_INFO("[RagdollEdit] Capsule %d now controls bone %d", m_ragdollEditSelectedCapsule, boneIndex);
  return true;
}

bool SceneTemplate::RemoveControlledBoneFromSelectedCapsule(int boneIndex) {
  EnsureRagdollControlledBones();
  EnsureRagdollFreezeState();
  if (m_ragdollEditSelectedCapsule < 0 ||
      m_ragdollEditSelectedCapsule >= static_cast<int>(m_ragdollAnimationBinding.controlledBoneIndices.size()) ||
      boneIndex < 0) {
    return false;
  }
  if (IsRagdollCapsuleFrozen(m_ragdollEditSelectedCapsule)) {
    T8_LOG_INFO("[RagdollEdit] Capsule %d is frozen; unfreeze it before removing bones", m_ragdollEditSelectedCapsule);
    return false;
  }

  const std::size_t capsuleIndex = static_cast<std::size_t>(m_ragdollEditSelectedCapsule);
  auto& controlledBones = m_ragdollAnimationBinding.controlledBoneIndices[capsuleIndex];
  auto& controlledFrames = m_ragdollAnimationBinding.controlledBodyFromBone[capsuleIndex];
  for (std::size_t i = 0; i < controlledBones.size(); ++i) {
    if (controlledBones[i] == boneIndex) {
      controlledBones.erase(controlledBones.begin() + static_cast<std::ptrdiff_t>(i));
      if (i < controlledFrames.size()) {
        controlledFrames.erase(controlledFrames.begin() + static_cast<std::ptrdiff_t>(i));
      }
      m_ragdollEditSelectedUnassignedBone = boneIndex;
      m_ragdollEditSelectedAffectedBone = -1;
      m_ragdollEditDirty = true;
      T8_LOG_INFO("[RagdollEdit] Capsule %d no longer controls bone %d", m_ragdollEditSelectedCapsule, boneIndex);
      return true;
    }
  }
  return false;
}

int SceneTemplate::FindGeneratedRagdollCapsuleForBone(int boneIndex) const {
  const auto& bones = m_ragdollGeneratedBinding.referencePose.bones;
  for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
    if (bones[static_cast<std::size_t>(i)].body.boneIndex == boneIndex) {
      return i;
    }
  }
  return -1;
}

bool SceneTemplate::GetSkeletonEditBoneWorldTransform(int boneIndex, XMATRIX44& outWorld) const {
  if (boneIndex >= 0 && boneIndex < static_cast<int>(m_skeletonEditCombined.size())) {
    outWorld = FlipMatrixZ(m_skeletonEditCombined[static_cast<std::size_t>(boneIndex)]) * Meshes[0].Final;
    return true;
  }

  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;
  if (!skeleton || boneIndex < 0 || boneIndex >= static_cast<int>(skeleton->Bones.size())) {
    return false;
  }
  outWorld = FlipMatrixZ(skeleton->Bones[static_cast<std::size_t>(boneIndex)].Combined) * Meshes[0].Final;
  return true;
}

bool SceneTemplate::GetRagdollAuthoringBoneWorldTransform(int boneIndex, XMATRIX44& outWorld) const {
  if (boneIndex >= 0 && boneIndex < static_cast<int>(m_skeletonEditCombined.size())) {
    outWorld = FlipMatrixZ(m_skeletonEditCombined[static_cast<std::size_t>(boneIndex)]) * Meshes[0].Final;
    return true;
  }

  const int generatedIndex = FindGeneratedRagdollCapsuleForBone(boneIndex);
  if (generatedIndex >= 0 &&
      generatedIndex < static_cast<int>(m_ragdollGeneratedBinding.referencePose.bones.size()) &&
      generatedIndex < static_cast<int>(m_ragdollGeneratedBinding.bodyFromBone.size())) {
    XMATRIX44 boneFromGeneratedBody;
    if (InvertAffineNoExit(m_ragdollGeneratedBinding.bodyFromBone[static_cast<std::size_t>(generatedIndex)],
                           boneFromGeneratedBody)) {
      outWorld = boneFromGeneratedBody *
          m_ragdollGeneratedBinding.referencePose.bones[static_cast<std::size_t>(generatedIndex)].body.worldTransform;
      return true;
    }
  }

  return GetSkeletonEditBoneWorldTransform(boneIndex, outWorld);
}

int SceneTemplate::FindSkeletonEditDisplayEndpoint(int boneIndex) const {
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;
  if (!skeleton || boneIndex < 0 || boneIndex >= static_cast<int>(skeleton->Bones.size())) {
    return -1;
  }

  const std::string ownerName = LowerName(skeleton->Bones[static_cast<std::size_t>(boneIndex)].Name);
  if (!IsHumanoidDisplayBoneName(ownerName)) {
    return -1;
  }

  const xF::xBone& bone = skeleton->Bones[static_cast<std::size_t>(boneIndex)];
  if (bone.Dad < skeleton->Bones.size() && bone.Dad != static_cast<unsigned short>(boneIndex)) {
    const std::string parentName = LowerName(skeleton->Bones[bone.Dad].Name);
    XMATRIX44 boneWorld;
    XMATRIX44 parentWorld;
    if (IsSpineLikeDisplayName(ownerName) &&
        IsSpineLikeDisplayName(parentName) &&
        GetSkeletonEditBoneWorldTransform(boneIndex, boneWorld) &&
        GetSkeletonEditBoneWorldTransform(bone.Dad, parentWorld) &&
        Length3(XVECTOR3(boneWorld.m41 - parentWorld.m41, boneWorld.m42 - parentWorld.m42, boneWorld.m43 - parentWorld.m43, 0.0f)) < 0.001f) {
      return -1;
    }
  }

  auto combinedChildren = [&](int index) {
    std::vector<int> result;
    if (index < 0 || index >= static_cast<int>(skeleton->Bones.size())) {
      return result;
    }
    for (unsigned int child : skeleton->Bones[static_cast<std::size_t>(index)].Sons) {
      if (child < skeleton->Bones.size() &&
          std::find(result.begin(), result.end(), static_cast<int>(child)) == result.end()) {
        result.push_back(static_cast<int>(child));
      }
    }
    for (int child = 0; child < static_cast<int>(skeleton->Bones.size()); ++child) {
      if (skeleton->Bones[static_cast<std::size_t>(child)].Dad == static_cast<unsigned short>(index) &&
          child != index &&
          std::find(result.begin(), result.end(), child) == result.end()) {
        result.push_back(child);
      }
    }
    return result;
  };

  XMATRIX44 ownerWorld;
  if (!GetSkeletonEditBoneWorldTransform(boneIndex, ownerWorld)) {
    return -1;
  }
  const XVECTOR3 ownerPosition(ownerWorld.m41, ownerWorld.m42, ownerWorld.m43, 1.0f);

  struct Candidate {
    int boneIndex = -1;
    int score = 0;
    float length = 0.0f;
  };
  std::vector<Candidate> candidates;
  std::function<void(int, int)> gather = [&](int searchBoneIndex, int depth) {
    if (searchBoneIndex < 0 || searchBoneIndex >= static_cast<int>(skeleton->Bones.size()) || depth > 4) {
      return;
    }
    for (int childIndex : combinedChildren(searchBoneIndex)) {
      const std::string childName = LowerName(skeleton->Bones[static_cast<std::size_t>(childIndex)].Name);
      if (IsAttachmentBoneName(childName)) {
        continue;
      }

      XMATRIX44 childWorld;
      if (!GetSkeletonEditBoneWorldTransform(childIndex, childWorld)) {
        continue;
      }
      const XVECTOR3 childPosition(childWorld.m41, childWorld.m42, childWorld.m43, 1.0f);
      const float length = Length3(childPosition - ownerPosition);
      const bool displayChild = IsHumanoidDisplayBoneName(childName);
      const bool endpointHelper = IsEndpointHelperForBone(ownerName, childName);
      if ((displayChild || endpointHelper) && length >= 0.001f) {
        candidates.push_back({childIndex, DisplayChildPriority(ownerName, childName) - depth * 10, length});
      }
      if (length < 0.001f || IsDeformationHelperBoneName(childName) || (!displayChild && !endpointHelper)) {
        gather(childIndex, depth + 1);
      }
    }
  };
  gather(boneIndex, 0);
  if (candidates.empty()) {
    return -1;
  }
  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }
    return a.length > b.length;
  });
  return candidates.front().boneIndex;
}

bool SceneTemplate::BuildSkeletonEditBoneOctahedron(int boneIndex, float widthScale, std::array<XVECTOR3, 6>& outPoints) const {
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;
  if (!skeleton || boneIndex < 0 || boneIndex >= static_cast<int>(skeleton->Bones.size())) {
    return false;
  }

  const xF::xBone& bone = skeleton->Bones[static_cast<std::size_t>(boneIndex)];
  if (bone.Dad == static_cast<unsigned short>(boneIndex) || bone.Dad >= skeleton->Bones.size()) {
    return false;
  }

  XMATRIX44 rootWorld;
  XMATRIX44 tipWorld;
  if (!GetSkeletonEditBoneWorldTransform(bone.Dad, rootWorld) ||
      !GetSkeletonEditBoneWorldTransform(boneIndex, tipWorld)) {
    return false;
  }

  const XVECTOR3 root(rootWorld.m41, rootWorld.m42, rootWorld.m43, 1.0f);
  const XVECTOR3 tip(tipWorld.m41, tipWorld.m42, tipWorld.m43, 1.0f);
  BuildOctahedralBonePoints(root, tip, widthScale, (std::max)(0.001f, m_modelRadius * 0.004f), outPoints);
  return true;
}

bool SceneTemplate::RebuildRagdollParentLinks() {
  if (!m_ragdollParentCapsules.empty()) {
    return ApplyRagdollParentCapsuleLinks();
  }

  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;
  if (!skeleton) {
    return false;
  }

  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  for (auto& ragdollBone : bones) {
    ragdollBone.parentBoneIndex = -1;
    int current = ragdollBone.body.boneIndex;
    for (std::size_t depth = 0; depth < skeleton->Bones.size(); ++depth) {
      if (current < 0 || current >= static_cast<int>(skeleton->Bones.size())) {
        break;
      }
      const xF::xBone& skeletonBone = skeleton->Bones[static_cast<std::size_t>(current)];
      if (skeletonBone.Dad == static_cast<unsigned short>(current) ||
          skeletonBone.Dad >= skeleton->Bones.size()) {
        break;
      }
      current = skeletonBone.Dad;
      if (FindRagdollCapsuleForBone(current) >= 0) {
        ragdollBone.parentBoneIndex = current;
        break;
      }
    }
  }
  SyncRagdollParentCapsulesFromBoneLinks();
  return true;
}

bool SceneTemplate::BuildDefaultRagdollCapsuleForBone(int boneIndex,
                                                     t850::PhysicsRagdollBoneDesc& outBone,
                                                     XMATRIX44& outBodyFromBone) const {
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;
  if (!skeleton || boneIndex < 0 || boneIndex >= static_cast<int>(skeleton->Bones.size())) {
    return false;
  }

  XMATRIX44 boneWorld;
  if (!GetSkeletonEditBoneWorldTransform(boneIndex, boneWorld)) {
    return false;
  }
  const XVECTOR3 selectedBonePosition(boneWorld.m41, boneWorld.m42, boneWorld.m43, 1.0f);
  XVECTOR3 root = selectedBonePosition;
  XVECTOR3 end = selectedBonePosition;
  bool hasAuthoringSegment = false;

  const xF::xBone& selectedBone = skeleton->Bones[static_cast<std::size_t>(boneIndex)];
  if (selectedBone.Dad < skeleton->Bones.size() &&
      selectedBone.Dad != static_cast<unsigned short>(boneIndex)) {
    XMATRIX44 parentWorld;
    if (GetSkeletonEditBoneWorldTransform(selectedBone.Dad, parentWorld)) {
      const XVECTOR3 parentPosition(parentWorld.m41, parentWorld.m42, parentWorld.m43, 1.0f);
      if (Length3(selectedBonePosition - parentPosition) > 0.0001f) {
        root = parentPosition;
        end = selectedBonePosition;
        hasAuthoringSegment = true;
      }
    }
  }

  int endpointBone = hasAuthoringSegment ? -1 : FindSkeletonEditDisplayEndpoint(boneIndex);
  float bestLength = 0.0f;
  if (!hasAuthoringSegment && endpointBone < 0) {
    for (int i = 0; i < static_cast<int>(skeleton->Bones.size()); ++i) {
      const xF::xBone& candidate = skeleton->Bones[static_cast<std::size_t>(i)];
      if (candidate.Dad != static_cast<unsigned short>(boneIndex)) {
        continue;
      }
      XMATRIX44 childWorld;
      if (!GetSkeletonEditBoneWorldTransform(i, childWorld)) {
        continue;
      }
      const XVECTOR3 childPosition(childWorld.m41, childWorld.m42, childWorld.m43, 1.0f);
      const float length = Length3(childPosition - root);
      if (length > bestLength) {
        bestLength = length;
        endpointBone = i;
      }
    }
  }

  if (!hasAuthoringSegment && endpointBone >= 0) {
    XMATRIX44 childWorld;
    if (!GetSkeletonEditBoneWorldTransform(endpointBone, childWorld)) {
      return false;
    }
    end = XVECTOR3(childWorld.m41, childWorld.m42, childWorld.m43, 1.0f);
  } else if (!hasAuthoringSegment) {
    const xF::xBone& bone = skeleton->Bones[static_cast<std::size_t>(boneIndex)];
    if (bone.Dad == static_cast<unsigned short>(boneIndex) || bone.Dad >= skeleton->Bones.size()) {
      return false;
    }
    XMATRIX44 parentWorld;
    if (!GetSkeletonEditBoneWorldTransform(bone.Dad, parentWorld)) {
      return false;
    }
    const XVECTOR3 parent(parentWorld.m41, parentWorld.m42, parentWorld.m43, 1.0f);
    end = root + (root - parent);
  }

  XVECTOR3 axis = end - root;
  float length = Length3(axis);
  if (length <= 0.0001f) {
    return false;
  }
  axis = axis / length;

  const float radius = (std::max)(0.004f, (std::min)((std::max)(0.004f, m_modelRadius * 0.035f), length * 0.18f));
  const float capsuleLength = (std::max)(radius * 2.0f + 0.002f, length);
  const XVECTOR3 center = root + axis * (capsuleLength * 0.5f);
  const XMATRIX44 bodyWorld = MakeCapsuleBodyTransform(center, axis);

  XMATRIX44 inverseBoneWorld;
  if (!InvertAffineNoExit(boneWorld, inverseBoneWorld)) {
    T8_LOG_ERROR("[RagdollEdit] Failed to create capsule for bone %d: bone transform is singular", boneIndex);
    return false;
  }

  t850::PhysicsRagdollBoneDesc desc;
  desc.parentBoneIndex = -1;
  desc.jointWorldPosition = root;
  desc.body.entityId = Meshes[0].GetEntityId();
  desc.body.boneIndex = boneIndex;
  desc.body.debugName = skeleton->Bones[static_cast<std::size_t>(boneIndex)].Name;
  desc.body.motion = t850::PhysicsBodyMotion::Kinematic;
  desc.body.mass = (std::max)(0.1f, capsuleLength + radius * 2.0f);
  desc.body.worldTransform = bodyWorld;
  desc.body.shape = t850::PhysicsShapeDesc::Capsule(radius, (std::max)(0.001f, capsuleLength * 0.5f - radius));
  desc.parentJointTwistAxis = MatrixAxisY(bodyWorld);
  desc.parentJointPlaneAxis = MatrixAxisX(bodyWorld);
  desc.childJointTwistAxis = MatrixAxisY(bodyWorld);
  desc.childJointPlaneAxis = MatrixAxisX(bodyWorld);

  outBone = desc;
  outBodyFromBone = bodyWorld * inverseBoneWorld;
  return true;
}

bool SceneTemplate::CreateRagdollCapsuleForBone(int boneIndex) {
  EnsureRagdollControlledBones();
  if (boneIndex < 0 ||
      FindRagdollCapsuleForBone(boneIndex) >= 0 ||
      FindRagdollCapsuleControllingBone(boneIndex) >= 0) {
    return false;
  }

  t850::PhysicsRagdollBoneDesc bone;
  XMATRIX44 bodyFromBone;
  if (!BuildDefaultRagdollCapsuleForBone(boneIndex, bone, bodyFromBone)) {
    const int generatedIndex = FindGeneratedRagdollCapsuleForBone(boneIndex);
    if (generatedIndex < 0 ||
        generatedIndex >= static_cast<int>(m_ragdollGeneratedBinding.referencePose.bones.size()) ||
        generatedIndex >= static_cast<int>(m_ragdollGeneratedBinding.bodyFromBone.size())) {
      T8_LOG_ERROR("[RagdollEdit] Failed to create capsule for bone %d", boneIndex);
      return false;
    }
    bone = m_ragdollGeneratedBinding.referencePose.bones[static_cast<std::size_t>(generatedIndex)];
    bodyFromBone = m_ragdollGeneratedBinding.bodyFromBone[static_cast<std::size_t>(generatedIndex)];
    bone.body.entityId = Meshes[0].GetEntityId();
    bone.body.motion = t850::PhysicsBodyMotion::Kinematic;
  }

  auto& desc = m_ragdollAnimationBinding.referencePose;
  if (desc.entityId == 0) {
    desc.entityId = Meshes[0].GetEntityId();
    desc.animationMode = t850::PhysicsAnimationMode::AnimationDriven;
    desc.animationToPhysicsBlend = 0.0f;
  }
  desc.bones.push_back(bone);
  m_ragdollAnimationBinding.bodyFromBone.push_back(bodyFromBone);
  m_ragdollAnimationBinding.jointFromBone.push_back(XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
  m_ragdollAnimationBinding.parentJointTwistFromBody.push_back(XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  m_ragdollAnimationBinding.parentJointPlaneFromBody.push_back(XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  m_ragdollAnimationBinding.childJointTwistFromBody.push_back(XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  m_ragdollAnimationBinding.childJointPlaneFromBody.push_back(XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  m_ragdollAnimationBinding.controlledBoneIndices.push_back(std::vector<int>{bone.body.boneIndex});
  m_ragdollAnimationBinding.controlledBodyFromBone.push_back(std::vector<XMATRIX44>{bodyFromBone});
  EnsureRagdollParentCapsules();
  if (m_ragdollParentCapsules.size() < desc.bones.size()) {
    m_ragdollParentCapsules.resize(desc.bones.size(), -1);
  } else if (m_ragdollParentCapsules.size() > desc.bones.size()) {
    m_ragdollParentCapsules.resize(desc.bones.size());
  }
  m_ragdollParentCapsules.back() = -1;
  m_ragdollJointParentCapsules.resize(desc.bones.size(), kRagdollJointInheritParent);
  m_ragdollJointParentCapsules.back() = kRagdollJointInheritParent;
  m_ragdollFrozenCapsules.resize(desc.bones.size(), 0u);
  m_ragdollFrozenCapsules.back() = 0u;
  m_ragdollFrozenJoints.resize(desc.bones.size(), 0u);
  m_ragdollFrozenJoints.back() = 0u;
  m_ragdollContactJoints.resize(desc.bones.size(), 0u);
  m_ragdollContactJoints.back() = 0u;
  const int newIndex = static_cast<int>(desc.bones.size()) - 1;
  UpdateRagdollJointOffsetFromWorld(newIndex);
  if (!UpdateRagdollReferenceBodyFromLocal(newIndex)) {
    desc.bones.pop_back();
    m_ragdollAnimationBinding.bodyFromBone.pop_back();
    m_ragdollAnimationBinding.jointFromBone.pop_back();
    m_ragdollAnimationBinding.parentJointTwistFromBody.pop_back();
    m_ragdollAnimationBinding.parentJointPlaneFromBody.pop_back();
    m_ragdollAnimationBinding.childJointTwistFromBody.pop_back();
    m_ragdollAnimationBinding.childJointPlaneFromBody.pop_back();
    m_ragdollAnimationBinding.controlledBoneIndices.pop_back();
    m_ragdollAnimationBinding.controlledBodyFromBone.pop_back();
    if (!m_ragdollParentCapsules.empty()) {
      m_ragdollParentCapsules.pop_back();
    }
    if (!m_ragdollJointParentCapsules.empty()) {
      m_ragdollJointParentCapsules.pop_back();
    }
    if (!m_ragdollFrozenCapsules.empty()) {
      m_ragdollFrozenCapsules.pop_back();
    }
    if (!m_ragdollFrozenJoints.empty()) {
      m_ragdollFrozenJoints.pop_back();
    }
    if (!m_ragdollContactJoints.empty()) {
      m_ragdollContactJoints.pop_back();
    }
    T8_LOG_ERROR("[RagdollEdit] Failed to create capsule for bone %d '%s': could not update reference transform",
                 bone.body.boneIndex, bone.body.debugName.c_str());
    return false;
  }
  RebuildRagdollParentLinks();

  SelectRagdollEditCapsule(newIndex, true);
  m_ragdollEditSelectedJoint = -1;
  m_ragdollEditDirty = true;
  m_ragdollEditRebuildRequested = true;
  T8_LOG_INFO("[RagdollEdit] Created capsule for bone %d '%s'", bone.body.boneIndex, bone.body.debugName.c_str());
  return true;
}

bool SceneTemplate::CreateRagdollBoxForBone(int boneIndex) {
  if (!CreateRagdollCapsuleForBone(boneIndex)) {
    return false;
  }
  return MorphRagdollBodyToBox(m_ragdollEditSelectedCapsule);
}

bool SceneTemplate::MorphRagdollBodyToBox(int capsuleIndex) {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (capsuleIndex < 0 || capsuleIndex >= static_cast<int>(bones.size())) {
    return false;
  }
  EnsureRagdollFreezeState();
  if (IsRagdollCapsuleFrozen(capsuleIndex)) {
    T8_LOG_INFO("[RagdollEdit] Body %d is frozen; unfreeze it before morphing to box", capsuleIndex);
    return false;
  }
  auto& shape = bones[static_cast<std::size_t>(capsuleIndex)].body.shape;
  if (shape.type == t850::PhysicsShapeType::Box) {
    return true;
  }
  if (shape.type != t850::PhysicsShapeType::Capsule) {
    T8_LOG_ERROR("[RagdollEdit] Body %d cannot morph from %s to Box",
                 capsuleIndex, RagdollShapeTypeName(shape.type));
    return false;
  }
  MorphShapeToBox(shape);
  m_ragdollEditDirty = true;
  m_ragdollEditRebuildRequested = true;
  T8_LOG_INFO("[RagdollEdit] Body %d morphed to Box; links, joints and affected bones preserved", capsuleIndex);
  return ApplyRagdollEditPose(true);
}

bool SceneTemplate::MorphRagdollBodyToCapsule(int capsuleIndex) {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (capsuleIndex < 0 || capsuleIndex >= static_cast<int>(bones.size())) {
    return false;
  }
  EnsureRagdollFreezeState();
  if (IsRagdollCapsuleFrozen(capsuleIndex)) {
    T8_LOG_INFO("[RagdollEdit] Body %d is frozen; unfreeze it before morphing to capsule", capsuleIndex);
    return false;
  }
  auto& shape = bones[static_cast<std::size_t>(capsuleIndex)].body.shape;
  if (shape.type == t850::PhysicsShapeType::Capsule) {
    return true;
  }
  if (shape.type != t850::PhysicsShapeType::Box) {
    T8_LOG_ERROR("[RagdollEdit] Body %d cannot morph from %s to Capsule",
                 capsuleIndex, RagdollShapeTypeName(shape.type));
    return false;
  }
  MorphShapeToCapsule(shape);
  m_ragdollEditDirty = true;
  m_ragdollEditRebuildRequested = true;
  T8_LOG_INFO("[RagdollEdit] Body %d morphed to Capsule; links, joints and affected bones preserved", capsuleIndex);
  return ApplyRagdollEditPose(true);
}

bool SceneTemplate::DeleteSelectedRagdollCapsule() {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  auto& locals = m_ragdollAnimationBinding.bodyFromBone;
  const int index = m_ragdollEditSelectedCapsule;
  if (index < 0 || index >= static_cast<int>(bones.size()) || index >= static_cast<int>(locals.size())) {
    return false;
  }
  EnsureRagdollFreezeState();
  if (IsRagdollCapsuleFrozen(index)) {
    T8_LOG_INFO("[RagdollEdit] Capsule %d is frozen; unfreeze it before deleting", index);
    return false;
  }

  const int boneIndex = bones[static_cast<std::size_t>(index)].body.boneIndex;
  const std::string debugName = bones[static_cast<std::size_t>(index)].body.debugName;
  bones.erase(bones.begin() + index);
  locals.erase(locals.begin() + index);
  if (index < static_cast<int>(m_ragdollAnimationBinding.jointFromBone.size())) {
    m_ragdollAnimationBinding.jointFromBone.erase(
        m_ragdollAnimationBinding.jointFromBone.begin() + index);
  }
  if (index < static_cast<int>(m_ragdollAnimationBinding.parentJointTwistFromBody.size())) {
    m_ragdollAnimationBinding.parentJointTwistFromBody.erase(
        m_ragdollAnimationBinding.parentJointTwistFromBody.begin() + index);
  }
  if (index < static_cast<int>(m_ragdollAnimationBinding.parentJointPlaneFromBody.size())) {
    m_ragdollAnimationBinding.parentJointPlaneFromBody.erase(
        m_ragdollAnimationBinding.parentJointPlaneFromBody.begin() + index);
  }
  if (index < static_cast<int>(m_ragdollAnimationBinding.childJointTwistFromBody.size())) {
    m_ragdollAnimationBinding.childJointTwistFromBody.erase(
        m_ragdollAnimationBinding.childJointTwistFromBody.begin() + index);
  }
  if (index < static_cast<int>(m_ragdollAnimationBinding.childJointPlaneFromBody.size())) {
    m_ragdollAnimationBinding.childJointPlaneFromBody.erase(
        m_ragdollAnimationBinding.childJointPlaneFromBody.begin() + index);
  }
  if (index < static_cast<int>(m_ragdollAnimationBinding.controlledBoneIndices.size())) {
    m_ragdollAnimationBinding.controlledBoneIndices.erase(
        m_ragdollAnimationBinding.controlledBoneIndices.begin() + index);
  }
  if (index < static_cast<int>(m_ragdollAnimationBinding.controlledBodyFromBone.size())) {
    m_ragdollAnimationBinding.controlledBodyFromBone.erase(
        m_ragdollAnimationBinding.controlledBodyFromBone.begin() + index);
  }
  if (index < static_cast<int>(m_ragdollParentCapsules.size())) {
    m_ragdollParentCapsules.erase(m_ragdollParentCapsules.begin() + index);
    for (int& parentCapsule : m_ragdollParentCapsules) {
      if (parentCapsule == index) {
        parentCapsule = -1;
      } else if (parentCapsule > index) {
        --parentCapsule;
      }
    }
  } else {
    m_ragdollParentCapsules.clear();
  }
  if (index < static_cast<int>(m_ragdollJointParentCapsules.size())) {
    m_ragdollJointParentCapsules.erase(m_ragdollJointParentCapsules.begin() + index);
    for (int& jointParentCapsule : m_ragdollJointParentCapsules) {
      if (jointParentCapsule == index) {
        jointParentCapsule = kRagdollJointDisabled;
      } else if (jointParentCapsule > index) {
        --jointParentCapsule;
      }
    }
  } else {
    m_ragdollJointParentCapsules.clear();
  }
  if (index < static_cast<int>(m_ragdollFrozenCapsules.size())) {
    m_ragdollFrozenCapsules.erase(m_ragdollFrozenCapsules.begin() + index);
  } else {
    m_ragdollFrozenCapsules.clear();
  }
  if (index < static_cast<int>(m_ragdollFrozenJoints.size())) {
    m_ragdollFrozenJoints.erase(m_ragdollFrozenJoints.begin() + index);
  } else {
    m_ragdollFrozenJoints.clear();
  }
  if (index < static_cast<int>(m_ragdollContactJoints.size())) {
    m_ragdollContactJoints.erase(m_ragdollContactJoints.begin() + index);
  } else {
    m_ragdollContactJoints.clear();
  }
  if (m_ragdollEditSelectedJoint == index) {
    m_ragdollEditSelectedJoint = -1;
  } else if (m_ragdollEditSelectedJoint > index) {
    --m_ragdollEditSelectedJoint;
  }
  auto fixSelectedCapsuleIndex = [index](int& value) {
    if (value == index) {
      value = -1;
    } else if (value > index) {
      --value;
    }
  };
  fixSelectedCapsuleIndex(m_ragdollEditSelectedParentCapsule);
  fixSelectedCapsuleIndex(m_ragdollEditSelectedJointParentCapsule);
  if (m_ragdollEditRenamingCapsule == index) {
    m_ragdollEditRenamingCapsule = -1;
    m_ragdollEditRenameFocusPending = false;
  } else if (m_ragdollEditRenamingCapsule > index) {
    --m_ragdollEditRenamingCapsule;
  }
  m_ragdollEditSelectedHandle = 0;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  m_ragdollEditSelectedCapsule = bones.empty()
      ? -1
      : (std::min)(index, static_cast<int>(bones.size()) - 1);
  RebuildRagdollParentLinks();
  m_ragdollEditDirty = true;

  if (bones.empty()) {
    m_ragdollClearRequested = true;
    m_ragdollEditSelectedCapsule = -1;
    m_ragdollEditSelectedJoint = -1;
    m_ragdollEditSelectedParentCapsule = -1;
    m_ragdollEditSelectedJointParentCapsule = -1;
    m_ragdollEditSelectedHandle = -1;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_ragdollEditJointAxis = -1;
    m_driveRagdollFromAnimation = false;
    m_ragdollPhysicsDriven = false;
    T8_LOG_INFO("[RagdollEdit] Deleted last capsule assignment for bone %d '%s'", boneIndex, debugName.c_str());
    return true;
  }

  T8_LOG_INFO("[RagdollEdit] Deleted capsule assignment for bone %d '%s'", boneIndex, debugName.c_str());
  return ApplyRagdollEditPose(true);
}

bool SceneTemplate::ClearRagdollCapsules() {
  const t850::PhysicsRagdollHandle ragdollHandle = Meshes[0].GetPhysicsRagdoll();
  Meshes[0].AttachPhysicsRagdoll(t850::PhysicsRagdollHandle{});

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (engineContext && engineContext->physics && ragdollHandle.IsValid()) {
    engineContext->physics->DestroyRagdoll(ragdollHandle);
  }

  m_ragdollAnimationBinding.referencePose.bones.clear();
  m_ragdollAnimationBinding.bodyFromBone.clear();
  m_ragdollAnimationBinding.jointFromBone.clear();
  m_ragdollAnimationBinding.parentJointTwistFromBody.clear();
  m_ragdollAnimationBinding.parentJointPlaneFromBody.clear();
  m_ragdollAnimationBinding.childJointTwistFromBody.clear();
  m_ragdollAnimationBinding.childJointPlaneFromBody.clear();
  m_ragdollAnimationBinding.controlledBoneIndices.clear();
  m_ragdollAnimationBinding.controlledBodyFromBone.clear();
  m_ragdollParentCapsules.clear();
  m_ragdollJointParentCapsules.clear();
  m_ragdollFrozenCapsules.clear();
  m_ragdollFrozenJoints.clear();
  m_ragdollContactJoints.clear();
  m_ragdollAnimationPose = t850::PhysicsRagdollDesc{};
  m_ragdollPhysicsStates.clear();
  m_ragdollPhysicsBoneIndices.clear();
  m_ragdollPhysicsCombinedMatrices.clear();
  m_ragdollEditSelectedCapsule = -1;
  m_ragdollEditSelectedJoint = -1;
  m_ragdollEditSelectedParentCapsule = -1;
  m_ragdollEditSelectedJointParentCapsule = -1;
  m_ragdollEditSelectedHandle = -1;
  m_ragdollEditRenamingCapsule = -1;
  m_ragdollEditRenameFocusPending = false;
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  m_skeletonEditDragging = false;
  m_ragdollClearRequested = false;
  m_ragdollEditRebuildRequested = false;
  m_driveRagdollFromAnimation = false;
  m_ragdollPhysicsDriven = false;
  m_showPhysics = false;
  m_ragdollEditDirty = true;
  T8_LOG_INFO("[RagdollEdit] Cleared all capsule assignments for '%s'", ActiveModelPath().c_str());
  return true;
}

bool SceneTemplate::UpdateRagdollReferenceBodyFromLocal(int capsuleIndex) {
  if (capsuleIndex < 0 ||
      capsuleIndex >= static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size()) ||
      capsuleIndex >= static_cast<int>(m_ragdollAnimationBinding.bodyFromBone.size())) {
    return false;
  }

  auto& bone = m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(capsuleIndex)];
  XMATRIX44 boneWorld;
  if (!GetRagdollAuthoringBoneWorldTransform(bone.body.boneIndex, boneWorld)) {
    const int generatedIndex = FindGeneratedRagdollCapsuleForBone(bone.body.boneIndex);
    if (generatedIndex < 0 ||
        generatedIndex >= static_cast<int>(m_ragdollGeneratedBinding.referencePose.bones.size()) ||
        generatedIndex >= static_cast<int>(m_ragdollGeneratedBinding.bodyFromBone.size())) {
      return false;
    }
    XMATRIX44 generatedBodyFromBone = m_ragdollGeneratedBinding.bodyFromBone[static_cast<std::size_t>(generatedIndex)];
    XMATRIX44 boneFromGeneratedBody;
    if (!InvertAffineNoExit(generatedBodyFromBone, boneFromGeneratedBody)) {
      T8_LOG_ERROR("[RagdollEdit] Cannot update capsule %d: generated capsule frame is singular", capsuleIndex);
      return false;
    }
    boneWorld =
        boneFromGeneratedBody *
        m_ragdollGeneratedBinding.referencePose.bones[static_cast<std::size_t>(generatedIndex)].body.worldTransform;
  }

  m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(capsuleIndex)].body.worldTransform =
      m_ragdollAnimationBinding.bodyFromBone[static_cast<std::size_t>(capsuleIndex)] * boneWorld;
  if (capsuleIndex < static_cast<int>(m_ragdollAnimationBinding.jointFromBone.size())) {
    m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(capsuleIndex)].jointWorldPosition =
        t850::TransformPoint(m_ragdollAnimationBinding.jointFromBone[static_cast<std::size_t>(capsuleIndex)], boneWorld);
  } else {
    m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(capsuleIndex)].jointWorldPosition =
        XVECTOR3(boneWorld.m41, boneWorld.m42, boneWorld.m43, 1.0f);
    UpdateRagdollJointOffsetFromWorld(capsuleIndex);
  }
  if (capsuleIndex < static_cast<int>(m_ragdollAnimationBinding.controlledBoneIndices.size()) &&
      capsuleIndex < static_cast<int>(m_ragdollAnimationBinding.controlledBodyFromBone.size())) {
    const XMATRIX44& bodyWorld =
        m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(capsuleIndex)].body.worldTransform;
    const std::vector<int>& controlledBones =
        m_ragdollAnimationBinding.controlledBoneIndices[static_cast<std::size_t>(capsuleIndex)];
    std::vector<XMATRIX44>& controlledOffsets =
        m_ragdollAnimationBinding.controlledBodyFromBone[static_cast<std::size_t>(capsuleIndex)];
    controlledOffsets.clear();
    controlledOffsets.reserve(controlledBones.size());
    for (int controlledBone : controlledBones) {
      XMATRIX44 controlledBoneWorld;
      if (!GetRagdollAuthoringBoneWorldTransform(controlledBone, controlledBoneWorld)) {
        controlledOffsets.push_back(m_ragdollAnimationBinding.bodyFromBone[static_cast<std::size_t>(capsuleIndex)]);
        continue;
      }
      XMATRIX44 inverseControlledBoneWorld;
      if (InvertAffineNoExit(controlledBoneWorld, inverseControlledBoneWorld)) {
        controlledOffsets.push_back(bodyWorld * inverseControlledBoneWorld);
      } else {
        T8_LOG_ERROR("[RagdollEdit] Controlled bone %d for capsule %d has a singular transform; using primary capsule offset",
                     controlledBone, capsuleIndex);
        controlledOffsets.push_back(m_ragdollAnimationBinding.bodyFromBone[static_cast<std::size_t>(capsuleIndex)]);
      }
    }
  }
  UpdateRagdollJointFrameOffsetsForBody(capsuleIndex);
  return true;
}

bool SceneTemplate::SetRagdollEditCapsuleWorldTransform(int capsuleIndex, const XMATRIX44& bodyWorld, bool rebuildRagdoll) {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  auto& locals = m_ragdollAnimationBinding.bodyFromBone;
  if (capsuleIndex < 0 ||
      capsuleIndex >= static_cast<int>(bones.size()) ||
      capsuleIndex >= static_cast<int>(locals.size())) {
    return false;
  }
  EnsureRagdollFreezeState();
  if (IsRagdollCapsuleFrozen(capsuleIndex)) {
    return false;
  }
  EnsureRagdollJointFrames();

  XMATRIX44 boneWorld;
  if (!GetSkeletonEditBoneWorldTransform(bones[static_cast<std::size_t>(capsuleIndex)].body.boneIndex, boneWorld)) {
    return false;
  }
  XMATRIX44 inverseBoneWorld;
  if (!InvertAffineNoExit(boneWorld, inverseBoneWorld)) {
    T8_LOG_ERROR("[RagdollEdit] Cannot set capsule %d transform: bone %d transform is singular",
                 capsuleIndex, bones[static_cast<std::size_t>(capsuleIndex)].body.boneIndex);
    return false;
  }
  locals[static_cast<std::size_t>(capsuleIndex)] = bodyWorld * inverseBoneWorld;
  if (!UpdateRagdollReferenceBodyFromLocal(capsuleIndex)) {
    return false;
  }
  UpdateRagdollJointFrameOffsetsForBody(capsuleIndex);
  m_ragdollEditDirty = true;
  return ApplyRagdollEditPose(rebuildRagdoll);
}

bool SceneTemplate::MoveRagdollEditCapsuleByWorldDelta(int capsuleIndex, const XVECTOR3& worldDelta, bool rebuildRagdoll) {
  if (IsRagdollCapsuleFrozen(capsuleIndex)) {
    return false;
  }
  XMATRIX44 bodyWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(capsuleIndex, bodyWorld)) {
    return false;
  }
  bodyWorld.m41 += worldDelta.x;
  bodyWorld.m42 += worldDelta.y;
  bodyWorld.m43 += worldDelta.z;
  return SetRagdollEditCapsuleWorldTransform(capsuleIndex, bodyWorld, rebuildRagdoll);
}

bool SceneTemplate::RotateRagdollEditCapsuleWorld(int capsuleIndex,
                                                 const XVECTOR3& axisWorld,
                                                 float angleRadians,
                                                 bool rebuildRagdoll) {
  if (std::fabs(angleRadians) < 0.000001f) {
    return true;
  }
  if (IsRagdollCapsuleFrozen(capsuleIndex)) {
    return false;
  }

  XMATRIX44 bodyWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(capsuleIndex, bodyWorld)) {
    return false;
  }
  const XVECTOR3 center(bodyWorld.m41, bodyWorld.m42, bodyWorld.m43, 1.0f);
  XMATRIX44 toOrigin;
  XMATRIX44 rotation;
  XMATRIX44 fromOrigin;
  XMatTranslation(toOrigin, -center.x, -center.y, -center.z);
  XMatRotationAxis(rotation, Normalize3(axisWorld, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f)), angleRadians);
  XMatTranslation(fromOrigin, center.x, center.y, center.z);
  const XMATRIX44 rotatedWorld = bodyWorld * toOrigin * rotation * fromOrigin;
  return SetRagdollEditCapsuleWorldTransform(capsuleIndex, rotatedWorld, rebuildRagdoll);
}

bool SceneTemplate::FlipRagdollEditCapsuleLocalAxis(int capsuleIndex, int axisIndex) {
  if (axisIndex < 0 || axisIndex > 2 || IsRagdollCapsuleFrozen(capsuleIndex)) {
    return false;
  }
  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  if (!GetRagdollEditGizmoFrame(capsuleIndex, center, axes, size)) {
    return false;
  }
  (void)center;
  (void)size;
  return RotateRagdollEditCapsuleWorld(capsuleIndex, axes[static_cast<std::size_t>(axisIndex)], xPI, true);
}

bool SceneTemplate::AlignRagdollEditCapsuleToWorldAxis(int capsuleIndex, int axisIndex) {
  if (axisIndex < 0 || axisIndex > 2 || IsRagdollCapsuleFrozen(capsuleIndex)) {
    return false;
  }

  XMATRIX44 bodyWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(capsuleIndex, bodyWorld)) {
    return false;
  }

  std::array<XVECTOR3, 3> worldAxes = {
      XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f),
      XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f),
      XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f)};
  XVECTOR3 targetAxis = worldAxes[static_cast<std::size_t>(axisIndex)];
  const XVECTOR3 currentY = MatrixAxisY(bodyWorld);
  if (Dot3(currentY, targetAxis) < 0.0f) {
    targetAxis = targetAxis * -1.0f;
  }

  const XVECTOR3 center(bodyWorld.m41, bodyWorld.m42, bodyWorld.m43, 1.0f);
  const XMATRIX44 alignedWorld = MakeCapsuleBodyTransform(center, targetAxis, MatrixAxisX(bodyWorld));
  return SetRagdollEditCapsuleWorldTransform(capsuleIndex, alignedWorld, true);
}

bool SceneTemplate::SyncRagdollCapsuleSymmetry() {
  auto& binding = m_ragdollAnimationBinding;
  auto& bones = binding.referencePose.bones;
  auto syncStatus = [&](const char* fmt, auto... args) {
    char buffer[768];
    if constexpr (sizeof...(args) == 0) {
      std::snprintf(buffer, sizeof(buffer), "%s", fmt);
    } else {
      std::snprintf(buffer, sizeof(buffer), fmt, args...);
    }
    buffer[sizeof(buffer) - 1] = '\0';
    m_ragdollLastSyncStatus = buffer;
    T8_LOG_INFO("%s", buffer);
  };

  if (bones.empty() || bones.size() != binding.bodyFromBone.size()) {
    syncStatus("[RagdollEdit] Sync skipped: capsules=%zu localTransforms=%zu",
               bones.size(), binding.bodyFromBone.size());
    return false;
  }

  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;

  EnsureRagdollControlledBones();
  EnsureRagdollJointState();
  EnsureRagdollFreezeState();

  struct SyncCapsuleInfo {
    int capsuleIndex = -1;
    int side = 0;
    std::string key;
    XMATRIX44 world;
    XVECTOR3 center = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    std::string lowerName;
  };

  auto boneName = [&](int boneIndex) -> std::string {
    if (skeleton && boneIndex >= 0 && boneIndex < static_cast<int>(skeleton->Bones.size())) {
      return skeleton->Bones[static_cast<std::size_t>(boneIndex)].Name;
    }
    return std::string();
  };

  auto capsuleSideAndKey = [&](int capsuleIndex, int& outSide, std::string& outKey, std::string& outLowerName) {
    outSide = 0;
    outKey.clear();
    outLowerName = LowerName(bones[static_cast<std::size_t>(capsuleIndex)].body.debugName);
    const std::string primaryBoneName = boneName(bones[static_cast<std::size_t>(capsuleIndex)].body.boneIndex);
    std::vector<std::string> primaryNames;
    primaryNames.push_back(bones[static_cast<std::size_t>(capsuleIndex)].body.debugName);
    if (!primaryBoneName.empty()) {
      primaryNames.push_back(primaryBoneName);
    }

    for (const std::string& name : primaryNames) {
      const std::string lower = LowerName(name);
      const int side = DetectSymmetrySideFromLowerName(lower);
      if (side != 0) {
        outSide = side;
        outKey = NormalizeRagdollSymmetryKey(name);
        outLowerName = lower;
        return;
      }
    }
    for (const std::string& name : primaryNames) {
      const std::string lower = LowerName(name);
      if (IsCoreSymmetricCapsuleName(lower)) {
        outKey = NormalizeRagdollSymmetryKey(name);
        outLowerName = lower;
        return;
      }
    }

    bool controlledLeft = false;
    bool controlledRight = false;
    std::string controlledSideName;
    if (capsuleIndex < static_cast<int>(binding.controlledBoneIndices.size())) {
      for (int controlledBone : binding.controlledBoneIndices[static_cast<std::size_t>(capsuleIndex)]) {
        const std::string controlledName = boneName(controlledBone);
        if (controlledName.empty()) {
          continue;
        }
        const std::string lower = LowerName(controlledName);
        const int side = DetectSymmetrySideFromLowerName(lower);
        if (side < 0) {
          controlledLeft = true;
          controlledSideName = controlledName;
        } else if (side > 0) {
          controlledRight = true;
          controlledSideName = controlledName;
        }
      }
    }
    if (controlledLeft != controlledRight && !controlledSideName.empty()) {
      outSide = controlledLeft ? -1 : 1;
      outKey = NormalizeRagdollSymmetryKey(controlledSideName);
      outLowerName = LowerName(controlledSideName);
      return;
    }

    if (!primaryNames.empty()) {
      outKey = NormalizeRagdollSymmetryKey(primaryNames.front());
    }
  };

  std::vector<SyncCapsuleInfo> infos;
  infos.reserve(bones.size());
  for (int capsuleIndex = 0; capsuleIndex < static_cast<int>(bones.size()); ++capsuleIndex) {
    if (!IsEditableRagdollShape(bones[static_cast<std::size_t>(capsuleIndex)].body.shape)) {
      continue;
    }
    SyncCapsuleInfo info;
    info.capsuleIndex = capsuleIndex;
    if (!GetCurrentRagdollEditCapsuleWorld(capsuleIndex, info.world)) {
      info.world = bones[static_cast<std::size_t>(capsuleIndex)].body.worldTransform;
    }
    info.center = XVECTOR3(info.world.m41, info.world.m42, info.world.m43, 1.0f);
    capsuleSideAndKey(capsuleIndex, info.side, info.key, info.lowerName);
    infos.push_back(std::move(info));
  }

  int leftCount = 0;
  int rightCount = 0;
  int centerCount = 0;
  int unknownCount = 0;
  std::string unknownSamples;
  for (const SyncCapsuleInfo& info : infos) {
    if (info.side < 0) ++leftCount;
    else if (info.side > 0) ++rightCount;
    else {
      ++centerCount;
      if (!IsCoreSymmetricCapsuleName(info.lowerName)) {
        ++unknownCount;
        if (unknownSamples.size() < 160) {
          if (!unknownSamples.empty()) unknownSamples += ", ";
          unknownSamples += std::to_string(info.capsuleIndex) + ":" + bones[static_cast<std::size_t>(info.capsuleIndex)].body.debugName;
        }
      }
    }
  }
  T8_LOG_INFO("[RagdollEdit] Sync scan: capsules=%zu left=%d right=%d center=%d unknown=%d",
              infos.size(), leftCount, rightCount, centerCount, unknownCount);

  if (infos.size() < 2) {
    syncStatus("[RagdollEdit] Sync skipped: at least two ragdoll bodies are required");
    return false;
  }

  auto infoForCapsule = [&](int capsuleIndex) -> const SyncCapsuleInfo* {
    for (const SyncCapsuleInfo& info : infos) {
      if (info.capsuleIndex == capsuleIndex) {
        return &info;
      }
    }
    return nullptr;
  };

  auto pairScore = [&](const SyncCapsuleInfo& left, const SyncCapsuleInfo& right) {
    const auto& leftShape = bones[static_cast<std::size_t>(left.capsuleIndex)].body.shape;
    const auto& rightShape = bones[static_cast<std::size_t>(right.capsuleIndex)].body.shape;
    float bestSpatialScore = FLT_MAX;
    for (int axis = 0; axis < 3; ++axis) {
      float score = 0.0f;
      for (int other = 0; other < 3; ++other) {
        if (other == axis) {
          continue;
        }
        score += std::fabs(AxisCoord(left.center, other) - AxisCoord(right.center, other));
      }
      score -= std::fabs(AxisCoord(left.center, axis) - AxisCoord(right.center, axis)) * 0.02f;
      bestSpatialScore = (std::min)(bestSpatialScore, score);
    }
    const auto leftExtents = RagdollShapeComparableExtents(leftShape);
    const auto rightExtents = RagdollShapeComparableExtents(rightShape);
    const float shapeScore =
        std::fabs(leftExtents[0] - rightExtents[0]) +
        std::fabs(leftExtents[1] - rightExtents[1]) * 0.5f +
        std::fabs(leftExtents[2] - rightExtents[2]) +
        (leftShape.type == rightShape.type ? 0.0f : m_modelRadius * 0.05f);
    return bestSpatialScore + shapeScore;
  };

  std::vector<std::pair<int, int>> pairs;
  std::vector<const char*> pairMethods;
  std::vector<uint8_t> paired(bones.size(), 0u);
  for (const SyncCapsuleInfo& left : infos) {
    if (left.side >= 0 || left.key.empty() || paired[static_cast<std::size_t>(left.capsuleIndex)]) {
      continue;
    }
    int bestRight = -1;
    float bestScore = FLT_MAX;
    for (const SyncCapsuleInfo& right : infos) {
      if (right.side <= 0 || right.key != left.key ||
          paired[static_cast<std::size_t>(right.capsuleIndex)]) {
        continue;
      }
      const float score = pairScore(left, right);
      if (score < bestScore) {
        bestScore = score;
        bestRight = right.capsuleIndex;
      }
    }
    if (bestRight >= 0) {
      pairs.emplace_back(left.capsuleIndex, bestRight);
      pairMethods.push_back("name");
      paired[static_cast<std::size_t>(left.capsuleIndex)] = 1u;
      paired[static_cast<std::size_t>(bestRight)] = 1u;
    }
  }

  const float positionalPairThreshold = (std::max)(0.05f, m_modelRadius * 0.55f);
  int geometryPairCount = 0;
  for (const SyncCapsuleInfo& left : infos) {
    if (left.side >= 0 || paired[static_cast<std::size_t>(left.capsuleIndex)]) {
      continue;
    }
    int bestRight = -1;
    float bestScore = FLT_MAX;
    for (const SyncCapsuleInfo& right : infos) {
      if (right.side <= 0 || paired[static_cast<std::size_t>(right.capsuleIndex)]) {
        continue;
      }
      const float score = pairScore(left, right);
      if (score < bestScore) {
        bestScore = score;
        bestRight = right.capsuleIndex;
      }
    }
    if (bestRight >= 0 && bestScore <= positionalPairThreshold) {
      pairs.emplace_back(left.capsuleIndex, bestRight);
      pairMethods.push_back("geometry");
      paired[static_cast<std::size_t>(left.capsuleIndex)] = 1u;
      paired[static_cast<std::size_t>(bestRight)] = 1u;
      ++geometryPairCount;
    }
  }
  if (geometryPairCount > 0) {
    T8_LOG_INFO("[RagdollEdit] Sync used %d geometry-based fallback pairs", geometryPairCount);
  }

  std::string unmatchedSamples;
  int unmatchedCount = 0;
  for (const SyncCapsuleInfo& info : infos) {
    if (info.side == 0 || paired[static_cast<std::size_t>(info.capsuleIndex)]) {
      continue;
    }
    ++unmatchedCount;
    if (unmatchedSamples.size() < 220) {
      if (!unmatchedSamples.empty()) unmatchedSamples += ", ";
      unmatchedSamples += std::to_string(info.capsuleIndex) + ":" + bones[static_cast<std::size_t>(info.capsuleIndex)].body.debugName;
    }
  }
  if (unmatchedCount > 0) {
    T8_LOG_INFO("[RagdollEdit] Sync unmatched side capsules=%d: %s",
                unmatchedCount,
                unmatchedSamples.empty() ? "<none>" : unmatchedSamples.c_str());
  }

  auto findMirroredBone = [&](int boneIndex) {
    const std::string name = boneName(boneIndex);
    if (!skeleton || name.empty()) {
      return -1;
    }
    const std::string lower = LowerName(name);
    const int side = DetectSymmetrySideFromLowerName(lower);
    if (side == 0) {
      return -1;
    }
    const std::string key = NormalizeRagdollSymmetryKey(name);
    if (key.empty()) {
      return -1;
    }
    for (int candidate = 0; candidate < static_cast<int>(skeleton->Bones.size()); ++candidate) {
      if (candidate == boneIndex) {
        continue;
      }
      const std::string candidateName = skeleton->Bones[static_cast<std::size_t>(candidate)].Name;
      const std::string candidateLower = LowerName(candidateName);
      if (DetectSymmetrySideFromLowerName(candidateLower) == -side &&
          NormalizeRagdollSymmetryKey(candidateName) == key) {
        return candidate;
      }
    }
    return -1;
  };

  auto inferMirrorPlaneFromSkeleton = [&](int& outAxis, float& outPlane) {
    if (!skeleton) {
      return false;
    }
    struct BoneMirrorPair {
      XVECTOR3 left = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
      XVECTOR3 right = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    };
    std::vector<BoneMirrorPair> bonePairs;
    std::vector<uint8_t> used(skeleton->Bones.size(), 0u);
    for (int boneIndex = 0; boneIndex < static_cast<int>(skeleton->Bones.size()); ++boneIndex) {
      if (used[static_cast<std::size_t>(boneIndex)] != 0u) {
        continue;
      }
      const std::string lower = LowerName(skeleton->Bones[static_cast<std::size_t>(boneIndex)].Name);
      const int side = DetectSymmetrySideFromLowerName(lower);
      if (side == 0) {
        continue;
      }
      const int mirroredBone = findMirroredBone(boneIndex);
      if (mirroredBone < 0 ||
          mirroredBone >= static_cast<int>(skeleton->Bones.size()) ||
          used[static_cast<std::size_t>(mirroredBone)] != 0u) {
        continue;
      }
      XMATRIX44 boneWorld;
      XMATRIX44 mirroredWorld;
      if (!GetSkeletonEditBoneWorldTransform(boneIndex, boneWorld) ||
          !GetSkeletonEditBoneWorldTransform(mirroredBone, mirroredWorld)) {
        continue;
      }
      BoneMirrorPair pair;
      const XVECTOR3 a(boneWorld.m41, boneWorld.m42, boneWorld.m43, 1.0f);
      const XVECTOR3 b(mirroredWorld.m41, mirroredWorld.m42, mirroredWorld.m43, 1.0f);
      pair.left = side < 0 ? a : b;
      pair.right = side < 0 ? b : a;
      bonePairs.push_back(pair);
      used[static_cast<std::size_t>(boneIndex)] = 1u;
      used[static_cast<std::size_t>(mirroredBone)] = 1u;
    }
    if (bonePairs.empty()) {
      return false;
    }

    float bestScore = FLT_MAX;
    int bestAxis = 0;
    float bestPlane = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
      float plane = 0.0f;
      for (const BoneMirrorPair& pair : bonePairs) {
        plane += (AxisCoord(pair.left, axis) + AxisCoord(pair.right, axis)) * 0.5f;
      }
      plane /= static_cast<float>(bonePairs.size());

      float score = 0.0f;
      for (const BoneMirrorPair& pair : bonePairs) {
        const float midpoint = (AxisCoord(pair.left, axis) + AxisCoord(pair.right, axis)) * 0.5f;
        score += std::fabs(midpoint - plane) * 0.25f;
        for (int other = 0; other < 3; ++other) {
          if (other != axis) {
            score += std::fabs(AxisCoord(pair.left, other) - AxisCoord(pair.right, other));
          }
        }
      }
      if (score < bestScore) {
        bestScore = score;
        bestAxis = axis;
        bestPlane = plane;
      }
    }

    outAxis = bestAxis;
    outPlane = bestPlane;
    return true;
  };

  for (std::size_t i = 0; i < pairs.size() && i < 16; ++i) {
    const int left = pairs[i].first;
    const int right = pairs[i].second;
    T8_LOG_INFO("[RagdollEdit] Sync pair %zu (%s): %d '%s' <-> %d '%s'",
                i,
                i < pairMethods.size() ? pairMethods[i] : "unknown",
                left,
                bones[static_cast<std::size_t>(left)].body.debugName.c_str(),
                right,
                bones[static_cast<std::size_t>(right)].body.debugName.c_str());
  }

  int mirrorAxis = 0;
  float mirrorPlane = 0.0f;
  if (!pairs.empty()) {
    float bestAxisScore = FLT_MAX;
    for (int axis = 0; axis < 3; ++axis) {
      float plane = 0.0f;
      for (const auto& pair : pairs) {
        const SyncCapsuleInfo* a = infoForCapsule(pair.first);
        const SyncCapsuleInfo* b = infoForCapsule(pair.second);
        if (!a || !b) {
          continue;
        }
        plane += (AxisCoord(a->center, axis) + AxisCoord(b->center, axis)) * 0.5f;
      }
      plane /= static_cast<float>(pairs.size());

      float score = 0.0f;
      for (const auto& pair : pairs) {
        const SyncCapsuleInfo* a = infoForCapsule(pair.first);
        const SyncCapsuleInfo* b = infoForCapsule(pair.second);
        if (!a || !b) {
          continue;
        }
        const float midpoint = (AxisCoord(a->center, axis) + AxisCoord(b->center, axis)) * 0.5f;
        score += std::fabs(midpoint - plane) * 0.25f;
        for (int other = 0; other < 3; ++other) {
          if (other != axis) {
            score += std::fabs(AxisCoord(a->center, other) - AxisCoord(b->center, other));
          }
        }
      }

      if (score < bestAxisScore) {
        bestAxisScore = score;
        mirrorAxis = axis;
        mirrorPlane = plane;
      }
    }
  } else if (!inferMirrorPlaneFromSkeleton(mirrorAxis, mirrorPlane)) {
    syncStatus("[RagdollEdit] Sync skipped: no left/right body pairs were detected and the skeleton mirror plane could not be inferred. Unclassified examples: %s",
               unknownSamples.empty() ? "<none>" : unknownSamples.c_str());
    return false;
  }
  const char mirrorAxisName = mirrorAxis == 0 ? 'X' : (mirrorAxis == 1 ? 'Y' : 'Z');
  T8_LOG_INFO("[RagdollEdit] Sync mirror plane: %c=%.3f from %zu pairs",
              mirrorAxisName, mirrorPlane, pairs.size());

  std::vector<int> mirrorCapsule(bones.size(), -1);
  for (const auto& pair : pairs) {
    mirrorCapsule[static_cast<std::size_t>(pair.first)] = pair.second;
    mirrorCapsule[static_cast<std::size_t>(pair.second)] = pair.first;
  }
  for (const SyncCapsuleInfo& info : infos) {
    if (info.side == 0 && IsCoreSymmetricCapsuleName(info.lowerName)) {
      mirrorCapsule[static_cast<std::size_t>(info.capsuleIndex)] = info.capsuleIndex;
    }
  }

  std::vector<int> createdCapsules;
  int createdBodyCount = 0;
  int boneMatchedPairCount = 0;
  auto registerMirrorPair = [&](int sourceCapsule, int targetCapsule, int sourceSide, const char* method) {
    if (sourceCapsule < 0 || targetCapsule < 0 || sourceCapsule == targetCapsule) {
      return false;
    }
    const int requiredSize = (std::max)(sourceCapsule, targetCapsule) + 1;
    if (static_cast<int>(mirrorCapsule.size()) < requiredSize) {
      mirrorCapsule.resize(static_cast<std::size_t>(requiredSize), -1);
    }
    if (mirrorCapsule[static_cast<std::size_t>(sourceCapsule)] >= 0 ||
        mirrorCapsule[static_cast<std::size_t>(targetCapsule)] >= 0) {
      return false;
    }
    mirrorCapsule[static_cast<std::size_t>(sourceCapsule)] = targetCapsule;
    mirrorCapsule[static_cast<std::size_t>(targetCapsule)] = sourceCapsule;
    if (sourceSide < 0) {
      pairs.emplace_back(sourceCapsule, targetCapsule);
    } else {
      pairs.emplace_back(targetCapsule, sourceCapsule);
    }
    pairMethods.push_back(method);
    return true;
  };

  auto appendInfoForCapsule = [&](int capsuleIndex, int side, const std::string& key, const XMATRIX44& world) {
    SyncCapsuleInfo info;
    info.capsuleIndex = capsuleIndex;
    info.side = side;
    info.key = key;
    info.world = world;
    info.center = XVECTOR3(world.m41, world.m42, world.m43, 1.0f);
    info.lowerName = LowerName(bones[static_cast<std::size_t>(capsuleIndex)].body.debugName);
    infos.push_back(std::move(info));
  };

  auto createMirroredBody = [&](const SyncCapsuleInfo& sourceInfo, int targetBone) {
    const int sourceCapsule = sourceInfo.capsuleIndex;
    if (sourceCapsule < 0 ||
        sourceCapsule >= static_cast<int>(bones.size()) ||
        targetBone < 0) {
      return -1;
    }

    XMATRIX44 targetBoneWorld;
    if (!GetSkeletonEditBoneWorldTransform(targetBone, targetBoneWorld)) {
      return -1;
    }
    XMATRIX44 inverseTargetBoneWorld;
    if (!InvertAffineNoExit(targetBoneWorld, inverseTargetBoneWorld)) {
      T8_LOG_ERROR("[RagdollEdit] Sync cannot create mirrored body for bone %d: target bone transform is singular", targetBone);
      return -1;
    }

    const XMATRIX44 mirroredWorld = MirrorCapsuleTransformAcrossAxisPlane(sourceInfo.world, mirrorAxis, mirrorPlane);
    t850::PhysicsRagdollBoneDesc mirroredBone = bones[static_cast<std::size_t>(sourceCapsule)];
    mirroredBone.body.boneIndex = targetBone;
    mirroredBone.body.debugName = boneName(targetBone);
    if (mirroredBone.body.debugName.empty()) {
      mirroredBone.body.debugName = "mirrored_body_" + std::to_string(targetBone);
    }
    mirroredBone.body.entityId = Meshes[0].GetEntityId();
    mirroredBone.body.motion = t850::PhysicsBodyMotion::Kinematic;
    mirroredBone.body.worldTransform = mirroredWorld;
    mirroredBone.parentBoneIndex = -1;
    mirroredBone.jointWorldPosition = MirrorPointAcrossAxisPlane(mirroredBone.jointWorldPosition, mirrorAxis, mirrorPlane);
    mirroredBone.parentJointTwistAxis = MirrorVectorAcrossAxis(mirroredBone.parentJointTwistAxis, mirrorAxis);
    mirroredBone.parentJointPlaneAxis = MirrorVectorAcrossAxis(mirroredBone.parentJointPlaneAxis, mirrorAxis);
    mirroredBone.childJointTwistAxis = MirrorVectorAcrossAxis(mirroredBone.childJointTwistAxis, mirrorAxis);
    mirroredBone.childJointPlaneAxis = MirrorVectorAcrossAxis(mirroredBone.childJointPlaneAxis, mirrorAxis);

    const XMATRIX44 bodyFromBone = mirroredWorld * inverseTargetBoneWorld;
    auto& desc = binding.referencePose;
    desc.bones.push_back(mirroredBone);
    binding.bodyFromBone.push_back(bodyFromBone);
    binding.jointFromBone.push_back(XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
    binding.parentJointTwistFromBody.push_back(XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
    binding.parentJointPlaneFromBody.push_back(XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
    binding.childJointTwistFromBody.push_back(XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
    binding.childJointPlaneFromBody.push_back(XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
    binding.controlledBoneIndices.emplace_back();
    binding.controlledBodyFromBone.emplace_back();
    m_ragdollParentCapsules.push_back(-1);
    m_ragdollJointParentCapsules.push_back(kRagdollJointInheritParent);
    m_ragdollFrozenCapsules.push_back(0u);
    m_ragdollFrozenJoints.push_back(0u);
    m_ragdollContactJoints.push_back(0u);

    const int newIndex = static_cast<int>(desc.bones.size()) - 1;
    if (static_cast<int>(mirrorCapsule.size()) <= newIndex) {
      mirrorCapsule.resize(static_cast<std::size_t>(newIndex + 1), -1);
    }
    if (sourceCapsule < static_cast<int>(binding.controlledBoneIndices.size())) {
      auto& targetControlled = binding.controlledBoneIndices[static_cast<std::size_t>(newIndex)];
      for (int sourceBone : binding.controlledBoneIndices[static_cast<std::size_t>(sourceCapsule)]) {
        const int mirroredControlledBone = findMirroredBone(sourceBone);
        if (mirroredControlledBone < 0 ||
            std::find(targetControlled.begin(), targetControlled.end(), mirroredControlledBone) != targetControlled.end()) {
          continue;
        }
        const int currentOwner = FindRagdollCapsuleControllingBone(mirroredControlledBone);
        if (currentOwner >= 0 && currentOwner != newIndex) {
          continue;
        }
        targetControlled.push_back(mirroredControlledBone);
      }
    }

    if (!UpdateRagdollJointOffsetFromWorld(newIndex) ||
        !UpdateRagdollReferenceBodyFromLocal(newIndex)) {
      desc.bones.pop_back();
      binding.bodyFromBone.pop_back();
      binding.jointFromBone.pop_back();
      binding.parentJointTwistFromBody.pop_back();
      binding.parentJointPlaneFromBody.pop_back();
      binding.childJointTwistFromBody.pop_back();
      binding.childJointPlaneFromBody.pop_back();
      binding.controlledBoneIndices.pop_back();
      binding.controlledBodyFromBone.pop_back();
      m_ragdollParentCapsules.pop_back();
      m_ragdollJointParentCapsules.pop_back();
      m_ragdollFrozenCapsules.pop_back();
      m_ragdollFrozenJoints.pop_back();
      m_ragdollContactJoints.pop_back();
      T8_LOG_ERROR("[RagdollEdit] Sync failed to create mirrored body for source %d -> bone %d",
                   sourceCapsule, targetBone);
      return -1;
    }

    createdCapsules.push_back(newIndex);
    ++createdBodyCount;
    appendInfoForCapsule(newIndex, -sourceInfo.side, sourceInfo.key, mirroredWorld);
    T8_LOG_INFO("[RagdollEdit] Sync created mirrored body %d '%s' from body %d '%s'",
                newIndex,
                desc.bones[static_cast<std::size_t>(newIndex)].body.debugName.c_str(),
                sourceCapsule,
                desc.bones[static_cast<std::size_t>(sourceCapsule)].body.debugName.c_str());
    return newIndex;
  };

  const int originalInfoCount = static_cast<int>(infos.size());
  for (int infoIndex = 0; infoIndex < originalInfoCount; ++infoIndex) {
    const SyncCapsuleInfo sourceInfo = infos[static_cast<std::size_t>(infoIndex)];
    if (sourceInfo.side == 0 ||
        sourceInfo.capsuleIndex < 0 ||
        sourceInfo.capsuleIndex >= static_cast<int>(mirrorCapsule.size()) ||
        mirrorCapsule[static_cast<std::size_t>(sourceInfo.capsuleIndex)] >= 0) {
      continue;
    }

    const int targetBone = findMirroredBone(bones[static_cast<std::size_t>(sourceInfo.capsuleIndex)].body.boneIndex);
    if (targetBone < 0) {
      continue;
    }

    int targetCapsule = FindRagdollCapsuleForBone(targetBone);
    if (targetCapsule < 0) {
      const int controllingCapsule = FindRagdollCapsuleControllingBone(targetBone);
      if (controllingCapsule >= 0) {
        continue;
      }
      targetCapsule = createMirroredBody(sourceInfo, targetBone);
    } else {
      ++boneMatchedPairCount;
    }

    if (targetCapsule >= 0 &&
        registerMirrorPair(sourceInfo.capsuleIndex,
                           targetCapsule,
                           sourceInfo.side,
                           targetCapsule >= originalInfoCount ? "created" : "bone")) {
      if (static_cast<int>(paired.size()) <= (std::max)(sourceInfo.capsuleIndex, targetCapsule)) {
        paired.resize(static_cast<std::size_t>((std::max)(sourceInfo.capsuleIndex, targetCapsule) + 1), 0u);
      }
      paired[static_cast<std::size_t>(sourceInfo.capsuleIndex)] = 1u;
      paired[static_cast<std::size_t>(targetCapsule)] = 1u;
    }
  }
  if (createdBodyCount > 0 || boneMatchedPairCount > 0) {
    T8_LOG_INFO("[RagdollEdit] Sync mirrored missing bodies: created=%d bone-matched=%d",
                createdBodyCount,
                boneMatchedPairCount);
  }

  auto assignShapeFrom = [&](int capsuleIndex, const t850::PhysicsShapeDesc& sourceShape) {
    auto& shape = bones[static_cast<std::size_t>(capsuleIndex)].body.shape;
    bool changed = shape.type != sourceShape.type;
    if (sourceShape.type == t850::PhysicsShapeType::Box) {
      const XVECTOR3 newHalfExtents = ClampRagdollBoxHalfExtents(sourceShape.halfExtents);
      if (std::fabs(shape.halfExtents.x - newHalfExtents.x) > 0.000001f ||
          std::fabs(shape.halfExtents.y - newHalfExtents.y) > 0.000001f ||
          std::fabs(shape.halfExtents.z - newHalfExtents.z) > 0.000001f) {
        changed = true;
      }
      shape.type = t850::PhysicsShapeType::Box;
      shape.halfExtents = newHalfExtents;
    } else {
      const float newRadius = (std::max)(kRagdollMinShapeExtent, sourceShape.radius);
      const float newHalfHeight = (std::max)(kRagdollMinShapeExtent, sourceShape.halfHeight);
      if (std::fabs(shape.radius - newRadius) > 0.000001f ||
          std::fabs(shape.halfHeight - newHalfHeight) > 0.000001f) {
        changed = true;
      }
      shape.type = t850::PhysicsShapeType::Capsule;
      shape.radius = newRadius;
      shape.halfHeight = newHalfHeight;
      shape.halfExtents = EquivalentBoxHalfExtentsFromCapsule(shape);
    }
    return changed;
  };

  auto assignAveragedShape = [&](int left, int right) {
    const auto& leftShape = bones[static_cast<std::size_t>(left)].body.shape;
    const auto& rightShape = bones[static_cast<std::size_t>(right)].body.shape;
    if (leftShape.type != rightShape.type) {
      return 0;
    }
    t850::PhysicsShapeDesc averaged = leftShape;
    if (leftShape.type == t850::PhysicsShapeType::Box) {
      const XVECTOR3 leftExtents = ClampRagdollBoxHalfExtents(leftShape.halfExtents);
      const XVECTOR3 rightExtents = ClampRagdollBoxHalfExtents(rightShape.halfExtents);
      averaged.halfExtents = XVECTOR3((leftExtents.x + rightExtents.x) * 0.5f,
                                      (leftExtents.y + rightExtents.y) * 0.5f,
                                      (leftExtents.z + rightExtents.z) * 0.5f,
                                      0.0f);
    } else {
      averaged.radius = (leftShape.radius + rightShape.radius) * 0.5f;
      averaged.halfHeight = (leftShape.halfHeight + rightShape.halfHeight) * 0.5f;
    }
    int changes = 0;
    changes += assignShapeFrom(left, averaged) ? 1 : 0;
    changes += assignShapeFrom(right, averaged) ? 1 : 0;
    return changes;
  };

  auto averagedMirrorTransform = [&](const XMATRIX44& aWorld, const XMATRIX44& bWorld) {
    const XMATRIX44 bMirrored = MirrorCapsuleTransformAcrossAxisPlane(bWorld, mirrorAxis, mirrorPlane);
    const XVECTOR3 centerA(aWorld.m41, aWorld.m42, aWorld.m43, 1.0f);
    const XVECTOR3 centerBMirrored(bMirrored.m41, bMirrored.m42, bMirrored.m43, 1.0f);
    XVECTOR3 averageCenter = (centerA + centerBMirrored) * 0.5f;
    averageCenter.w = 1.0f;

    XVECTOR3 yA = MatrixAxisY(aWorld);
    XVECTOR3 yB = MatrixAxisY(bMirrored);
    if (Dot3(yA, yB) < 0.0f) {
      yB = yB * -1.0f;
    }
    XVECTOR3 xA = MatrixAxisX(aWorld);
    XVECTOR3 xB = MatrixAxisX(bMirrored);
    if (Dot3(xA, xB) < 0.0f) {
      xB = xB * -1.0f;
    }
    const XVECTOR3 averageY = Normalize3(yA + yB, yA);
    const XVECTOR3 averageX = Normalize3(xA + xB, xA);
    return MakeCapsuleBodyTransform(averageCenter, averageY, averageX);
  };

  int transformCount = 0;
  int shapeCount = 0;
  auto applySyncedWorld = [&](int capsuleIndex, const XMATRIX44& targetWorld) {
    XMATRIX44 currentWorld;
    if (GetCurrentRagdollEditCapsuleWorld(capsuleIndex, currentWorld) &&
        MatrixMaxAbsDiff(currentWorld, targetWorld) <= 0.0001f) {
      return false;
    }
    return SetRagdollEditCapsuleWorldTransform(capsuleIndex, targetWorld, false);
  };

  for (const auto& pair : pairs) {
    const int left = pair.first;
    const int right = pair.second;
    const SyncCapsuleInfo* leftInfo = infoForCapsule(left);
    const SyncCapsuleInfo* rightInfo = infoForCapsule(right);
    if (!leftInfo || !rightInfo) {
      continue;
    }

    const bool leftFrozen = IsRagdollCapsuleFrozen(left);
    const bool rightFrozen = IsRagdollCapsuleFrozen(right);
    const auto& leftShape = bones[static_cast<std::size_t>(left)].body.shape;
    const auto& rightShape = bones[static_cast<std::size_t>(right)].body.shape;

    if (!leftFrozen && !rightFrozen) {
      shapeCount += assignAveragedShape(left, right);

      const XMATRIX44 syncedLeft = averagedMirrorTransform(leftInfo->world, rightInfo->world);
      const XMATRIX44 syncedRight = MirrorCapsuleTransformAcrossAxisPlane(syncedLeft, mirrorAxis, mirrorPlane);
      if (applySyncedWorld(left, syncedLeft)) {
        ++transformCount;
      }
      if (applySyncedWorld(right, syncedRight)) {
        ++transformCount;
      }
    } else if (leftFrozen && !rightFrozen) {
      shapeCount += assignShapeFrom(right, leftShape) ? 1 : 0;
      const XMATRIX44 syncedRight = MirrorCapsuleTransformAcrossAxisPlane(leftInfo->world, mirrorAxis, mirrorPlane);
      if (applySyncedWorld(right, syncedRight)) {
        ++transformCount;
      }
    } else if (!leftFrozen && rightFrozen) {
      shapeCount += assignShapeFrom(left, rightShape) ? 1 : 0;
      const XMATRIX44 syncedLeft = MirrorCapsuleTransformAcrossAxisPlane(rightInfo->world, mirrorAxis, mirrorPlane);
      if (applySyncedWorld(left, syncedLeft)) {
        ++transformCount;
      }
    }
  }

  auto axisUnit = [](int axis) {
    if (axis == 0) return XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
    if (axis == 1) return XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
    return XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
  };

  int bodyUpAxis = mirrorAxis == 1 ? 2 : 1;
  float bestBodyRange = -1.0f;
  for (int axis = 0; axis < 3; ++axis) {
    if (axis == mirrorAxis) {
      continue;
    }
    float minCoord = (std::numeric_limits<float>::max)();
    float maxCoord = -(std::numeric_limits<float>::max)();
    for (const SyncCapsuleInfo& info : infos) {
      XMATRIX44 world;
      if (!GetCurrentRagdollEditCapsuleWorld(info.capsuleIndex, world)) {
        continue;
      }
      const XVECTOR3 center(world.m41, world.m42, world.m43, 1.0f);
      const float coord = AxisCoord(center, axis);
      minCoord = (std::min)(minCoord, coord);
      maxCoord = (std::max)(maxCoord, coord);
    }
    if (minCoord <= maxCoord && maxCoord - minCoord > bestBodyRange) {
      bestBodyRange = maxCoord - minCoord;
      bodyUpAxis = axis;
    }
  }

  XVECTOR3 centerlineDirection = axisUnit(bodyUpAxis);
  XVECTOR3 lowestCore;
  XVECTOR3 highestCore;
  float lowestCoreCoord = (std::numeric_limits<float>::max)();
  float highestCoreCoord = -(std::numeric_limits<float>::max)();
  int centerCoreCount = 0;
  for (const SyncCapsuleInfo& info : infos) {
    if (info.side != 0 ||
        mirrorCapsule[static_cast<std::size_t>(info.capsuleIndex)] != info.capsuleIndex) {
      continue;
    }
    XMATRIX44 world;
    if (!GetCurrentRagdollEditCapsuleWorld(info.capsuleIndex, world)) {
      continue;
    }
    XVECTOR3 center(world.m41, world.m42, world.m43, 1.0f);
    SetAxisCoord(center, mirrorAxis, mirrorPlane);
    const float coord = AxisCoord(center, bodyUpAxis);
    if (coord < lowestCoreCoord) {
      lowestCoreCoord = coord;
      lowestCore = center;
    }
    if (coord > highestCoreCoord) {
      highestCoreCoord = coord;
      highestCore = center;
    }
    ++centerCoreCount;
  }
  if (centerCoreCount >= 2) {
    XVECTOR3 inferred = highestCore - lowestCore;
    inferred.w = 0.0f;
    SetAxisCoord(inferred, mirrorAxis, 0.0f);
    centerlineDirection = Normalize3(inferred, centerlineDirection);
    if (AxisCoord(centerlineDirection, bodyUpAxis) < 0.0f) {
      centerlineDirection = centerlineDirection * -1.0f;
    }
  }

  const XVECTOR3 mirrorNormal = axisUnit(mirrorAxis);
  int centeredCount = 0;
  for (const SyncCapsuleInfo& info : infos) {
    if (info.side != 0 ||
        mirrorCapsule[static_cast<std::size_t>(info.capsuleIndex)] != info.capsuleIndex ||
        IsRagdollCapsuleFrozen(info.capsuleIndex)) {
      continue;
    }
    XMATRIX44 world;
    if (!GetCurrentRagdollEditCapsuleWorld(info.capsuleIndex, world)) {
      continue;
    }
    XVECTOR3 center(world.m41, world.m42, world.m43, 1.0f);
    SetAxisCoord(center, mirrorAxis, mirrorPlane);
    const XMATRIX44 centeredWorld = MakeCapsuleBodyTransform(center, centerlineDirection, mirrorNormal);
    if (applySyncedWorld(info.capsuleIndex, centeredWorld)) {
      ++centeredCount;
    }
  }

  auto mirrorCapsuleIndex = [&](int capsuleIndex) {
    if (capsuleIndex < 0) {
      return capsuleIndex;
    }
    if (capsuleIndex >= static_cast<int>(mirrorCapsule.size())) {
      return -1;
    }
    return mirrorCapsule[static_cast<std::size_t>(capsuleIndex)] >= 0
        ? mirrorCapsule[static_cast<std::size_t>(capsuleIndex)]
        : -1;
  };

  auto wouldCreateCycle = [&](int child, int parent, const std::vector<int>& parents) {
    if (parent < 0 || parent >= static_cast<int>(parents.size()) || child == parent) {
      return true;
    }
    int current = parent;
    for (std::size_t depth = 0; depth < parents.size(); ++depth) {
      if (current == child) {
        return true;
      }
      if (current < 0 || current >= static_cast<int>(parents.size())) {
        return false;
      }
      current = parents[static_cast<std::size_t>(current)];
    }
    return true;
  };

  const std::vector<int> originalParents = m_ragdollParentCapsules;
  const std::vector<int> originalJointParents = m_ragdollJointParentCapsules;
  auto relationshipConfidence = [&](int capsuleIndex) {
    int score = 0;
    if (capsuleIndex >= 0 && capsuleIndex < static_cast<int>(originalParents.size()) &&
        originalParents[static_cast<std::size_t>(capsuleIndex)] >= 0) {
      score += 2;
    }
    if (capsuleIndex >= 0 && capsuleIndex < static_cast<int>(originalJointParents.size()) &&
        originalJointParents[static_cast<std::size_t>(capsuleIndex)] >= 0) {
      score += 4;
    }
    if (capsuleIndex >= 0 && capsuleIndex < static_cast<int>(m_ragdollContactJoints.size()) &&
        m_ragdollContactJoints[static_cast<std::size_t>(capsuleIndex)] != 0u) {
      score += 1;
    }
    if (capsuleIndex >= 0 && capsuleIndex < static_cast<int>(binding.controlledBoneIndices.size())) {
      score += static_cast<int>((std::min<std::size_t>)(binding.controlledBoneIndices[static_cast<std::size_t>(capsuleIndex)].size(), 4u));
    }
    return score;
  };

  int relationshipCount = 0;
  for (const auto& pair : pairs) {
    int source = pair.first;
    int target = pair.second;
    if (relationshipConfidence(pair.second) > relationshipConfidence(pair.first)) {
      source = pair.second;
      target = pair.first;
    }
    if (IsRagdollCapsuleFrozen(target)) {
      continue;
    }

    if (source < static_cast<int>(originalParents.size())) {
      const int mirroredParent = mirrorCapsuleIndex(originalParents[static_cast<std::size_t>(source)]);
      if (mirroredParent >= 0 &&
          mirroredParent < static_cast<int>(m_ragdollParentCapsules.size()) &&
          mirroredParent != target &&
          !wouldCreateCycle(target, mirroredParent, m_ragdollParentCapsules) &&
          m_ragdollParentCapsules[static_cast<std::size_t>(target)] != mirroredParent) {
        m_ragdollParentCapsules[static_cast<std::size_t>(target)] = mirroredParent;
        ++relationshipCount;
      }
    }

    if (target < static_cast<int>(m_ragdollJointParentCapsules.size()) &&
        source < static_cast<int>(originalJointParents.size()) &&
        !IsRagdollJointFrozen(target)) {
      const int sourceJointParent = originalJointParents[static_cast<std::size_t>(source)];
      int mirroredJointParent = sourceJointParent;
      if (sourceJointParent >= 0) {
        mirroredJointParent = mirrorCapsuleIndex(sourceJointParent);
      }
      if ((sourceJointParent == kRagdollJointDisabled || sourceJointParent == kRagdollJointInheritParent ||
           (mirroredJointParent >= 0 && mirroredJointParent < static_cast<int>(bones.size()) && mirroredJointParent != target)) &&
          m_ragdollJointParentCapsules[static_cast<std::size_t>(target)] != mirroredJointParent) {
        m_ragdollJointParentCapsules[static_cast<std::size_t>(target)] = mirroredJointParent;
        ++relationshipCount;
      }
      if (source < static_cast<int>(m_ragdollContactJoints.size()) &&
          target < static_cast<int>(m_ragdollContactJoints.size()) &&
          m_ragdollContactJoints[static_cast<std::size_t>(target)] != m_ragdollContactJoints[static_cast<std::size_t>(source)]) {
        m_ragdollContactJoints[static_cast<std::size_t>(target)] = m_ragdollContactJoints[static_cast<std::size_t>(source)];
        ++relationshipCount;
      }
    }
  }

  auto inferSkeletonParentCapsule = [&](int childCapsule) {
    if (!skeleton ||
        childCapsule < 0 ||
        childCapsule >= static_cast<int>(bones.size())) {
      return -1;
    }
    int currentBone = bones[static_cast<std::size_t>(childCapsule)].body.boneIndex;
    for (std::size_t depth = 0; depth < skeleton->Bones.size(); ++depth) {
      if (currentBone < 0 || currentBone >= static_cast<int>(skeleton->Bones.size())) {
        break;
      }
      const xF::xBone& skeletonBone = skeleton->Bones[static_cast<std::size_t>(currentBone)];
      if (skeletonBone.Dad == static_cast<unsigned short>(currentBone) ||
          skeletonBone.Dad >= skeleton->Bones.size()) {
        break;
      }
      currentBone = skeletonBone.Dad;
      const int parentCapsule = FindRagdollCapsuleForBone(currentBone);
      if (parentCapsule >= 0 && parentCapsule != childCapsule) {
        return parentCapsule;
      }
    }
    return -1;
  };

  for (int createdCapsule : createdCapsules) {
    if (createdCapsule < 0 ||
        createdCapsule >= static_cast<int>(m_ragdollParentCapsules.size()) ||
        m_ragdollParentCapsules[static_cast<std::size_t>(createdCapsule)] >= 0) {
      continue;
    }
    const int inferredParent = inferSkeletonParentCapsule(createdCapsule);
    if (inferredParent >= 0 &&
        inferredParent < static_cast<int>(m_ragdollParentCapsules.size()) &&
        !wouldCreateCycle(createdCapsule, inferredParent, m_ragdollParentCapsules)) {
      m_ragdollParentCapsules[static_cast<std::size_t>(createdCapsule)] = inferredParent;
      ++relationshipCount;
    }
  }

  auto addMirroredControlledBones = [&](int sourceCapsule, int targetCapsule) {
    if (!skeleton ||
        sourceCapsule < 0 || targetCapsule < 0 ||
        sourceCapsule >= static_cast<int>(binding.controlledBoneIndices.size()) ||
        targetCapsule >= static_cast<int>(binding.controlledBoneIndices.size()) ||
        IsRagdollCapsuleFrozen(targetCapsule)) {
      return 0;
    }
    const std::vector<int> sourceBones = binding.controlledBoneIndices[static_cast<std::size_t>(sourceCapsule)];
    std::vector<int>& targetBones = binding.controlledBoneIndices[static_cast<std::size_t>(targetCapsule)];
    int added = 0;
    for (int sourceBone : sourceBones) {
      const int mirroredBone = findMirroredBone(sourceBone);
      if (mirroredBone < 0 ||
          std::find(targetBones.begin(), targetBones.end(), mirroredBone) != targetBones.end()) {
        continue;
      }
      const int currentOwner = FindRagdollCapsuleControllingBone(mirroredBone);
      if (currentOwner >= 0 && currentOwner != targetCapsule) {
        continue;
      }
      targetBones.push_back(mirroredBone);
      ++added;
    }
    if (added > 0) {
      UpdateRagdollReferenceBodyFromLocal(targetCapsule);
    }
    return added;
  };

  int controlledBoneCount = 0;
  for (const auto& pair : pairs) {
    controlledBoneCount += addMirroredControlledBones(pair.first, pair.second);
    controlledBoneCount += addMirroredControlledBones(pair.second, pair.first);
  }

  int jointLimitCount = 0;
  for (const auto& pair : pairs) {
    const int left = pair.first;
    const int right = pair.second;
    if (IsRagdollJointFrozen(left) || IsRagdollJointFrozen(right)) {
      continue;
    }
    auto& leftBone = bones[static_cast<std::size_t>(left)];
    auto& rightBone = bones[static_cast<std::size_t>(right)];
    const float swing = (leftBone.swingLimitRadians + rightBone.swingLimitRadians) * 0.5f;
    const float twist = (leftBone.twistLimitRadians + rightBone.twistLimitRadians) * 0.5f;
    if (std::fabs(leftBone.swingLimitRadians - swing) > 0.000001f ||
        std::fabs(rightBone.swingLimitRadians - swing) > 0.000001f ||
        std::fabs(leftBone.twistLimitRadians - twist) > 0.000001f ||
        std::fabs(rightBone.twistLimitRadians - twist) > 0.000001f) {
      leftBone.swingLimitRadians = swing;
      rightBone.swingLimitRadians = swing;
      leftBone.twistLimitRadians = twist;
      rightBone.twistLimitRadians = twist;
      ++jointLimitCount;
    }
    if (leftBone.jointType != rightBone.jointType) {
      const int source = relationshipConfidence(left) >= relationshipConfidence(right) ? left : right;
      const t850::PhysicsRagdollJointType type = bones[static_cast<std::size_t>(source)].jointType;
      leftBone.jointType = type;
      rightBone.jointType = type;
      ++jointLimitCount;
    }
  }

  if (!ApplyRagdollParentCapsuleLinks()) {
    syncStatus("[RagdollEdit] Sync failed: could not apply mirrored parent links");
    T8_LOG_ERROR("%s", m_ragdollLastSyncStatus.c_str());
    return false;
  }

  int jointAnchorCount = 0;
  for (int child = 0; child < static_cast<int>(bones.size()); ++child) {
    if (IsRagdollJointFrozen(child)) {
      continue;
    }
    const int parent = GetRagdollEffectiveJointParentCapsule(child);
    if (parent < 0 || parent >= static_cast<int>(bones.size()) || parent == child) {
      continue;
    }

    XVECTOR3 anchor;
    bool anchorReady = false;
    if (child < static_cast<int>(m_ragdollContactJoints.size()) &&
        m_ragdollContactJoints[static_cast<std::size_t>(child)] != 0u) {
      anchorReady = ComputeRagdollCapsuleContactAnchor(child, parent, anchor);
    }
    if (!anchorReady) {
      const XMATRIX44& childWorld = bones[static_cast<std::size_t>(child)].body.worldTransform;
      const XMATRIX44& parentWorld = bones[static_cast<std::size_t>(parent)].body.worldTransform;
      anchor = XVECTOR3((childWorld.m41 + parentWorld.m41) * 0.5f,
                        (childWorld.m42 + parentWorld.m42) * 0.5f,
                        (childWorld.m43 + parentWorld.m43) * 0.5f,
                        1.0f);
      anchorReady = true;
    }
    if (anchorReady) {
      const XVECTOR3 previousAnchor = bones[static_cast<std::size_t>(child)].jointWorldPosition;
      if (Length3(previousAnchor - anchor) <= (std::max)(0.0001f, m_modelRadius * 0.00005f)) {
        continue;
      }
      bones[static_cast<std::size_t>(child)].jointWorldPosition = anchor;
      if (UpdateRagdollJointOffsetFromWorld(child)) {
        ++jointAnchorCount;
      }
    }
  }

  const int totalChanges = createdBodyCount + transformCount + shapeCount + centeredCount + relationshipCount +
      controlledBoneCount + jointLimitCount + jointAnchorCount;
  if (totalChanges <= 0) {
    syncStatus("[RagdollEdit] Sync found %zu left/right pairs but no editable differences",
               pairs.size());
    return false;
  }

  m_ragdollEditDirty = true;
  const bool applied = ApplyRagdollEditPose(true);
  if (applied) {
    syncStatus("[RagdollEdit] Sync adjusted %zu pairs around %c=%.3f: created=%d transforms=%d shapes=%d centered=%d links=%d bones=%d limits=%d anchors=%d",
               pairs.size(), mirrorAxisName, mirrorPlane, createdBodyCount, transformCount, shapeCount, centeredCount,
               relationshipCount, controlledBoneCount, jointLimitCount, jointAnchorCount);
  } else {
    syncStatus("[RagdollEdit] Sync failed: ApplyRagdollEditPose returned false after %d pending changes",
               totalChanges);
    T8_LOG_ERROR("%s", m_ragdollLastSyncStatus.c_str());
  }
  return applied;
}

bool SceneTemplate::RecreateRagdollFromPose(const t850::PhysicsRagdollDesc& pose) {
  if (pose.bones.empty()) {
    return false;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (!engineContext || !engineContext->physics || !engineContext->physics->IsInitialized()) {
    return false;
  }

  const t850::PhysicsRagdollHandle oldHandle = Meshes[0].GetPhysicsRagdoll();
  const t850::PhysicsRagdollHandle newHandle =
      engineContext->physics->CreateRagdoll(pose, t850::PhysicsBodyMotion::Kinematic);
  if (!newHandle.IsValid()) {
    T8_LOG_ERROR("[RagdollEdit] Failed to recreate ragdoll for '%s'", ActiveModelPath().c_str());
    return false;
  }

  if (oldHandle.IsValid()) {
    engineContext->physics->DestroyRagdoll(oldHandle);
  }
  Meshes[0].AttachPhysicsRagdoll(newHandle);
  m_ragdollAnimationPose = pose;
  m_driveRagdollFromAnimation = true;
  m_ragdollPhysicsDriven = false;
  m_ragdollDriveLogEmitted = false;
  m_ragdollPhysicsLogEmitted = false;
  return true;
}

bool SceneTemplate::ApplyRagdollEditPose(bool rebuildRagdoll) {
  if (m_ragdollAnimationBinding.referencePose.bones.empty() ||
      m_ragdollAnimationBinding.referencePose.bones.size() != m_ragdollAnimationBinding.bodyFromBone.size()) {
    return false;
  }
  EnsureRagdollControlledBones();
  EnsureRagdollJointFrames();

  t850::PhysicsRagdollDesc pose = m_ragdollAnimationBinding.referencePose;
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  if (skinned && skinned->HasSkinData()) {
    t850::PhysicsRagdollDesc animationPose;
    if (t850::BuildRagdollPoseFromAnimation(*skinned, Meshes[0].Final, m_ragdollAnimationBinding, animationPose)) {
      pose = std::move(animationPose);
    }
  }

  if (rebuildRagdoll || !Meshes[0].HasPhysicsRagdoll()) {
    return RecreateRagdollFromPose(pose);
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (!engineContext || !engineContext->physics) {
    return false;
  }

  const bool updated = engineContext->physics->DriveRagdollFromPose(Meshes[0].GetPhysicsRagdoll(), pose, 0.0f);
  if (updated) {
    m_ragdollAnimationPose = pose;
    m_ragdollDriveLogEmitted = false;
  }
  return updated;
}

bool SceneTemplate::LoadRagdollEditPose() {
  if (m_ragdollEditSavePath.empty()) {
    m_ragdollEditSavePath = BuildRagdollEditSavePath();
  }

  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  if (!skinned || !skinned->HasSkinData()) {
    return false;
  }

  const t850::PhysicsRagdollAnimationBinding& generatedBinding =
      m_ragdollGeneratedBinding.referencePose.bones.empty()
          ? m_ragdollAnimationBinding
          : m_ragdollGeneratedBinding;
  t850::PhysicsRagdollAuthoringDesc authoring;
  int applied = 0;
  if (!t850::LoadRagdollAuthoringAsset(
          m_ragdollEditSavePath,
          *skinned,
          Meshes[0].Final,
          generatedBinding,
          authoring,
          &applied)) {
    return false;
  }

  m_ragdollAnimationBinding = std::move(authoring.binding);
  m_ragdollParentCapsules = std::move(authoring.parentBodyIndices);
  m_ragdollJointParentCapsules = std::move(authoring.jointParentBodyIndices);
  m_ragdollFrozenCapsules = std::move(authoring.frozenBodies);
  m_ragdollFrozenJoints = std::move(authoring.frozenJoints);
  m_ragdollContactJoints = std::move(authoring.contactJoints);
  EnsureRagdollFreezeState();
  EnsureRagdollControlledBones();
  EnsureRagdollJointState();
  ApplyRagdollParentCapsuleLinks();
  EnsureRagdollJointFrames();

  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  SelectRagdollEditCapsule(
      (m_ragdollEditSelectedCapsule < 0 || m_ragdollEditSelectedCapsule >= static_cast<int>(bones.size()))
          ? (bones.empty() ? -1 : 0)
          : m_ragdollEditSelectedCapsule,
      false);
  m_ragdollEditRenamingCapsule = -1;
  m_ragdollEditRenameFocusPending = false;
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  m_ragdollEditDirty = false;
  if (!bones.empty()) {
    ApplyRagdollEditPose(true);
  }
  T8_LOG_INFO("[RagdollEdit] Loaded %d edited bodies from '%s'", applied, m_ragdollEditSavePath.c_str());
  return true;
}

bool SceneTemplate::SaveRagdollEditPose() {
  if (m_ragdollEditSavePath.empty()) {
    m_ragdollEditSavePath = BuildRagdollEditSavePath();
  }

  EnsureRagdollParentCapsules();
  EnsureRagdollJointState();
  EnsureRagdollJointFrames();
  EnsureRagdollFreezeState();
  ApplyRagdollParentCapsuleLinks();

  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (bones.size() != m_ragdollAnimationBinding.bodyFromBone.size()) {
    return false;
  }

  t850::PhysicsRagdollAuthoringDesc authoring;
  authoring.model = m_profileModelKey.empty() ? t850::BuildRagdollEditModelKey(ActiveModelPath()) : m_profileModelKey;
  authoring.binding = m_ragdollAnimationBinding;
  authoring.parentBodyIndices = m_ragdollParentCapsules;
  authoring.jointParentBodyIndices = m_ragdollJointParentCapsules;
  authoring.frozenBodies = m_ragdollFrozenCapsules;
  authoring.frozenJoints = m_ragdollFrozenJoints;
  authoring.contactJoints = m_ragdollContactJoints;

  std::filesystem::path path;
  if (!t850::SaveRagdollAuthoringAsset(m_ragdollEditSavePath, authoring.model, authoring, &path)) {
    return false;
  }

  m_ragdollEditDirty = false;
  T8_LOG_INFO("[RagdollEdit] Saved %zu bodies to '%s'", bones.size(), path.string().c_str());
  return true;
}

bool SceneTemplate::ResetRagdollEditPose() {
  if (m_ragdollGeneratedBinding.referencePose.bones.empty() ||
      m_ragdollGeneratedBinding.referencePose.bones.size() != m_ragdollGeneratedBinding.bodyFromBone.size()) {
    return false;
  }
  m_ragdollAnimationBinding = m_ragdollGeneratedBinding;
  m_ragdollParentCapsules.clear();
  m_ragdollJointParentCapsules.clear();
  m_ragdollFrozenCapsules.assign(m_ragdollAnimationBinding.referencePose.bones.size(), 0u);
  m_ragdollFrozenJoints.assign(m_ragdollAnimationBinding.referencePose.bones.size(), 0u);
  m_ragdollContactJoints.assign(m_ragdollAnimationBinding.referencePose.bones.size(), 0u);
  RebuildRagdollParentLinks();
  m_ragdollEditSelectedJoint = -1;
  m_ragdollEditRenamingCapsule = -1;
  m_ragdollEditRenameFocusPending = false;
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  m_ragdollEditDirty = true;
  return ApplyRagdollEditPose(true);
}

bool SceneTemplate::ResetSelectedRagdollCapsule() {
  EnsureRagdollControlledBones();
  const int index = m_ragdollEditSelectedCapsule;
  if (index < 0 ||
      index >= static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size()) ||
      index >= static_cast<int>(m_ragdollAnimationBinding.bodyFromBone.size())) {
    return false;
  }
  EnsureRagdollFreezeState();
  if (IsRagdollCapsuleFrozen(index)) {
    T8_LOG_INFO("[RagdollEdit] Capsule %d is frozen; unfreeze it before resetting", index);
    return false;
  }

  const int boneIndex = m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(index)].body.boneIndex;
  t850::PhysicsRagdollBoneDesc resetBone;
  XMATRIX44 resetBodyFromBone;
  int generatedIndex = -1;
  const bool hasDefault = BuildDefaultRagdollCapsuleForBone(boneIndex, resetBone, resetBodyFromBone);
  if (!hasDefault) {
    generatedIndex = FindGeneratedRagdollCapsuleForBone(boneIndex);
    if (generatedIndex < 0 ||
        generatedIndex >= static_cast<int>(m_ragdollGeneratedBinding.referencePose.bones.size()) ||
        generatedIndex >= static_cast<int>(m_ragdollGeneratedBinding.bodyFromBone.size())) {
      return false;
    }
    resetBone = m_ragdollGeneratedBinding.referencePose.bones[static_cast<std::size_t>(generatedIndex)];
    resetBodyFromBone = m_ragdollGeneratedBinding.bodyFromBone[static_cast<std::size_t>(generatedIndex)];
  }

  m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(index)] =
      resetBone;
  m_ragdollAnimationBinding.bodyFromBone[static_cast<std::size_t>(index)] =
      resetBodyFromBone;
  if (m_ragdollAnimationBinding.jointFromBone.size() < m_ragdollAnimationBinding.referencePose.bones.size()) {
    m_ragdollAnimationBinding.jointFromBone.resize(m_ragdollAnimationBinding.referencePose.bones.size(),
                                                   XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
  }
  if (!hasDefault && generatedIndex < static_cast<int>(m_ragdollGeneratedBinding.jointFromBone.size())) {
    m_ragdollAnimationBinding.jointFromBone[static_cast<std::size_t>(index)] =
        m_ragdollGeneratedBinding.jointFromBone[static_cast<std::size_t>(generatedIndex)];
  } else {
    UpdateRagdollJointOffsetFromWorld(index);
  }
  if (index < static_cast<int>(m_ragdollAnimationBinding.controlledBoneIndices.size())) {
    m_ragdollAnimationBinding.controlledBoneIndices[static_cast<std::size_t>(index)] =
        std::vector<int>{m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(index)].body.boneIndex};
  }
  if (index < static_cast<int>(m_ragdollAnimationBinding.controlledBodyFromBone.size())) {
    m_ragdollAnimationBinding.controlledBodyFromBone[static_cast<std::size_t>(index)] =
        std::vector<XMATRIX44>{m_ragdollAnimationBinding.bodyFromBone[static_cast<std::size_t>(index)]};
  }
  ApplyRagdollParentCapsuleLinks();
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  m_ragdollEditDirty = true;
  return ApplyRagdollEditPose(true);
}

bool SceneTemplate::GetSkeletonEditBoneWorldPosition(int boneIndex, XVECTOR3& outWorld) const {
  if (boneIndex < 0 || boneIndex >= static_cast<int>(m_skeletonEditCombined.size())) {
    return false;
  }
  const XMATRIX44& combined = m_skeletonEditCombined[static_cast<std::size_t>(boneIndex)];
  const XVECTOR3 meshPosition(combined.m41, combined.m42, -combined.m43, 1.0f);
  outWorld = t850::TransformPoint(meshPosition, Meshes[0].Final);
  return true;
}

bool SceneTemplate::SetSkeletonEditBoneWorldPosition(int boneIndex, const XVECTOR3& worldPosition) {
  if (boneIndex < 0 || boneIndex >= static_cast<int>(m_skeletonEditCombined.size())) {
    return false;
  }
  XMATRIX44 meshFromWorld;
  Meshes[0].Final.Inverse(&meshFromWorld);
  const XVECTOR3 meshPosition = t850::TransformPoint(worldPosition, meshFromWorld);
  XMATRIX44& combined = m_skeletonEditCombined[static_cast<std::size_t>(boneIndex)];
  combined.m41 = meshPosition.x;
  combined.m42 = meshPosition.y;
  combined.m43 = -meshPosition.z;
  m_skeletonEditDirty = true;
  return ApplySkeletonEditPose();
}

void SceneTemplate::SelectSkeletonEditBone(int boneIndex) {
  if (boneIndex != m_skeletonEditSelectedBone) {
    RestoreSkeletonPreviewBone();
  }
  m_skeletonEditSelectedBone = boneIndex;
}

void SceneTemplate::RestoreSkeletonPreviewBone() {
  if (!m_skeletonPreviewBoneActive) {
    return;
  }

  const std::vector<XMATRIX44> originalCombined = m_skeletonPreviewOriginalCombined;
  m_skeletonPreviewBoneActive = false;
  m_skeletonPreviewBoneIndex = -1;
  m_skeletonPreviewOriginalCombined.clear();
  if (originalCombined.size() != m_skeletonEditCombined.size()) {
    return;
  }

  m_skeletonEditCombined = originalCombined;
  ApplySkeletonEditPose();
}

bool SceneTemplate::BeginSkeletonPreviewBone(int boneIndex) {
  if (boneIndex < 0 || boneIndex >= static_cast<int>(m_skeletonEditCombined.size())) {
    return false;
  }
  if (m_skeletonPreviewBoneActive && m_skeletonPreviewBoneIndex == boneIndex) {
    return true;
  }

  RestoreSkeletonPreviewBone();
  m_skeletonPreviewBoneIndex = boneIndex;
  m_skeletonPreviewOriginalCombined = m_skeletonEditCombined;
  m_skeletonPreviewBoneActive = true;
  return true;
}

void SceneTemplate::GatherSkeletonEditBoneSubtree(int boneIndex, std::vector<int>& outBones) const {
  outBones.clear();
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;
  if (!skeleton || boneIndex < 0 || boneIndex >= static_cast<int>(skeleton->Bones.size())) {
    return;
  }

  const int boneCount = (std::min)(static_cast<int>(skeleton->Bones.size()),
                                   static_cast<int>(m_skeletonEditCombined.size()));
  if (boneIndex >= boneCount) {
    return;
  }

  std::vector<std::vector<int>> children(static_cast<std::size_t>(boneCount));
  for (int parent = 0; parent < boneCount; ++parent) {
    for (unsigned int child : skeleton->Bones[static_cast<std::size_t>(parent)].Sons) {
      if (child < static_cast<unsigned int>(boneCount) &&
          child != static_cast<unsigned int>(parent) &&
          std::find(children[static_cast<std::size_t>(parent)].begin(),
                    children[static_cast<std::size_t>(parent)].end(),
                    static_cast<int>(child)) == children[static_cast<std::size_t>(parent)].end()) {
        children[static_cast<std::size_t>(parent)].push_back(static_cast<int>(child));
      }
    }
  }
  for (int child = 0; child < boneCount; ++child) {
    const unsigned short dad = skeleton->Bones[static_cast<std::size_t>(child)].Dad;
    if (dad < boneCount && dad != static_cast<unsigned short>(child) &&
        std::find(children[static_cast<std::size_t>(dad)].begin(),
                  children[static_cast<std::size_t>(dad)].end(),
                  child) == children[static_cast<std::size_t>(dad)].end()) {
      children[static_cast<std::size_t>(dad)].push_back(child);
    }
  }

  std::vector<unsigned char> visited(static_cast<std::size_t>(boneCount), 0);
  std::function<void(int)> visit = [&](int index) {
    if (index < 0 || index >= boneCount || visited[static_cast<std::size_t>(index)]) {
      return;
    }
    visited[static_cast<std::size_t>(index)] = 1;
    outBones.push_back(index);
    for (int child : children[static_cast<std::size_t>(index)]) {
      visit(child);
    }
  };
  visit(boneIndex);
}

bool SceneTemplate::SetSkeletonPreviewBoneWorldTransform(int boneIndex, const XMATRIX44& worldTransform) {
  if (!BeginSkeletonPreviewBone(boneIndex)) {
    return false;
  }

  XMATRIX44 meshFromWorld;
  if (!InvertAffineNoExit(Meshes[0].Final, meshFromWorld)) {
    return false;
  }

  const XMATRIX44 meshTransform = worldTransform * meshFromWorld;
  m_skeletonEditCombined[static_cast<std::size_t>(boneIndex)] = FlipMatrixZ(meshTransform);
  return ApplySkeletonEditPose();
}

bool SceneTemplate::MoveSkeletonPreviewBoneByWorldDelta(int boneIndex, const XVECTOR3& worldDelta) {
  XMATRIX44 boneWorld;
  if (!GetSkeletonEditBoneWorldTransform(boneIndex, boneWorld)) {
    return false;
  }

  boneWorld.m41 += worldDelta.x;
  boneWorld.m42 += worldDelta.y;
  boneWorld.m43 += worldDelta.z;
  return SetSkeletonPreviewBoneWorldTransform(boneIndex, boneWorld);
}

bool SceneTemplate::RotateSkeletonPreviewBoneWorld(int boneIndex, const XVECTOR3& axisWorld, float angleRadians) {
  if (std::fabs(angleRadians) < 0.000001f) {
    return true;
  }

  if (!BeginSkeletonPreviewBone(boneIndex)) {
    return false;
  }

  std::vector<int> subtreeBones;
  GatherSkeletonEditBoneSubtree(boneIndex, subtreeBones);
  if (subtreeBones.empty()) {
    return false;
  }

  XMATRIX44 meshFromWorld;
  if (!InvertAffineNoExit(Meshes[0].Final, meshFromWorld)) {
    return false;
  }

  const XMATRIX44 boneWorld =
      FlipMatrixZ(m_skeletonEditCombined[static_cast<std::size_t>(boneIndex)]) * Meshes[0].Final;
  const XVECTOR3 center(boneWorld.m41, boneWorld.m42, boneWorld.m43, 1.0f);
  XMATRIX44 toOrigin;
  XMATRIX44 rotation;
  XMATRIX44 fromOrigin;
  XMatTranslation(toOrigin, -center.x, -center.y, -center.z);
  XMatRotationAxis(rotation, Normalize3(axisWorld, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f)), angleRadians);
  XMatTranslation(fromOrigin, center.x, center.y, center.z);
  const XMATRIX44 worldDelta = toOrigin * rotation * fromOrigin;
  const std::vector<XMATRIX44> sourceCombined = m_skeletonEditCombined;
  for (int subtreeBone : subtreeBones) {
    if (subtreeBone < 0 || subtreeBone >= static_cast<int>(sourceCombined.size())) {
      continue;
    }
    const XMATRIX44 currentWorld =
        FlipMatrixZ(sourceCombined[static_cast<std::size_t>(subtreeBone)]) * Meshes[0].Final;
    m_skeletonEditCombined[static_cast<std::size_t>(subtreeBone)] =
        FlipMatrixZ(currentWorld * worldDelta * meshFromWorld);
  }
  return ApplySkeletonEditPose();
}

bool SceneTemplate::GetSkeletonPreviewBoneGizmoFrame(int boneIndex,
                                                   XVECTOR3& outCenter,
                                                   std::array<XVECTOR3, 3>& outAxes,
                                                   float& outSize,
                                                   bool globalAxes) const {
  XMATRIX44 boneWorld;
  if (!GetSkeletonEditBoneWorldTransform(boneIndex, boneWorld)) {
    return false;
  }

  outCenter = XVECTOR3(boneWorld.m41, boneWorld.m42, boneWorld.m43, 1.0f);
  if (globalAxes) {
    outAxes[0] = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
    outAxes[1] = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
    outAxes[2] = XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
  } else {
    outAxes[0] = MatrixAxisX(boneWorld);
    outAxes[1] = MatrixAxisY(boneWorld);
    outAxes[2] = MatrixAxisZ(boneWorld);
  }

  const float distanceToCamera = Length3(outCenter - Cam.Eye);
  const float minSize = (std::max)(0.03f, m_modelRadius * 0.08f);
  const float maxSize = (std::max)(minSize, m_modelRadius * 0.45f);
  outSize = (std::max)(minSize, (std::min)(maxSize, distanceToCamera * 0.10f));
  return true;
}

std::array<float, 3> SceneTemplate::GetSkeletonEditBoneScale(int boneIndex) const {
  std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};
  if (boneIndex < 0 || boneIndex >= static_cast<int>(m_skeletonEditCombined.size())) {
    return scale;
  }
  const XMATRIX44& matrix = m_skeletonEditCombined[static_cast<std::size_t>(boneIndex)];
  for (int row = 0; row < 3; ++row) {
    const float x = matrix.m[row][0];
    const float y = matrix.m[row][1];
    const float z = matrix.m[row][2];
    scale[static_cast<std::size_t>(row)] = std::sqrt(x * x + y * y + z * z);
  }
  return scale;
}

bool SceneTemplate::SetSkeletonEditBoneScale(int boneIndex, const std::array<float, 3>& scale) {
  if (boneIndex < 0 || boneIndex >= static_cast<int>(m_skeletonEditCombined.size())) {
    return false;
  }
  XMATRIX44& matrix = m_skeletonEditCombined[static_cast<std::size_t>(boneIndex)];
  for (int row = 0; row < 3; ++row) {
    const float x = matrix.m[row][0];
    const float y = matrix.m[row][1];
    const float z = matrix.m[row][2];
    const float length = std::sqrt(x * x + y * y + z * z);
    const float target = (std::max)(0.001f, scale[static_cast<std::size_t>(row)]);
    if (length > 0.000001f) {
      const float factor = target / length;
      matrix.m[row][0] *= factor;
      matrix.m[row][1] *= factor;
      matrix.m[row][2] *= factor;
    }
  }
  m_skeletonEditDirty = true;
  return ApplySkeletonEditPose();
}

bool SceneTemplate::GetCurrentRagdollEditCapsuleWorld(int capsuleIndex, XMATRIX44& outWorld) {
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (capsuleIndex < 0 || capsuleIndex >= static_cast<int>(bones.size())) {
    return false;
  }

  if (m_ragdollPhysicsDriven && Meshes[0].HasPhysicsRagdoll()) {
    const int boneIndex = bones[static_cast<std::size_t>(capsuleIndex)].body.boneIndex;
    auto findPhysicsState = [&](const std::vector<t850::PhysicsBodyState>& states) {
      for (const t850::PhysicsBodyState& state : states) {
        if (state.boneIndex == boneIndex) {
          outWorld = state.worldTransform;
          return true;
        }
      }
      return false;
    };

    if (findPhysicsState(m_ragdollPhysicsStates)) {
      return true;
    }

    t850::EngineContext* engineContext = GetEngineContext();
    if (!engineContext) engineContext = &t850::GetEngineContext();
    if (engineContext && engineContext->physics) {
      std::vector<t850::PhysicsBodyState> states;
      if (engineContext->physics->GetRagdollState(Meshes[0].GetPhysicsRagdoll(), states) &&
          findPhysicsState(states)) {
        m_ragdollPhysicsStates = std::move(states);
        return true;
      }
    }
  }

  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  t850::PhysicsRagdollDesc pose;
  if (skinned && skinned->HasSkinData() &&
      t850::BuildRagdollPoseFromAnimation(*skinned, Meshes[0].Final, m_ragdollAnimationBinding, pose) &&
      capsuleIndex < static_cast<int>(pose.bones.size())) {
    outWorld = pose.bones[static_cast<std::size_t>(capsuleIndex)].body.worldTransform;
    return true;
  }

  outWorld = bones[static_cast<std::size_t>(capsuleIndex)].body.worldTransform;
  return true;
}

bool SceneTemplate::SetRagdollEditJointWorldPosition(int childCapsule, const XVECTOR3& worldPosition) {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollJointState();
  EnsureRagdollFreezeState();
  if (childCapsule < 0 ||
      childCapsule >= static_cast<int>(bones.size()) ||
      GetRagdollEffectiveJointParentCapsule(childCapsule) < 0) {
    return false;
  }
  if (IsRagdollJointFrozen(childCapsule)) {
    return false;
  }

  bones[static_cast<std::size_t>(childCapsule)].jointWorldPosition =
      XVECTOR3(worldPosition.x, worldPosition.y, worldPosition.z, 1.0f);
  if (childCapsule < static_cast<int>(m_ragdollContactJoints.size())) {
    m_ragdollContactJoints[static_cast<std::size_t>(childCapsule)] = 0u;
  }
  UpdateRagdollJointOffsetFromWorld(childCapsule);
  m_ragdollEditDirty = true;
  return true;
}

bool SceneTemplate::MoveRagdollEditJointByWorldDelta(int childCapsule, const XVECTOR3& worldDelta) {
  if (childCapsule < 0 || childCapsule >= static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size())) {
    return false;
  }
  if (IsRagdollJointFrozen(childCapsule)) {
    return false;
  }
  XVECTOR3 joint = m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(childCapsule)].jointWorldPosition;
  joint.x += worldDelta.x;
  joint.y += worldDelta.y;
  joint.z += worldDelta.z;
  return SetRagdollEditJointWorldPosition(childCapsule, joint);
}

bool SceneTemplate::RotateRagdollEditJointWorld(int childCapsule, const XVECTOR3& axisWorld, float angleRadians) {
  if (std::fabs(angleRadians) < 0.000001f) {
    return true;
  }
  if (IsRagdollJointFrozen(childCapsule)) {
    return false;
  }

  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollJointFrames();
  if (childCapsule < 0 || childCapsule >= static_cast<int>(bones.size())) {
    return false;
  }
  const int parentCapsule = GetRagdollEffectiveJointParentCapsule(childCapsule);
  if (parentCapsule < 0 || parentCapsule >= static_cast<int>(bones.size()) || parentCapsule == childCapsule) {
    return false;
  }

  XMATRIX44 parentWorld;
  XMATRIX44 childWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(parentCapsule, parentWorld) ||
      !GetCurrentRagdollEditCapsuleWorld(childCapsule, childWorld)) {
    return false;
  }

  auto& bone = bones[static_cast<std::size_t>(childCapsule)];
  bone.parentJointTwistAxis = RotateVectorAroundAxis(bone.parentJointTwistAxis, axisWorld, angleRadians);
  bone.parentJointPlaneAxis = RotateVectorAroundAxis(bone.parentJointPlaneAxis, axisWorld, angleRadians);
  bone.childJointTwistAxis = RotateVectorAroundAxis(bone.childJointTwistAxis, axisWorld, angleRadians);
  bone.childJointPlaneAxis = RotateVectorAroundAxis(bone.childJointPlaneAxis, axisWorld, angleRadians);
  NormalizeRagdollJointFrameAxes(bone.parentJointTwistAxis, bone.parentJointPlaneAxis, MatrixAxisY(parentWorld), MatrixAxisX(parentWorld));
  NormalizeRagdollJointFrameAxes(bone.childJointTwistAxis, bone.childJointPlaneAxis, MatrixAxisY(childWorld), MatrixAxisX(childWorld));
  if (!UpdateRagdollJointFrameOffsetsFromWorld(childCapsule)) {
    return false;
  }
  m_ragdollEditDirty = true;
  return true;
}

bool SceneTemplate::FlipRagdollEditJointLocalAxis(int childCapsule, int axisIndex) {
  if (axisIndex < 0 || axisIndex > 2 || IsRagdollJointFrozen(childCapsule)) {
    return false;
  }
  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  if (!GetRagdollJointGizmoFrame(childCapsule, center, axes, size)) {
    return false;
  }
  (void)center;
  (void)size;
  if (!RotateRagdollEditJointWorld(childCapsule, axes[static_cast<std::size_t>(axisIndex)], xPI)) {
    return false;
  }
  return ApplyRagdollEditPose(true);
}

bool SceneTemplate::GetRagdollJointVisualFrame(int childCapsule,
                                              XVECTOR3& outJoint,
                                              XVECTOR3& outParentCenter,
                                              XVECTOR3& outChildCenter,
                                              XVECTOR3& outParentTwistAxis,
                                               XVECTOR3& outChildTwistAxis,
                                               XVECTOR3& outChildPlaneAxis,
                                               float& outSize) {
  EnsureRagdollJointFrames();
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollParentCapsules();
  if (childCapsule < 0 ||
      childCapsule >= static_cast<int>(bones.size()) ||
      childCapsule >= static_cast<int>(m_ragdollParentCapsules.size())) {
    return false;
  }

  const int parentCapsule = GetRagdollEffectiveJointParentCapsule(childCapsule);
  if (parentCapsule < 0 || parentCapsule >= static_cast<int>(bones.size()) || parentCapsule == childCapsule) {
    return false;
  }

  XMATRIX44 parentWorld;
  XMATRIX44 childWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(parentCapsule, parentWorld) ||
      !GetCurrentRagdollEditCapsuleWorld(childCapsule, childWorld)) {
    return false;
  }

  outParentCenter = XVECTOR3(parentWorld.m41, parentWorld.m42, parentWorld.m43, 1.0f);
  outChildCenter = XVECTOR3(childWorld.m41, childWorld.m42, childWorld.m43, 1.0f);
  const auto* visualBone = &bones[static_cast<std::size_t>(childCapsule)];
  outJoint = visualBone->jointWorldPosition;
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  t850::PhysicsRagdollDesc currentPose;
  if (skinned && skinned->HasSkinData() &&
      t850::BuildRagdollPoseFromAnimation(*skinned, Meshes[0].Final, m_ragdollAnimationBinding, currentPose) &&
      childCapsule < static_cast<int>(currentPose.bones.size())) {
    visualBone = &currentPose.bones[static_cast<std::size_t>(childCapsule)];
    outJoint = visualBone->jointWorldPosition;
  }

  outParentTwistAxis = Normalize3(visualBone->parentJointTwistAxis, MatrixAxisY(parentWorld));
  outChildTwistAxis = Normalize3(visualBone->childJointTwistAxis, MatrixAxisY(childWorld));
  outChildPlaneAxis = Normalize3(visualBone->childJointPlaneAxis, MatrixAxisX(childWorld));

  const float distanceToCamera = Length3(outJoint - Cam.Eye);
  const float minSize = (std::max)(0.03f, m_modelRadius * 0.06f);
  const float maxSize = (std::max)(minSize, m_modelRadius * 0.35f);
  outSize = (std::max)(minSize, (std::min)(maxSize, distanceToCamera * 0.08f));
  return true;
}

bool SceneTemplate::GetRagdollJointGizmoFrame(int childCapsule,
                                             XVECTOR3& outCenter,
                                             std::array<XVECTOR3, 3>& outAxes,
                                             float& outSize) {
  XVECTOR3 parentCenter;
  XVECTOR3 childCenter;
  XVECTOR3 parentTwist;
  XVECTOR3 childTwist;
  XVECTOR3 childPlane;
  if (!GetRagdollJointVisualFrame(childCapsule, outCenter, parentCenter, childCenter, parentTwist, childTwist, childPlane, outSize)) {
    return false;
  }

  outAxes[1] = Normalize3(childTwist, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  outAxes[0] = Normalize3(childPlane, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  outAxes[2] = Normalize3(Cross3(outAxes[0], outAxes[1]), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
  outAxes[0] = Normalize3(Cross3(outAxes[1], outAxes[2]), XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  return true;
}

bool SceneTemplate::PickRagdollEditJoint(float mouseX, float mouseY, float thresholdPixels, int& outChildCapsule) {
  outChildCapsule = -1;
  if (!m_skeletonEditMode || !g_pBaseDriver || m_ragdollAnimationBinding.referencePose.bones.empty()) {
    return false;
  }

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  const ImVec2 mouse(mouseX, mouseY);
  float bestDistanceSq = thresholdPixels * thresholdPixels;
  for (int childCapsule = 0; childCapsule < static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size()); ++childCapsule) {
    XVECTOR3 joint;
    XVECTOR3 parentCenter;
    XVECTOR3 childCenter;
    XVECTOR3 parentTwist;
    XVECTOR3 childTwist;
    XVECTOR3 childPlane;
    float size = 0.0f;
    if (!GetRagdollJointVisualFrame(childCapsule, joint, parentCenter, childCenter, parentTwist, childTwist, childPlane, size)) {
      continue;
    }

    bool jointVisible = false;
    const ImVec2 jointScreen = ProjectWorldToScreen(joint, VP, width, height, jointVisible);
    if (!jointVisible) {
      continue;
    }
    float distanceSq = (jointScreen.x - mouse.x) * (jointScreen.x - mouse.x) +
                       (jointScreen.y - mouse.y) * (jointScreen.y - mouse.y);

    bool parentVisible = false;
    bool childVisible = false;
    const ImVec2 parentScreen = ProjectWorldToScreen(parentCenter, VP, width, height, parentVisible);
    const ImVec2 childScreen = ProjectWorldToScreen(childCenter, VP, width, height, childVisible);
    if (parentVisible) {
      distanceSq = (std::min)(distanceSq, DistancePointToSegmentSq(mouse, jointScreen, parentScreen));
    }
    if (childVisible) {
      distanceSq = (std::min)(distanceSq, DistancePointToSegmentSq(mouse, jointScreen, childScreen));
    }

    if (distanceSq < bestDistanceSq) {
      bestDistanceSq = distanceSq;
      outChildCapsule = childCapsule;
    }
  }

  return outChildCapsule >= 0;
}

bool SceneTemplate::PickRagdollEditJointGizmo(float mouseX, float mouseY, int& outAxis) {
  outAxis = -1;
  if (!m_skeletonEditMode || !g_pBaseDriver || m_ragdollEditSelectedJoint < 0 ||
      m_ragdollEditSelectionMode != kRagdollSelectJoints ||
      (m_ragdollEditGizmoMode != kRagdollToolEditCapsule &&
       m_ragdollEditGizmoMode != kRagdollToolMove &&
       m_ragdollEditGizmoMode != kRagdollToolRotate) ||
      IsRagdollJointFrozen(m_ragdollEditSelectedJoint)) {
    return false;
  }

  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  if (!GetRagdollJointGizmoFrame(m_ragdollEditSelectedJoint, center, axes, size)) {
    return false;
  }

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  const ImVec2 mouse(mouseX, mouseY);
  constexpr float kThresholdPixels = 12.0f;
  float bestDistanceSq = kThresholdPixels * kThresholdPixels;

  if (m_ragdollEditGizmoMode == kRagdollToolEditCapsule ||
      m_ragdollEditGizmoMode == kRagdollToolMove) {
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      bool startVisible = false;
      bool endVisible = false;
      const ImVec2 start = ProjectWorldToScreen(center + axes[static_cast<std::size_t>(axisIndex)] * (size * 0.12f),
                                                VP, width, height, startVisible);
      const ImVec2 end = ProjectWorldToScreen(center + axes[static_cast<std::size_t>(axisIndex)] * (size * 0.75f),
                                              VP, width, height, endVisible);
      if (!startVisible || !endVisible) {
        continue;
      }
      const float distanceSq = DistancePointToSegmentSq(mouse, start, end);
      if (distanceSq < bestDistanceSq) {
        bestDistanceSq = distanceSq;
        outAxis = axisIndex;
      }
    }
  } else if (m_ragdollEditGizmoMode == kRagdollToolRotate) {
    constexpr int kSegments = 64;
    const float radius = size * 0.62f;
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      const XVECTOR3 u = axes[static_cast<std::size_t>((axisIndex + 1) % 3)];
      const XVECTOR3 v = axes[static_cast<std::size_t>((axisIndex + 2) % 3)];
      ImVec2 previous;
      bool previousVisible = false;
      for (int segment = 0; segment <= kSegments; ++segment) {
        const float t = static_cast<float>(segment) / static_cast<float>(kSegments) * (2.0f * xPI);
        const XVECTOR3 point = center + (u * std::cos(t) + v * std::sin(t)) * radius;
        bool visible = false;
        const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
        if (visible && previousVisible) {
          const float distanceSq = DistancePointToSegmentSq(mouse, previous, screen);
          if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            outAxis = axisIndex;
          }
        }
        previous = screen;
        previousVisible = visible;
      }
    }
  }
  return outAxis >= 0;
}

bool SceneTemplate::BeginRagdollEditJointGizmoDrag(float mouseX, float mouseY) {
  int pickedAxis = -1;
  if (!PickRagdollEditJointGizmo(mouseX, mouseY, pickedAxis)) {
    return false;
  }

  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  if (!GetRagdollJointGizmoFrame(m_ragdollEditSelectedJoint, center, axes, size)) {
    return false;
  }
  (void)size;

  XMATRIX44 invVP;
  VP.Inverse(&invVP);
  const int width = (std::max)(1, g_pBaseDriver ? g_pBaseDriver->width : 1);
  const int height = (std::max)(1, g_pBaseDriver ? g_pBaseDriver->height : 1);
  const t850::Ray ray = t850::ScreenPointToRay(mouseX, mouseY, 0, 0, width, height, invVP);
  const XVECTOR3 axis = axes[static_cast<std::size_t>(pickedAxis)];
  if (m_ragdollEditGizmoMode == kRagdollToolEditCapsule ||
      m_ragdollEditGizmoMode == kRagdollToolMove) {
    if (!ClosestRayAxisParameter(ray, center, axis, m_ragdollEditJointLastParameter)) {
      return false;
    }
  } else if (m_ragdollEditGizmoMode == kRagdollToolRotate) {
    XVECTOR3 hitPoint;
    if (!RayPlaneIntersection(ray, center, axis, hitPoint)) {
      return false;
    }
    m_ragdollEditJointLastVector = Normalize3(hitPoint - center,
                                              axes[static_cast<std::size_t>((pickedAxis + 1) % 3)]);
  } else {
    return false;
  }

  m_ragdollEditJointAxis = pickedAxis;
  m_ragdollEditJointDragCenter = center;
  m_ragdollEditJointDragAxis = axis;
  m_ragdollEditJointDragging = true;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditHandleDragging = false;
  m_skeletonEditDragging = false;
  return true;
}

bool SceneTemplate::DragRagdollEditJointGizmo(float mouseX, float mouseY) {
  if (!m_ragdollEditJointDragging ||
      m_ragdollEditSelectedJoint < 0 ||
      m_ragdollEditJointAxis < 0 ||
      !g_pBaseDriver) {
    return false;
  }

  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  if (!GetRagdollJointGizmoFrame(m_ragdollEditSelectedJoint, center, axes, size)) {
    return false;
  }
  (void)center;
  (void)size;

  XMATRIX44 invVP;
  VP.Inverse(&invVP);
  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  const t850::Ray ray = t850::ScreenPointToRay(mouseX, mouseY, 0, 0, width, height, invVP);
  const XVECTOR3 axis = Normalize3(m_ragdollEditJointDragAxis, axes[static_cast<std::size_t>(m_ragdollEditJointAxis)]);

  if (m_ragdollEditGizmoMode == kRagdollToolEditCapsule ||
      m_ragdollEditGizmoMode == kRagdollToolMove) {
    float currentParameter = 0.0f;
    if (!ClosestRayAxisParameter(ray, m_ragdollEditJointDragCenter, axis, currentParameter)) {
      return false;
    }
    const float deltaParameter = currentParameter - m_ragdollEditJointLastParameter;
    m_ragdollEditJointLastParameter = currentParameter;
    if (std::fabs(deltaParameter) <= 0.000001f) {
      return true;
    }
    return MoveRagdollEditJointByWorldDelta(m_ragdollEditSelectedJoint, axis * deltaParameter);
  }

  if (m_ragdollEditGizmoMode != kRagdollToolRotate) {
    return false;
  }

  XVECTOR3 hitPoint;
  if (!RayPlaneIntersection(ray, m_ragdollEditJointDragCenter, axis, hitPoint)) {
    return false;
  }
  const XVECTOR3 currentVector = Normalize3(hitPoint - m_ragdollEditJointDragCenter, m_ragdollEditJointLastVector);
  const float dot = (std::max)(-1.0f, (std::min)(1.0f, Dot3(m_ragdollEditJointLastVector, currentVector)));
  const float signedAngle = std::atan2(Dot3(axis, Cross3(m_ragdollEditJointLastVector, currentVector)), dot);
  m_ragdollEditJointLastVector = currentVector;
  if (std::fabs(signedAngle) <= 0.000001f) {
    return true;
  }
  return RotateRagdollEditJointWorld(m_ragdollEditSelectedJoint, axis, signedAngle);
}

void SceneTemplate::DrawRagdollJointGizmos(bool editable) {
  const bool allowEditing = editable && m_skeletonEditMode && m_ragdollEditSelectionMode == kRagdollSelectJoints;
  if (!allowEditing || !g_pBaseDriver || !ImGui::GetCurrentContext()) {
    return;
  }

  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (bones.empty()) {
    return;
  }
  EnsureRagdollParentCapsules();

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  ImDrawList* drawList = ImGui::GetBackgroundDrawList();
  const ImU32 lineColor = IM_COL32(255, 185, 40, 165);
  const ImU32 jointColor = IM_COL32(255, 220, 80, 230);
  const ImU32 selectedColor = IM_COL32(255, 245, 120, 255);
  const ImU32 parentAxisColor = IM_COL32(255, 130, 40, 245);
  const ImU32 childAxisColor = IM_COL32(80, 220, 255, 255);
  const ImU32 planeAxisColor = IM_COL32(255, 90, 220, 245);
  const ImU32 coneColor = IM_COL32(255, 215, 70, 205);
  const ImU32 twistColor = IM_COL32(190, 120, 255, 230);

  for (int childCapsule = 0; childCapsule < static_cast<int>(bones.size()); ++childCapsule) {
    if (GetRagdollEffectiveJointParentCapsule(childCapsule) < 0) {
      continue;
    }

    XVECTOR3 joint;
    XVECTOR3 parentCenter;
    XVECTOR3 childCenter;
    XVECTOR3 parentTwist;
    XVECTOR3 childTwist;
    XVECTOR3 childPlane;
    float size = 0.0f;
    if (!GetRagdollJointVisualFrame(childCapsule, joint, parentCenter, childCenter, parentTwist, childTwist, childPlane, size)) {
      continue;
    }

    bool jointVisible = false;
    bool parentVisible = false;
    bool childVisible = false;
    const ImVec2 jointScreen = ProjectWorldToScreen(joint, VP, width, height, jointVisible);
    const ImVec2 parentScreen = ProjectWorldToScreen(parentCenter, VP, width, height, parentVisible);
    const ImVec2 childScreen = ProjectWorldToScreen(childCenter, VP, width, height, childVisible);
    if (!jointVisible) {
      continue;
    }

    const bool selected = allowEditing && childCapsule == m_ragdollEditSelectedJoint;
    if (parentVisible) {
      drawList->AddLine(parentScreen, jointScreen, selected ? selectedColor : lineColor, selected ? 3.0f : 1.6f);
    }
    if (childVisible) {
      drawList->AddLine(jointScreen, childScreen, selected ? selectedColor : lineColor, selected ? 3.0f : 1.6f);
    }
    drawList->AddCircleFilled(jointScreen, selected ? 6.0f : 4.0f, selected ? selectedColor : jointColor, 16);
    drawList->AddCircle(jointScreen, selected ? 11.0f : 7.0f, selected ? selectedColor : jointColor, 20, selected ? 2.5f : 1.5f);

    if (!selected) {
      continue;
    }

    drawList->AddText(ImVec2(jointScreen.x + 10.0f, jointScreen.y + 5.0f), selectedColor, "joint");

    auto drawAxis = [&](const XVECTOR3& axis, float length, ImU32 color, const char* label) {
      bool endVisible = false;
      const ImVec2 endScreen = ProjectWorldToScreen(joint + axis * length, VP, width, height, endVisible);
      if (!endVisible) {
        return;
      }
      drawList->AddLine(jointScreen, endScreen, color, 3.0f);
      drawList->AddCircleFilled(endScreen, 4.5f, color, 12);
      drawList->AddText(ImVec2(endScreen.x + 6.0f, endScreen.y - 6.0f), color, label);
    };
    drawAxis(parentTwist, size * 0.85f, parentAxisColor, "parent +Y");
    drawAxis(childTwist, size, childAxisColor, "child +Y twist");
    drawAxis(childPlane, size * 0.7f, planeAxisColor, "child +X plane");

    std::array<XVECTOR3, 3> gizmoAxes = {
        childPlane,
        childTwist,
        Normalize3(Cross3(childPlane, childTwist), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f))};

    if (IsRagdollJointFrozen(childCapsule)) {
      drawList->AddText(ImVec2(jointScreen.x + 10.0f, jointScreen.y + 37.0f), selectedColor, "frozen");
    } else if (m_ragdollEditGizmoMode == kRagdollToolEditCapsule ||
               m_ragdollEditGizmoMode == kRagdollToolMove) {
      drawList->AddText(ImVec2(jointScreen.x + 10.0f, jointScreen.y + 37.0f), selectedColor, "edit anchor");
      for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
        const bool active = m_ragdollEditJointDragging && m_ragdollEditJointAxis == axisIndex;
        const ImU32 color = RagdollGizmoAxisColor(axisIndex, active);
        bool endVisible = false;
        const ImVec2 end = ProjectWorldToScreen(joint + gizmoAxes[static_cast<std::size_t>(axisIndex)] * (size * 0.75f),
                                                VP, width, height, endVisible);
        if (!endVisible) {
          continue;
        }
        drawList->AddLine(jointScreen, end, color, active ? 4.0f : 2.5f);
        drawList->AddCircleFilled(end, active ? 5.5f : 4.0f, color, 12);
      }
    } else if (m_ragdollEditGizmoMode == kRagdollToolRotate) {
      drawList->AddText(ImVec2(jointScreen.x + 10.0f, jointScreen.y + 37.0f), selectedColor, "rotate child frame");
      constexpr int kGizmoSegments = 72;
      const float radius = size * 0.62f;
      for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
        const bool active = m_ragdollEditJointDragging && m_ragdollEditJointAxis == axisIndex;
        const ImU32 color = RagdollGizmoAxisColor(axisIndex, active);
        const XVECTOR3 u = gizmoAxes[static_cast<std::size_t>((axisIndex + 1) % 3)];
        const XVECTOR3 v = gizmoAxes[static_cast<std::size_t>((axisIndex + 2) % 3)];
        ImVec2 previous;
        bool previousVisible = false;
        for (int segment = 0; segment <= kGizmoSegments; ++segment) {
          const float t = static_cast<float>(segment) / static_cast<float>(kGizmoSegments) * (2.0f * xPI);
          const XVECTOR3 point = joint + (u * std::cos(t) + v * std::sin(t)) * radius;
          bool visible = false;
          const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
          if (visible && previousVisible) {
            drawList->AddLine(previous, screen, color, active ? 3.5f : 2.5f);
          }
          previous = screen;
          previousVisible = visible;
        }
      }
    }

    if (bones[static_cast<std::size_t>(childCapsule)].jointType == t850::PhysicsRagdollJointType::Fixed) {
      drawList->AddText(ImVec2(jointScreen.x + 10.0f, jointScreen.y + 21.0f), selectedColor, "fixed");
      continue;
    }

    const float coneLength = size * 0.75f;
    const float swing = (std::max)(0.0f, (std::min)(Deg2Rad(85.0f), bones[static_cast<std::size_t>(childCapsule)].swingLimitRadians));
    const float coneRadius = (std::min)(size * 1.25f, std::tan(swing) * coneLength);
    const XVECTOR3 coneCenter = joint + childTwist * coneLength;
    const XVECTOR3 coneU = childPlane;
    const XVECTOR3 coneV = Normalize3(Cross3(childTwist, coneU), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
    ImVec2 previousCone;
    bool previousConeVisible = false;
    constexpr int kConeSegments = 48;
    for (int segment = 0; segment <= kConeSegments; ++segment) {
      const float t = static_cast<float>(segment) / static_cast<float>(kConeSegments) * (2.0f * xPI);
      const XVECTOR3 point = coneCenter + (coneU * std::cos(t) + coneV * std::sin(t)) * coneRadius;
      bool visible = false;
      const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
      if (visible && previousConeVisible) {
        drawList->AddLine(previousCone, screen, coneColor, 2.0f);
      }
      previousCone = screen;
      previousConeVisible = visible;
    }
    for (int spoke = 0; spoke < 4; ++spoke) {
      const float t = static_cast<float>(spoke) * (0.5f * xPI);
      const XVECTOR3 point = coneCenter + (coneU * std::cos(t) + coneV * std::sin(t)) * coneRadius;
      bool visible = false;
      const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
      if (visible) {
        drawList->AddLine(jointScreen, screen, coneColor, 1.4f);
      }
    }

    const float twist = (std::max)(0.0f, (std::min)(Deg2Rad(180.0f), bones[static_cast<std::size_t>(childCapsule)].twistLimitRadians));
    const float twistRadius = size * 0.38f;
    ImVec2 previousTwist;
    bool previousTwistVisible = false;
    constexpr int kTwistSegments = 32;
    for (int segment = 0; segment <= kTwistSegments; ++segment) {
      const float t = -twist + (2.0f * twist * static_cast<float>(segment) / static_cast<float>(kTwistSegments));
      const XVECTOR3 point = joint + (coneU * std::cos(t) + coneV * std::sin(t)) * twistRadius;
      bool visible = false;
      const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
      if (visible && previousTwistVisible) {
        drawList->AddLine(previousTwist, screen, twistColor, 2.5f);
      }
      previousTwist = screen;
      previousTwistVisible = visible;
    }
  }
}

bool SceneTemplate::GetRagdollEditGizmoFrame(int capsuleIndex,
                                            XVECTOR3& outCenter,
                                            std::array<XVECTOR3, 3>& outAxes,
                                            float& outSize,
                                            bool globalAxes) {
  XMATRIX44 bodyWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(capsuleIndex, bodyWorld)) {
    return false;
  }

  outCenter = XVECTOR3(bodyWorld.m41, bodyWorld.m42, bodyWorld.m43, 1.0f);
  if (globalAxes) {
    outAxes[0] = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
    outAxes[1] = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
    outAxes[2] = XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
  } else {
    outAxes[0] = Normalize3(XVECTOR3(bodyWorld.m11, bodyWorld.m12, bodyWorld.m13, 0.0f),
                            XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
    outAxes[1] = Normalize3(XVECTOR3(bodyWorld.m21, bodyWorld.m22, bodyWorld.m23, 0.0f),
                            XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
    outAxes[2] = Normalize3(XVECTOR3(bodyWorld.m31, bodyWorld.m32, bodyWorld.m33, 0.0f),
                            XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
  }

  const float distanceToCamera = Length3(outCenter - Cam.Eye);
  const float minSize = (std::max)(0.03f, m_modelRadius * 0.08f);
  const float maxSize = (std::max)(minSize, m_modelRadius * 0.45f);
  outSize = (std::max)(minSize, (std::min)(maxSize, distanceToCamera * 0.10f));
  return true;
}

bool SceneTemplate::BuildRagdollEditHandlePoints(int capsuleIndex, std::array<XVECTOR3, 7>& outPoints) {
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (capsuleIndex < 0 || capsuleIndex >= static_cast<int>(bones.size())) {
    return false;
  }

  const auto& shape = bones[static_cast<std::size_t>(capsuleIndex)].body.shape;
  if (!IsEditableRagdollShape(shape)) {
    return false;
  }

  XMATRIX44 bodyWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(capsuleIndex, bodyWorld)) {
    return false;
  }

  const float radius = (std::max)(kRagdollMinShapeExtent, shape.radius);
  const float extent = (std::max)(0.002f, shape.halfHeight + radius);
  const XVECTOR3 boxHalfExtents = ClampRagdollBoxHalfExtents(shape.halfExtents);
  const float extentX = shape.type == t850::PhysicsShapeType::Box ? boxHalfExtents.x : radius;
  const float extentY = shape.type == t850::PhysicsShapeType::Box ? boxHalfExtents.y : extent;
  const float extentZ = shape.type == t850::PhysicsShapeType::Box ? boxHalfExtents.z : radius;
  outPoints[0] = t850::TransformPoint(XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f), bodyWorld);
  outPoints[1] = t850::TransformPoint(XVECTOR3(0.0f,  extentY, 0.0f, 1.0f), bodyWorld);
  outPoints[2] = t850::TransformPoint(XVECTOR3(0.0f, -extentY, 0.0f, 1.0f), bodyWorld);
  outPoints[3] = t850::TransformPoint(XVECTOR3( extentX, 0.0f, 0.0f, 1.0f), bodyWorld);
  outPoints[4] = t850::TransformPoint(XVECTOR3(-extentX, 0.0f, 0.0f, 1.0f), bodyWorld);
  outPoints[5] = t850::TransformPoint(XVECTOR3(0.0f, 0.0f,  extentZ, 1.0f), bodyWorld);
  outPoints[6] = t850::TransformPoint(XVECTOR3(0.0f, 0.0f, -extentZ, 1.0f), bodyWorld);
  return true;
}

bool SceneTemplate::PickRagdollEditHandle(float mouseX, float mouseY, float thresholdPixels, int& outCapsuleIndex, int& outHandleIndex) {
  outCapsuleIndex = -1;
  outHandleIndex = -1;
  if (!m_skeletonEditMode || !g_pBaseDriver || m_ragdollAnimationBinding.referencePose.bones.empty() ||
      m_ragdollEditSelectionMode != kRagdollSelectCapsules ||
      m_ragdollEditGizmoMode != kRagdollToolEditCapsule) {
    return false;
  }

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  int bestPriority = (std::numeric_limits<int>::max)();
  float bestDistanceSq = FLT_MAX;
  const float handleWorldRadius = (std::max)(0.01f, m_modelRadius * 0.014f);

  auto pickRadiusPixels = [&](const XVECTOR3& worldPoint) {
    float radiusPixels = thresholdPixels;
    bool centerVisible = false;
    bool edgeVisible = false;
    const ImVec2 center = ProjectWorldToScreen(worldPoint, VP, width, height, centerVisible);
    const ImVec2 edge = ProjectWorldToScreen(worldPoint + Cam.Right * handleWorldRadius, VP, width, height, edgeVisible);
    if (centerVisible && edgeVisible) {
      const float dx = edge.x - center.x;
      const float dy = edge.y - center.y;
      radiusPixels = (std::max)(radiusPixels, std::sqrt(dx * dx + dy * dy) * 1.2f);
    }
    return radiusPixels;
  };

  for (int capsuleIndex = 0; capsuleIndex < static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size()); ++capsuleIndex) {
    if (IsRagdollCapsuleFrozen(capsuleIndex)) {
      continue;
    }
    std::array<XVECTOR3, 7> points;
    if (!BuildRagdollEditHandlePoints(capsuleIndex, points)) {
      continue;
    }
    for (int handleIndex = 0; handleIndex < static_cast<int>(points.size()); ++handleIndex) {
      bool visible = false;
      const ImVec2 screen = ProjectWorldToScreen(points[static_cast<std::size_t>(handleIndex)], VP, width, height, visible);
      if (!visible) {
        continue;
      }
      const float dx = screen.x - mouseX;
      const float dy = screen.y - mouseY;
      const float distanceSq = dx * dx + dy * dy;
      const float radiusPixels = pickRadiusPixels(points[static_cast<std::size_t>(handleIndex)]);
      if (distanceSq > radiusPixels * radiusPixels) {
        continue;
      }
      const bool selectedCapsule = capsuleIndex == m_ragdollEditSelectedCapsule;
      const bool centerHandle = handleIndex == 0;
      const int priority = (selectedCapsule ? 0 : 2) + (centerHandle ? 1 : 0);
      if (priority < bestPriority ||
          (priority == bestPriority && distanceSq < bestDistanceSq)) {
        bestPriority = priority;
        bestDistanceSq = distanceSq;
        outCapsuleIndex = capsuleIndex;
        outHandleIndex = handleIndex;
      }
    }
  }
  return outCapsuleIndex >= 0 && outHandleIndex >= 0;
}

bool SceneTemplate::PickRagdollEditCapsule(float mouseX, float mouseY, float thresholdPixels, int& outCapsuleIndex) {
  outCapsuleIndex = -1;
  if (!m_skeletonEditMode || !g_pBaseDriver || m_ragdollAnimationBinding.referencePose.bones.empty()) {
    return false;
  }

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  const ImVec2 mouse(mouseX, mouseY);
  float bestScore = FLT_MAX;

  for (int capsuleIndex = 0; capsuleIndex < static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size()); ++capsuleIndex) {
    std::array<XVECTOR3, 7> points;
    if (!BuildRagdollEditHandlePoints(capsuleIndex, points)) {
      continue;
    }

    bool centerVisible = false;
    bool topVisible = false;
    bool bottomVisible = false;
    const ImVec2 center = ProjectWorldToScreen(points[0], VP, width, height, centerVisible);
    const ImVec2 top = ProjectWorldToScreen(points[1], VP, width, height, topVisible);
    const ImVec2 bottom = ProjectWorldToScreen(points[2], VP, width, height, bottomVisible);
    if (!centerVisible || !topVisible || !bottomVisible) {
      continue;
    }

    float radiusPixels = thresholdPixels;
    for (int handleIndex = 3; handleIndex < 7; ++handleIndex) {
      bool sideVisible = false;
      const ImVec2 side = ProjectWorldToScreen(points[static_cast<std::size_t>(handleIndex)], VP, width, height, sideVisible);
      if (!sideVisible) {
        continue;
      }
      const float dx = side.x - center.x;
      const float dy = side.y - center.y;
      radiusPixels = (std::max)(radiusPixels, std::sqrt(dx * dx + dy * dy));
    }

    const float axisDistance = std::sqrt(DistancePointToSegmentSq(mouse, top, bottom));
    const float surfaceDistance = (std::max)(0.0f, axisDistance - radiusPixels);
    const float score = surfaceDistance + axisDistance * 0.001f;
    if (surfaceDistance <= thresholdPixels && score < bestScore) {
      bestScore = score;
      outCapsuleIndex = capsuleIndex;
    }
  }

  return outCapsuleIndex >= 0;
}

bool SceneTemplate::PickRagdollEditTransformGizmo(float mouseX, float mouseY, int& outAxis) {
  outAxis = -1;
  if (!m_skeletonEditMode || !g_pBaseDriver ||
      (m_ragdollEditGizmoMode != kRagdollToolMove && m_ragdollEditGizmoMode != kRagdollToolRotate)) {
    return false;
  }

  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  const bool globalGizmoAxes = m_ragdollEditTransformSpace == kRagdollTransformSpaceGlobal;
  if (m_ragdollEditSelectionMode == kRagdollSelectCapsules) {
    if (m_ragdollEditSelectedCapsule < 0 || IsRagdollCapsuleFrozen(m_ragdollEditSelectedCapsule) ||
        !GetRagdollEditGizmoFrame(m_ragdollEditSelectedCapsule, center, axes, size, globalGizmoAxes)) {
      return false;
    }
  } else if (m_ragdollEditSelectionMode == kRagdollSelectBones) {
    if (m_skeletonEditSelectedBone < 0 ||
        !GetSkeletonPreviewBoneGizmoFrame(m_skeletonEditSelectedBone, center, axes, size, globalGizmoAxes)) {
      return false;
    }
  } else {
    return false;
  }

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  const ImVec2 mouse(mouseX, mouseY);
  constexpr float kThresholdPixels = 12.0f;
  float bestDistanceSq = kThresholdPixels * kThresholdPixels;

  if (m_ragdollEditGizmoMode == kRagdollToolMove) {
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      bool startVisible = false;
      bool endVisible = false;
      const ImVec2 start = ProjectWorldToScreen(center + axes[static_cast<std::size_t>(axisIndex)] * (size * 0.16f),
                                                VP, width, height, startVisible);
      const ImVec2 end = ProjectWorldToScreen(center + axes[static_cast<std::size_t>(axisIndex)] * size,
                                              VP, width, height, endVisible);
      if (!startVisible || !endVisible) {
        continue;
      }
      const float distanceSq = DistancePointToSegmentSq(mouse, start, end);
      if (distanceSq < bestDistanceSq) {
        bestDistanceSq = distanceSq;
        outAxis = axisIndex;
      }
    }
  } else if (m_ragdollEditGizmoMode == kRagdollToolRotate) {
    constexpr int kSegments = 64;
    const float radius = size * 0.78f;
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      const XVECTOR3 u = axes[static_cast<std::size_t>((axisIndex + 1) % 3)];
      const XVECTOR3 v = axes[static_cast<std::size_t>((axisIndex + 2) % 3)];
      ImVec2 previous;
      bool previousVisible = false;
      for (int segment = 0; segment <= kSegments; ++segment) {
        const float t = static_cast<float>(segment) / static_cast<float>(kSegments) * (2.0f * xPI);
        const XVECTOR3 point = center + (u * std::cos(t) + v * std::sin(t)) * radius;
        bool visible = false;
        const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
        if (visible && previousVisible) {
          const float distanceSq = DistancePointToSegmentSq(mouse, previous, screen);
          if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            outAxis = axisIndex;
          }
        }
        previous = screen;
        previousVisible = visible;
      }
    }
  }

  return outAxis >= 0;
}

bool SceneTemplate::BeginRagdollEditTransformGizmoDrag(float mouseX, float mouseY) {
  int pickedAxis = -1;
  if (!PickRagdollEditTransformGizmo(mouseX, mouseY, pickedAxis)) {
    return false;
  }

  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  const bool globalGizmoAxes = m_ragdollEditTransformSpace == kRagdollTransformSpaceGlobal;
  if (m_ragdollEditSelectionMode == kRagdollSelectCapsules) {
    if (m_ragdollEditSelectedCapsule < 0 ||
        !GetRagdollEditGizmoFrame(m_ragdollEditSelectedCapsule, center, axes, size, globalGizmoAxes)) {
      return false;
    }
  } else if (m_ragdollEditSelectionMode == kRagdollSelectBones) {
    if (m_skeletonEditSelectedBone < 0 ||
        !GetSkeletonPreviewBoneGizmoFrame(m_skeletonEditSelectedBone, center, axes, size, globalGizmoAxes) ||
        !BeginSkeletonPreviewBone(m_skeletonEditSelectedBone)) {
      return false;
    }
  } else {
    return false;
  }
  (void)size;

  XMATRIX44 invVP;
  VP.Inverse(&invVP);
  const int width = (std::max)(1, g_pBaseDriver ? g_pBaseDriver->width : 1);
  const int height = (std::max)(1, g_pBaseDriver ? g_pBaseDriver->height : 1);
  const t850::Ray ray = t850::ScreenPointToRay(mouseX, mouseY, 0, 0, width, height, invVP);
  const XVECTOR3 axis = axes[static_cast<std::size_t>(pickedAxis)];
  m_ragdollEditGizmoDragCenter = center;
  m_ragdollEditGizmoDragAxis = axis;

  if (m_ragdollEditGizmoMode == kRagdollToolMove) {
    if (!ClosestRayAxisParameter(ray, m_ragdollEditGizmoDragCenter, m_ragdollEditGizmoDragAxis, m_ragdollEditGizmoLastParameter)) {
      return false;
    }
  } else if (m_ragdollEditGizmoMode == kRagdollToolRotate) {
    XVECTOR3 hitPoint;
    if (!RayPlaneIntersection(ray, m_ragdollEditGizmoDragCenter, m_ragdollEditGizmoDragAxis, hitPoint)) {
      return false;
    }
    m_ragdollEditGizmoLastVector = Normalize3(hitPoint - m_ragdollEditGizmoDragCenter,
                                              axes[static_cast<std::size_t>((pickedAxis + 1) % 3)]);
  } else {
    return false;
  }

  m_ragdollEditGizmoAxis = pickedAxis;
  m_ragdollEditGizmoDragging = true;
  m_ragdollEditHandleDragging = false;
  m_skeletonEditDragging = false;
  return true;
}

bool SceneTemplate::DragRagdollEditTransformGizmo(float mouseX, float mouseY) {
  if (!m_ragdollEditGizmoDragging ||
      m_ragdollEditGizmoAxis < 0 ||
      !g_pBaseDriver) {
    return false;
  }

  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  const bool globalGizmoAxes = m_ragdollEditTransformSpace == kRagdollTransformSpaceGlobal;
  if (m_ragdollEditSelectionMode == kRagdollSelectCapsules) {
    if (m_ragdollEditSelectedCapsule < 0 ||
        !GetRagdollEditGizmoFrame(m_ragdollEditSelectedCapsule, center, axes, size, globalGizmoAxes)) {
      return false;
    }
  } else if (m_ragdollEditSelectionMode == kRagdollSelectBones) {
    if (m_skeletonEditSelectedBone < 0 ||
        !GetSkeletonPreviewBoneGizmoFrame(m_skeletonEditSelectedBone, center, axes, size, globalGizmoAxes) ||
        !BeginSkeletonPreviewBone(m_skeletonEditSelectedBone)) {
      return false;
    }
  } else {
    return false;
  }
  (void)center;
  (void)size;

  XMATRIX44 invVP;
  VP.Inverse(&invVP);
  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  const t850::Ray ray = t850::ScreenPointToRay(mouseX, mouseY, 0, 0, width, height, invVP);
  const XVECTOR3 axis = Normalize3(m_ragdollEditGizmoDragAxis, axes[static_cast<std::size_t>(m_ragdollEditGizmoAxis)]);

  if (m_ragdollEditGizmoMode == kRagdollToolMove) {
    float currentParameter = 0.0f;
    if (!ClosestRayAxisParameter(ray, m_ragdollEditGizmoDragCenter, axis, currentParameter)) {
      return false;
    }
    const float deltaParameter = currentParameter - m_ragdollEditGizmoLastParameter;
    m_ragdollEditGizmoLastParameter = currentParameter;
    if (std::fabs(deltaParameter) <= 0.000001f) {
      return true;
    }
    return m_ragdollEditSelectionMode == kRagdollSelectBones
        ? MoveSkeletonPreviewBoneByWorldDelta(m_skeletonEditSelectedBone, axis * deltaParameter)
        : MoveRagdollEditCapsuleByWorldDelta(m_ragdollEditSelectedCapsule, axis * deltaParameter, false);
  }

  if (m_ragdollEditGizmoMode != kRagdollToolRotate) {
    return false;
  }

  XVECTOR3 hitPoint;
  if (!RayPlaneIntersection(ray, m_ragdollEditGizmoDragCenter, axis, hitPoint)) {
    return false;
  }
  const XVECTOR3 currentVector = Normalize3(hitPoint - m_ragdollEditGizmoDragCenter, m_ragdollEditGizmoLastVector);
  const float dot = (std::max)(-1.0f, (std::min)(1.0f, Dot3(m_ragdollEditGizmoLastVector, currentVector)));
  const float signedAngle = std::atan2(Dot3(axis, Cross3(m_ragdollEditGizmoLastVector, currentVector)), dot);
  m_ragdollEditGizmoLastVector = currentVector;
  if (std::fabs(signedAngle) <= 0.000001f) {
    return true;
  }
  return m_ragdollEditSelectionMode == kRagdollSelectBones
      ? RotateSkeletonPreviewBoneWorld(m_skeletonEditSelectedBone, axis, signedAngle)
      : RotateRagdollEditCapsuleWorld(m_ragdollEditSelectedCapsule, axis, signedAngle, false);
}

void SceneTemplate::DrawRagdollEditTransformGizmo() {
  if (!m_skeletonEditMode ||
      m_ragdollEditSelectionMode != kRagdollSelectCapsules ||
      m_ragdollEditSelectedCapsule < 0 ||
      !g_pBaseDriver ||
      !ImGui::GetCurrentContext()) {
    return;
  }

  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  const bool globalGizmoAxes = m_ragdollEditTransformSpace == kRagdollTransformSpaceGlobal;
  if (!GetRagdollEditGizmoFrame(m_ragdollEditSelectedCapsule, center, axes, size)) {
    return;
  }
  const std::array<XVECTOR3, 3> localAxes = axes;
  if (globalGizmoAxes) {
    axes[0] = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
    axes[1] = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
    axes[2] = XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
  }

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  bool centerVisible = false;
  const ImVec2 centerScreen = ProjectWorldToScreen(center, VP, width, height, centerVisible);
  if (!centerVisible) {
    return;
  }

  ImDrawList* drawList = ImGui::GetBackgroundDrawList();
  const ImU32 originColor = IM_COL32(64, 160, 255, 255);
  const ImU32 originFill = IM_COL32(24, 96, 255, 180);
  drawList->AddCircle(centerScreen, 9.0f, originColor, 24, 2.5f);
  drawList->AddCircleFilled(centerScreen, 3.5f, originFill, 16);
  drawList->AddLine(ImVec2(centerScreen.x - 11.0f, centerScreen.y), ImVec2(centerScreen.x + 11.0f, centerScreen.y), originColor, 2.0f);
  drawList->AddLine(ImVec2(centerScreen.x, centerScreen.y - 11.0f), ImVec2(centerScreen.x, centerScreen.y + 11.0f), originColor, 2.0f);
  drawList->AddText(ImVec2(centerScreen.x + 11.0f, centerScreen.y + 5.0f), originColor, "origin");

  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (m_ragdollEditSelectedCapsule < static_cast<int>(bones.size())) {
    const auto& shape = bones[static_cast<std::size_t>(m_ragdollEditSelectedCapsule)].body.shape;
    const float capsuleExtent = shape.type == t850::PhysicsShapeType::Capsule
        ? (std::max)(0.002f, shape.halfHeight + shape.radius)
        : ClampRagdollBoxHalfExtents(shape.halfExtents).y;
    const float markerLength = (std::min)((std::max)(capsuleExtent, size * 0.45f), size * 1.15f);
    const XVECTOR3 localY = localAxes[1];
    const XVECTOR3 yPositive = center + localY * markerLength;
    const XVECTOR3 yNegative = center - localY * (markerLength * 0.72f);
    bool yPositiveVisible = false;
    bool yNegativeVisible = false;
    const ImVec2 yPositiveScreen = ProjectWorldToScreen(yPositive, VP, width, height, yPositiveVisible);
    const ImVec2 yNegativeScreen = ProjectWorldToScreen(yNegative, VP, width, height, yNegativeVisible);
    if (yPositiveVisible) {
      drawList->AddLine(centerScreen, yPositiveScreen, originColor, 3.0f);
      drawList->AddCircleFilled(yPositiveScreen, 5.0f, originColor, 16);
      drawList->AddText(ImVec2(yPositiveScreen.x + 7.0f, yPositiveScreen.y - 7.0f), originColor, "+Y top");
    }
    if (yNegativeVisible) {
      const ImU32 negativeColor = IM_COL32(80, 110, 180, 230);
      drawList->AddLine(centerScreen, yNegativeScreen, negativeColor, 1.8f);
      drawList->AddCircle(yNegativeScreen, 5.0f, negativeColor, 16, 2.0f);
      drawList->AddText(ImVec2(yNegativeScreen.x + 7.0f, yNegativeScreen.y - 7.0f), negativeColor, "-Y bottom");
    }
  }

  if (IsRagdollCapsuleFrozen(m_ragdollEditSelectedCapsule)) {
    return;
  }

  if (m_ragdollEditGizmoMode == kRagdollToolMove) {
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      const bool active = m_ragdollEditGizmoDragging && m_ragdollEditGizmoAxis == axisIndex;
      const ImU32 color = RagdollGizmoAxisColor(axisIndex, active);
      bool endVisible = false;
      const ImVec2 end = ProjectWorldToScreen(center + axes[static_cast<std::size_t>(axisIndex)] * size,
                                              VP, width, height, endVisible);
      if (!endVisible) {
        continue;
      }
      drawList->AddLine(centerScreen, end, color, active ? 4.0f : 3.0f);
      const float dx = end.x - centerScreen.x;
      const float dy = end.y - centerScreen.y;
      const float len = std::sqrt(dx * dx + dy * dy);
      if (len > 0.001f) {
        const float ux = dx / len;
        const float uy = dy / len;
        const ImVec2 perp(-uy, ux);
        const ImVec2 base(end.x - ux * 14.0f, end.y - uy * 14.0f);
        drawList->AddTriangleFilled(end,
                                    ImVec2(base.x + perp.x * 5.0f, base.y + perp.y * 5.0f),
                                    ImVec2(base.x - perp.x * 5.0f, base.y - perp.y * 5.0f),
                                    color);
      }
    }
    return;
  }
  if (m_ragdollEditGizmoMode != kRagdollToolRotate) {
    return;
  }

  constexpr int kSegments = 72;
  const float radius = size * 0.78f;
  for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
    const bool active = m_ragdollEditGizmoDragging && m_ragdollEditGizmoAxis == axisIndex;
    const ImU32 color = RagdollGizmoAxisColor(axisIndex, active);
    const XVECTOR3 u = axes[static_cast<std::size_t>((axisIndex + 1) % 3)];
    const XVECTOR3 v = axes[static_cast<std::size_t>((axisIndex + 2) % 3)];
    ImVec2 previous;
    bool previousVisible = false;
    for (int segment = 0; segment <= kSegments; ++segment) {
      const float t = static_cast<float>(segment) / static_cast<float>(kSegments) * (2.0f * xPI);
      const XVECTOR3 point = center + (u * std::cos(t) + v * std::sin(t)) * radius;
      bool visible = false;
      const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
      if (visible && previousVisible) {
        drawList->AddLine(previous, screen, color, active ? 3.5f : 2.5f);
      }
      previous = screen;
      previousVisible = visible;
    }
  }
}

void SceneTemplate::DrawSkeletonPreviewBoneGizmo() {
  if (!m_skeletonEditMode ||
      m_ragdollEditSelectionMode != kRagdollSelectBones ||
      m_skeletonEditSelectedBone < 0 ||
      (m_ragdollEditGizmoMode != kRagdollToolMove && m_ragdollEditGizmoMode != kRagdollToolRotate) ||
      !g_pBaseDriver ||
      !ImGui::GetCurrentContext()) {
    return;
  }

  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  const bool globalGizmoAxes = m_ragdollEditTransformSpace == kRagdollTransformSpaceGlobal;
  if (!GetSkeletonPreviewBoneGizmoFrame(m_skeletonEditSelectedBone, center, axes, size, globalGizmoAxes)) {
    return;
  }

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  bool centerVisible = false;
  const ImVec2 centerScreen = ProjectWorldToScreen(center, VP, width, height, centerVisible);
  if (!centerVisible) {
    return;
  }

  ImDrawList* drawList = ImGui::GetBackgroundDrawList();
  const ImU32 originColor = IM_COL32(255, 255, 255, 245);
  const ImU32 originFill = IM_COL32(255, 255, 255, 150);
  drawList->AddCircle(centerScreen, 8.0f, originColor, 24, 2.0f);
  drawList->AddCircleFilled(centerScreen, 3.0f, originFill, 16);
  drawList->AddText(ImVec2(centerScreen.x + 10.0f, centerScreen.y + 5.0f), originColor, "bone");

  if (m_ragdollEditGizmoMode == kRagdollToolMove) {
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      const bool active = m_ragdollEditGizmoDragging &&
          m_ragdollEditSelectionMode == kRagdollSelectBones &&
          m_ragdollEditGizmoAxis == axisIndex;
      const ImU32 color = RagdollGizmoAxisColor(axisIndex, active);
      bool endVisible = false;
      const ImVec2 end = ProjectWorldToScreen(center + axes[static_cast<std::size_t>(axisIndex)] * size,
                                              VP, width, height, endVisible);
      if (!endVisible) {
        continue;
      }
      drawList->AddLine(centerScreen, end, color, active ? 4.0f : 3.0f);
      const float dx = end.x - centerScreen.x;
      const float dy = end.y - centerScreen.y;
      const float len = std::sqrt(dx * dx + dy * dy);
      if (len > 0.001f) {
        const float ux = dx / len;
        const float uy = dy / len;
        const ImVec2 perp(-uy, ux);
        const ImVec2 base(end.x - ux * 14.0f, end.y - uy * 14.0f);
        drawList->AddTriangleFilled(end,
                                    ImVec2(base.x + perp.x * 5.0f, base.y + perp.y * 5.0f),
                                    ImVec2(base.x - perp.x * 5.0f, base.y - perp.y * 5.0f),
                                    color);
      }
    }
    return;
  }

  constexpr int kSegments = 72;
  const float radius = size * 0.78f;
  for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
    const bool active = m_ragdollEditGizmoDragging &&
        m_ragdollEditSelectionMode == kRagdollSelectBones &&
        m_ragdollEditGizmoAxis == axisIndex;
    const ImU32 color = RagdollGizmoAxisColor(axisIndex, active);
    const XVECTOR3 u = axes[static_cast<std::size_t>((axisIndex + 1) % 3)];
    const XVECTOR3 v = axes[static_cast<std::size_t>((axisIndex + 2) % 3)];
    ImVec2 previous;
    bool previousVisible = false;
    for (int segment = 0; segment <= kSegments; ++segment) {
      const float t = static_cast<float>(segment) / static_cast<float>(kSegments) * (2.0f * xPI);
      const XVECTOR3 point = center + (u * std::cos(t) + v * std::sin(t)) * radius;
      bool visible = false;
      const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
      if (visible && previousVisible) {
        drawList->AddLine(previous, screen, color, active ? 3.5f : 2.5f);
      }
      previous = screen;
      previousVisible = visible;
    }
  }
}

bool SceneTemplate::DragRagdollEditHandle(int capsuleIndex, int handleIndex, const XVECTOR3& worldDelta) {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (capsuleIndex < 0 || capsuleIndex >= static_cast<int>(bones.size()) ||
      capsuleIndex >= static_cast<int>(m_ragdollAnimationBinding.bodyFromBone.size()) ||
      handleIndex < 0 || handleIndex >= 7) {
    return false;
  }

  auto& bone = bones[static_cast<std::size_t>(capsuleIndex)];
  auto& local = m_ragdollAnimationBinding.bodyFromBone[static_cast<std::size_t>(capsuleIndex)];
  auto& shape = bone.body.shape;
  if (IsRagdollCapsuleFrozen(capsuleIndex)) {
    return false;
  }
  if (!IsEditableRagdollShape(shape)) {
    return false;
  }

  XMATRIX44 bodyWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(capsuleIndex, bodyWorld)) {
    return false;
  }

  XMATRIX44 inverseLocal;
  if (!InvertAffineNoExit(local, inverseLocal)) {
    T8_LOG_ERROR("[RagdollEdit] Cannot drag capsule %d handle: local capsule frame is singular", capsuleIndex);
    return false;
  }
  XMATRIX44 boneWorld = inverseLocal * bodyWorld;
  XMATRIX44 inverseBoneWorld;
  if (!InvertAffineNoExit(boneWorld, inverseBoneWorld)) {
    T8_LOG_ERROR("[RagdollEdit] Cannot drag capsule %d handle: bone frame is singular", capsuleIndex);
    return false;
  }
  XMATRIX44 inverseBodyWorld;
  if (!InvertAffineNoExit(bodyWorld, inverseBodyWorld)) {
    T8_LOG_ERROR("[RagdollEdit] Cannot drag capsule %d handle: body frame is singular", capsuleIndex);
    return false;
  }

  auto translateCenterByWorld = [&](const XVECTOR3& deltaWorld) {
    const XVECTOR3 deltaBone = TransformVectorNoTranslation(deltaWorld, inverseBoneWorld);
    local.m41 += deltaBone.x;
    local.m42 += deltaBone.y;
    local.m43 += deltaBone.z;
  };

  bool rebuildRagdoll = false;
  if (handleIndex == 0) {
    translateCenterByWorld(worldDelta);
  } else {
    const XVECTOR3 deltaBody = TransformVectorNoTranslation(worldDelta, inverseBodyWorld);
    auto bodyAxis = [&](int axis) {
      if (axis == 0) return XVECTOR3(bodyWorld.m11, bodyWorld.m12, bodyWorld.m13, 0.0f);
      if (axis == 1) return XVECTOR3(bodyWorld.m21, bodyWorld.m22, bodyWorld.m23, 0.0f);
      return XVECTOR3(bodyWorld.m31, bodyWorld.m32, bodyWorld.m33, 0.0f);
    };
    auto deltaCoord = [&](int axis) {
      if (axis == 0) return deltaBody.x;
      if (axis == 1) return deltaBody.y;
      return deltaBody.z;
    };

    if (shape.type == t850::PhysicsShapeType::Box) {
      XVECTOR3 halfExtents = ClampRagdollBoxHalfExtents(shape.halfExtents);
      int axis = 1;
      float sign = 1.0f;
      if (handleIndex == 2) {
        axis = 1;
        sign = -1.0f;
      } else if (handleIndex == 3) {
        axis = 0;
      } else if (handleIndex == 4) {
        axis = 0;
        sign = -1.0f;
      } else if (handleIndex == 5) {
        axis = 2;
      } else if (handleIndex == 6) {
        axis = 2;
        sign = -1.0f;
      }

      const float delta = deltaCoord(axis);
      const float newExtent = (std::max)(kRagdollMinShapeExtent, AxisCoord(halfExtents, axis) + sign * delta * 0.5f);
      SetAxisCoord(halfExtents, axis, newExtent);
      translateCenterByWorld(bodyAxis(axis) * (delta * 0.5f));
      shape.halfExtents = halfExtents;
      rebuildRagdoll = true;
    } else {
      const float radius = (std::max)(kRagdollMinShapeExtent, shape.radius);
      float extent = (std::max)(radius + kRagdollMinShapeExtent, shape.halfHeight + radius);

      if (handleIndex == 1 || handleIndex == 2) {
        const float signedDelta = handleIndex == 1 ? deltaBody.y : -deltaBody.y;
        const float centerShiftBodyY = deltaBody.y * 0.5f;
        extent = (std::max)(radius + kRagdollMinShapeExtent, extent + signedDelta * 0.5f);
        translateCenterByWorld(bodyAxis(1) * centerShiftBodyY);
        shape.halfHeight = (std::max)(kRagdollMinShapeExtent, extent - radius);
        rebuildRagdoll = true;
      } else {
        float radiusDelta = 0.0f;
        if (handleIndex == 3) radiusDelta = deltaBody.x;
        else if (handleIndex == 4) radiusDelta = -deltaBody.x;
        else if (handleIndex == 5) radiusDelta = deltaBody.z;
        else if (handleIndex == 6) radiusDelta = -deltaBody.z;
        const float newRadius = (std::max)(kRagdollMinShapeExtent, radius + radiusDelta);
        extent = (std::max)(newRadius + kRagdollMinShapeExtent, extent);
        shape.radius = newRadius;
        shape.halfHeight = (std::max)(kRagdollMinShapeExtent, extent - newRadius);
        rebuildRagdoll = true;
      }
    }
  }

  UpdateRagdollReferenceBodyFromLocal(capsuleIndex);
  m_ragdollEditDirty = true;
  return ApplyRagdollEditPose(rebuildRagdoll);
}

int SceneTemplate::PickSkeletonEditBone(float mouseX, float mouseY, float thresholdPixels) const {
  if (!m_skeletonEditMode || m_skeletonEditCombined.empty() || !g_pBaseDriver) {
    return -1;
  }
  (void)thresholdPixels;

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);

  XMATRIX44 viewProjection = VP;
  XMATRIX44 invVP;
  viewProjection.Inverse(&invVP);
  const t850::Ray ray = t850::ScreenPointToRay(mouseX, mouseY, 0, 0, width, height, invVP);

  static constexpr int kFaces[8][3] = {
      {0, 2, 4}, {0, 4, 3}, {0, 3, 5}, {0, 5, 2},
      {1, 4, 2}, {1, 3, 4}, {1, 5, 3}, {1, 2, 5}};

  float bestDistance = FLT_MAX;
  int bestBone = -1;
  for (int boneIndex = 0; boneIndex < static_cast<int>(m_skeletonEditCombined.size()); ++boneIndex) {
    std::array<XVECTOR3, 6> points{};
    if (!BuildSkeletonEditBoneOctahedron(boneIndex, 0.18f, points)) {
      continue;
    }
    for (const auto& face : kFaces) {
      float t = 0.0f;
      float u = 0.0f;
      float v = 0.0f;
      if (t850::RayIntersectsTriangle(ray, points[face[0]], points[face[1]], points[face[2]], t, u, v) &&
          t >= 0.0f && t < bestDistance) {
        bestDistance = t;
        bestBone = boneIndex;
      }
    }
  }
  return bestBone;
}

void SceneTemplate::PickSkeletonEditBonesInScreenRect(float minX, float minY, float maxX, float maxY, std::vector<int>& outBones) const {
  outBones.clear();
  if (!m_skeletonEditMode || m_skeletonEditCombined.empty() || !g_pBaseDriver) {
    return;
  }

  if (minX > maxX) std::swap(minX, maxX);
  if (minY > maxY) std::swap(minY, maxY);
  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);

  for (int boneIndex = 0; boneIndex < static_cast<int>(m_skeletonEditCombined.size()); ++boneIndex) {
    if (FindRagdollCapsuleControllingBone(boneIndex) >= 0) {
      continue;
    }

    std::array<XVECTOR3, 6> points{};
    if (!BuildSkeletonEditBoneOctahedron(boneIndex, 0.18f, points)) {
      continue;
    }

    bool hasVisiblePoint = false;
    float boneMinX = FLT_MAX;
    float boneMinY = FLT_MAX;
    float boneMaxX = -FLT_MAX;
    float boneMaxY = -FLT_MAX;
    for (const XVECTOR3& point : points) {
      bool visible = false;
      const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
      if (!visible) {
        continue;
      }
      hasVisiblePoint = true;
      boneMinX = (std::min)(boneMinX, screen.x);
      boneMinY = (std::min)(boneMinY, screen.y);
      boneMaxX = (std::max)(boneMaxX, screen.x);
      boneMaxY = (std::max)(boneMaxY, screen.y);
    }

    if (!hasVisiblePoint) {
      continue;
    }

    if (!(boneMaxX < minX || boneMinX > maxX || boneMaxY < minY || boneMinY > maxY)) {
      outBones.push_back(boneIndex);
    }
  }
}

bool SceneTemplate::SelectRagdollContextTargetAt(float mouseX, float mouseY) {
  auto selectBody = [&](int capsuleIndex) {
    RestoreSkeletonPreviewBone();
    SelectRagdollEditCapsule(capsuleIndex, true);
    m_ragdollEditSelectionMode = kRagdollSelectCapsules;
    return true;
  };

  auto selectJoint = [&](int childCapsule) {
    RestoreSkeletonPreviewBone();
    SelectRagdollEditCapsule(childCapsule, true);
    m_ragdollEditSelectedJoint = childCapsule;
    m_ragdollEditSelectedHandle = -1;
    m_ragdollEditSelectionMode = kRagdollSelectJoints;
    return true;
  };

  auto selectBone = [&](int boneIndex) {
    SelectSkeletonEditBone(boneIndex);
    m_ragdollEditSelectedUnassignedBone = -1;
    m_ragdollEditSelectedAffectedBone = -1;
    m_ragdollEditSelectionMode = kRagdollSelectBones;
    return true;
  };

  auto tryPickBody = [&]() {
    int pickedCapsule = -1;
    return PickRagdollEditCapsule(mouseX, mouseY, 18.0f, pickedCapsule) && selectBody(pickedCapsule);
  };

  auto tryPickJoint = [&]() {
    int pickedJoint = -1;
    return PickRagdollEditJoint(mouseX, mouseY, 18.0f, pickedJoint) && selectJoint(pickedJoint);
  };

  auto tryPickBone = [&]() {
    const int pickedBone = PickSkeletonEditBone(mouseX, mouseY, 18.0f);
    return pickedBone >= 0 && selectBone(pickedBone);
  };

  switch (m_ragdollEditSelectionMode) {
    case kRagdollSelectJoints:
      if (tryPickJoint() || tryPickBody() || tryPickBone()) {
        return true;
      }
      break;
    case kRagdollSelectBones:
      if (tryPickBone() || tryPickBody() || tryPickJoint()) {
        return true;
      }
      break;
    default:
      if (tryPickBody() || tryPickJoint() || tryPickBone()) {
        return true;
      }
      break;
  }

  return false;
}

SceneTemplate::RagdollAuthoringUndoSnapshot SceneTemplate::CaptureRagdollUndoSnapshot(const char* label) const {
  RagdollAuthoringUndoSnapshot snapshot;
  snapshot.binding = m_ragdollAnimationBinding;
  snapshot.animationPose = m_ragdollAnimationPose;
  snapshot.parentCapsules = m_ragdollParentCapsules;
  snapshot.jointParentCapsules = m_ragdollJointParentCapsules;
  snapshot.frozenCapsules = m_ragdollFrozenCapsules;
  snapshot.frozenJoints = m_ragdollFrozenJoints;
  snapshot.contactJoints = m_ragdollContactJoints;
  snapshot.skeletonEditCombined = m_skeletonEditCombined;
  if (m_skeletonPreviewBoneActive &&
      m_skeletonPreviewOriginalCombined.size() == snapshot.skeletonEditCombined.size()) {
    snapshot.skeletonEditCombined = m_skeletonPreviewOriginalCombined;
  }
  snapshot.selectedCapsule = m_ragdollEditSelectedCapsule;
  snapshot.selectedJoint = m_ragdollEditSelectedJoint;
  snapshot.selectedParentCapsule = m_ragdollEditSelectedParentCapsule;
  snapshot.selectedJointParentCapsule = m_ragdollEditSelectedJointParentCapsule;
  snapshot.selectedBone = m_skeletonEditSelectedBone;
  snapshot.selectedUnassignedBone = m_ragdollEditSelectedUnassignedBone;
  snapshot.selectedAffectedBone = m_ragdollEditSelectedAffectedBone;
  snapshot.selectedHandle = m_ragdollEditSelectedHandle;
  snapshot.selectionMode = m_ragdollEditSelectionMode;
  snapshot.transformSpace = m_ragdollEditTransformSpace;
  snapshot.gizmoMode = m_ragdollEditGizmoMode;
  snapshot.label = label ? label : "";
  return snapshot;
}

bool SceneTemplate::RagdollUndoContentEquals(const RagdollAuthoringUndoSnapshot& a,
                                            const RagdollAuthoringUndoSnapshot& b) const {
  return t850::ragdoll_editor::SameAuthoringUndoContent(a, b);
}

void SceneTemplate::BeginRagdollUndoScope(const char* label) {
  if (m_ragdollUndoState.scopeActive || m_ragdollUndoState.suppressRecording) {
    return;
  }
  m_ragdollUndoState.scopeBefore = CaptureRagdollUndoSnapshot(label);
  m_ragdollUndoState.scopeLabel = label ? label : "";
  m_ragdollUndoState.scopeActive = true;
}

void SceneTemplate::PushRagdollUndoSnapshot(const RagdollAuthoringUndoSnapshot& snapshot) {
  if (!m_ragdollUndoState.stack.empty() &&
      RagdollUndoContentEquals(m_ragdollUndoState.stack.back(), snapshot)) {
    return;
  }
  m_ragdollUndoState.stack.push_back(snapshot);
  constexpr std::size_t kMaxUndoSnapshots = 10;
  if (m_ragdollUndoState.stack.size() > kMaxUndoSnapshots) {
    m_ragdollUndoState.stack.erase(
        m_ragdollUndoState.stack.begin(),
        m_ragdollUndoState.stack.begin() + static_cast<std::ptrdiff_t>(m_ragdollUndoState.stack.size() - kMaxUndoSnapshots));
  }
}

void SceneTemplate::EndRagdollUndoScope(bool gestureActive) {
  if (!m_ragdollUndoState.scopeActive) {
    if (m_ragdollUndoState.pendingActive && !gestureActive) {
      const RagdollAuthoringUndoSnapshot current = CaptureRagdollUndoSnapshot();
      if (!RagdollUndoContentEquals(m_ragdollUndoState.pendingBefore, current)) {
        PushRagdollUndoSnapshot(m_ragdollUndoState.pendingBefore);
      }
      m_ragdollUndoState.pendingActive = false;
    }
    return;
  }

  const RagdollAuthoringUndoSnapshot before = m_ragdollUndoState.scopeBefore;
  m_ragdollUndoState.scopeActive = false;

  if (m_ragdollUndoState.suppressRecording) {
    m_ragdollUndoState.suppressRecording = false;
    m_ragdollUndoState.pendingActive = false;
    return;
  }

  const RagdollAuthoringUndoSnapshot current = CaptureRagdollUndoSnapshot();
  const bool changed = !RagdollUndoContentEquals(before, current);
  if (changed) {
    if (gestureActive) {
      if (!m_ragdollUndoState.pendingActive) {
        m_ragdollUndoState.pendingBefore = before;
        m_ragdollUndoState.pendingBefore.label = m_ragdollUndoState.scopeLabel;
        m_ragdollUndoState.pendingActive = true;
      }
      return;
    }

    if (m_ragdollUndoState.pendingActive) {
      PushRagdollUndoSnapshot(m_ragdollUndoState.pendingBefore);
      m_ragdollUndoState.pendingActive = false;
    } else {
      RagdollAuthoringUndoSnapshot labeledBefore = before;
      labeledBefore.label = m_ragdollUndoState.scopeLabel;
      PushRagdollUndoSnapshot(labeledBefore);
    }
    return;
  }

  if (m_ragdollUndoState.pendingActive && !gestureActive) {
    const RagdollAuthoringUndoSnapshot latest = CaptureRagdollUndoSnapshot();
    if (!RagdollUndoContentEquals(m_ragdollUndoState.pendingBefore, latest)) {
      PushRagdollUndoSnapshot(m_ragdollUndoState.pendingBefore);
    }
    m_ragdollUndoState.pendingActive = false;
  }
}

bool SceneTemplate::CanUndoRagdollAuthoringEdit() const {
  return !m_ragdollUndoState.stack.empty();
}

const char* SceneTemplate::CurrentRagdollUndoLabel() const {
  return m_ragdollUndoState.stack.empty() || m_ragdollUndoState.stack.back().label.empty()
      ? "Undo"
      : m_ragdollUndoState.stack.back().label.c_str();
}

bool SceneTemplate::UndoRagdollAuthoringEdit() {
  if (m_ragdollUndoState.stack.empty()) {
    return false;
  }

  const RagdollAuthoringUndoSnapshot snapshot = m_ragdollUndoState.stack.back();
  m_ragdollUndoState.stack.pop_back();
  m_ragdollUndoState.pendingActive = false;
  m_ragdollUndoState.suppressRecording = true;
  m_skeletonPreviewBoneActive = false;
  m_skeletonPreviewBoneIndex = -1;
  m_skeletonPreviewOriginalCombined.clear();

  m_ragdollAnimationBinding = snapshot.binding;
  m_ragdollAnimationPose = snapshot.animationPose;
  m_ragdollParentCapsules = snapshot.parentCapsules;
  m_ragdollJointParentCapsules = snapshot.jointParentCapsules;
  m_ragdollFrozenCapsules = snapshot.frozenCapsules;
  m_ragdollFrozenJoints = snapshot.frozenJoints;
  m_ragdollContactJoints = snapshot.contactJoints;
  m_skeletonEditCombined = snapshot.skeletonEditCombined;
  m_ragdollEditSelectedCapsule = snapshot.selectedCapsule;
  m_ragdollEditSelectedJoint = snapshot.selectedJoint;
  m_ragdollEditSelectedParentCapsule = snapshot.selectedParentCapsule;
  m_ragdollEditSelectedJointParentCapsule = snapshot.selectedJointParentCapsule;
  m_skeletonEditSelectedBone = snapshot.selectedBone;
  m_ragdollEditSelectedUnassignedBone = snapshot.selectedUnassignedBone;
  m_ragdollEditSelectedAffectedBone = snapshot.selectedAffectedBone;
  m_ragdollEditSelectedHandle = snapshot.selectedHandle;
  m_ragdollEditSelectionMode = snapshot.selectionMode;
  m_ragdollEditTransformSpace = snapshot.transformSpace;
  m_ragdollEditGizmoMode = snapshot.gizmoMode;

  m_ragdollBoneSelectionActive = false;
  m_ragdollBoneMarqueeDragging = false;
  m_ragdollBoneSelectionPending.clear();
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_skeletonEditDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  m_ragdollEditRenamingCapsule = -1;
  m_ragdollEditRenameFocusPending = false;
  m_ragdollContextMenuRequested = false;
  m_ragdollContextMenuRightButtonHeld = false;
  m_ragdollClearRequested = false;
  m_ragdollEditRebuildRequested = false;

  if (!m_skeletonEditCombined.empty()) {
    ApplySkeletonEditPose();
    m_skeletonEditDirty = true;
  }

  if (m_ragdollAnimationBinding.referencePose.bones.empty()) {
    const t850::PhysicsRagdollHandle ragdollHandle = Meshes[0].GetPhysicsRagdoll();
    Meshes[0].AttachPhysicsRagdoll(t850::PhysicsRagdollHandle{});
    t850::EngineContext* engineContext = GetEngineContext();
    if (!engineContext) engineContext = &t850::GetEngineContext();
    if (engineContext && engineContext->physics && ragdollHandle.IsValid()) {
      engineContext->physics->DestroyRagdoll(ragdollHandle);
    }
    m_ragdollPhysicsStates.clear();
    m_ragdollPhysicsBoneIndices.clear();
    m_ragdollPhysicsCombinedMatrices.clear();
    m_driveRagdollFromAnimation = false;
    m_ragdollPhysicsDriven = false;
  } else {
    EnsureRagdollControlledBones();
    EnsureRagdollParentCapsules();
    EnsureRagdollJointState();
    EnsureRagdollFreezeState();
    ApplyRagdollEditPose(true);
  }

  m_ragdollEditDirty = true;
  m_ragdollEditTopologyChangedThisFrame = true;
  T8_LOG_INFO("[RagdollEdit] Undo restored '%s'", snapshot.label.empty() ? "previous edit" : snapshot.label.c_str());
  return true;
}

void SceneTemplate::DrawRagdollViewportContextMenu() {
  if (!ImGui::GetCurrentContext()) {
    m_ragdollContextMenuRequested = false;
    return;
  }

  static constexpr const char* kPopupId = "RagdollViewportContextMenu";
  constexpr float kLeftWidth = 148.0f;
  constexpr float kRightWidth = 164.0f;
  constexpr float kTopHeight = 132.0f;
  constexpr float kBottomHeight = 110.0f;
  constexpr float kGap = 16.0f;
  const ImVec2 popupSize(kLeftWidth + kRightWidth + kGap * 2.0f,
                         kTopHeight + kBottomHeight + kGap * 2.0f);
  const float rightX = kLeftWidth + kGap * 2.0f;
  const float bottomY = kTopHeight + kGap * 2.0f;
  const ImVec2 idealOrigin(kLeftWidth + kGap, kTopHeight + kGap);

  if (m_ragdollContextMenuRequested) {
    ImVec2 popupPos(m_ragdollContextMenuX - idealOrigin.x,
                    m_ragdollContextMenuY - idealOrigin.y);
    if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
      const float margin = 6.0f;
      const ImVec2 minPos(viewport->WorkPos.x + margin, viewport->WorkPos.y + margin);
      const ImVec2 maxPos(viewport->WorkPos.x + viewport->WorkSize.x - popupSize.x - margin,
                          viewport->WorkPos.y + viewport->WorkSize.y - popupSize.y - margin);
      if (maxPos.x >= minPos.x) {
        popupPos.x = (std::max)(minPos.x, (std::min)(maxPos.x, popupPos.x));
      } else {
        popupPos.x = minPos.x;
      }
      if (maxPos.y >= minPos.y) {
        popupPos.y = (std::max)(minPos.y, (std::min)(maxPos.y, popupPos.y));
      } else {
        popupPos.y = minPos.y;
      }
    }
    ImGui::SetNextWindowPos(popupPos, ImGuiCond_Always);
    ImGui::OpenPopup(kPopupId);
    m_ragdollContextMenuRequested = false;
  }
  ImGui::SetNextWindowSize(popupSize, ImGuiCond_Always);

  const ImGuiWindowFlags popupFlags =
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoScrollbar;
  if (!ImGui::BeginPopup(kPopupId, popupFlags)) {
    return;
  }

  auto cancelRagdollDrags = [&]() {
    m_skeletonEditDragging = false;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_ragdollEditJointAxis = -1;
  };

  auto chooseSelectionMode = [&](const char* label, int selectionMode) {
    if (ImGui::Selectable(label, m_ragdollEditSelectionMode == selectionMode)) {
      if (selectionMode != kRagdollSelectBones) {
        RestoreSkeletonPreviewBone();
      }
      m_ragdollEditSelectionMode = selectionMode;
      cancelRagdollDrags();
      ImGui::CloseCurrentPopup();
    }
  };

  auto chooseTransformSpace = [&](const char* label, int transformSpace) {
    if (ImGui::Selectable(label, m_ragdollEditTransformSpace == transformSpace)) {
      m_ragdollEditTransformSpace = transformSpace;
      cancelRagdollDrags();
      ImGui::CloseCurrentPopup();
    }
  };

  auto chooseTool = [&](const char* label, int toolMode, bool forceBodySelection) {
    if (ImGui::Selectable(label, m_ragdollEditGizmoMode == toolMode)) {
      if (forceBodySelection) {
        RestoreSkeletonPreviewBone();
        m_ragdollEditSelectionMode = kRagdollSelectCapsules;
      }
      m_ragdollEditGizmoMode = toolMode;
      cancelRagdollDrags();
      ImGui::CloseCurrentPopup();
    }
  };

  auto chooseUndo = [&]() {
    const bool canUndo = CanUndoRagdollAuthoringEdit();
    if (!canUndo) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Selectable("Undo", false) && canUndo) {
      UndoRagdollAuthoringEdit();
      ImGui::CloseCurrentPopup();
    }
    if (!canUndo) {
      ImGui::EndDisabled();
    }
  };

  auto beginPane = [](const char* id, const char* title, const ImVec2& position, const ImVec2& size) {
    ImGui::SetCursorPos(position);
    ImGui::BeginChild(id, size, true, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("%s", title);
    ImGui::Separator();
  };
  auto endPane = []() {
    ImGui::EndChild();
  };

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
  beginPane("ragdoll_context_transform", "Transform", ImVec2(0.0f, 0.0f), ImVec2(kLeftWidth, kTopHeight));
  chooseTransformSpace("Local transform", kRagdollTransformSpaceLocal);
  chooseTransformSpace("Global transform", kRagdollTransformSpaceGlobal);
  endPane();

  beginPane("ragdoll_context_tools", "Tool", ImVec2(rightX, 0.0f), ImVec2(kRightWidth, kTopHeight));
  chooseTool("Select", kRagdollToolSelect, false);
  chooseTool(m_ragdollEditSelectionMode == kRagdollSelectJoints ? "Edit Joint" : "Edit Body",
             kRagdollToolEditCapsule,
             m_ragdollEditSelectionMode != kRagdollSelectJoints);
  chooseTool("Move", kRagdollToolMove, false);
  chooseTool("Rotate", kRagdollToolRotate, false);
  endPane();

  beginPane("ragdoll_context_targets", "Target", ImVec2(0.0f, bottomY), ImVec2(kLeftWidth, kBottomHeight));
  chooseSelectionMode("Bodies", kRagdollSelectCapsules);
  chooseSelectionMode("Joints", kRagdollSelectJoints);
  chooseSelectionMode("Bones", kRagdollSelectBones);
  endPane();

  beginPane("ragdoll_context_undo", "Undo", ImVec2(rightX, bottomY), ImVec2(kRightWidth, kBottomHeight));
  chooseUndo();
  ImGui::TextDisabled("%zu/10 stored", m_ragdollUndoState.stack.size());
  ImGui::TextDisabled("%s", CurrentRagdollUndoLabel());
  endPane();
  ImGui::PopStyleVar();

  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImVec2 windowPos = ImGui::GetWindowPos();
  ImVec2 origin(m_ragdollContextMenuX - windowPos.x, m_ragdollContextMenuY - windowPos.y);
  origin.x = (std::max)(7.0f, (std::min)(popupSize.x - 7.0f, origin.x));
  origin.y = (std::max)(7.0f, (std::min)(popupSize.y - 7.0f, origin.y));
  const ImVec2 originScreen(windowPos.x + origin.x, windowPos.y + origin.y);
  const ImU32 axisColor = IM_COL32(255, 210, 80, 230);
  drawList->AddLine(ImVec2(originScreen.x - 7.0f, originScreen.y),
                    ImVec2(originScreen.x + 7.0f, originScreen.y),
                    axisColor,
                    1.5f);
  drawList->AddLine(ImVec2(originScreen.x, originScreen.y - 7.0f),
                    ImVec2(originScreen.x, originScreen.y + 7.0f),
                    axisColor,
                    1.5f);
  drawList->AddCircleFilled(originScreen, 2.5f, axisColor);
  drawList->AddText(ImVec2(originScreen.x + 9.0f, originScreen.y - 8.0f), axisColor, "+X");
  drawList->AddText(ImVec2(originScreen.x - 25.0f, originScreen.y - 8.0f), axisColor, "-X");
  drawList->AddText(ImVec2(originScreen.x - 8.0f, originScreen.y - 26.0f), axisColor, "+Y");
  drawList->AddText(ImVec2(originScreen.x - 8.0f, originScreen.y + 10.0f), axisColor, "-Y");
  ImGui::EndPopup();
}

bool SceneTemplate::HandleSkeletonEditInput(InputManager* input, bool imguiWantsMouse) {
  if (!m_skeletonEditMode || !input) {
    return false;
  }

  if (m_ragdollEditRenamingCapsule >= 0) {
    m_skeletonEditDragging = false;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_ragdollEditJointAxis = -1;
    m_ragdollContextMenuRightButtonHeld = false;
    return true;
  }

  if (!input->PressedMouseButton(0)) {
    const bool finishedGizmoDrag = m_ragdollEditGizmoDragging;
    const bool finishedJointDrag = m_ragdollEditJointDragging;
    m_skeletonEditDragging = false;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_ragdollEditJointAxis = -1;
    if (finishedGizmoDrag &&
        m_ragdollEditSelectionMode == kRagdollSelectCapsules &&
        m_ragdollEditSelectedCapsule >= 0 &&
        m_ragdollEditSelectedCapsule < static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size())) {
      ApplyRagdollEditPose(true);
    }
    if (finishedJointDrag &&
        m_ragdollEditSelectedJoint >= 0 &&
        m_ragdollEditSelectedJoint < static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size())) {
      ApplyRagdollEditPose(true);
    }
  }

  if (!input->PressedMouseButton(2)) {
    m_ragdollContextMenuRightButtonHeld = false;
  }

  auto cancelRagdollDrags = [&]() {
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    m_skeletonEditDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_ragdollEditJointAxis = -1;
  };

  const bool imguiContextActive = ImGui::GetCurrentContext() != nullptr;
  const bool imguiWantsKeyboard = imguiContextActive && ImGui::GetIO().WantCaptureKeyboard;
  const bool imguiTextInputActive = imguiContextActive && ImGui::GetIO().WantTextInput;
  const bool ctrlDown = input->PressedKey(T800K_LCTRL) || input->PressedKey(T800K_RCTRL);
  if (ctrlDown && input->PressedOnceKey(T800K_z) && !imguiTextInputActive) {
    UndoRagdollAuthoringEdit();
    return true;
  }
  if (!imguiWantsKeyboard) {
    if (input->PressedOnceKey(T800K_q)) {
      m_ragdollEditGizmoMode = kRagdollToolSelect;
      cancelRagdollDrags();
    }
    if (input->PressedOnceKey(T800K_r)) {
      m_ragdollEditGizmoMode = kRagdollToolEditCapsule;
      cancelRagdollDrags();
    }
    if (input->PressedOnceKey(T800K_w)) {
      m_ragdollEditGizmoMode = kRagdollToolMove;
      cancelRagdollDrags();
    }
    if (input->PressedOnceKey(T800K_e)) {
      m_ragdollEditGizmoMode = kRagdollToolRotate;
      cancelRagdollDrags();
    }
  }

  if (m_ragdollEditJointDragging && input->PressedMouseButton(0)) {
    DragRagdollEditJointGizmo(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY));
    return true;
  }
  if (m_ragdollEditGizmoDragging && input->PressedMouseButton(0)) {
    DragRagdollEditTransformGizmo(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY));
    return true;
  }
  if (m_ragdollContextMenuRightButtonHeld && input->PressedMouseButton(2)) {
    return true;
  }
  if (imguiWantsMouse) {
    return false;
  }

  const bool leftClick = input->PressedOnceMouseButton(0);
  const bool rightClick = input->PressedOnceMouseButton(2);

  if (m_ragdollBoneSelectionActive) {
    m_ragdollEditSelectionMode = kRagdollSelectBones;
    m_ragdollEditGizmoMode = kRagdollToolSelect;
    m_skeletonEditDragging = false;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_ragdollEditJointAxis = -1;

    if (leftClick) {
      m_ragdollBoneMarqueeDragging = true;
      m_ragdollBoneMarqueeStartX = static_cast<float>(input->mouseX);
      m_ragdollBoneMarqueeStartY = static_cast<float>(input->mouseY);
      m_ragdollBoneMarqueeCurrentX = m_ragdollBoneMarqueeStartX;
      m_ragdollBoneMarqueeCurrentY = m_ragdollBoneMarqueeStartY;
      return true;
    }

    if (m_ragdollBoneMarqueeDragging && input->PressedMouseButton(0)) {
      m_ragdollBoneMarqueeCurrentX = static_cast<float>(input->mouseX);
      m_ragdollBoneMarqueeCurrentY = static_cast<float>(input->mouseY);
      return true;
    }

    if (m_ragdollBoneMarqueeDragging && !input->PressedMouseButton(0)) {
      m_ragdollBoneMarqueeDragging = false;
      m_ragdollBoneMarqueeCurrentX = static_cast<float>(input->mouseX);
      m_ragdollBoneMarqueeCurrentY = static_cast<float>(input->mouseY);
      const float dxSelect = m_ragdollBoneMarqueeCurrentX - m_ragdollBoneMarqueeStartX;
      const float dySelect = m_ragdollBoneMarqueeCurrentY - m_ragdollBoneMarqueeStartY;

      m_ragdollBoneSelectionPending.clear();
      if (std::fabs(dxSelect) < 5.0f && std::fabs(dySelect) < 5.0f) {
        const int picked = PickSkeletonEditBone(m_ragdollBoneMarqueeCurrentX, m_ragdollBoneMarqueeCurrentY, 18.0f);
        if (picked >= 0 && FindRagdollCapsuleControllingBone(picked) < 0) {
          m_ragdollBoneSelectionPending.push_back(picked);
          SelectSkeletonEditBone(picked);
        }
      } else {
        PickSkeletonEditBonesInScreenRect(m_ragdollBoneMarqueeStartX,
                                          m_ragdollBoneMarqueeStartY,
                                          m_ragdollBoneMarqueeCurrentX,
                                          m_ragdollBoneMarqueeCurrentY,
                                          m_ragdollBoneSelectionPending);
        if (!m_ragdollBoneSelectionPending.empty()) {
          SelectSkeletonEditBone(m_ragdollBoneSelectionPending.front());
        }
      }
      m_ragdollEditSelectedUnassignedBone = -1;
      m_ragdollEditSelectedAffectedBone = -1;
      return true;
    }

    return true;
  }

  if (rightClick) {
    cancelRagdollDrags();
    SelectRagdollContextTargetAt(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY));
    m_ragdollContextMenuX = static_cast<float>(input->mouseX);
    m_ragdollContextMenuY = static_cast<float>(input->mouseY);
    m_ragdollContextMenuRequested = true;
    m_ragdollContextMenuRightButtonHeld = true;
    return true;
  }

  if (leftClick) {
    if (m_ragdollEditSelectionMode == kRagdollSelectCapsules) {
      if (BeginRagdollEditTransformGizmoDrag(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY))) {
        return true;
      }

      int pickedCapsule = -1;
      int pickedHandle = -1;
      if (m_ragdollEditGizmoMode == kRagdollToolEditCapsule &&
          PickRagdollEditHandle(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY), 18.0f, pickedCapsule, pickedHandle)) {
        SelectRagdollEditCapsule(pickedCapsule, true);
        m_ragdollEditSelectedHandle = pickedHandle;
        m_ragdollEditHandleDragging = true;
        return true;
      }
      if (PickRagdollEditCapsule(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY), 18.0f, pickedCapsule)) {
        SelectRagdollEditCapsule(pickedCapsule, true);
        m_ragdollEditHandleDragging = false;
        return true;
      }
    } else if (m_ragdollEditSelectionMode == kRagdollSelectJoints) {
      if (BeginRagdollEditJointGizmoDrag(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY))) {
        return true;
      }
      int pickedJoint = -1;
      if (PickRagdollEditJoint(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY), 18.0f, pickedJoint)) {
        m_ragdollEditSelectedJoint = pickedJoint;
        SelectRagdollEditCapsule(pickedJoint, true);
        m_ragdollEditSelectedHandle = -1;
        return true;
      }
    } else {
      if (BeginRagdollEditTransformGizmoDrag(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY))) {
        return true;
      }
      const int picked = PickSkeletonEditBone(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY), 18.0f);
      if (picked >= 0) {
        SelectSkeletonEditBone(picked);
        m_skeletonEditDragging = false;
        return true;
      }
    }
  }

  if (m_ragdollEditHandleDragging &&
      m_ragdollEditSelectionMode == kRagdollSelectCapsules &&
      m_ragdollEditGizmoMode == kRagdollToolEditCapsule &&
      m_ragdollEditSelectedCapsule >= 0 &&
      m_ragdollEditSelectedHandle >= 0 &&
      input->PressedMouseButton(0)) {
    const float dragScale = (std::max)(0.001f, m_orbitDist) * 0.0015f;
    XVECTOR3 worldDelta = Cam.Right * (static_cast<float>(input->xDelta) * dragScale);
    worldDelta += Cam.Up * (-static_cast<float>(input->yDelta) * dragScale);
    if (DragRagdollEditHandle(m_ragdollEditSelectedCapsule, m_ragdollEditSelectedHandle, worldDelta)) {
      return true;
    }
  }

  if (m_skeletonEditDragging && m_skeletonEditSelectedBone >= 0 && input->PressedMouseButton(0)) {
    XVECTOR3 worldPosition;
    if (GetSkeletonEditBoneWorldPosition(m_skeletonEditSelectedBone, worldPosition)) {
      const float dragScale = (std::max)(0.001f, m_orbitDist) * 0.0015f;
      worldPosition += Cam.Right * (static_cast<float>(input->xDelta) * dragScale);
      worldPosition += Cam.Up * (-static_cast<float>(input->yDelta) * dragScale);
      SetSkeletonEditBoneWorldPosition(m_skeletonEditSelectedBone, worldPosition);
      return true;
    }
  }

  return false;
}

void SceneTemplate::DrawRagdollCapsuleEditPanel(t850::DevGuiContext& gui) {
  ImGui::Separator();
  if (m_ragdollEditRenamingCapsule >= 0) {
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  }
  if (!ImGui::CollapsingHeader("Ragdoll Bodies", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollControlledBones();
  EnsureRagdollFreezeState();
  if (m_ragdollEditSavePath.empty()) {
    m_ragdollEditSavePath = BuildRagdollEditSavePath();
  }
  RenderSkinnedMesh* selectedSkinned = Meshes[0].GetSkinnedMesh();
  const xF::xSkeleton* selectedSkeleton = selectedSkinned ? selectedSkinned->GetAnimController().GetAnimSkeleton() : nullptr;

  auto validSkeletonBone = [&](int boneIndex) {
    return selectedSkeleton &&
           boneIndex >= 0 &&
           boneIndex < static_cast<int>(selectedSkeleton->Bones.size());
  };
  auto skeletonBoneName = [&](int boneIndex) -> const char* {
    return validSkeletonBone(boneIndex)
        ? selectedSkeleton->Bones[static_cast<std::size_t>(boneIndex)].Name.c_str()
        : "<unknown>";
  };
  auto hasRagdollBodyForBone = [&](int boneIndex) {
    return FindRagdollCapsuleForBone(boneIndex) >= 0;
  };
  auto firstChildBone = [&](int boneIndex) {
    if (!validSkeletonBone(boneIndex)) {
      return -1;
    }
    const auto& sons = selectedSkeleton->Bones[static_cast<std::size_t>(boneIndex)].Sons;
    for (unsigned int child : sons) {
      if (child < selectedSkeleton->Bones.size()) {
        return static_cast<int>(child);
      }
    }
    for (int candidate = 0; candidate < static_cast<int>(selectedSkeleton->Bones.size()); ++candidate) {
      const auto& candidateBone = selectedSkeleton->Bones[static_cast<std::size_t>(candidate)];
      if (candidateBone.Dad == static_cast<unsigned short>(boneIndex) && candidate != boneIndex) {
        return candidate;
      }
    }
    return -1;
  };
  auto parentBone = [&](int boneIndex) {
    if (!validSkeletonBone(boneIndex)) {
      return -1;
    }
    const unsigned short parent = selectedSkeleton->Bones[static_cast<std::size_t>(boneIndex)].Dad;
    return parent < selectedSkeleton->Bones.size() && parent != static_cast<unsigned short>(boneIndex)
        ? static_cast<int>(parent)
        : -1;
  };
  auto drawBoneStatusMarker = [&](int boneIndex) {
    const bool hasBody = hasRagdollBodyForBone(boneIndex);
    const ImVec4 color = hasBody
        ? ImVec4(0.25f, 0.95f, 0.35f, 1.0f)
        : ImVec4(1.0f, 0.25f, 0.20f, 1.0f);
    ImGui::TextColored(color, "%s", hasBody ? "(B)" : "(NB)");
  };
  auto drawRelationshipBone = [&](int boneIndex) {
    if (!validSkeletonBone(boneIndex)) {
      ImGui::TextDisabled("<none>");
      return;
    }
    ImGui::TextUnformatted(skeletonBoneName(boneIndex));
    ImGui::SameLine(0.0f, 4.0f);
    drawBoneStatusMarker(boneIndex);
  };
  auto drawClosestBoneRelationship = [&](int boneIndex) {
    ImGui::TextUnformatted("Closest relationship:");
    ImGui::SameLine();
    drawRelationshipBone(parentBone(boneIndex));
    ImGui::SameLine();
    ImGui::TextUnformatted("-");
    ImGui::SameLine();
    drawRelationshipBone(boneIndex);
    ImGui::SameLine();
    ImGui::TextUnformatted("-");
    ImGui::SameLine();
    drawRelationshipBone(firstChildBone(boneIndex));
  };

  auto commitCapsuleRename = [&](int capsuleIndex) {
    std::string newName(m_ragdollEditNameBuffer.data());
    if (newName.empty()) {
      newName = "body_" + std::to_string(capsuleIndex);
    }
    auto& renamedBone = bones[static_cast<std::size_t>(capsuleIndex)];
    if (newName != renamedBone.body.debugName) {
      renamedBone.body.debugName = std::move(newName);
      m_ragdollEditDirty = true;
    }
    m_ragdollEditRenamingCapsule = -1;
    m_ragdollEditRenameFocusPending = false;
  };

  if (m_ragdollEditRenamingCapsule >= 0) {
    const int renameCapsule = m_ragdollEditRenamingCapsule;
    if (renameCapsule >= static_cast<int>(bones.size()) || IsRagdollCapsuleFrozen(renameCapsule)) {
      m_ragdollEditRenamingCapsule = -1;
      m_ragdollEditRenameFocusPending = false;
    } else {
      ImGui::Text("Renaming body %d. Press Enter to apply.", renameCapsule);
      ImGui::PushID(renameCapsule);
      if (m_ragdollEditRenameFocusPending) {
        ImGui::SetKeyboardFocusHere();
        m_ragdollEditRenameFocusPending = false;
      }
      ImGui::SetNextItemWidth(260.0f);
      const bool submitted = ImGui::InputText("Body name",
                                              m_ragdollEditNameBuffer.data(),
                                              m_ragdollEditNameBuffer.size(),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
      if (submitted) {
        commitCapsuleRename(renameCapsule);
      } else if (!ImGui::IsItemActive()) {
        m_ragdollEditRenameFocusPending = true;
      }
      ImGui::PopID();
      return;
    }
  }

  auto cancelRagdollDrags = [&]() {
    m_skeletonEditDragging = false;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_ragdollEditJointAxis = -1;
  };

  if (ImGui::CollapsingHeader("Viewport tools", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Selection:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Bodies", m_ragdollEditSelectionMode == kRagdollSelectCapsules)) {
      RestoreSkeletonPreviewBone();
      m_ragdollEditSelectionMode = kRagdollSelectCapsules;
      cancelRagdollDrags();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Joints", m_ragdollEditSelectionMode == kRagdollSelectJoints)) {
      RestoreSkeletonPreviewBone();
      m_ragdollEditSelectionMode = kRagdollSelectJoints;
      cancelRagdollDrags();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Bones", m_ragdollEditSelectionMode == kRagdollSelectBones)) {
      m_ragdollEditSelectionMode = kRagdollSelectBones;
      cancelRagdollDrags();
    }

    ImGui::Text("Tool:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Select", m_ragdollEditGizmoMode == kRagdollToolSelect)) {
      m_ragdollEditGizmoMode = kRagdollToolSelect;
      cancelRagdollDrags();
    }
    ImGui::SameLine();
    const char* editToolLabel = m_ragdollEditSelectionMode == kRagdollSelectJoints ? "Edit Joint" : "Edit Body";
    if (ImGui::RadioButton(editToolLabel, m_ragdollEditGizmoMode == kRagdollToolEditCapsule)) {
      m_ragdollEditGizmoMode = kRagdollToolEditCapsule;
      cancelRagdollDrags();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Move", m_ragdollEditGizmoMode == kRagdollToolMove)) {
      m_ragdollEditGizmoMode = kRagdollToolMove;
      cancelRagdollDrags();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", m_ragdollEditGizmoMode == kRagdollToolRotate)) {
      m_ragdollEditGizmoMode = kRagdollToolRotate;
      cancelRagdollDrags();
    }
    ImGui::Text("Active tool: %s", RagdollToolName(m_ragdollEditGizmoMode));
  }

  if (ImGui::CollapsingHeader("Body actions", ImGuiTreeNodeFlags_DefaultOpen)) {
  if (gui.Button("Load Ragdoll Edits")) {
    if (LoadRagdollEditPose()) {
      m_ragdollEditTopologyChangedThisFrame = true;
      return;
    }
  }
  ImGui::SameLine();
  if (gui.Button(m_ragdollEditDirty ? "Save Ragdoll Edits *" : "Save Ragdoll Edits")) {
    SaveRagdollEditPose();
  }

  if (gui.Button("Reset All Bodies")) {
    if (ResetRagdollEditPose()) {
      m_ragdollEditTopologyChangedThisFrame = true;
      return;
    }
  }
  ImGui::SameLine();
  if (gui.Button("Reset Selected Body", m_ragdollEditSelectedCapsule >= 0 &&
      m_ragdollEditSelectedCapsule < static_cast<int>(bones.size()) &&
      !IsRagdollCapsuleFrozen(m_ragdollEditSelectedCapsule))) {
    if (ResetSelectedRagdollCapsule()) {
      m_ragdollEditTopologyChangedThisFrame = true;
      return;
    }
  }
  ImGui::SameLine();
  if (gui.Button("Sync", !bones.empty())) {
    SyncRagdollCapsuleSymmetry();
  }
  if (!m_ragdollLastSyncStatus.empty()) {
    ImGui::TextWrapped("%s", m_ragdollLastSyncStatus.c_str());
  }

  if (!m_ragdollEditSavePath.empty()) {
    ImGui::TextWrapped("Ragdoll save path: %s", m_ragdollEditSavePath.c_str());
  }

  const bool selectedBoneValid = m_skeletonEditSelectedBone >= 0 &&
      m_skeletonEditSelectedBone < static_cast<int>(m_skeletonEditCombined.size());
  const int selectedBoneCapsule = selectedBoneValid ? FindRagdollCapsuleControllingBone(m_skeletonEditSelectedBone) : -1;
  const int selectedBonePrimaryCapsule = selectedBoneValid ? FindRagdollCapsuleForBone(m_skeletonEditSelectedBone) : -1;
  const bool canCreateBody = selectedBoneValid && selectedBoneCapsule < 0 && selectedBonePrimaryCapsule < 0;
  const bool canDeleteCapsule = m_ragdollEditSelectedCapsule >= 0 &&
      m_ragdollEditSelectedCapsule < static_cast<int>(bones.size()) &&
      !IsRagdollCapsuleFrozen(m_ragdollEditSelectedCapsule);

  if (selectedBoneValid) {
    if (selectedBoneCapsule >= 0) {
      ImGui::Text("Selected bone %d %s is controlled by body %d.",
                  m_skeletonEditSelectedBone,
                  skeletonBoneName(m_skeletonEditSelectedBone),
                  selectedBoneCapsule);
    } else if (selectedBonePrimaryCapsule >= 0) {
      ImGui::Text("Selected bone %d %s already owns body %d but is not assigned to it.",
                  m_skeletonEditSelectedBone,
                  skeletonBoneName(m_skeletonEditSelectedBone),
                  selectedBonePrimaryCapsule);
    } else {
      ImGui::Text("Selected bone %d %s has no body assignment.",
                  m_skeletonEditSelectedBone,
                  skeletonBoneName(m_skeletonEditSelectedBone));
    }
    drawClosestBoneRelationship(m_skeletonEditSelectedBone);
  } else {
    gui.Text("Select a bone to create a ragdoll body assignment.");
  }
  if (gui.Button("Create Capsule", canCreateBody)) {
    if (CreateRagdollCapsuleForBone(m_skeletonEditSelectedBone)) {
      m_ragdollEditTopologyChangedThisFrame = true;
      return;
    }
  }
  ImGui::SameLine();
  if (gui.Button("Create Box", canCreateBody)) {
    if (CreateRagdollBoxForBone(m_skeletonEditSelectedBone)) {
      m_ragdollEditTopologyChangedThisFrame = true;
      return;
    }
  }
  ImGui::SameLine();
  if (gui.Button("Delete Selected Body", canDeleteCapsule)) {
    if (DeleteSelectedRagdollCapsule()) {
      m_ragdollEditTopologyChangedThisFrame = true;
      return;
    }
  }
  ImGui::SameLine();
  if (gui.Button("Clear All Bodies", !bones.empty())) {
    m_ragdollClearRequested = true;
    m_ragdollEditSelectedCapsule = -1;
    m_ragdollEditSelectedJoint = -1;
    m_ragdollEditSelectedParentCapsule = -1;
    m_ragdollEditSelectedJointParentCapsule = -1;
    m_ragdollEditSelectedHandle = -1;
    m_ragdollEditRenamingCapsule = -1;
    m_ragdollEditRenameFocusPending = false;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_skeletonEditDragging = false;
    m_driveRagdollFromAnimation = false;
    m_ragdollPhysicsDriven = false;
    m_showPhysics = false;
    m_ragdollEditDirty = true;
    m_ragdollEditTopologyChangedThisFrame = true;
    return;
  }
  }

  if (m_ragdollClearRequested) {
    gui.Text("Clearing ragdoll bodies...");
    return;
  }

  if (bones.empty() || bones.size() != m_ragdollAnimationBinding.bodyFromBone.size()) {
    gui.Text("No editable ragdoll bodies are attached to this model.");
    return;
  }
  EnsureRagdollParentCapsules();
  EnsureRagdollJointState();

  if (m_ragdollEditSelectedCapsule < 0 ||
      m_ragdollEditSelectedCapsule >= static_cast<int>(bones.size())) {
    SelectRagdollEditCapsule(0, true);
  }

  std::vector<std::string> capsuleOptions;
  capsuleOptions.reserve(bones.size());
  for (std::size_t i = 0; i < bones.size(); ++i) {
    const auto& bone = bones[i];
    capsuleOptions.push_back(std::to_string(i) + ": " + RagdollShapeTypeName(bone.body.shape.type) +
                             " bone " + std::to_string(bone.body.boneIndex) + " " + bone.body.debugName);
  }

  t850::SelectorDesc capsuleSelector;
  capsuleSelector.name = "ragdoll_edit_capsule";
  capsuleSelector.label = "Body";
  int selectedCapsule = m_ragdollEditSelectedCapsule;
  if (gui.Combo(capsuleSelector, selectedCapsule, &capsuleOptions)) {
    SelectRagdollEditCapsule(selectedCapsule, true);
  }

  if (m_ragdollEditSelectedCapsule < 0 ||
      m_ragdollEditSelectedCapsule >= static_cast<int>(bones.size())) {
    return;
  }

  const int capsuleIndex = m_ragdollEditSelectedCapsule;
  auto& bone = bones[static_cast<std::size_t>(capsuleIndex)];
  auto& local = m_ragdollAnimationBinding.bodyFromBone[static_cast<std::size_t>(capsuleIndex)];
  auto& shape = bone.body.shape;
  if (!IsEditableRagdollShape(shape)) {
    gui.Text("Selected ragdoll body shape is not editable.");
    return;
  }
  const bool capsuleFrozen = IsRagdollCapsuleFrozen(capsuleIndex);

  ImGui::PushID(capsuleIndex);
  if (ImGui::CollapsingHeader("Selected body", ImGuiTreeNodeFlags_DefaultOpen)) {
  if (capsuleFrozen && m_ragdollEditRenamingCapsule == capsuleIndex) {
    m_ragdollEditRenamingCapsule = -1;
    m_ragdollEditRenameFocusPending = false;
  }
  if (m_ragdollEditRenamingCapsule == capsuleIndex) {
    if (m_ragdollEditRenameFocusPending) {
      ImGui::SetKeyboardFocusHere();
      m_ragdollEditRenameFocusPending = false;
    }
    ImGui::SetNextItemWidth(260.0f);
    const bool submitted = ImGui::InputText("Body name", m_ragdollEditNameBuffer.data(),
                                            m_ragdollEditNameBuffer.size(),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    if (submitted) {
      commitCapsuleRename(capsuleIndex);
    } else if (!ImGui::IsItemActive()) {
      m_ragdollEditRenameFocusPending = true;
    }
  } else {
    ImGui::Text("Body name: %s", bone.body.debugName.c_str());
    if (!capsuleFrozen && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
      std::fill(m_ragdollEditNameBuffer.begin(), m_ragdollEditNameBuffer.end(), '\0');
      std::snprintf(m_ragdollEditNameBuffer.data(), m_ragdollEditNameBuffer.size(), "%s", bone.body.debugName.c_str());
      m_ragdollEditRenamingCapsule = capsuleIndex;
      m_ragdollEditRenameFocusPending = true;
    }
  }
  ImGui::Text("Bone: %d %s", bone.body.boneIndex, skeletonBoneName(bone.body.boneIndex));
  ImGui::Text("Shape: %s", RagdollShapeTypeName(shape.type));
  ImGui::SameLine();
  if (shape.type == t850::PhysicsShapeType::Capsule) {
    if (gui.Button("Morph to Box", !capsuleFrozen)) {
      MorphRagdollBodyToBox(capsuleIndex);
    }
  } else if (shape.type == t850::PhysicsShapeType::Box) {
    if (gui.Button("Morph to Capsule", !capsuleFrozen)) {
      MorphRagdollBodyToCapsule(capsuleIndex);
    }
  }
  ImGui::Text("Viewport handle: %s", RagdollCapsuleHandleName(m_ragdollEditSelectedHandle));
  if (gui.Button(capsuleFrozen ? "Unfreeze Body" : "Freeze Body")) {
    SetRagdollCapsuleFrozen(capsuleIndex, !capsuleFrozen);
  }
  ImGui::SameLine();
  ImGui::TextUnformatted(capsuleFrozen ? "Frozen" : "Editable");
  ImGui::Text("Flip body local axis:");
  ImGui::SameLine();
  if (gui.Button("Flip X", !capsuleFrozen)) {
    FlipRagdollEditCapsuleLocalAxis(capsuleIndex, 0);
  }
  ImGui::SameLine();
  if (gui.Button("Flip Y", !capsuleFrozen)) {
    FlipRagdollEditCapsuleLocalAxis(capsuleIndex, 1);
  }
  ImGui::SameLine();
  if (gui.Button("Flip Z", !capsuleFrozen)) {
    FlipRagdollEditCapsuleLocalAxis(capsuleIndex, 2);
  }
  ImGui::Text("Align body axis to world:");
  ImGui::SameLine();
  if (gui.Button("Align X", !capsuleFrozen)) {
    AlignRagdollEditCapsuleToWorldAxis(capsuleIndex, 0);
  }
  ImGui::SameLine();
  if (gui.Button("Align Y", !capsuleFrozen)) {
    AlignRagdollEditCapsuleToWorldAxis(capsuleIndex, 1);
  }
  ImGui::SameLine();
  if (gui.Button("Align Z", !capsuleFrozen)) {
    AlignRagdollEditCapsuleToWorldAxis(capsuleIndex, 2);
  }
  }
  DrawRagdollEditTransformGizmo();
  const int parentCapsule =
      capsuleIndex < static_cast<int>(m_ragdollParentCapsules.size())
          ? m_ragdollParentCapsules[static_cast<std::size_t>(capsuleIndex)]
          : -1;
  const int jointParentCapsule = GetRagdollEffectiveJointParentCapsule(capsuleIndex);
  if (ImGui::CollapsingHeader("Body relationships", ImGuiTreeNodeFlags_DefaultOpen)) {
  if (parentCapsule >= 0 && parentCapsule < static_cast<int>(bones.size())) {
    const auto& parentBone = bones[static_cast<std::size_t>(parentCapsule)];
    ImGui::Text("Parent body: %d %s", parentCapsule, parentBone.body.debugName.c_str());
    ImGui::SameLine();
    if (gui.Button("Clear Parent", !capsuleFrozen)) {
      ClearRagdollCapsuleParent(capsuleIndex);
    }
  } else {
    ImGui::Text("Parent body: None");
  }
  ImGui::Text("Child bodies:");
  bool hasChildCapsules = false;
  for (int childCapsule = 0; childCapsule < static_cast<int>(m_ragdollParentCapsules.size()); ++childCapsule) {
    if (m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)] != capsuleIndex ||
        childCapsule >= static_cast<int>(bones.size())) {
      continue;
    }
    hasChildCapsules = true;
    ImGui::Text("  %d %s", childCapsule, bones[static_cast<std::size_t>(childCapsule)].body.debugName.c_str());
  }
  if (!hasChildCapsules) {
    ImGui::Text("  None");
  }

  auto isValidRelationshipCandidate = [&](int candidate) {
    return candidate >= 0 && candidate < static_cast<int>(bones.size()) && candidate != capsuleIndex;
  };
  auto capsuleRelationshipLabel = [&](int candidate, int currentParent, const char* currentSuffix) {
    const auto& candidateBone = bones[static_cast<std::size_t>(candidate)];
    std::string label = std::to_string(candidate) + ": " +
        RagdollShapeTypeName(candidateBone.body.shape.type) + " " + candidateBone.body.debugName;
    if (candidate == currentParent) {
      label += currentSuffix;
    }
    return label;
  };
  if (!isValidRelationshipCandidate(m_ragdollEditSelectedParentCapsule)) {
    m_ragdollEditSelectedParentCapsule = isValidRelationshipCandidate(parentCapsule) ? parentCapsule : -1;
  }
  if (!isValidRelationshipCandidate(m_ragdollEditSelectedJointParentCapsule)) {
    m_ragdollEditSelectedJointParentCapsule =
        isValidRelationshipCandidate(jointParentCapsule) ? jointParentCapsule :
        (isValidRelationshipCandidate(parentCapsule) ? parentCapsule : -1);
  }

  const float capsuleRelationshipListHeight = (std::max)(120.0f, ImGui::GetTextLineHeightWithSpacing() * 7.0f);
  ImGui::Columns(3, "ragdoll_capsule_relationship_columns", false);

  ImGui::Text("Logical parent");
  ImGui::BeginChild("logical_parent_bodies", ImVec2(0.0f, capsuleRelationshipListHeight), true);
  for (int candidate = 0; candidate < static_cast<int>(bones.size()); ++candidate) {
    if (candidate == capsuleIndex) {
      continue;
    }
    const std::string label = capsuleRelationshipLabel(candidate, parentCapsule, "  (current)");
    if (ImGui::Selectable(label.c_str(), m_ragdollEditSelectedParentCapsule == candidate)) {
      m_ragdollEditSelectedParentCapsule = candidate;
    }
  }
  ImGui::EndChild();

  ImGui::NextColumn();
  ImGui::Spacing();
  const bool canSetParent =
      !capsuleFrozen &&
      isValidRelationshipCandidate(m_ragdollEditSelectedParentCapsule) &&
      m_ragdollEditSelectedParentCapsule != parentCapsule;
  if (gui.Button("Set Parent", canSetParent)) {
    if (SetRagdollCapsuleParent(capsuleIndex, m_ragdollEditSelectedParentCapsule)) {
      m_ragdollEditSelectedJointParentCapsule = m_ragdollEditSelectedParentCapsule;
    }
  }
  if (gui.Button("Clear Parent", !capsuleFrozen && isValidRelationshipCandidate(parentCapsule))) {
    if (ClearRagdollCapsuleParent(capsuleIndex)) {
      m_ragdollEditSelectedParentCapsule = -1;
      if (m_ragdollEditSelectedJointParentCapsule == parentCapsule) {
        m_ragdollEditSelectedJointParentCapsule = -1;
      }
    }
  }
  ImGui::Spacing();
  const bool jointFrozen = IsRagdollJointFrozen(capsuleIndex);
  const bool canCreateJoint =
      !capsuleFrozen &&
      !jointFrozen &&
      isValidRelationshipCandidate(m_ragdollEditSelectedJointParentCapsule);
  if (gui.Button("Create Joint", canCreateJoint)) {
    if (SetRagdollCapsuleJoint(capsuleIndex, m_ragdollEditSelectedJointParentCapsule)) {
      m_ragdollEditSelectedParentCapsule = m_ragdollEditSelectedJointParentCapsule;
    }
  }
  if (gui.Button("Create Contact Joint", canCreateJoint)) {
    if (SetRagdollCapsuleJointAtContact(capsuleIndex, m_ragdollEditSelectedJointParentCapsule)) {
      m_ragdollEditSelectedParentCapsule = m_ragdollEditSelectedJointParentCapsule;
    }
  }
  if (gui.Button("Delete Joint", !jointFrozen && isValidRelationshipCandidate(jointParentCapsule))) {
    ClearRagdollCapsuleJoint(capsuleIndex);
  }

  ImGui::NextColumn();
  ImGui::Text("Joint parent");
  ImGui::BeginChild("joint_parent_bodies", ImVec2(0.0f, capsuleRelationshipListHeight), true);
  for (int candidate = 0; candidate < static_cast<int>(bones.size()); ++candidate) {
    if (candidate == capsuleIndex) {
      continue;
    }
    const std::string label = capsuleRelationshipLabel(candidate, jointParentCapsule, "  (current joint)");
    if (ImGui::Selectable(label.c_str(), m_ragdollEditSelectedJointParentCapsule == candidate)) {
      m_ragdollEditSelectedJointParentCapsule = candidate;
    }
  }
  ImGui::EndChild();
  ImGui::Columns(1);
  }

  if (capsuleIndex < static_cast<int>(m_ragdollAnimationBinding.controlledBoneIndices.size())) {
    auto& controlledBones = m_ragdollAnimationBinding.controlledBoneIndices[static_cast<std::size_t>(capsuleIndex)];
    RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
    const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;
    const int boneCount = skeleton
        ? (std::min)(static_cast<int>(skeleton->Bones.size()), static_cast<int>(m_skeletonEditCombined.size()))
        : 0;
    auto containsBone = [](const std::vector<int>& boneList, int boneIndex) {
      return std::find(boneList.begin(), boneList.end(), boneIndex) != boneList.end();
    };
    auto boneLabel = [&](int boneIndex) {
      std::string label = std::to_string(boneIndex) + ": ";
      label += BoneNameOrEmpty(skeleton, boneIndex);
      const int ownerCapsule = FindRagdollCapsuleForBone(boneIndex);
      if (ownerCapsule >= 0) {
        label += "  (body ";
        label += std::to_string(ownerCapsule);
        label += ")";
      }
      return label;
    };

    std::vector<int> unassignedBones;
    if (boneCount > 0) {
      unassignedBones.reserve(static_cast<std::size_t>(boneCount));
      for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        if (FindRagdollCapsuleControllingBone(boneIndex) < 0) {
          unassignedBones.push_back(boneIndex);
        }
      }
    }

    if (!containsBone(unassignedBones, m_ragdollEditSelectedUnassignedBone)) {
      m_ragdollEditSelectedUnassignedBone = -1;
    }
    if (!containsBone(controlledBones, m_ragdollEditSelectedAffectedBone)) {
      m_ragdollEditSelectedAffectedBone = -1;
    }
    m_ragdollBoneSelectionPending.erase(
        std::remove_if(m_ragdollBoneSelectionPending.begin(),
                       m_ragdollBoneSelectionPending.end(),
                       [&](int boneIndex) { return !containsBone(unassignedBones, boneIndex); }),
        m_ragdollBoneSelectionPending.end());
    if (capsuleFrozen && m_ragdollBoneSelectionActive) {
      if (m_ragdollBoneSelectionPreviousSelectionMode != kRagdollSelectBones) {
        RestoreSkeletonPreviewBone();
      }
      m_ragdollEditSelectionMode = m_ragdollBoneSelectionPreviousSelectionMode;
      m_ragdollEditGizmoMode = m_ragdollBoneSelectionPreviousGizmoMode;
      cancelRagdollDrags();
      m_ragdollBoneSelectionActive = false;
      m_ragdollBoneMarqueeDragging = false;
      m_ragdollBoneSelectionPending.clear();
    }

    if (ImGui::CollapsingHeader("Affected bone assignment", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (!skeleton || boneCount <= 0) {
      ImGui::Text("Skeleton bone data is unavailable.");
    } else {
      const float listHeight = (std::max)(140.0f, ImGui::GetTextLineHeightWithSpacing() * 9.0f);
      ImGui::Columns(3, "ragdoll_bone_assignment_columns", false);

      ImGui::Text("Unassigned bones (%zu)", unassignedBones.size());
      ImGui::BeginChild("unassigned_bones", ImVec2(0.0f, listHeight), true);
      for (int unassignedBone : unassignedBones) {
        const std::string label = boneLabel(unassignedBone);
        if (ImGui::Selectable(label.c_str(), m_ragdollEditSelectedUnassignedBone == unassignedBone)) {
          m_ragdollEditSelectedUnassignedBone = unassignedBone;
          m_ragdollEditSelectedAffectedBone = -1;
        }
      }
      ImGui::EndChild();

      ImGui::NextColumn();
      ImGui::Spacing();
      const bool canAddBone = !capsuleFrozen && !m_ragdollBoneSelectionActive && m_ragdollEditSelectedUnassignedBone >= 0;
      if (gui.Button("Add ->", canAddBone)) {
        const int boneToAdd = m_ragdollEditSelectedUnassignedBone;
        if (AddControlledBoneToSelectedCapsule(boneToAdd)) {
          m_ragdollEditSelectedAffectedBone = boneToAdd;
          m_ragdollEditSelectedUnassignedBone = -1;
        }
      }
      const bool canRemoveBone = !capsuleFrozen && m_ragdollEditSelectedAffectedBone >= 0;
      if (gui.Button("<- Remove", canRemoveBone)) {
        const int boneToRemove = m_ragdollEditSelectedAffectedBone;
        if (RemoveControlledBoneFromSelectedCapsule(boneToRemove)) {
          m_ragdollEditSelectedUnassignedBone = boneToRemove;
          m_ragdollEditSelectedAffectedBone = -1;
        }
      }
      ImGui::Spacing();
      if (!m_ragdollBoneSelectionActive) {
        if (gui.Button("Select bones", !capsuleFrozen)) {
          m_ragdollBoneSelectionPreviousSelectionMode = m_ragdollEditSelectionMode;
          m_ragdollBoneSelectionPreviousGizmoMode = m_ragdollEditGizmoMode;
          m_ragdollBoneSelectionActive = true;
          m_ragdollBoneMarqueeDragging = false;
          m_ragdollBoneSelectionPending.clear();
          m_ragdollEditSelectedUnassignedBone = -1;
          m_ragdollEditSelectedAffectedBone = -1;
          m_ragdollEditSelectionMode = kRagdollSelectBones;
          m_ragdollEditGizmoMode = kRagdollToolSelect;
          m_showSkeleton = true;
          cancelRagdollDrags();
        }
      } else {
        ImGui::Text("Viewport selected: %zu", m_ragdollBoneSelectionPending.size());
        const bool canAddSelectedBones = !capsuleFrozen && !m_ragdollBoneSelectionPending.empty();
        if (gui.Button("Add selected bones", canAddSelectedBones)) {
          int lastAddedBone = -1;
          const std::vector<int> bonesToAdd = m_ragdollBoneSelectionPending;
          for (int boneToAdd : bonesToAdd) {
            if (AddControlledBoneToSelectedCapsule(boneToAdd)) {
              lastAddedBone = boneToAdd;
            }
          }
          if (m_ragdollBoneSelectionPreviousSelectionMode != kRagdollSelectBones) {
            RestoreSkeletonPreviewBone();
          }
          m_ragdollEditSelectionMode = m_ragdollBoneSelectionPreviousSelectionMode;
          m_ragdollEditGizmoMode = m_ragdollBoneSelectionPreviousGizmoMode;
          cancelRagdollDrags();
          m_ragdollBoneSelectionActive = false;
          m_ragdollBoneMarqueeDragging = false;
          m_ragdollBoneSelectionPending.clear();
          if (lastAddedBone >= 0) {
            m_ragdollEditSelectedAffectedBone = lastAddedBone;
          }
        }
        if (gui.Button("Cancel")) {
          if (m_ragdollBoneSelectionPreviousSelectionMode != kRagdollSelectBones) {
            RestoreSkeletonPreviewBone();
          }
          m_ragdollEditSelectionMode = m_ragdollBoneSelectionPreviousSelectionMode;
          m_ragdollEditGizmoMode = m_ragdollBoneSelectionPreviousGizmoMode;
          cancelRagdollDrags();
          m_ragdollBoneSelectionActive = false;
          m_ragdollBoneMarqueeDragging = false;
          m_ragdollBoneSelectionPending.clear();
        }
        ImGui::TextWrapped("Drag in the viewport to marquee-select unassigned bones. Click empty space to clear the magenta selection.");
      }

      ImGui::NextColumn();
      ImGui::Text("Affected bones (%zu)", controlledBones.size());
      ImGui::BeginChild("affected_bones", ImVec2(0.0f, listHeight), true);
      for (int affectedBone : controlledBones) {
        const std::string label = boneLabel(affectedBone);
        if (ImGui::Selectable(label.c_str(), m_ragdollEditSelectedAffectedBone == affectedBone)) {
          m_ragdollEditSelectedAffectedBone = affectedBone;
          m_ragdollEditSelectedUnassignedBone = -1;
        }
      }
      ImGui::EndChild();
      ImGui::Columns(1);
    }
    }
  }

  const bool selectedJointActive =
      m_ragdollEditSelectionMode == kRagdollSelectJoints &&
      m_ragdollEditSelectedJoint == capsuleIndex &&
      GetRagdollEffectiveJointParentCapsule(capsuleIndex) >= 0;
  if (selectedJointActive) {
    if (ImGui::CollapsingHeader("Joint transform", ImGuiTreeNodeFlags_DefaultOpen)) {
      const int jointParent = GetRagdollEffectiveJointParentCapsule(capsuleIndex);
      const bool jointFrozen = IsRagdollJointFrozen(capsuleIndex);
      XVECTOR3 joint;
      XVECTOR3 parentCenter;
      XVECTOR3 childCenter;
      XVECTOR3 parentTwist;
      XVECTOR3 childTwist;
      XVECTOR3 childPlane;
      float jointSize = 0.0f;
      if (GetRagdollJointVisualFrame(capsuleIndex, joint, parentCenter, childCenter, parentTwist, childTwist, childPlane, jointSize)) {
        ImGui::Text("Editing joint: child body %d <- parent body %d", capsuleIndex, jointParent);
        ImGui::TextWrapped("Joint mode: Edit Joint shows the anchor move gizmo. Move the anchor here instead of moving the child body.");
        float jointPositionValues[3] = {joint.x, joint.y, joint.z};
        const float jointMoveStep = (std::max)(0.001f, m_modelRadius * 0.0005f);
        if (jointFrozen) {
          ImGui::BeginDisabled();
        }
        if (ImGui::DragFloat3("Joint anchor world", jointPositionValues, jointMoveStep, 0.0f, 0.0f, "%.4f")) {
          SetRagdollEditJointWorldPosition(
              capsuleIndex,
              XVECTOR3(jointPositionValues[0], jointPositionValues[1], jointPositionValues[2], 1.0f));
          m_ragdollEditRebuildRequested = true;
        }
        if (jointFrozen) {
          ImGui::EndDisabled();
        }
      } else {
        ImGui::TextDisabled("Selected body has no editable joint anchor.");
      }
    }
  } else if (ImGui::CollapsingHeader("Shape transform", ImGuiTreeNodeFlags_DefaultOpen)) {
  XMATRIX44 bodyWorld;
  const bool hasBodyWorld = GetCurrentRagdollEditCapsuleWorld(capsuleIndex, bodyWorld);
  if (!hasBodyWorld && m_ragdollEditTransformSpace == kRagdollTransformSpaceGlobal) {
    m_ragdollEditTransformSpace = kRagdollTransformSpaceLocal;
  }
  std::array<float, 3> localTranslation = MatrixTranslation(local);
  std::array<float, 3> localRotation = MatrixEulerDegreesXYZ(local);
  std::array<float, 3> worldTranslation = hasBodyWorld ? MatrixTranslation(bodyWorld) : localTranslation;
  std::array<float, 3> worldRotation = hasBodyWorld ? MatrixEulerDegreesXYZ(bodyWorld) : localRotation;
  float localTranslationValues[3] = {localTranslation[0], localTranslation[1], localTranslation[2]};
  float localRotationValues[3] = {localRotation[0], localRotation[1], localRotation[2]};
  float worldTranslationValues[3] = {worldTranslation[0], worldTranslation[1], worldTranslation[2]};
  float worldRotationValues[3] = {worldRotation[0], worldRotation[1], worldRotation[2]};
  float radius = shape.radius;
  float totalLength = (shape.halfHeight + shape.radius) * 2.0f;
  XVECTOR3 boxHalfExtents = ClampRagdollBoxHalfExtents(shape.halfExtents);
  float boxHalfExtentValues[3] = {boxHalfExtents.x, boxHalfExtents.y, boxHalfExtents.z};
  float swingLimitDeg = Rad2Deg(bone.swingLimitRadians);
  float twistLimitDeg = Rad2Deg(bone.twistLimitRadians);

  bool transformChanged = false;
  bool rebuildRagdoll = false;
  const float moveStep = (std::max)(0.001f, m_modelRadius * 0.0005f);
  if (capsuleFrozen) {
    ImGui::BeginDisabled();
  }
  ImGui::Text("Transform space:");
  ImGui::SameLine();
  if (ImGui::RadioButton("Local", m_ragdollEditTransformSpace == kRagdollTransformSpaceLocal)) {
    m_ragdollEditTransformSpace = kRagdollTransformSpaceLocal;
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Global transform", m_ragdollEditTransformSpace == kRagdollTransformSpaceGlobal)) {
    if (hasBodyWorld) {
      m_ragdollEditTransformSpace = kRagdollTransformSpaceGlobal;
    }
  }
  if (!hasBodyWorld && m_ragdollEditTransformSpace == kRagdollTransformSpaceLocal) {
    ImGui::TextDisabled("Global transform unavailable: body world transform could not be evaluated.");
  }

  if (m_ragdollEditTransformSpace == kRagdollTransformSpaceGlobal && hasBodyWorld) {
    if (ImGui::DragFloat3("Global translate", worldTranslationValues, moveStep, 0.0f, 0.0f, "%.4f")) {
      transformChanged = true;
    }
    if (ImGui::DragFloat3("Global rotate XYZ", worldRotationValues, 0.25f, -180.0f, 180.0f, "%.2f deg")) {
      transformChanged = true;
    }
  } else {
    if (ImGui::DragFloat3("Local translate", localTranslationValues, moveStep, 0.0f, 0.0f, "%.4f")) {
      transformChanged = true;
    }
    if (ImGui::DragFloat3("Local rotate XYZ", localRotationValues, 0.25f, -180.0f, 180.0f, "%.2f deg")) {
      transformChanged = true;
    }
  }
  if (shape.type == t850::PhysicsShapeType::Capsule) {
    if (ImGui::DragFloat("Capsule radius", &radius, moveStep, kRagdollMinShapeExtent, (std::max)(kRagdollMinShapeExtent, m_modelRadius), "%.4f")) {
      rebuildRagdoll = true;
    }
    if (ImGui::DragFloat("Capsule total length", &totalLength, moveStep, kRagdollMinShapeExtent * 3.0f, (std::max)(0.003f, m_modelRadius * 4.0f), "%.4f")) {
      rebuildRagdoll = true;
    }
  } else if (shape.type == t850::PhysicsShapeType::Box) {
    if (ImGui::DragFloat3("Box half extents", boxHalfExtentValues, moveStep, kRagdollMinShapeExtent, (std::max)(kRagdollMinShapeExtent, m_modelRadius * 2.0f), "%.4f")) {
      rebuildRagdoll = true;
    }
  }
  if (ImGui::DragFloat("Swing limit", &swingLimitDeg, 0.5f, 0.0f, 180.0f, "%.1f deg")) {
    rebuildRagdoll = true;
  }
  if (ImGui::DragFloat("Twist limit", &twistLimitDeg, 0.5f, 0.0f, 180.0f, "%.1f deg")) {
    rebuildRagdoll = true;
  }
  if (capsuleFrozen) {
    ImGui::EndDisabled();
  }

  if (!capsuleFrozen && (transformChanged || rebuildRagdoll)) {
    if (shape.type == t850::PhysicsShapeType::Capsule) {
      radius = (std::max)(kRagdollMinShapeExtent, radius);
      totalLength = (std::max)(radius * 2.0f + kRagdollMinShapeExtent * 2.0f, totalLength);
      shape.radius = radius;
      shape.halfHeight = (std::max)(kRagdollMinShapeExtent, totalLength * 0.5f - radius);
      shape.halfExtents = EquivalentBoxHalfExtentsFromCapsule(shape);
    } else if (shape.type == t850::PhysicsShapeType::Box) {
      shape.halfExtents = ClampRagdollBoxHalfExtents(
          XVECTOR3(boxHalfExtentValues[0], boxHalfExtentValues[1], boxHalfExtentValues[2], 0.0f));
    }
    bone.swingLimitRadians = Deg2Rad((std::max)(0.0f, (std::min)(180.0f, swingLimitDeg)));
    bone.twistLimitRadians = Deg2Rad((std::max)(0.0f, (std::min)(180.0f, twistLimitDeg)));

    bool applied = false;
    const bool applyGlobalTransform =
        transformChanged &&
        m_ragdollEditTransformSpace == kRagdollTransformSpaceGlobal &&
        hasBodyWorld;
    if (applyGlobalTransform) {
      worldTranslation = {worldTranslationValues[0], worldTranslationValues[1], worldTranslationValues[2]};
      worldRotation = {worldRotationValues[0], worldRotationValues[1], worldRotationValues[2]};
      const XMATRIX44 editedWorld = MatrixFromTranslationEulerDegreesXYZ(worldTranslation, worldRotation);
      applied = SetRagdollEditCapsuleWorldTransform(capsuleIndex, editedWorld, rebuildRagdoll || transformChanged);
    } else {
      if (transformChanged) {
        localTranslation = {localTranslationValues[0], localTranslationValues[1], localTranslationValues[2]};
        localRotation = {localRotationValues[0], localRotationValues[1], localRotationValues[2]};
        local = MatrixFromTranslationEulerDegreesXYZ(localTranslation, localRotation);
      }
      UpdateRagdollReferenceBodyFromLocal(capsuleIndex);
      applied = ApplyRagdollEditPose(rebuildRagdoll || transformChanged);
      if (applied) {
        m_ragdollEditDirty = true;
      }
    }
    if (!applied) {
      T8_LOG_ERROR("[RagdollEdit] Failed to apply %s transform for body %d",
                   applyGlobalTransform ? "global helper" : "local",
                   capsuleIndex);
    }
  }

  }
  ImGui::PopID();
}

void SceneTemplate::DrawRagdollJointEditPanel(t850::DevGuiContext& gui) {
  ImGui::Separator();
  if (!ImGui::CollapsingHeader("Ragdoll Joints", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (bones.empty()) {
    gui.Text("No ragdoll bodies are available.");
    return;
  }

  EnsureRagdollJointState();
  EnsureRagdollFreezeState();
  std::vector<int> jointChildren;
  std::vector<std::string> jointOptions;
  jointChildren.reserve(bones.size());
  jointOptions.reserve(bones.size());
  for (int childCapsule = 0; childCapsule < static_cast<int>(bones.size()); ++childCapsule) {
    const int parentCapsule = GetRagdollEffectiveJointParentCapsule(childCapsule);
    if (parentCapsule < 0 ||
        parentCapsule >= static_cast<int>(bones.size()) ||
        parentCapsule == childCapsule) {
      continue;
    }

    jointChildren.push_back(childCapsule);
    jointOptions.push_back(
        std::to_string(childCapsule) + " " + RagdollShapeTypeName(bones[static_cast<std::size_t>(childCapsule)].body.shape.type) +
        " " + bones[static_cast<std::size_t>(childCapsule)].body.debugName +
        " <- " + std::to_string(parentCapsule) + " " + RagdollShapeTypeName(bones[static_cast<std::size_t>(parentCapsule)].body.shape.type) +
        " " + bones[static_cast<std::size_t>(parentCapsule)].body.debugName);
  }

  if (jointChildren.empty()) {
    ImGui::TextWrapped("No joints are assigned yet. Select a body and use Body relationships to create one.");
    m_ragdollEditSelectedJoint = -1;
    return;
  }

  auto findJointOption = [&](int childCapsule) {
    for (int i = 0; i < static_cast<int>(jointChildren.size()); ++i) {
      if (jointChildren[static_cast<std::size_t>(i)] == childCapsule) {
        return i;
      }
    }
    return -1;
  };

  const int selectedCapsuleJointOption = findJointOption(m_ragdollEditSelectedCapsule);
  int optionIndex = selectedCapsuleJointOption >= 0
      ? selectedCapsuleJointOption
      : findJointOption(m_ragdollEditSelectedJoint);

  t850::SelectorDesc jointSelector;
  jointSelector.name = "ragdoll_edit_joint";
  jointSelector.label = "Joint";
  int selectedOption = optionIndex >= 0 ? optionIndex : 0;
  if (gui.Combo(jointSelector, selectedOption, &jointOptions) &&
      selectedOption >= 0 &&
      selectedOption < static_cast<int>(jointChildren.size())) {
    m_ragdollEditSelectedJoint = jointChildren[static_cast<std::size_t>(selectedOption)];
    SelectRagdollEditCapsule(m_ragdollEditSelectedJoint, true);
    optionIndex = selectedOption;
  }
  if (optionIndex < 0) {
    m_ragdollEditSelectedJoint = -1;
    DrawRagdollJointGizmos(true);
    ImGui::TextWrapped("Selected body has no joint. Choose an existing joint from the list to edit it.");
    return;
  }
  m_ragdollEditSelectedJoint = jointChildren[static_cast<std::size_t>(optionIndex)];

  DrawRagdollJointGizmos(true);

  const int childCapsule = m_ragdollEditSelectedJoint;
  if (childCapsule < 0 ||
      childCapsule >= static_cast<int>(bones.size()) ||
      childCapsule >= static_cast<int>(m_ragdollJointParentCapsules.size())) {
    return;
  }
  const int parentCapsule = GetRagdollEffectiveJointParentCapsule(childCapsule);
  if (parentCapsule < 0 || parentCapsule >= static_cast<int>(bones.size())) {
    return;
  }

  auto& childBone = bones[static_cast<std::size_t>(childCapsule)];
  const auto& parentBone = bones[static_cast<std::size_t>(parentCapsule)];
  const bool jointFrozen = IsRagdollJointFrozen(childCapsule);
  ImGui::Text("Parent body: %d %s %s", parentCapsule, RagdollShapeTypeName(parentBone.body.shape.type), parentBone.body.debugName.c_str());
  ImGui::Text("Child body: %d %s %s", childCapsule, RagdollShapeTypeName(childBone.body.shape.type), childBone.body.debugName.c_str());
  ImGui::Text("Joint type: %s", RagdollJointTypeName(childBone.jointType));
  ImGui::Text("Joint gizmo: %s",
              m_ragdollEditGizmoMode == kRagdollToolEditCapsule ? "Edit/move anchor" :
              m_ragdollEditGizmoMode == kRagdollToolMove ? "Move anchor" :
              m_ragdollEditGizmoMode == kRagdollToolRotate ? "Rotate child frame" :
              "Select only");
  ImGui::Text("Constraint axes are independent from body orientation; +Y is twist and +X is plane.");
  if (gui.Button(jointFrozen ? "Unfreeze Joint" : "Freeze Joint")) {
    SetRagdollJointFrozen(childCapsule, !jointFrozen);
  }
  ImGui::SameLine();
  ImGui::TextUnformatted(jointFrozen ? "Frozen" : "Editable");
  ImGui::Text("Flip joint local axis:");
  ImGui::SameLine();
  if (gui.Button("Flip X", !jointFrozen)) {
    FlipRagdollEditJointLocalAxis(childCapsule, 0);
  }
  ImGui::SameLine();
  if (gui.Button("Flip Y", !jointFrozen)) {
    FlipRagdollEditJointLocalAxis(childCapsule, 1);
  }
  ImGui::SameLine();
  if (gui.Button("Flip Z", !jointFrozen)) {
    FlipRagdollEditJointLocalAxis(childCapsule, 2);
  }

  std::vector<std::string> jointTypeOptions = {"Swing/Twist", "Fixed"};
  int jointTypeOption = RagdollJointTypeToInt(childBone.jointType);
  t850::SelectorDesc jointTypeSelector;
  jointTypeSelector.name = "ragdoll_joint_type";
  jointTypeSelector.label = "Type";
  if (jointFrozen) {
    ImGui::BeginDisabled();
  }
  if (gui.Combo(jointTypeSelector, jointTypeOption, &jointTypeOptions) && !jointFrozen) {
    childBone.jointType = RagdollJointTypeFromInt(jointTypeOption);
    m_ragdollEditDirty = true;
    m_ragdollEditRebuildRequested = true;
  }

  XVECTOR3 joint;
  XVECTOR3 parentCenter;
  XVECTOR3 childCenter;
  XVECTOR3 parentTwist;
  XVECTOR3 childTwist;
  XVECTOR3 childPlane;
  float jointSize = 0.0f;
  if (GetRagdollJointVisualFrame(childCapsule, joint, parentCenter, childCenter, parentTwist, childTwist, childPlane, jointSize)) {
    ImGui::Text("Joint anchor: %.3f, %.3f, %.3f", joint.x, joint.y, joint.z);
    float anchorValues[3] = {joint.x, joint.y, joint.z};
    if (ImGui::DragFloat3("Joint anchor world", anchorValues, (std::max)(0.001f, m_modelRadius * 0.001f), 0.0f, 0.0f, "%.3f") &&
        !jointFrozen) {
      SetRagdollEditJointWorldPosition(childCapsule, XVECTOR3(anchorValues[0], anchorValues[1], anchorValues[2], 1.0f));
      m_ragdollEditRebuildRequested = true;
    }
  }

  float swingApertureDeg = Rad2Deg(childBone.swingLimitRadians);
  float twistAngleDeg = Rad2Deg(childBone.twistLimitRadians);
  bool changed = false;
  if (childBone.jointType == t850::PhysicsRagdollJointType::SwingTwist) {
    if (ImGui::DragFloat("Swing aperture cone", &swingApertureDeg, 0.5f, 0.0f, 180.0f, "%.1f deg") && !jointFrozen) {
      changed = true;
    }
    if (ImGui::DragFloat("Twist angle +/-", &twistAngleDeg, 0.5f, 0.0f, 180.0f, "%.1f deg") && !jointFrozen) {
      changed = true;
    }
  } else {
    ImGui::TextWrapped("Fixed joints weld translation and rotation; switch back to Swing/Twist to edit cone and twist limits.");
  }
  if (jointFrozen) {
    ImGui::EndDisabled();
  }

  if (!jointFrozen && changed) {
    childBone.swingLimitRadians = Deg2Rad((std::max)(0.0f, (std::min)(180.0f, swingApertureDeg)));
    childBone.twistLimitRadians = Deg2Rad((std::max)(0.0f, (std::min)(180.0f, twistAngleDeg)));
    m_ragdollEditDirty = true;
    m_ragdollEditRebuildRequested = true;
  }

  if (gui.Button("Select Child Body")) {
    SelectRagdollEditCapsule(childCapsule, true);
  }
  ImGui::SameLine();
  if (gui.Button("Select Parent Body")) {
    SelectRagdollEditCapsule(parentCapsule, true);
  }
  ImGui::SameLine();
  if (gui.Button("Delete Joint", !jointFrozen)) {
    ClearRagdollCapsuleJoint(childCapsule);
  }
}

void SceneTemplate::DrawSkeletonEditPanel(t850::DevGuiContext& gui) {
  if (!gui.BeginSection("Skeleton Edit")) {
    return;
  }

  std::vector<int> skinnedMeshIndices;
  std::vector<std::string> skinnedMeshOptions = BuildSkinnedMeshOptions(&skinnedMeshIndices);
  if (skinnedMeshIndices.empty()) {
    gui.Text("Load a skinned model to edit its skeleton.");
    return;
  }

  m_selectedSkinningMeshIndex = ClampSkinnedMeshSelection(m_selectedSkinningMeshIndex);
  int selectedSkinnedOption = 0;
  for (int optionIndex = 0; optionIndex < static_cast<int>(skinnedMeshIndices.size()); ++optionIndex) {
    if (skinnedMeshIndices[static_cast<std::size_t>(optionIndex)] == m_selectedSkinningMeshIndex) {
      selectedSkinnedOption = optionIndex;
      break;
    }
  }

  t850::SelectorDesc skinnedSelector;
  skinnedSelector.name = "skinning_model";
  skinnedSelector.label = "Skinned Model";
  if (gui.Combo(skinnedSelector, selectedSkinnedOption, &skinnedMeshOptions) &&
      selectedSkinnedOption >= 0 &&
      selectedSkinnedOption < static_cast<int>(skinnedMeshIndices.size())) {
    if (m_skeletonEditMode) {
      ExitSkeletonEditMode();
    }
    m_selectedSkinningMeshIndex = skinnedMeshIndices[static_cast<std::size_t>(selectedSkinnedOption)];
    m_ragdollEditRenamingCapsule = -1;
    m_ragdollEditRenameFocusPending = false;
  }

  const int selectedMeshIndex = m_selectedSkinningMeshIndex;
  RenderSkinnedMesh* skinned = GetSkinnedMeshForIndex(selectedMeshIndex);
  if (!skinned || !skinned->HasSkinData()) {
    gui.Text("Load a skinned model to edit its skeleton.");
    return;
  }

  const SceneRagdollRuntime* sceneRagdoll = FindSceneRagdollRuntime(selectedMeshIndex);
  const bool selectedPrimaryAuthoring = (selectedMeshIndex == 0 && sceneRagdoll == nullptr && !m_loadedEditorScene);
  if (!selectedPrimaryAuthoring && m_skeletonEditMode) {
    ExitSkeletonEditMode();
  }

  if (m_ragdollEditRenamingCapsule >= 0) {
    if (!m_skeletonEditMode || !selectedPrimaryAuthoring) {
      m_ragdollEditRenamingCapsule = -1;
      m_ragdollEditRenameFocusPending = false;
      return;
    }
    m_ragdollEditTopologyChangedThisFrame = false;
    DrawRagdollCapsuleEditPanel(gui);
    return;
  }

  if (ImGui::CollapsingHeader("Runtime controls", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Selected slot: %d", selectedMeshIndex);
    if (sceneRagdoll) {
      ImGui::TextWrapped("Authored ragdoll: %s", sceneRagdoll->resourcePath.c_str());
      ImGui::Text("Bodies: %zu", sceneRagdoll->binding.referencePose.bones.size());
      ImGui::TextDisabled("Capsules/joints are loaded through the scene object's transform, so scale, rotation, and translation are applied at runtime.");
    } else if (selectedPrimaryAuthoring && !m_ragdollAnimationBinding.referencePose.bones.empty()) {
      ImGui::Text("Bodies: %zu", m_ragdollAnimationBinding.referencePose.bones.size());
    } else {
      ImGui::TextDisabled("No authored ragdoll is attached to this skinned model.");
    }
    if (gui.Button(m_showPhysics ? "Physics Debug: On" : "Physics Debug: Off")) {
      m_showPhysics = !m_showPhysics;
    }
    ImGui::SameLine();
    if (gui.Button(m_showSkeleton ? "Skeleton Debug: On" : "Skeleton Debug: Off")) {
      m_showSkeleton = !m_showSkeleton;
    }
    const bool hasSelectedRagdoll =
        selectedMeshIndex >= 0 &&
        selectedMeshIndex < kMaxSandboxMeshes &&
        Meshes[selectedMeshIndex].HasPhysicsRagdoll();
    if (gui.Button("Start Simulation", hasSelectedRagdoll)) {
      if (selectedPrimaryAuthoring) {
        SwitchRagdollToPhysics();
      } else {
        SwitchSceneRagdollsToPhysics(selectedMeshIndex);
      }
    }
    const bool selectedPhysicsDriven = selectedPrimaryAuthoring
        ? m_ragdollPhysicsDriven
        : IsSceneRagdollPhysicsDriven(selectedMeshIndex);
    if (selectedPhysicsDriven && selectedPrimaryAuthoring) {
      ImGui::TextWrapped("Simulation grab: left-click a capsule/box to hold it, drag to move or throw it, release to let it fall.");
      if (m_ragdollSimulationGrabActive && m_ragdollSimulationGrabBodyIndex >= 0) {
        const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
        const char* name = m_ragdollSimulationGrabBodyIndex < static_cast<int>(bones.size())
            ? bones[static_cast<std::size_t>(m_ragdollSimulationGrabBodyIndex)].body.debugName.c_str()
            : "body";
        ImGui::Text("Holding body %d: %s", m_ragdollSimulationGrabBodyIndex, name);
      }
    } else if (selectedPhysicsDriven) {
      ImGui::TextWrapped("Selected scene ragdoll is running as dynamic Jolt physics.");
    }
    ImGui::SetNextItemWidth(160.0f);
    int simulationSpeedIndex = ClampRagdollSimulationSpeedIndex(m_ragdollSimulationSpeedIndex);
    if (ImGui::SliderInt("Simulation speed",
                         &simulationSpeedIndex,
                         0,
                         static_cast<int>(kRagdollSimulationSpeedScales.size()) - 1,
                         RagdollSimulationSpeedLabelForIndex(simulationSpeedIndex))) {
      m_ragdollSimulationSpeedIndex = ClampRagdollSimulationSpeedIndex(simulationSpeedIndex);
      t850::EngineContext* engineContext = GetEngineContext();
      if (!engineContext) engineContext = &t850::GetEngineContext();
      if (engineContext && engineContext->physics) {
        engineContext->physics->SetSimulationSpeedScale(
            RagdollSimulationSpeedScaleForIndex(m_ragdollSimulationSpeedIndex));
      }
    }
    if (ImGui::Checkbox("Fixed 1/60 physics delta", &m_ragdollUseFixedSimulationDelta)) {
      t850::EngineContext* engineContext = GetEngineContext();
      if (!engineContext) engineContext = &t850::GetEngineContext();
      if (engineContext && engineContext->physics) {
        engineContext->physics->SetUseFixedSimulationDelta(m_ragdollUseFixedSimulationDelta);
      }
    }
    ImGui::TextDisabled(m_ragdollUseFixedSimulationDelta
                            ? "Physics advances one 1/60s input per frame."
                            : "Physics uses the measured frame delta.");
    if (!selectedPrimaryAuthoring) {
      ImGui::BeginDisabled();
    }
    if (gui.Button(m_skeletonEditMode ? "Exit Edit Mode" : "Enter Edit Mode")) {
      if (m_skeletonEditMode) ExitSkeletonEditMode();
      else EnterSkeletonEditMode();
    }
    if (!selectedPrimaryAuthoring) {
      ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (gui.Button("Reset Physics/Animation")) {
      if (selectedPrimaryAuthoring) {
        ResetRagdollPhysicsAndAnimation();
      } else {
        ResetSceneRagdollPhysicsAndAnimation(selectedMeshIndex);
      }
    }
    ImGui::SameLine();
    if (gui.Button("Undo", CanUndoRagdollAuthoringEdit())) {
      UndoRagdollAuthoringEdit();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+Z (%zu/10)", m_ragdollUndoState.stack.size());
  }

  if (!selectedPrimaryAuthoring) {
    if (ImGui::CollapsingHeader("Scene ragdoll", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::TextWrapped("This is a scene instance. It uses the authored ragdoll asset for simulation, transformed by the scene object matrix. Capsule authoring still targets directly loaded GLBs.");
    }
    return;
  }

  if (ImGui::CollapsingHeader("Skeleton edit mode", ImGuiTreeNodeFlags_DefaultOpen)) {
    gui.Text(m_skeletonEditMode
        ? "Bind-pose edit mode is active. Bone clicks edit the animation skeleton; shape handle clicks edit the physical ragdoll body."
        : "Enter mode to pause animation and move the model to bind pose.");

    if (!m_skeletonEditMode) {
      if (gui.Button("Enter Skeleton Edit Mode")) {
        EnterSkeletonEditMode();
      }
      return;
    }

    if (gui.Button("Exit Edit Mode")) {
      ExitSkeletonEditMode();
    }
    ImGui::SameLine();
    if (gui.Button("Reset Bind Pose")) {
      ResetSkeletonEditPose();
    }

    if (gui.Button("Load Saved Edits")) {
      LoadSkeletonEditPose();
    }
    ImGui::SameLine();
    if (gui.Button(m_skeletonEditDirty ? "Save Edits *" : "Save Edits")) {
      SaveSkeletonEditPose();
    }

    if (!m_skeletonEditSavePath.empty()) {
      ImGui::TextWrapped("Save path: %s", m_skeletonEditSavePath.c_str());
    }
  }

  if (!m_skeletonEditMode) {
    return;
  }

  const xF::xSkeleton* skeleton = skinned->GetAnimController().GetAnimSkeleton();
  if (!skeleton || skeleton->Bones.empty() || m_skeletonEditCombined.empty()) {
    gui.Text("Skeleton data is unavailable.");
    return;
  }

  std::vector<std::string> boneOptions;
  const int boneCount = (std::min)(static_cast<int>(skeleton->Bones.size()), static_cast<int>(m_skeletonEditCombined.size()));
  boneOptions.reserve(static_cast<std::size_t>(boneCount));
  for (int i = 0; i < boneCount; ++i) {
    boneOptions.push_back(std::to_string(i) + ": " + skeleton->Bones[i].Name);
  }

  if (m_skeletonEditSelectedBone < 0 || m_skeletonEditSelectedBone >= boneCount) {
    SelectSkeletonEditBone(boneCount > 0 ? 0 : -1);
  }

  if (ImGui::CollapsingHeader("Bone edit", ImGuiTreeNodeFlags_DefaultOpen)) {
    t850::SelectorDesc boneSelector;
    boneSelector.name = "skeleton_edit_bone";
    boneSelector.label = "Bone";
    int selectedBone = m_skeletonEditSelectedBone;
    if (gui.Combo(boneSelector, selectedBone, &boneOptions)) {
      SelectSkeletonEditBone(selectedBone);
    }

    if (m_skeletonEditSelectedBone < 0 || m_skeletonEditSelectedBone >= boneCount) {
      return;
    }

    ImGui::PushID(m_skeletonEditSelectedBone);
    XVECTOR3 worldPosition;
    if (GetSkeletonEditBoneWorldPosition(m_skeletonEditSelectedBone, worldPosition)) {
      float position[3] = {worldPosition.x, worldPosition.y, worldPosition.z};
      if (ImGui::DragFloat3("World position", position, (std::max)(0.001f, m_modelRadius * 0.002f), 0.0f, 0.0f, "%.3f")) {
        RestoreSkeletonPreviewBone();
        SetSkeletonEditBoneWorldPosition(m_skeletonEditSelectedBone, XVECTOR3(position[0], position[1], position[2], 1.0f));
      }
    }

    std::array<float, 3> scale = GetSkeletonEditBoneScale(m_skeletonEditSelectedBone);
    float scaleValues[3] = {scale[0], scale[1], scale[2]};
    if (ImGui::DragFloat3("Bone basis scale", scaleValues, 0.01f, 0.001f, 100.0f, "%.3f")) {
      RestoreSkeletonPreviewBone();
      SetSkeletonEditBoneScale(m_skeletonEditSelectedBone, {scaleValues[0], scaleValues[1], scaleValues[2]});
    }

    if (gui.Button("Reset Selected Bone")) {
      RestoreSkeletonPreviewBone();
      if (m_skeletonEditSelectedBone >= 0 &&
          m_skeletonEditSelectedBone < static_cast<int>(m_skeletonEditBindCombined.size()) &&
          m_skeletonEditSelectedBone < static_cast<int>(m_skeletonEditCombined.size())) {
        m_skeletonEditCombined[static_cast<std::size_t>(m_skeletonEditSelectedBone)] =
            m_skeletonEditBindCombined[static_cast<std::size_t>(m_skeletonEditSelectedBone)];
        m_skeletonEditDirty = true;
        ApplySkeletonEditPose();
      }
    }
    ImGui::SameLine();
    if (gui.Button("Frame Selected")) {
      if (GetSkeletonEditBoneWorldPosition(m_skeletonEditSelectedBone, worldPosition)) {
        m_orbitTarget = worldPosition;
        m_panOffset = XVECTOR3(0.0f, 0.0f, 0.0f);
        ComputeOrbitCamera();
        VP = Cam.VP;
      }
    }
    ImGui::PopID();
  }
  m_ragdollEditTopologyChangedThisFrame = false;
  const bool renamingCapsuleThisFrame = m_ragdollEditRenamingCapsule >= 0;
  DrawRagdollCapsuleEditPanel(gui);
  if (renamingCapsuleThisFrame || m_ragdollEditRenamingCapsule >= 0) {
    return;
  }
  if (!m_ragdollEditTopologyChangedThisFrame) {
    DrawRagdollJointEditPanel(gui);
  }
  DrawSkeletonPreviewBoneGizmo();
  DrawRagdollViewportContextMenu();
}

void SceneTemplate::DrawRagdollPhysicsSimulationPanel(t850::DevGuiContext& gui) {
  if (!gui.EmbedPanels()) {
    ImGui::SetNextWindowSize(ImVec2(460.0f, 680.0f), ImGuiCond_FirstUseEver);
  }
  const bool begun = gui.BeginPanel("Ragdoll Physics Simulation");
  if (begun) {
    BeginRagdollUndoScope("Panel edit");
    DrawSkeletonEditPanel(gui);
    const bool gestureActive =
        ImGui::IsAnyItemActive() ||
        ImGui::IsMouseDown(0) ||
        m_skeletonEditDragging ||
        m_ragdollEditHandleDragging ||
        m_ragdollEditGizmoDragging ||
        m_ragdollEditJointDragging;
    EndRagdollUndoScope(gestureActive);
  }
  gui.EndPanel();
}

#ifdef OS_ANDROID
void SceneTemplate::DrawAndroidPhysicsPanel(t850::DevGuiContext& gui) {
  if (!gui.BeginSection("Physics")) {
    return;
  }

  if (gui.Button(m_showPhysics ? "Physics Debug: On" : "Physics Debug: Off")) {
    m_showPhysics = !m_showPhysics;
  }
  ImGui::SameLine();
  if (gui.Button(m_showNavMesh ? "NavMesh: On" : "NavMesh: Off")) {
    m_showNavMesh = !m_showNavMesh;
    if (m_showNavMesh && !m_navMesh.IsReady()) m_navMeshBuildAttempted = false;
  }
  ImGui::SameLine();
  if (gui.Button(m_showSkeleton ? "Skeleton Debug: On" : "Skeleton Debug: Off")) {
    m_showSkeleton = !m_showSkeleton;
  }

  std::vector<int> skinnedMeshIndices;
  std::vector<std::string> skinnedMeshOptions = BuildSkinnedMeshOptions(&skinnedMeshIndices);
  if (!skinnedMeshIndices.empty()) {
    m_selectedSkinningMeshIndex = ClampSkinnedMeshSelection(m_selectedSkinningMeshIndex);
    int selectedSkinnedOption = 0;
    for (int optionIndex = 0; optionIndex < static_cast<int>(skinnedMeshIndices.size()); ++optionIndex) {
      if (skinnedMeshIndices[static_cast<std::size_t>(optionIndex)] == m_selectedSkinningMeshIndex) {
        selectedSkinnedOption = optionIndex;
        break;
      }
    }
    t850::SelectorDesc skinnedSelector;
    skinnedSelector.name = "android_physics_model";
    skinnedSelector.label = "Skinned Model";
    if (gui.Combo(skinnedSelector, selectedSkinnedOption, &skinnedMeshOptions) &&
        selectedSkinnedOption >= 0 &&
        selectedSkinnedOption < static_cast<int>(skinnedMeshIndices.size())) {
      m_selectedSkinningMeshIndex = skinnedMeshIndices[static_cast<std::size_t>(selectedSkinnedOption)];
    }
  }

  const int selectedMeshIndex = ClampSkinnedMeshSelection(m_selectedSkinningMeshIndex);
  const SceneRagdollRuntime* sceneRagdoll = FindSceneRagdollRuntime(selectedMeshIndex);
  const bool selectedPrimaryAuthoring = (selectedMeshIndex == 0 && sceneRagdoll == nullptr && !m_loadedEditorScene);
  const bool hasSelectedRagdoll =
      selectedMeshIndex >= 0 &&
      selectedMeshIndex < kMaxSandboxMeshes &&
      Meshes[selectedMeshIndex].HasPhysicsRagdoll();

  if (hasSelectedRagdoll) {
    if (gui.Button("Start Simulation")) {
      if (selectedPrimaryAuthoring) {
        SwitchRagdollToPhysics();
      } else {
        SwitchSceneRagdollsToPhysics(selectedMeshIndex);
      }
    }
    const bool selectedPhysicsDriven = selectedPrimaryAuthoring
        ? m_ragdollPhysicsDriven
        : IsSceneRagdollPhysicsDriven(selectedMeshIndex);
    if (selectedPhysicsDriven && selectedPrimaryAuthoring) {
      ImGui::TextWrapped("Simulation grab: touch/left-click a body, drag to move or throw it, release to drop.");
      if (m_ragdollSimulationGrabActive && m_ragdollSimulationGrabBodyIndex >= 0) {
        const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
        const char* name = m_ragdollSimulationGrabBodyIndex < static_cast<int>(bones.size())
            ? bones[static_cast<std::size_t>(m_ragdollSimulationGrabBodyIndex)].body.debugName.c_str()
            : "body";
        ImGui::Text("Holding body %d: %s", m_ragdollSimulationGrabBodyIndex, name);
      }
    } else if (selectedPhysicsDriven) {
      ImGui::TextWrapped("Selected scene ragdoll is running as dynamic Jolt physics.");
    }
    ImGui::SetNextItemWidth(220.0f);
    int simulationSpeedIndex = ClampRagdollSimulationSpeedIndex(m_ragdollSimulationSpeedIndex);
    if (ImGui::SliderInt("Simulation speed",
                         &simulationSpeedIndex,
                         0,
                         static_cast<int>(kRagdollSimulationSpeedScales.size()) - 1,
                         RagdollSimulationSpeedLabelForIndex(simulationSpeedIndex))) {
      m_ragdollSimulationSpeedIndex = ClampRagdollSimulationSpeedIndex(simulationSpeedIndex);
      t850::EngineContext* engineContext = GetEngineContext();
      if (!engineContext) engineContext = &t850::GetEngineContext();
      if (engineContext && engineContext->physics) {
        engineContext->physics->SetSimulationSpeedScale(
            RagdollSimulationSpeedScaleForIndex(m_ragdollSimulationSpeedIndex));
      }
    }
    if (ImGui::Checkbox("Fixed 1/60 physics delta", &m_ragdollUseFixedSimulationDelta)) {
      t850::EngineContext* engineContext = GetEngineContext();
      if (!engineContext) engineContext = &t850::GetEngineContext();
      if (engineContext && engineContext->physics) {
        engineContext->physics->SetUseFixedSimulationDelta(m_ragdollUseFixedSimulationDelta);
      }
    }
    ImGui::TextDisabled(m_ragdollUseFixedSimulationDelta ? "Fixed per-frame input." : "Measured frame delta.");
    if (gui.Button("Reset Physics/Animation")) {
      if (selectedPrimaryAuthoring) {
        ResetRagdollPhysicsAndAnimation();
      } else {
        ResetSceneRagdollPhysicsAndAnimation(selectedMeshIndex);
      }
    }
  } else {
    gui.Text("No physics ragdoll is attached to this model.");
  }

  ImGui::TextWrapped("Left triple-tap opens this physics panel. Right triple-tap opens full scene controls.");
}
#endif

void SceneTemplate::OnInput(InputManager* IManager) {
  t850::game::InputFrame gameInput;
  const float keyboardRight = (IManager->PressedKey(T800K_d) ? 1.0f : 0.0f) -
      (IManager->PressedKey(T800K_a) ? 1.0f : 0.0f);
  const float keyboardForward = (IManager->PressedKey(T800K_w) ? 1.0f : 0.0f) -
      (IManager->PressedKey(T800K_s) ? 1.0f : 0.0f);
  const float moveRight = std::clamp(keyboardRight + IManager->Gamepad.leftX, -1.0f, 1.0f);
  const float moveForward = std::clamp(keyboardForward - IManager->Gamepad.leftY, -1.0f, 1.0f);
  XVECTOR3 cameraRight = Cam.Right;
  cameraRight.y = 0.0f;
  cameraRight.w = 0.0f;
  if (cameraRight.Length() > 0.000001f) cameraRight.Normalize();
  XVECTOR3 cameraForward = Cam.Look;
  cameraForward.y = 0.0f;
  cameraForward.w = 0.0f;
  if (cameraForward.Length() > 0.000001f) cameraForward.Normalize();
  gameInput.moveAxis = cameraRight * moveRight + cameraForward * moveForward;
  gameInput.lookDelta = XVECTOR3(
      static_cast<float>(IManager->xDelta) * 0.002f + IManager->Gamepad.rightX * 0.04f,
      static_cast<float>(-IManager->yDelta) * 0.002f - IManager->Gamepad.rightY * 0.04f,
      0.0f,
      0.0f);
  gameInput.buttonsDown = IManager->PressedMouseButton(0) || IManager->Gamepad.rightTrigger > 0.5f ? 1u : 0u;
  gameInput.buttonsPressed = IManager->PressedOnceMouseButton(0) ? 1u : 0u;
  gameInput.dtSeconds = DtSecs;
  m_gameLogic.SetInputFrame(std::move(gameInput));

  // Skip mouse-driven camera when replay snapshot is active
  if (m_dumper.IsReplayActive()) return;

  if (m_ragdollEditRenamingCapsule >= 0) {
    m_skeletonEditDragging = false;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_ragdollEditJointAxis = -1;
    m_ragdollContextMenuRightButtonHeld = false;
    return;
  }

  bool imguiWantsMouse = false;
#ifndef OS_ANDROID
  imguiWantsMouse = !m_ignoreImGuiMouseCaptureForInput &&
      ImGui::GetCurrentContext() &&
      ImGui::GetIO().WantCaptureMouse;
#endif

  if (IManager->PressedOnceKey(T800K_F9)) {
    const int nextProfile = (m_cameraController.GetActiveProfileIndex() + 1) %
        static_cast<int>(t850::CameraProfileType::Count);
    SetCameraProfile(t850::CameraProfileTypeFromIndex(nextProfile));
  }

  m_cameraController.HandleInput(BuildCameraInputState(IManager, imguiWantsMouse));

  if (IManager->PressedOnceKey(T800K_k)) {
    if (m_cameraController.GetActiveProfileType() == t850::CameraProfileType::Orbit) {
      T8_LOG_INFO("Orbit: target[%f,%f,%f] dist=%f yaw=%f pitch=%f",
        m_orbitTarget.x, m_orbitTarget.y, m_orbitTarget.z,
        m_orbitDist, m_orbitYaw, m_orbitPitch);
    } else {
      T8_LOG_INFO("Camera profile='%s' position[%f,%f,%f] orientation[pitch=%f yaw=%f roll=%f]",
        t850::CameraProfileName(m_cameraController.GetActiveProfileType()),
        Cam.Eye.x, Cam.Eye.y, Cam.Eye.z, Cam.Pitch, Cam.Yaw, Cam.Roll);
    }
  }

  // API switching
  if (IManager->PressedOnceKey(T800K_1))
    pFramework->ChangeAPI(GraphicsApi::D3D11);
  if (IManager->PressedOnceKey(T800K_2))
    pFramework->ChangeAPI(GraphicsApi::OPENGL);

  // Debug toggles
  if (IManager->PressedOnceKey(T800K_F2)) {
    m_showCullStats = !m_showCullStats;
    SceneProp.ShowCullingDebug = m_showCullStats;
  }
  if (IManager->PressedOnceKey(T800K_KP6) || IManager->PressedOnceKey(T800K_6)) {
    if (SceneProp.FrustumCullingToggleAllowed) {
      const bool requested = !SceneProp.FrustumCullingEnabled;
      if (!requested || !Meshes[0].pBase || static_cast<RenderMesh*>(Meshes[0].pBase)->EnsureCullingMetadata()) {
        SceneProp.FrustumCullingEnabled = requested;
      }
      T8_LOG_INFO("[CULLING] Frustum culling %s", SceneProp.FrustumCullingEnabled ? "enabled" : "disabled");
    } else {
      SceneProp.FrustumCullingEnabled = false;
      T8_LOG_INFO("[CULLING] Frustum culling locked off by startup policy");
    }
  }
  if (IManager->PressedOnceKey(T800K_F3))
    m_showAABBs = !m_showAABBs;
  if (IManager->PressedOnceKey(T800K_F4))
    m_showPhysics = !m_showPhysics;

  // Arrow keys: step keyframes when in keyframe mode
  RenderSkinnedMesh* sk = GetSelectedAnimationMesh();
  if (sk && sk->GetKeyframeMode()) {
    if (IManager->PressedOnceKey(T800K_RIGHT))
      sk->StepKeyframe(1);
    if (IManager->PressedOnceKey(T800K_LEFT))
      sk->StepKeyframe(-1);
  }
}

void SceneTemplate::FitModelToView() {
  RenderMesh::AABB total;
  total.Reset();
  bool hasBounds = false;
  const int count = (std::max)(m_meshCount, Meshes[0].pBase ? 1 : 0);
  for (int meshIndex = 0; meshIndex < count; ++meshIndex) {
    if (!Meshes[meshIndex].pBase || !Meshes[meshIndex].Visible) continue;
    RenderMesh* rm = static_cast<RenderMesh*>(Meshes[meshIndex].pBase);
    RenderMesh::AABB worldBounds;
    if (BuildWorldBounds(rm, Meshes[meshIndex].Final, worldBounds)) {
      if (!hasBounds) {
        total = worldBounds;
      } else {
        ExpandBounds(total, worldBounds);
      }
      hasBounds = true;
    }
  }
  if (!hasBounds) {
    return;
  }

  m_orbitTarget = XVECTOR3(
    (total.min.x + total.max.x) * 0.5f,
    (total.min.y + total.max.y) * 0.5f,
    (total.min.z + total.max.z) * 0.5f);
  m_panOffset = XVECTOR3(0, 0, 0);

  float ex = (total.max.x - total.min.x) * 0.5f;
  float ey = (total.max.y - total.min.y) * 0.5f;
  float ez = (total.max.z - total.min.z) * 0.5f;
  m_modelRadius = std::sqrt(ex*ex + ey*ey + ez*ez);
  if (m_modelRadius < 1e-4f) m_modelRadius = 1.0f;

  // Place camera at a distance that fits the bounding sphere in the FOV
  float halfFov = Cam.Fov * 0.5f;
  m_orbitDist = m_modelRadius / std::tan(halfFov);
  m_orbitYaw = g_config.orbitYawOverride ? g_config.orbitYaw : 0.0f;
  m_orbitPitch = 0.0f;
  SyncOrbitProfileFromSandbox();

  Cam.NPlane = 4.0f / 32.0f;
  Cam.FPlane = m_modelRadius * 100.0f;
  Cam.CreatePojection();

  T8_LOG_INFO("[SceneTemplate] Model center=(%.2f,%.2f,%.2f) radius=%.2f dist=%.2f near=%.3f",
    m_orbitTarget.x, m_orbitTarget.y, m_orbitTarget.z, m_modelRadius, m_orbitDist, Cam.NPlane);
}

void SceneTemplate::ComputeOrbitCamera() {
  SyncOrbitProfileFromSandbox();
  if (t850::OrbitCameraProfile* orbit = m_cameraController.GetOrbitProfile()) {
    orbit->Update(Cam, 0.0f, t850::CameraUpdateContext{});
    SyncSandboxOrbitFromProfile();
    return;
  }

  XVECTOR3 target = m_orbitTarget + m_panOffset;
  Cam.Velocity = XVECTOR3(0, 0, 0);
  Cam.SetLookAt(target);
}

bool SceneTemplate::SetCameraProfile(t850::CameraProfileType type) {
  if (type == t850::CameraProfileType::Orbit) {
    SyncOrbitProfileFromSandbox();
  }
  if (!m_cameraController.SetActiveProfile(type)) {
    T8_LOG_ERROR("[SceneTemplate] Failed to activate camera profile '%s'", t850::CameraProfileName(type));
    return false;
  }
  m_cameraProfileSelection = m_cameraController.GetActiveProfileIndex();
  if (type == t850::CameraProfileType::Orbit) {
    SyncSandboxOrbitFromProfile();
  }
#ifdef OS_ANDROID
  if (type == t850::CameraProfileType::Orbit) {
    ResetAndroidVirtualControls();
  }
#endif
  T8_LOG_INFO("[SceneTemplate] Camera profile: %s", t850::CameraProfileName(type));
  return true;
}

#ifdef OS_ANDROID
bool SceneTemplate::AndroidVirtualControlsVisible() const {
  if (!ActiveCam) {
    return false;
  }
  return m_cameraController.GetActiveProfileType() != t850::CameraProfileType::Orbit;
}

bool SceneTemplate::AndroidVirtualControlsActive() const {
  return m_androidMovePointerId >= 0 || m_androidLookPointerId >= 0 ||
         m_androidJumpPointerId >= 0 || m_androidRunPointerId >= 0;
}

void SceneTemplate::ResetAndroidVirtualControls() {
  m_androidMovePointerId = -1;
  m_androidLookPointerId = -1;
  m_androidJumpPointerId = -1;
  m_androidRunPointerId = -1;
  m_androidMoveAxis = XVECTOR2(0.0f, 0.0f);
  m_androidLookAxis = XVECTOR2(0.0f, 0.0f);
  m_androidJump = false;
  m_androidRun = false;
}

bool SceneTemplate::HandleAndroidVirtualControls(AInputEvent* event) {
  if (!event || AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) {
    return false;
  }

  if (!AndroidVirtualControlsVisible()) {
    ResetAndroidVirtualControls();
    return false;
  }

  ImGuiIO& io = ImGui::GetIO();
  float width = io.DisplaySize.x;
  float height = io.DisplaySize.y;
  if (pFramework && pFramework->pVideoDriver) {
    if (pFramework->pVideoDriver->width > 0) {
      width = static_cast<float>(pFramework->pVideoDriver->width);
    }
    if (pFramework->pVideoDriver->height > 0) {
      height = static_cast<float>(pFramework->pVideoDriver->height);
    }
  }
  if (width <= 0.0f || height <= 0.0f) {
    return false;
  }

  const SandboxAndroidVirtualControlsLayout layout = BuildSandboxAndroidVirtualControlsLayout(width, height);
  const int32_t rawAction = AMotionEvent_getAction(event);
  const int32_t action = rawAction & AMOTION_EVENT_ACTION_MASK;
  int32_t actionPointerIndex =
      (rawAction & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
  const size_t pointerCount = AMotionEvent_getPointerCount(event);
  if (pointerCount == 0) {
    ResetAndroidVirtualControls();
    return false;
  }
  if (actionPointerIndex < 0 || actionPointerIndex >= static_cast<int32_t>(pointerCount)) {
    actionPointerIndex = 0;
  }

  auto resetPointer = [&](int pointerId) {
    if (pointerId == m_androidMovePointerId) {
      m_androidMovePointerId = -1;
      m_androidMoveAxis = XVECTOR2(0.0f, 0.0f);
    }
    if (pointerId == m_androidLookPointerId) {
      m_androidLookPointerId = -1;
      m_androidLookAxis = XVECTOR2(0.0f, 0.0f);
    }
    if (pointerId == m_androidJumpPointerId) {
      m_androidJumpPointerId = -1;
      m_androidJump = false;
    }
    if (pointerId == m_androidRunPointerId) {
      m_androidRunPointerId = -1;
      m_androidRun = false;
    }
  };

  if (action == AMOTION_EVENT_ACTION_CANCEL) {
    ResetAndroidVirtualControls();
    return true;
  }

  if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP) {
    const int pointerId = AMotionEvent_getPointerId(event, actionPointerIndex);
    if (action == AMOTION_EVENT_ACTION_UP) {
      ResetAndroidVirtualControls();
      return true;
    }
    resetPointer(pointerId);
    return true;
  }

  auto capturePointer = [&](int pointerIndex) {
    const int pointerId = AMotionEvent_getPointerId(event, pointerIndex);
    const float x = AMotionEvent_getX(event, pointerIndex);
    const float y = AMotionEvent_getY(event, pointerIndex);

    if (m_androidJumpPointerId < 0 && PointInsideCircle(x, y, layout.jumpCenter, layout.buttonRadius * 1.2f)) {
      m_androidJumpPointerId = pointerId;
      m_androidJump = true;
      return true;
    }
    if (m_androidRunPointerId < 0 && PointInsideCircle(x, y, layout.runCenter, layout.buttonRadius * 1.2f)) {
      m_androidRunPointerId = pointerId;
      m_androidRun = true;
      return true;
    }
    if (m_androidMovePointerId < 0 && PointInsideCircle(x, y, layout.moveCenter, layout.stickRadius * 1.45f)) {
      m_androidMovePointerId = pointerId;
      m_androidMoveAxis = StickAxisFromPoint(x, y, layout.moveCenter, layout.stickRadius);
      return true;
    }
    if (m_androidLookPointerId < 0 && PointInsideCircle(x, y, layout.lookCenter, layout.stickRadius * 1.45f)) {
      m_androidLookPointerId = pointerId;
      m_androidLookAxis = StickAxisFromPoint(x, y, layout.lookCenter, layout.stickRadius);
      return true;
    }
    return false;
  };

  if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
    capturePointer(actionPointerIndex);
    return true;
  }

  if (action != AMOTION_EVENT_ACTION_MOVE) {
    return true;
  }

  const int moveIndex = FindPointerIndexById(event, m_androidMovePointerId);
  if (moveIndex >= 0) {
    m_androidMoveAxis = StickAxisFromPoint(AMotionEvent_getX(event, moveIndex),
                                          AMotionEvent_getY(event, moveIndex),
                                          layout.moveCenter,
                                          layout.stickRadius);
  } else {
    m_androidMovePointerId = -1;
    m_androidMoveAxis = XVECTOR2(0.0f, 0.0f);
  }

  const int lookIndex = FindPointerIndexById(event, m_androidLookPointerId);
  if (lookIndex >= 0) {
    m_androidLookAxis = StickAxisFromPoint(AMotionEvent_getX(event, lookIndex),
                                          AMotionEvent_getY(event, lookIndex),
                                          layout.lookCenter,
                                          layout.stickRadius);
  } else {
    m_androidLookPointerId = -1;
    m_androidLookAxis = XVECTOR2(0.0f, 0.0f);
  }

  const int jumpIndex = FindPointerIndexById(event, m_androidJumpPointerId);
  if (jumpIndex >= 0) {
    m_androidJump = PointInsideCircle(AMotionEvent_getX(event, jumpIndex),
                                      AMotionEvent_getY(event, jumpIndex),
                                      layout.jumpCenter,
                                      layout.buttonRadius * 1.2f);
    if (!m_androidJump) {
      m_androidJumpPointerId = -1;
    }
  } else {
    m_androidJumpPointerId = -1;
    m_androidJump = false;
  }

  const int runIndex = FindPointerIndexById(event, m_androidRunPointerId);
  if (runIndex >= 0) {
    m_androidRun = PointInsideCircle(AMotionEvent_getX(event, runIndex),
                                     AMotionEvent_getY(event, runIndex),
                                     layout.runCenter,
                                     layout.buttonRadius * 1.2f);
    if (!m_androidRun) {
      m_androidRunPointerId = -1;
    }
  } else {
    m_androidRunPointerId = -1;
    m_androidRun = false;
  }

  return true;
}

void SceneTemplate::DrawAndroidVirtualControls(bool guiVisible) {
  if (guiVisible || !AndroidVirtualControlsVisible()) {
    ResetAndroidVirtualControls();
    return;
  }

  ImGuiIO& io = ImGui::GetIO();
  float width = io.DisplaySize.x;
  float height = io.DisplaySize.y;
  if (pFramework && pFramework->pVideoDriver) {
    if (pFramework->pVideoDriver->width > 0) {
      width = static_cast<float>(pFramework->pVideoDriver->width);
    }
    if (pFramework->pVideoDriver->height > 0) {
      height = static_cast<float>(pFramework->pVideoDriver->height);
    }
  }
  if (width <= 0.0f || height <= 0.0f) {
    return;
  }

  const SandboxAndroidVirtualControlsLayout layout =
      BuildSandboxAndroidVirtualControlsLayout(width, height);
  ImDrawList* drawList = ImGui::GetForegroundDrawList();
  if (!drawList) {
    return;
  }

  const ImU32 stickFill = IM_COL32(36, 44, 56, 76);
  const ImU32 stickLine = IM_COL32(220, 230, 255, 120);
  const ImU32 knobFill = IM_COL32(90, 170, 255, 120);
  const ImU32 buttonFill = IM_COL32(40, 50, 65, 96);
  const ImU32 buttonActiveFill = IM_COL32(90, 170, 255, 150);
  const ImU32 textColor = IM_COL32(235, 245, 255, 190);
  const ImU32 labelColor = IM_COL32(235, 245, 255, 130);

  auto drawStick = [&](const ImVec2& center, const XVECTOR2& axis, const char* label) {
    drawList->AddCircleFilled(center, layout.stickRadius, stickFill, 48);
    drawList->AddCircle(center, layout.stickRadius, stickLine, 48, 2.0f);
    drawList->AddCircle(center, layout.stickRadius * 0.42f, IM_COL32(220, 230, 255, 42), 32, 1.0f);
    const ImVec2 knob(center.x + axis.x * layout.stickRadius,
                      center.y + axis.y * layout.stickRadius);
    drawList->AddCircleFilled(knob, layout.knobRadius, knobFill, 32);
    drawList->AddCircle(knob, layout.knobRadius, stickLine, 32, 2.0f);
    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    drawList->AddText(ImVec2(center.x - labelSize.x * 0.5f,
                             center.y - layout.stickRadius - labelSize.y - 8.0f),
                      labelColor,
                      label);
  };

  drawStick(layout.moveCenter, m_androidMoveAxis, "MOVE");
  drawStick(layout.lookCenter, m_androidLookAxis, "LOOK");
  DrawLabeledCircle(drawList, layout.jumpCenter, layout.buttonRadius, "JMP",
                    m_androidJump ? buttonActiveFill : buttonFill, stickLine, textColor);
  DrawLabeledCircle(drawList, layout.runCenter, layout.buttonRadius, "RUN",
                    m_androidRun ? buttonActiveFill : buttonFill, stickLine, textColor);
}
#endif

void SceneTemplate::SyncOrbitProfileFromSandbox() {
  t850::OrbitCameraProfile* orbit = m_cameraController.GetOrbitProfile();
  if (!orbit) {
    return;
  }
  t850::OrbitCameraState state;
  state.target = m_orbitTarget;
  state.panOffset = m_panOffset;
  state.yaw = m_orbitYaw;
  state.pitch = m_orbitPitch;
  state.distance = m_orbitDist;
  state.modelRadius = m_modelRadius;
  orbit->SetState(state);
}

void SceneTemplate::SyncSandboxOrbitFromProfile() {
  const t850::OrbitCameraProfile* orbit = m_cameraController.GetOrbitProfile();
  if (!orbit) {
    return;
  }
  const t850::OrbitCameraState& state = orbit->GetState();
  m_orbitTarget = state.target;
  m_panOffset = state.panOffset;
  m_orbitYaw = state.yaw;
  m_orbitPitch = state.pitch;
  m_orbitDist = state.distance;
  m_modelRadius = state.modelRadius;
}

t850::CameraInputState SceneTemplate::BuildCameraInputState(InputManager* input, bool imguiWantsMouse) const {
  t850::CameraInputState state;
  if (input) {
    state.moveForward = input->PressedKey(T800K_w);
    state.moveBackward = input->PressedKey(T800K_s);
    state.moveLeft = input->PressedKey(T800K_a);
    state.moveRight = input->PressedKey(T800K_d);
    state.moveUp = input->PressedKey(T800K_q);
    state.moveDown = input->PressedKey(T800K_e);
    state.jump = input->PressedKey(T800K_SPACE);
    state.crouch = input->PressedKey(T800K_LCTRL) || input->PressedKey(T800K_RCTRL);
    state.sprint = input->PressedKey(T800K_LSHIFT) || input->PressedKey(T800K_RSHIFT);

    state.mouseDeltaX = static_cast<float>(input->xDelta);
    state.mouseDeltaY = static_cast<float>(input->yDelta);
    state.scrollDelta = input->scrollDelta;

    const bool allowMouse = !imguiWantsMouse;
    state.mouseLook = allowMouse && m_cameraController.GetActiveProfileType() != t850::CameraProfileType::Orbit;
    state.orbitRotate = allowMouse && input->PressedMouseButton(0);
    state.orbitPan = allowMouse && input->PressedMouseButton(1);
    state.orbitZoom = allowMouse && input->PressedMouseButton(2);

    ApplyGamepadToCameraInput(
        state,
        *input,
        DtSecs,
        allowMouse && m_cameraController.GetActiveProfileType() != t850::CameraProfileType::Orbit);
  }
#ifdef OS_ANDROID
  if (AndroidVirtualControlsVisible()) {
    constexpr float kLookThreshold = 0.02f;
    constexpr float kLookYawMouseDeltaPerSecond = 520.0f;
    constexpr float kLookPitchMouseDeltaPerSecond = 440.0f;

    state.mouseDeltaX = 0.0f;
    state.mouseDeltaY = 0.0f;
    state.scrollDelta = 0.0f;
    state.mouseLook = false;
    state.orbitRotate = false;
    state.orbitPan = false;
    state.orbitZoom = false;

    state.moveForwardAmount = ClampFloat(state.moveForwardAmount - m_androidMoveAxis.y, -1.0f, 1.0f);
    state.moveRightAmount = ClampFloat(state.moveRightAmount + m_androidMoveAxis.x, -1.0f, 1.0f);
    state.sprint = state.sprint || m_androidRun;

    const t850::CameraProfileType profileType = m_cameraController.GetActiveProfileType();
    const bool flyProfile = profileType == t850::CameraProfileType::FreeFly ||
                            profileType == t850::CameraProfileType::CollidingFly;
    state.jump = state.jump || m_androidJump;
    if (flyProfile && m_androidJump) {
      state.moveUp = true;
    }

    if (std::fabs(m_androidLookAxis.x) > kLookThreshold ||
        std::fabs(m_androidLookAxis.y) > kLookThreshold) {
      state.mouseLook = true;
      state.mouseDeltaX += m_androidLookAxis.x * kLookYawMouseDeltaPerSecond * DtSecs;
      state.mouseDeltaY += m_androidLookAxis.y * kLookPitchMouseDeltaPerSecond * DtSecs;
    }
  }
#endif
  state.mouseDeltaX *= m_mouseSensitivityX;
  state.mouseDeltaY *= m_mouseSensitivityY;
  return state;
}

bool SceneTemplate::SweepCapsule(const t850::CameraCollisionSweep& sweep, t850::CameraCollisionHit& outHit) const {
  outHit = t850::CameraCollisionHit{};
  bool hasHit = false;
  bool hasBlockingHit = false;
  bool physicsHasHit = false;
  t850::CameraCollisionHit cameraHit;
  const char* chosenSource = "none";

  const t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  const bool physicsReady = engineContext && engineContext->physics && engineContext->physics->IsInitialized();
  if (!physicsReady) {
    T8_LOG_VERBOSE(
        "[SceneTemplateSweepCapsule] start=(%.4f, %.4f, %.4f) disp=(%.4f, %.4f, %.4f) radius=%.4f halfHeight=%.4f joltReady=0 chosen=%s chosenFraction=%.5f blocking=%d",
        sweep.startCenter.x,
        sweep.startCenter.y,
        sweep.startCenter.z,
        sweep.displacement.x,
        sweep.displacement.y,
        sweep.displacement.z,
        sweep.radius,
        sweep.halfHeight,
        chosenSource,
        outHit.fraction,
        hasBlockingHit ? 1 : 0);
    return hasHit;
  }

  t850::PhysicsCapsuleCastDesc desc;
  desc.startCenter = sweep.startCenter;
  desc.displacement = sweep.displacement;
  desc.radius = sweep.radius;
  desc.halfHeight = sweep.halfHeight;

  t850::PhysicsCastHit physicsHit;
  physicsHasHit = engineContext->physics->CastCapsule(desc, physicsHit) && physicsHit.hit;
  if (physicsHasHit) {
    cameraHit.hit = true;
    cameraHit.fraction = physicsHit.fraction;
    cameraHit.position = physicsHit.position;
    cameraHit.normal = physicsHit.normal;
    cameraHit.entityId = physicsHit.entityId;
    if (ConsiderSweepHit(cameraHit, sweep.displacement, outHit, hasHit, hasBlockingHit)) {
      chosenSource = "jolt";
    }
  }
  T8_LOG_VERBOSE(
      "[SceneTemplateSweepCapsule] start=(%.4f, %.4f, %.4f) disp=(%.4f, %.4f, %.4f) radius=%.4f halfHeight=%.4f joltHit=%d joltFraction=%.5f joltNormal=(%.4f, %.4f, %.4f) chosen=%s chosenFraction=%.5f chosenNormal=(%.4f, %.4f, %.4f) blocking=%d",
      sweep.startCenter.x,
      sweep.startCenter.y,
      sweep.startCenter.z,
      sweep.displacement.x,
      sweep.displacement.y,
      sweep.displacement.z,
      sweep.radius,
      sweep.halfHeight,
      physicsHasHit ? 1 : 0,
      cameraHit.fraction,
      cameraHit.normal.x,
      cameraHit.normal.y,
      cameraHit.normal.z,
      chosenSource,
      outHit.fraction,
      outHit.normal.x,
      outHit.normal.y,
      outHit.normal.z,
      hasBlockingHit ? 1 : 0);
  return hasHit;
}

bool SceneTemplate::SweepBox(const t850::CharacterBoxSweep& sweep, t850::CameraCollisionHit& outHit) const {
  outHit = t850::CameraCollisionHit{};
  bool hasHit = false;
  bool hasBlockingHit = false;
  bool physicsHasHit = false;
  t850::CameraCollisionHit cameraHit;
  const char* chosenSource = "none";

  const t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  const bool physicsReady = engineContext && engineContext->physics && engineContext->physics->IsInitialized();
  if (!physicsReady) {
    T8_LOG_VERBOSE(
        "[SceneTemplateSweepBox] start=(%.4f, %.4f, %.4f) disp=(%.4f, %.4f, %.4f) half=(%.4f, %.4f, %.4f) joltReady=0 chosen=%s chosenFraction=%.5f blocking=%d",
        sweep.startCenter.x,
        sweep.startCenter.y,
        sweep.startCenter.z,
        sweep.displacement.x,
        sweep.displacement.y,
        sweep.displacement.z,
        sweep.halfExtents.x,
        sweep.halfExtents.y,
        sweep.halfExtents.z,
        chosenSource,
        outHit.fraction,
        hasBlockingHit ? 1 : 0);
    return hasHit;
  }

  t850::PhysicsBoxCastDesc desc;
  desc.startCenter = sweep.startCenter;
  desc.displacement = sweep.displacement;
  desc.halfExtents = sweep.halfExtents;

  t850::PhysicsCastHit physicsHit;
  physicsHasHit = engineContext->physics->CastBox(desc, physicsHit) && physicsHit.hit;
  if (physicsHasHit) {
    cameraHit.hit = true;
    cameraHit.fraction = physicsHit.fraction;
    cameraHit.position = physicsHit.position;
    cameraHit.normal = physicsHit.normal;
    cameraHit.entityId = physicsHit.entityId;
    if (ConsiderSweepHit(cameraHit, sweep.displacement, outHit, hasHit, hasBlockingHit)) {
      chosenSource = "jolt";
    }
  }
  T8_LOG_VERBOSE(
      "[SceneTemplateSweepBox] start=(%.4f, %.4f, %.4f) disp=(%.4f, %.4f, %.4f) half=(%.4f, %.4f, %.4f) joltHit=%d joltFraction=%.5f joltNormal=(%.4f, %.4f, %.4f) chosen=%s chosenFraction=%.5f chosenNormal=(%.4f, %.4f, %.4f) blocking=%d",
      sweep.startCenter.x,
      sweep.startCenter.y,
      sweep.startCenter.z,
      sweep.displacement.x,
      sweep.displacement.y,
      sweep.displacement.z,
      sweep.halfExtents.x,
      sweep.halfExtents.y,
      sweep.halfExtents.z,
      physicsHasHit ? 1 : 0,
      cameraHit.fraction,
      cameraHit.normal.x,
      cameraHit.normal.y,
      cameraHit.normal.z,
      chosenSource,
      outHit.fraction,
      outHit.normal.x,
      outHit.normal.y,
      outHit.normal.z,
      hasBlockingHit ? 1 : 0);
  return hasHit;
}

bool SceneTemplate::QueryTriggerTouch(const t850::CharacterTriggerQuery& query, t850::CharacterTriggerTouch& outTouch) const {
  (void)query;
  outTouch = t850::CharacterTriggerTouch{};
  return false;
}

void SceneTemplate::EnsureLightRuntimeState() {
  if (m_lightAttachToCamera.size() < SceneProp.Lights.size())
    m_lightAttachToCamera.resize(SceneProp.Lights.size(), false);
  else if (m_lightAttachToCamera.size() > SceneProp.Lights.size())
    m_lightAttachToCamera.resize(SceneProp.Lights.size());

  if (SceneProp.Lights.empty()) m_selectedLightIndex = 0;
  else if (m_selectedLightIndex < 0 || m_selectedLightIndex >= (int)SceneProp.Lights.size()) m_selectedLightIndex = 0;
  SceneProp.ActiveLights = (std::max)(0, (std::min)(SceneProp.ActiveLights, (int)SceneProp.Lights.size()));
}

void SceneTemplate::UpdateAttachedLights() {
  EnsureLightRuntimeState();
  Camera* attachCamera = ActiveCam ? ActiveCam : &Cam;
  for (int i = 0; i < (int)SceneProp.Lights.size(); ++i) {
    if (SceneProp.Lights[i].Type == LIGHT_POINT && m_lightAttachToCamera[i]) {
      SceneProp.Lights[i].Position = attachCamera->Eye;
    }
  }
}

void SceneTemplate::SyncLightCameraFromDirectionalLight() {
  for (const Light& light : SceneProp.Lights) {
    if (light.Type != LIGHT_DIRECTIONAL) continue;
    XVECTOR3 direction = light.Direction;
    if (direction.Length() <= 0.0001f) return;
    direction.Normalize();
    LightCam.SetLookAt(LightCam.Eye + direction);
    return;
  }
}

bool SceneTemplate::AdjustSelectedDirectionalLightFromMouse(float dx, float dy) {
  EnsureLightRuntimeState();
  if (SceneProp.Lights.empty()) return false;
  Light& light = SceneProp.Lights[m_selectedLightIndex];
  if (light.Type != LIGHT_DIRECTIONAL) return false;
  if (std::fabs(dx) < 0.001f && std::fabs(dy) < 0.001f) return true;

  XVECTOR3 direction = light.Direction;
  if (direction.Length() <= 0.0001f) direction = XVECTOR3(0.0f, -1.0f, 0.0f);
  direction.Normalize();

  const float sensitivity = 0.005f;
  direction += Cam.Right * (dx * sensitivity);
  direction += Cam.Up * (-dy * sensitivity);
  if (direction.Length() <= 0.0001f) return true;
  direction.Normalize();
  light.Direction = direction;
  SyncLightCameraFromDirectionalLight();
  return true;
}

void SceneTemplate::DrawSelectedDirectionalLightArrow() {
  EnsureLightRuntimeState();
  if (!m_drawLightDirection) return;
  if (SceneProp.Lights.empty() || !m_lightArrowRenderer.IsReady() || !m_lightArrowVB || !m_lightArrowIB) return;

  const Light& light = SceneProp.Lights[m_selectedLightIndex];
  if (light.Type != LIGHT_DIRECTIONAL) return;

  XVECTOR3 direction = light.Direction;
  if (direction.Length() <= 0.0001f) return;
  direction.Normalize();

  XVECTOR3 origin = m_orbitTarget + m_panOffset;
  float arrowLength = (std::max)(1.0f, m_modelRadius * 0.45f);
  float headLength = arrowLength * 0.22f;
  float headWidth = arrowLength * 0.08f;
  XVECTOR3 tip = origin + direction * arrowLength;

  XVECTOR3 side;
  XVecCross(side, Cam.Up, direction);
  if (side.Length() <= 0.0001f) XVecCross(side, XVECTOR3(0.0f, 1.0f, 0.0f), direction);
  if (side.Length() <= 0.0001f) XVecCross(side, XVECTOR3(1.0f, 0.0f, 0.0f), direction);
  side.Normalize();

  XVECTOR3 up;
  XVecCross(up, direction, side);
  up.Normalize();

  XVECTOR3 headBase = tip - direction * headLength;
  XVECTOR3 points[10] = {
    origin, tip,
    tip, headBase + side * headWidth,
    tip, headBase - side * headWidth,
    tip, headBase + up * headWidth,
    tip, headBase - up * headWidth,
  };

  float verts[10 * 4];
  for (int i = 0; i < 10; ++i) {
    verts[i * 4 + 0] = points[i].x;
    verts[i * 4 + 1] = points[i].y;
    verts[i * 4 + 2] = points[i].z;
    verts[i * 4 + 3] = 1.0f;
  }

  m_lightArrowVB->UpdateFromBuffer(*t850::T8DeviceContext, verts);
  XMATRIX44 identity;
  identity.Identity();
  m_lightArrowRenderer.SetDepthTestEnabled(false);
  pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
  pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
  m_lightArrowRenderer.DrawLines(identity, Cam.VP, XVECTOR3(1.0f, 0.82f, 0.25f, 1.0f),
                                 m_lightArrowVB, m_lightArrowIB, m_lightArrowIndexCount, 16,
                                 IndexBufferFormat::R16);
}

void SceneTemplate::CaptureSandboxProfileState(t850::SandboxProfileDesc& state) {
  state = t850::SandboxProfileDesc{};
  state.model = m_profileEmbeddedInScene
      ? std::string{}
      : (m_profileModelKey.empty() ? SandboxProfileModelKey(ActiveModelPath()) : m_profileModelKey);

  auto profileMeshPath = [&](int meshIndex) -> std::string {
    if (meshIndex >= 0 && meshIndex < static_cast<int>(m_sceneMeshPaths.size())) {
      return m_sceneMeshPaths[static_cast<std::size_t>(meshIndex)];
    }
    if (!m_profileModelKey.empty()) {
      return m_profileModelKey;
    }
    return SandboxProfileModelKey(ActiveModelPath());
  };

  auto addFloat = [&](const char* name, float value) {
    state.sliders.push_back({name, value});
  };
  auto addBool = [&](const char* name, bool value) {
    state.checkboxes.push_back({name, value});
  };
  auto addInt = [&](const char* name, int value) {
    state.selectors.push_back({name, value});
  };

  const t850::SelectorDesc* cubemapDesc = FindSelectorDesc(m_controlSetup.descriptor.selectors, "cubemap");
  std::string selectedCubemapPath = NormalizeSceneResourcePath(m_pendingCubemap.empty() ? m_currentCubemapPath : m_pendingCubemap);
  if (selectedCubemapPath.empty() && cubemapDesc) {
    selectedCubemapPath = CubemapPathForSelectorIndex(*cubemapDesc, m_currentCubemapIndex);
  }
  if (!selectedCubemapPath.empty()) {
    state.cubemap_path = selectedCubemapPath;
  }

  addFloat("exposure", SceneProp.Exposure);
  addFloat("bloom_factor", SceneProp.BloomFactor);
  addFloat("bloom_threshold", SceneProp.BloomThreshold);
  addFloat("tm_white_level", SceneProp.ToneMapWhiteLevel);
  addFloat("tm_adapt_tau", SceneProp.LuminanceTau);
  addFloat("shadow_map_resolution", SceneProp.ShadowMapResolution);
  addFloat("god_rays_resolution", SceneProp.GoodRaysResolution);
  addFloat("pcf_radius", SceneProp.PCFScale);
  addFloat("pcf_samples", SceneProp.PCFSamples);
  addFloat("ssao_kernel_size", (float)SceneProp.SSAOKernel.KernelSize);
  addFloat("ssao_radius", SceneProp.SSAOKernel.Radius);
  addFloat("dof_aperture", SceneProp.Aperture);
  addFloat("dof_focal_length", SceneProp.FocalLength);
  addFloat("dof_focus_depth", SceneProp.FocusDepth);
  addFloat("dof_max_coc", SceneProp.MaxCoc);
  addFloat("dof_far_samples", SceneProp.DOF_Far_Samples_squared);
  addFloat("dof_near_samples", SceneProp.DOF_Near_Samples_squared);
  addFloat("parallax_low_samples", SceneProp.ParallaxLowSamples);
  addFloat("parallax_high_samples", SceneProp.ParallaxHighSamples);
  addFloat("parallax_height", SceneProp.ParallaxHeight);
  addFloat("parallax_shadow_min_layers", SceneProp.ParallaxShadowMinLayers);
  addFloat("parallax_shadow_max_layers", SceneProp.ParallaxShadowMaxLayers);
  addFloat("parallax_shadow_softness", SceneProp.ParallaxShadowSoftness);
  addFloat("parallax_shadow_strength", SceneProp.ParallaxShadowStrength);
  addFloat("light_volume_steps", SceneProp.LightVolumeSteps);
  addFloat("godrays_factor", SceneProp.GodRaysFactor);
  addFloat("active_lights", static_cast<float>(SceneProp.ActiveLights));
  addFloat("fov", ActiveCam ? Rad2Deg(ActiveCam->Fov) : Rad2Deg(Cam.Fov));
  addFloat("light_radius_scale", SceneProp.LightRadiusScale);
  addFloat("light_intensity_scale", SceneProp.LightIntensityScale);
  addFloat("lightmap_intensity", SceneProp.LightmapIntensity);
  addFloat("shadow_bias", SceneProp.ShadowBias);
  addFloat("shadow_min", SceneProp.ShadowMin);
  addFloat("env_factor", SceneProp.EnvFactor);
  addFloat("ibl_factor", SceneProp.IBLFactor);
  addFloat("ibl_mip_count", SceneProp.IBLMipCount);
  addFloat("ibl_diffuse_mip_level", SceneProp.IBLDiffuseMipLevel);
  addFloat("ibl_brdf_lut_enabled", SceneProp.IBLBRDFLUTEnabled);
  addFloat("material_emissive_intensity", SceneProp.MaterialEmissiveIntensity);
  addFloat("material_transmission_multiplier", SceneProp.MaterialTransmissionMultiplier);
  addFloat("material_refraction_strength", SceneProp.MaterialRefractionStrength);
  addFloat("mouse_sensitivity_x", m_mouseSensitivityX);
  addFloat("mouse_sensitivity_y", m_mouseSensitivityY);
  addFloat("navmesh_debug_offset", m_navMeshDebugOffset);
  addFloat("nav_agent_speed_multiplier", m_navTestSpeed);
  addFloat("navmesh_cell_size", m_navMeshBuildSettings.cellSize);
  addFloat("navmesh_cell_height", m_navMeshBuildSettings.cellHeight);
  addFloat("navmesh_agent_height", m_navMeshBuildSettings.agentHeight);
  addFloat("navmesh_agent_radius", m_navMeshBuildSettings.agentRadius);
  addFloat("navmesh_agent_max_climb", m_navMeshBuildSettings.agentMaxClimb);
  addFloat("navmesh_agent_max_slope", m_navMeshBuildSettings.agentMaxSlope);
  addFloat("navmesh_region_min_size", m_navMeshBuildSettings.regionMinSize);
  addFloat("navmesh_region_merge_size", m_navMeshBuildSettings.regionMergeSize);
  addFloat("navmesh_edge_max_len", m_navMeshBuildSettings.edgeMaxLen);
  addFloat("navmesh_edge_max_error", m_navMeshBuildSettings.edgeMaxError);
  addFloat("navmesh_detail_sample_dist", m_navMeshBuildSettings.detailSampleDist);
  addFloat("navmesh_detail_sample_max_error", m_navMeshBuildSettings.detailSampleMaxError);
  addFloat("navmesh_query_extent_x", m_navMeshBuildSettings.queryExtents.x);
  addFloat("navmesh_query_extent_y", m_navMeshBuildSettings.queryExtents.y);
  addFloat("navmesh_query_extent_z", m_navMeshBuildSettings.queryExtents.z);
  addFloat("navmesh_drop_min_height", m_navMeshBuildSettings.dropLinkMinHeight);
  addFloat("navmesh_drop_max_height", m_navMeshBuildSettings.dropLinkMaxHeight);
  addFloat("navmesh_drop_max_horizontal", m_navMeshBuildSettings.dropLinkMaxHorizontalDistance);
  addFloat("navmesh_drop_sample_spacing", m_navMeshBuildSettings.dropLinkSampleSpacing);
  addFloat("navmesh_drop_link_radius", m_navMeshBuildSettings.dropLinkRadius);
  addFloat("navmesh_jump_max_horizontal", m_navMeshBuildSettings.jumpLinkMaxHorizontalDistance);
  addFloat("navmesh_jump_sample_spacing", m_navMeshBuildSettings.jumpLinkSampleSpacing);
  addFloat("navmesh_jump_link_radius", m_navMeshBuildSettings.jumpLinkRadius);

  for (int kernelIndex = 0; kernelIndex < (int)SceneProp.pGaussKernels.size(); ++kernelIndex) {
    GaussFilter* kernel = SceneProp.pGaussKernels[kernelIndex];
    if (!kernel) continue;
    std::string prefix = "gauss_" + std::to_string(kernelIndex) + "_";
    addFloat((prefix + "radius").c_str(), kernel->radius);
    addFloat((prefix + "sigma").c_str(), kernel->sigma);
    addInt((prefix + "kernel_size").c_str(), kernel->kernelSize);
  }

  if (RenderSkinnedMesh* skinned = GetSelectedAnimationMesh()) {
    addInt("animation_model", m_selectedAnimationMeshIndex);
    addFloat("anim_speed", skinned->GetAnimSpeed());
    addInt("anim_select", skinned->GetCurrentAnimSet());
    addInt("anim_mode", skinned->GetKeyframeMode() ? 1 : 0);
    if (skinned->GetKeyframeMode())
      state.current_keyframe = skinned->GetCurrentKeyframe();
  } else {
    addFloat("anim_speed", 1.0f);
    addInt("anim_select", 0);
    addInt("anim_mode", 0);
  }

  const int meshCount = GetRuntimeMeshCount();
  for (int meshIndex = 0; meshIndex < meshCount && meshIndex < kMaxSandboxMeshes; ++meshIndex) {
    RenderSkinnedMesh* skinned = GetSkinnedMeshForIndex(meshIndex);
    if (!skinned) {
      continue;
    }

    t850::SandboxAnimationOverrideDesc animation;
    animation.index = meshIndex;
    animation.mesh = profileMeshPath(meshIndex);
    animation.anim_speed = skinned->GetAnimSpeed();
    animation.anim_select = skinned->GetCurrentAnimSet();
    animation.anim_mode = skinned->GetKeyframeMode() ? 1 : 0;
    if (skinned->GetKeyframeMode()) {
      animation.current_keyframe = skinned->GetCurrentKeyframe();
    }
    state.animations.push_back(animation);
  }

  addBool("shadow_toggle", SceneProp.ToogleShadow != 0);
  addBool("ssao_toggle", SceneProp.ToogleSSAO != 0);
  addBool("dof_toggle", SceneProp.ToogleDOF != 0);
  addBool("dof_auto_focus", SceneProp.AutoFocus);
  addBool("parallax_toggle", SceneProp.ToogleParallax != 0);
  addBool("parallax_shadow_toggle", SceneProp.ToogleParallaxShadow != 0);
  addBool("godrays_toggle", SceneProp.ToogleGodRays != 0);
  addBool("show_wireframe", m_showWireframe);
  addBool("show_skeleton", GetSelectedSkinningMesh() != nullptr && m_showSkeleton);
  addBool("show_physics", m_showPhysics);
  addBool("show_navmesh", m_showNavMesh);
  addBool("show_light_volumes", m_showLightVolumes);
  addBool("point_lights_enabled", SceneProp.PointLightsEnabled);
  addBool("draw_direction", m_drawLightDirection);
  addBool("debug_luminance", SceneProp.DebugLuminanceEnabled);
  addBool("navmesh_auto_drop_links", m_navMeshBuildSettings.enableAutoDropLinks);
  addBool("navmesh_auto_jump_links", m_navMeshBuildSettings.enableAutoJumpLinks);
  addBool("navmesh_hybrid_jump_links", m_navMeshBuildSettings.enableHybridJumpLinks);

  addInt("debug_render_target", m_debugRTSelection);
  addInt("cubemap", m_currentCubemapIndex);
  addInt("luminance_mode", SceneProp.LuminanceMode);
  addInt("gauss_kernel_sample_count", 0);
  addInt("active_gauss_kernel", ChangeActiveGaussSelection);
  addInt("active_light", m_selectedLightIndex);
  addInt("navmesh_debug_shape", m_navMeshDebugShapeMode);
  addInt("nav_agent_mode", m_navTestMode);
  addInt("navmesh_verts_per_poly", m_navMeshBuildSettings.vertsPerPoly);
  addInt("navmesh_hybrid_max_links", m_navMeshBuildSettings.hybridJumpMaxLinks);

  EnsureLightRuntimeState();
  for (int lightIndex = 0; lightIndex < (int)SceneProp.Lights.size(); ++lightIndex) {
    const Light& light = SceneProp.Lights[lightIndex];
    t850::SandboxLightOverrideDesc lightState;
    lightState.index = lightIndex;
    lightState.position = ToArray(light.Position);
    lightState.direction = ToArray(light.Direction);
    lightState.color = ToArray(light.Color);
    lightState.diameter = light.radius * 2.0f;
    lightState.intensity = light.Intensity;
    lightState.attach_to_camera = light.Type == LIGHT_POINT && m_lightAttachToCamera[lightIndex];
    state.lights.push_back(lightState);
  }

  state.frustum_culling = SceneProp.FrustumCullingEnabled;
  state.show_culling_debug = m_showCullStats;

  if (m_cameraController.GetActiveProfileType() == t850::CameraProfileType::Orbit) {
    SyncSandboxOrbitFromProfile();
  }

  t850::SandboxOrbitCameraDesc orbit;
  orbit.target = {m_orbitTarget.x, m_orbitTarget.y, m_orbitTarget.z};
  orbit.pan_offset = {m_panOffset.x, m_panOffset.y, m_panOffset.z};
  orbit.eye = {Cam.Eye.x, Cam.Eye.y, Cam.Eye.z};
  orbit.yaw = m_orbitYaw;
  orbit.pitch = m_orbitPitch;
  orbit.distance = m_orbitDist;
  state.orbit_camera = orbit;

  Camera* profileCamera = ActiveCam ? ActiveCam : &Cam;
  t850::SandboxCameraDesc camera;
  camera.profile = m_cameraController.GetActiveProfileIndex();
  camera.eye = ToArray(profileCamera->Eye);
  camera.look = ToArray(profileCamera->Look);
  camera.up = ToArray(profileCamera->Up);
  camera.right = ToArray(profileCamera->Right);
  camera.yaw = profileCamera->Yaw;
  camera.pitch = profileCamera->Pitch;
  camera.roll = profileCamera->Roll;
  camera.fov = Rad2Deg(profileCamera->Fov);
  camera.aspect_ratio = profileCamera->AspectRatio;
  camera.near_plane = profileCamera->NPlane;
  camera.far_plane = profileCamera->FPlane;
  camera.ortho = profileCamera->Ortho;
  camera.width = profileCamera->Width;
  camera.height = profileCamera->Height;
  camera.left_handed = profileCamera->LeftHanded;
  camera.orbit = orbit;
  state.camera = camera;
}

void SceneTemplate::ApplySandboxProfileState(const t850::SandboxProfileDesc& state) {
  auto applyCubemapPath = [&](const std::string& cubemapPath) {
    const std::string normalizedPath = NormalizeSceneResourcePath(cubemapPath);
    if (normalizedPath.empty()) {
      return;
    }

    const t850::SelectorDesc* cubemapDesc = FindSelectorDesc(m_controlSetup.descriptor.selectors, "cubemap");
    if (cubemapDesc) {
      const int pathIndex = CubemapSelectorIndexForPath(*cubemapDesc, normalizedPath);
      if (pathIndex >= 0) {
        m_currentCubemapIndex = pathIndex;
      }
    }

    if (!ResourcePathEquals(normalizedPath, m_currentCubemapPath)) {
      m_pendingCubemap = normalizedPath;
    }
  };

  const bool hasCubemapPath = state.cubemap_path.has_value() &&
      !NormalizeSceneResourcePath(*state.cubemap_path).empty();
  if (hasCubemapPath) {
    applyCubemapPath(*state.cubemap_path);
  }

  auto profileMeshPath = [&](int meshIndex) -> std::string {
    if (meshIndex >= 0 && meshIndex < static_cast<int>(m_sceneMeshPaths.size())) {
      return m_sceneMeshPaths[static_cast<std::size_t>(meshIndex)];
    }
    if (!m_profileModelKey.empty()) {
      return m_profileModelKey;
    }
    return SandboxProfileModelKey(ActiveModelPath());
  };

  auto resolveAnimationMeshIndex = [&](const t850::SandboxAnimationOverrideDesc& animation) -> int {
    if (GetSkinnedMeshForIndex(animation.index) &&
        (animation.mesh.empty() || ResourcePathEquals(animation.mesh, profileMeshPath(animation.index)))) {
      return animation.index;
    }
    if (animation.mesh.empty()) {
      return -1;
    }

    int matchedMeshIndex = -1;
    const int meshCount = GetRuntimeMeshCount();
    for (int meshIndex = 0; meshIndex < meshCount && meshIndex < kMaxSandboxMeshes; ++meshIndex) {
      if (!GetSkinnedMeshForIndex(meshIndex) || !ResourcePathEquals(animation.mesh, profileMeshPath(meshIndex))) {
        continue;
      }
      if (matchedMeshIndex >= 0) {
        return -1;
      }
      matchedMeshIndex = meshIndex;
    }
    return matchedMeshIndex;
  };

  auto applyAnimationToMesh = [&](RenderSkinnedMesh* skinned,
                                  const t850::SandboxAnimationOverrideDesc& animation) {
    if (!skinned) {
      return;
    }
    skinned->SetAnimSpeed(animation.anim_speed);
    if (animation.anim_select >= 0 && animation.anim_select < skinned->GetNumAnimSets()) {
      int guard = skinned->GetNumAnimSets() + 1;
      while (skinned->GetCurrentAnimSet() != animation.anim_select && guard-- > 0) {
        skinned->NextAnimation();
      }
    }

    const bool keyframeMode = (animation.anim_mode == 1);
    skinned->SetKeyframeMode(keyframeMode);
    if (keyframeMode) {
      skinned->StepKeyframe(0);
    }
    if (animation.current_keyframe.has_value()) {
      const int targetKeyframe = *animation.current_keyframe;
      int guard = skinned->GetTotalKeyframes() + 1;
      while (skinned->GetCurrentKeyframe() != targetKeyframe && guard-- > 0) {
        const int direction = targetKeyframe > skinned->GetCurrentKeyframe() ? 1 : -1;
        skinned->StepKeyframe(direction);
      }
    }
  };

  for (const auto& value : state.selectors) {
    if (value.name == "animation_model" && GetSkinnedMeshForIndex(value.value)) {
      m_selectedAnimationMeshIndex = value.value;
    }
  }

  for (const auto& value : state.sliders) {
    if (value.name == "exposure") SceneProp.Exposure = value.value;
    else if (value.name == "bloom_factor") SceneProp.BloomFactor = value.value;
    else if (value.name == "bloom_threshold") SceneProp.BloomThreshold = value.value;
    else if (value.name == "tm_white_level") SceneProp.ToneMapWhiteLevel = value.value;
    else if (value.name == "tm_adapt_tau") SceneProp.LuminanceTau = value.value;
    else if (value.name == "shadow_map_resolution") SceneProp.ShadowMapResolution = value.value;
    else if (value.name == "god_rays_resolution") SceneProp.GoodRaysResolution = value.value;
    else if (value.name == "pcf_radius") SceneProp.PCFScale = value.value;
    else if (value.name == "pcf_samples") SceneProp.PCFSamples = value.value;
    else if (value.name == "ssao_kernel_size") { SceneProp.SSAOKernel.KernelSize = (int)value.value; SceneProp.SSAOKernel.Update(); }
    else if (value.name == "ssao_radius") SceneProp.SSAOKernel.Radius = value.value;
    else if (value.name == "dof_aperture") SceneProp.Aperture = value.value;
    else if (value.name == "dof_focal_length") SceneProp.FocalLength = value.value;
    else if (value.name == "dof_focus_depth") SceneProp.FocusDepth = value.value;
    else if (value.name == "dof_max_coc") SceneProp.MaxCoc = value.value;
    else if (value.name == "dof_far_samples") SceneProp.DOF_Far_Samples_squared = value.value;
    else if (value.name == "dof_near_samples") SceneProp.DOF_Near_Samples_squared = value.value;
    else if (value.name == "parallax_low_samples") SceneProp.ParallaxLowSamples = value.value;
    else if (value.name == "parallax_high_samples") SceneProp.ParallaxHighSamples = value.value;
    else if (value.name == "parallax_height") SceneProp.ParallaxHeight = value.value;
    else if (value.name == "parallax_shadow_min_layers") SceneProp.ParallaxShadowMinLayers = value.value;
    else if (value.name == "parallax_shadow_max_layers") SceneProp.ParallaxShadowMaxLayers = value.value;
    else if (value.name == "parallax_shadow_softness") SceneProp.ParallaxShadowSoftness = value.value;
    else if (value.name == "parallax_shadow_strength") SceneProp.ParallaxShadowStrength = value.value;
    else if (value.name == "light_volume_steps") SceneProp.LightVolumeSteps = value.value;
    else if (value.name == "godrays_factor") SceneProp.GodRaysFactor = value.value;
    else if (value.name == "active_lights") SceneProp.ActiveLights = (std::max)(0, static_cast<int>(std::round(value.value)));
    else if (value.name == "fov" && ActiveCam) { ActiveCam->SetFov(Deg2Rad(value.value)); VP = ActiveCam->VP; }
    else if (value.name == "light_intensity" && !SceneProp.Lights.empty()) SceneProp.Lights[0].Intensity = value.value;
    else if (value.name == "light_radius_scale") SceneProp.LightRadiusScale = value.value;
    else if (value.name == "light_intensity_scale") SceneProp.LightIntensityScale = value.value;
    else if (value.name == "lightmap_intensity") SceneProp.LightmapIntensity = value.value;
    else if (value.name == "shadow_bias") SceneProp.ShadowBias = value.value;
    else if (value.name == "shadow_min") SceneProp.ShadowMin = value.value;
    else if (value.name == "env_factor") SceneProp.EnvFactor = value.value;
    else if (value.name == "ibl_factor") SceneProp.IBLFactor = value.value;
    else if (value.name == "ibl_mip_count") SceneProp.IBLMipCount = (std::max)(0.0f, value.value);
    else if (value.name == "ibl_diffuse_mip_level") SceneProp.IBLDiffuseMipLevel = (std::max)(0.0f, value.value);
    else if (value.name == "ibl_brdf_lut_enabled") SceneProp.IBLBRDFLUTEnabled = value.value > 0.5f ? 1.0f : 0.0f;
    else if (value.name == "material_emissive_intensity") SceneProp.MaterialEmissiveIntensity = value.value;
    else if (value.name == "material_transmission_multiplier") SceneProp.MaterialTransmissionMultiplier = value.value;
    else if (value.name == "material_refraction_strength") SceneProp.MaterialRefractionStrength = value.value;
    else if (value.name == "mouse_sensitivity_x") m_mouseSensitivityX = ClampMouseSensitivity(value.value);
    else if (value.name == "mouse_sensitivity_y") m_mouseSensitivityY = ClampMouseSensitivity(value.value);
    else if (value.name == "navmesh_debug_offset") m_navMeshDebugOffset = (std::max)(0.0f, (std::min)(0.25f, value.value));
    else if (value.name == "nav_agent_speed_multiplier") m_navTestSpeed = (std::max)(0.0f, (std::min)(10.0f, value.value));
    else if (value.name == "navmesh_cell_size") { m_navMeshBuildSettings.cellSize = (std::max)(0.0f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_cell_height") { m_navMeshBuildSettings.cellHeight = (std::max)(0.0f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_agent_height") { m_navMeshBuildSettings.agentHeight = (std::max)(0.1f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_agent_radius") { m_navMeshBuildSettings.agentRadius = (std::max)(0.0f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_agent_max_climb") { m_navMeshBuildSettings.agentMaxClimb = (std::max)(0.0f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_agent_max_slope") { m_navMeshBuildSettings.agentMaxSlope = std::clamp(value.value, 0.0f, 89.0f); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_region_min_size") { m_navMeshBuildSettings.regionMinSize = (std::max)(0.0f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_region_merge_size") { m_navMeshBuildSettings.regionMergeSize = (std::max)(0.0f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_edge_max_len") { m_navMeshBuildSettings.edgeMaxLen = (std::max)(0.0f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_edge_max_error") { m_navMeshBuildSettings.edgeMaxError = (std::max)(0.0f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_detail_sample_dist") { m_navMeshBuildSettings.detailSampleDist = (std::max)(0.0f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_detail_sample_max_error") { m_navMeshBuildSettings.detailSampleMaxError = (std::max)(0.0f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_query_extent_x") { m_navMeshBuildSettings.queryExtents.x = (std::max)(0.05f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_query_extent_y") { m_navMeshBuildSettings.queryExtents.y = (std::max)(0.05f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_query_extent_z") { m_navMeshBuildSettings.queryExtents.z = (std::max)(0.05f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_drop_min_height") { m_navMeshBuildSettings.dropLinkMinHeight = (std::max)(0.0f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_drop_max_height") { m_navMeshBuildSettings.dropLinkMaxHeight = (std::max)(0.0f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_drop_max_horizontal") { m_navMeshBuildSettings.dropLinkMaxHorizontalDistance = (std::max)(0.0f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_drop_sample_spacing") { m_navMeshBuildSettings.dropLinkSampleSpacing = (std::max)(0.05f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_drop_link_radius") { m_navMeshBuildSettings.dropLinkRadius = (std::max)(0.05f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_jump_max_horizontal") { m_navMeshBuildSettings.jumpLinkMaxHorizontalDistance = (std::max)(0.0f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_jump_sample_spacing") { m_navMeshBuildSettings.jumpLinkSampleSpacing = (std::max)(0.05f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_jump_link_radius") { m_navMeshBuildSettings.jumpLinkRadius = (std::max)(0.05f, value.value); m_navMeshBuildAttempted = false; }
    else if (value.name == "anim_speed") { if (RenderSkinnedMesh* skinned = GetSelectedAnimationMesh()) skinned->SetAnimSpeed(value.value); }

    for (int kernelIndex = 0; kernelIndex < (int)SceneProp.pGaussKernels.size(); ++kernelIndex) {
      GaussFilter* kernel = SceneProp.pGaussKernels[kernelIndex];
      if (!kernel) continue;
      std::string prefix = "gauss_" + std::to_string(kernelIndex) + "_";
      if (value.name == prefix + "radius") { kernel->radius = value.value; kernel->Update(); }
      else if (value.name == prefix + "sigma") { kernel->sigma = value.value; kernel->Update(); }
    }
  }

  for (const auto& value : state.checkboxes) {
    if (value.name == "shadow_toggle") SceneProp.ToogleShadow = value.value ? 1 : 0;
    else if (value.name == "ssao_toggle") SceneProp.ToogleSSAO = value.value ? 1 : 0;
    else if (value.name == "dof_toggle") SceneProp.ToogleDOF = value.value ? 1 : 0;
    else if (value.name == "dof_auto_focus") SceneProp.AutoFocus = value.value;
    else if (value.name == "parallax_toggle") SceneProp.ToogleParallax = value.value ? 1 : 0;
    else if (value.name == "parallax_shadow_toggle") SceneProp.ToogleParallaxShadow = value.value ? 1 : 0;
    else if (value.name == "godrays_toggle") SceneProp.ToogleGodRays = value.value ? 1 : 0;
    else if (value.name == "show_wireframe") m_showWireframe = value.value;
    else if (value.name == "show_skeleton") m_showSkeleton = value.value && (GetSelectedSkinningMesh() != nullptr);
    else if (value.name == "show_physics") m_showPhysics = value.value;
    else if (value.name == "show_navmesh") {
      m_showNavMesh = value.value;
      if (m_showNavMesh && !m_navMesh.IsReady()) m_navMeshBuildAttempted = false;
    }
    else if (value.name == "show_light_volumes") m_showLightVolumes = value.value;
    else if (value.name == "point_lights_enabled") SceneProp.PointLightsEnabled = value.value;
    else if (value.name == "draw_direction") m_drawLightDirection = value.value;
    else if (value.name == "debug_luminance") {
      SceneProp.DebugLuminanceEnabled = value.value;
      if (!value.value) SceneProp.DebugAdaptedLuminanceValid = false;
    }
    else if (value.name == "navmesh_auto_drop_links") { m_navMeshBuildSettings.enableAutoDropLinks = value.value; m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_auto_jump_links") { m_navMeshBuildSettings.enableAutoJumpLinks = value.value; m_navMeshBuildAttempted = false; }
    else if (value.name == "navmesh_hybrid_jump_links") { m_navMeshBuildSettings.enableHybridJumpLinks = value.value; m_navMeshBuildAttempted = false; }
  }

  for (const auto& value : state.selectors) {
    if (value.name == "debug_render_target") m_debugRTSelection = value.value;
    else if (value.name == "luminance_mode") SceneProp.LuminanceMode = value.value;
    else if (value.name == "active_light") m_selectedLightIndex = value.value;
    else if (value.name == "cubemap") {
      const t850::SelectorDesc* cubemapDesc = hasCubemapPath
          ? nullptr
          : FindSelectorDesc(m_controlSetup.descriptor.selectors, "cubemap");
      if (cubemapDesc && value.value >= 0 && value.value < (int)cubemapDesc->options.size()) {
        const std::string profileCubemapPath = CubemapPathForSelectorIndex(*cubemapDesc, value.value);
        m_currentCubemapIndex = value.value;
        if (!profileCubemapPath.empty() && !ResourcePathEquals(profileCubemapPath, m_currentCubemapPath)) {
          m_pendingCubemap = profileCubemapPath;
        }
      }
    }
    else if (value.name == "active_gauss_kernel") ChangeActiveGaussSelection = value.value;
    else if (value.name == "navmesh_debug_shape") m_navMeshDebugShapeMode = (std::max)(0, (std::min)(1, value.value));
    else if (value.name == "nav_agent_mode") {
      m_navTestMode = ClampNavTestMode(value.value);
    }
    else if (value.name == "navmesh_verts_per_poly") {
      m_navMeshBuildSettings.vertsPerPoly = std::clamp(value.value, 3, 12);
      m_navMeshBuildAttempted = false;
    }
    else if (value.name == "navmesh_hybrid_max_links") {
      m_navMeshBuildSettings.hybridJumpMaxLinks = (std::max)(0, value.value);
      m_navMeshBuildAttempted = false;
    }
    else if (value.name == "animation_model") {
      if (GetSkinnedMeshForIndex(value.value)) {
        m_selectedAnimationMeshIndex = value.value;
      }
    }
    else if (value.name == "anim_select") {
      if (RenderSkinnedMesh* skinned = GetSelectedAnimationMesh()) {
        int guard = skinned->GetNumAnimSets() + 1;
        while (skinned->GetCurrentAnimSet() != value.value && guard-- > 0) skinned->NextAnimation();
      }
    }
    else if (value.name == "anim_mode") {
      if (RenderSkinnedMesh* skinned = GetSelectedAnimationMesh()) {
        bool keyframeMode = (value.value == 1);
        skinned->SetKeyframeMode(keyframeMode);
        if (keyframeMode) skinned->StepKeyframe(0);
      }
    }

    for (int kernelIndex = 0; kernelIndex < (int)SceneProp.pGaussKernels.size(); ++kernelIndex) {
      GaussFilter* kernel = SceneProp.pGaussKernels[kernelIndex];
      if (!kernel) continue;
      std::string name = "gauss_" + std::to_string(kernelIndex) + "_kernel_size";
      if (value.name == name) { kernel->kernelSize = value.value; kernel->Update(); }
    }
  }

  EnsureLightRuntimeState();
  for (const auto& lightState : state.lights) {
    if (lightState.index < 0 || lightState.index >= (int)SceneProp.Lights.size()) continue;
    Light& light = SceneProp.Lights[lightState.index];
    if (lightState.position.has_value()) light.Position = FromArray(*lightState.position);
    if (lightState.direction.has_value()) {
      XVECTOR3 direction = FromArray(*lightState.direction);
      if (direction.Length() > 0.0001f) {
        direction.Normalize();
        light.Direction = direction;
      }
    }
    if (lightState.color.has_value()) light.Color = FromArray(*lightState.color);
    if (lightState.diameter.has_value()) light.radius = (std::max)(0.001f, *lightState.diameter * 0.5f);
    if (lightState.intensity.has_value()) light.Intensity = *lightState.intensity;
    if (lightState.attach_to_camera.has_value() && light.Type == LIGHT_POINT)
      m_lightAttachToCamera[lightState.index] = *lightState.attach_to_camera;
  }
  UpdateAttachedLights();
  SyncLightCameraFromDirectionalLight();

  if (state.frustum_culling.has_value()) {
    if (!SceneProp.FrustumCullingToggleAllowed) {
      SceneProp.FrustumCullingEnabled = false;
    } else {
      SceneProp.FrustumCullingEnabled = *state.frustum_culling;
    }
  }
  if (state.show_culling_debug.has_value()) {
    m_showCullStats = *state.show_culling_debug;
    SceneProp.ShowCullingDebug = m_showCullStats;
  }

  auto applyCameraPoseProjection = [&](const t850::SandboxCameraDesc& cameraState) {
    Cam.Ortho = cameraState.ortho;
    Cam.Fov = Deg2Rad(cameraState.fov);
    const float liveWidth = (g_pBaseDriver && g_pBaseDriver->width > 0) ? static_cast<float>(g_pBaseDriver->width) : cameraState.width;
    const float liveHeight = (g_pBaseDriver && g_pBaseDriver->height > 0) ? static_cast<float>(g_pBaseDriver->height) : cameraState.height;
    Cam.AspectRatio = liveHeight > 0.0f ? liveWidth / liveHeight : cameraState.aspect_ratio;
    Cam.NPlane = cameraState.near_plane;
    Cam.FPlane = cameraState.far_plane;
    Cam.Width = liveWidth > 0.0f ? liveWidth : cameraState.width;
    Cam.Height = liveHeight > 0.0f ? liveHeight : cameraState.height;
    Cam.LeftHanded = cameraState.left_handed;
    Cam.Eye = FromArray(cameraState.eye);
    Cam.Eye.w = 1.0f;
    Cam.Yaw = cameraState.yaw;
    Cam.Pitch = cameraState.pitch;
    Cam.Roll = cameraState.roll;
    Cam.Velocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
    Cam.CreatePojection();
    Cam.Update(0.0f);
    VP = Cam.VP;
  };

  if (state.camera.has_value()) {
    const auto& cameraState = *state.camera;
    const t850::CameraProfileType profileType = t850::CameraProfileTypeFromIndex(cameraState.profile);
    if (cameraState.orbit.has_value()) {
      const auto& orbit = *cameraState.orbit;
      m_orbitTarget = XVECTOR3(orbit.target[0], orbit.target[1], orbit.target[2]);
      m_panOffset = XVECTOR3(orbit.pan_offset[0], orbit.pan_offset[1], orbit.pan_offset[2]);
      m_orbitYaw = orbit.yaw;
      m_orbitPitch = orbit.pitch;
      m_orbitDist = orbit.distance;
      SyncOrbitProfileFromSandbox();
    }
    applyCameraPoseProjection(cameraState);
    SetCameraProfile(profileType);
    if (profileType != t850::CameraProfileType::Orbit || !cameraState.orbit.has_value()) {
      applyCameraPoseProjection(cameraState);
    } else {
      VP = Cam.VP;
    }
    UpdateAttachedLights();
  } else if (state.orbit_camera.has_value()) {
    const auto& orbit = *state.orbit_camera;
    m_orbitTarget = XVECTOR3(orbit.target[0], orbit.target[1], orbit.target[2]);
    m_panOffset = XVECTOR3(orbit.pan_offset[0], orbit.pan_offset[1], orbit.pan_offset[2]);
    m_orbitYaw = orbit.yaw;
    m_orbitPitch = orbit.pitch;
    m_orbitDist = orbit.distance;
    Cam.Eye = XVECTOR3(orbit.eye[0], orbit.eye[1], orbit.eye[2]);
    ComputeOrbitCamera();
    VP = Cam.VP;
    UpdateAttachedLights();
  }
  if (state.current_keyframe.has_value()) {
    if (RenderSkinnedMesh* skinned = GetSelectedAnimationMesh()) {
      int targetKeyframe = *state.current_keyframe;
      int guard = skinned->GetTotalKeyframes() + 1;
      while (skinned->GetCurrentKeyframe() != targetKeyframe && guard-- > 0) {
        int direction = targetKeyframe > skinned->GetCurrentKeyframe() ? 1 : -1;
        skinned->StepKeyframe(direction);
      }
    }
  }

  for (const auto& animation : state.animations) {
    const int meshIndex = resolveAnimationMeshIndex(animation);
    if (meshIndex < 0) {
      T8_LOG_INFO("[SceneTemplate] Skipped profile animation for mesh slot %d path='%s'",
                  animation.index,
                  animation.mesh.c_str());
      continue;
    }
    applyAnimationToMesh(GetSkinnedMeshForIndex(meshIndex), animation);
  }
}

t850::SandboxProfileDesc SceneTemplate::BuildSparseSandboxProfile(const t850::SandboxProfileDesc& current) const {
  t850::SandboxProfileDesc sparse;
  sparse.name = current.name;
  sparse.platform = current.platform;
  sparse.architecture = current.architecture;
  sparse.gpu_family = current.gpu_family;
  sparse.gpu_name_contains = current.gpu_name_contains;
  sparse.model = current.model;
  for (const auto& value : current.sliders) {
    const auto* baseline = FindFloatOverride(m_profileBaselineState.sliders, value.name);
    if (!baseline || !NearlyEqual(value.value, baseline->value)) sparse.sliders.push_back(value);
  }
  for (const auto& value : current.checkboxes) {
    const auto* baseline = FindBoolOverride(m_profileBaselineState.checkboxes, value.name);
    if (!baseline || value.value != baseline->value) sparse.checkboxes.push_back(value);
  }
  for (const auto& value : current.selectors) {
    const auto* baseline = FindIntOverride(m_profileBaselineState.selectors, value.name);
    if (!baseline || value.value != baseline->value) sparse.selectors.push_back(value);
  }
  for (const auto& value : current.lights) {
    t850::SandboxLightOverrideDesc lightSparse;
    lightSparse.index = value.index;
    const auto* baseline = FindLightOverride(m_profileBaselineState.lights, value.index);
    if (!baseline) {
      lightSparse = value;
    } else {
      if (value.position.has_value() && (!baseline->position.has_value() || !VecNearlyEqual(*value.position, *baseline->position)))
        lightSparse.position = value.position;
      if (value.direction.has_value() && (!baseline->direction.has_value() || !VecNearlyEqual(*value.direction, *baseline->direction)))
        lightSparse.direction = value.direction;
      if (value.color.has_value() && (!baseline->color.has_value() || !VecNearlyEqual(*value.color, *baseline->color)))
        lightSparse.color = value.color;
      if (value.diameter.has_value() && (!baseline->diameter.has_value() || !NearlyEqual(*value.diameter, *baseline->diameter)))
        lightSparse.diameter = value.diameter;
      if (value.intensity.has_value() && (!baseline->intensity.has_value() || !NearlyEqual(*value.intensity, *baseline->intensity)))
        lightSparse.intensity = value.intensity;
      if (value.attach_to_camera.has_value() && (!baseline->attach_to_camera.has_value() || *value.attach_to_camera != *baseline->attach_to_camera))
        lightSparse.attach_to_camera = value.attach_to_camera;
      if (value.attach_to_camera.has_value() && *value.attach_to_camera) {
        lightSparse.position.reset();
      }
    }
    if (lightSparse.position.has_value() || lightSparse.direction.has_value() || lightSparse.color.has_value() ||
        lightSparse.diameter.has_value() || lightSparse.intensity.has_value() || lightSparse.attach_to_camera.has_value()) {
      sparse.lights.push_back(lightSparse);
    }
  }
  for (const auto& value : current.animations) {
    const auto* baseline = FindAnimationOverride(m_profileBaselineState.animations, value.index);
    if (!baseline || !AnimationOverrideNearlyEqual(value, *baseline)) {
      sparse.animations.push_back(value);
    }
  }
  if (current.cubemap_path != m_profileBaselineState.cubemap_path) sparse.cubemap_path = current.cubemap_path;
  if (current.frustum_culling != m_profileBaselineState.frustum_culling) sparse.frustum_culling = current.frustum_culling;
  if (current.show_culling_debug != m_profileBaselineState.show_culling_debug) sparse.show_culling_debug = current.show_culling_debug;
  if (current.current_keyframe != m_profileBaselineState.current_keyframe) sparse.current_keyframe = current.current_keyframe;
  if (current.camera.has_value() && m_profileBaselineState.camera.has_value()) {
    if (!CameraNearlyEqual(*current.camera, *m_profileBaselineState.camera)) {
      sparse.camera = current.camera;
    }
  } else if (current.camera != m_profileBaselineState.camera) {
    sparse.camera = current.camera;
  }
  if (current.orbit_camera.has_value() && m_profileBaselineState.orbit_camera.has_value()) {
    if (!OrbitCameraNearlyEqual(*current.orbit_camera, *m_profileBaselineState.orbit_camera)) {
      sparse.orbit_camera = current.orbit_camera;
    }
  } else if (current.orbit_camera != m_profileBaselineState.orbit_camera) {
    sparse.orbit_camera = current.orbit_camera;
  }
  return sparse;
}

bool SceneTemplate::SandboxProfileStatesEqual(const t850::SandboxProfileDesc& lhs, const t850::SandboxProfileDesc& rhs) const {
  const t850::SandboxProfileDesc lhsSparse = BuildSparseSandboxProfile(lhs);
  const t850::SandboxProfileDesc rhsSparse = BuildSparseSandboxProfile(rhs);
  return lhsSparse.sliders == rhsSparse.sliders &&
         lhsSparse.checkboxes == rhsSparse.checkboxes &&
         lhsSparse.selectors == rhsSparse.selectors &&
         lhsSparse.lights == rhsSparse.lights &&
         lhsSparse.animations == rhsSparse.animations &&
         lhsSparse.cubemap_path == rhsSparse.cubemap_path &&
         lhsSparse.camera == rhsSparse.camera &&
         lhsSparse.orbit_camera == rhsSparse.orbit_camera &&
         lhsSparse.frustum_culling == rhsSparse.frustum_culling &&
         lhsSparse.show_culling_debug == rhsSparse.show_culling_debug &&
         lhsSparse.current_keyframe == rhsSparse.current_keyframe;
}

void SceneTemplate::LoadSandboxProfile(bool embeddedInScene) {
  m_profileEmbeddedInScene = embeddedInScene;
  m_profileModelKey = embeddedInScene ? std::string{} : SandboxProfileModelKey(ActiveModelPath());
  m_selectedProfileTargetIndex = t850::DefaultProfileTargetIndex();
  CaptureSandboxProfileState(m_profileBaselineState);
  m_profileSavedState = m_profileBaselineState;
  m_profileReady = true;
  m_profileDirty = false;

  const t850::SandboxProfileDesc* baseProfile = nullptr;
  const t850::SandboxProfileDesc* runtimeProfile = nullptr;
  int bestRuntimeScore = -1;
  for (const auto& profile : m_controlSetup.descriptor.profiles) {
    const bool modelSpecific = !profile.model.empty();
    const bool modelMatches = embeddedInScene
        ? !modelSpecific
        : (!modelSpecific || SandboxProfileModelKey(profile.model) == m_profileModelKey);
    if (!modelMatches) continue;

    const bool hasTarget = !profile.name.empty() || !profile.platform.empty() || !profile.architecture.empty() ||
                           !profile.gpu_family.empty() || !profile.gpu_name_contains.empty();
    if (!hasTarget && (embeddedInScene || modelSpecific)) {
      baseProfile = &profile;
      continue;
    }

    int score = t850::ScoreSceneProfileMatch(profile, m_profileModelKey);
    if (score > bestRuntimeScore) {
      bestRuntimeScore = score;
      runtimeProfile = &profile;
    }
  }

  if (baseProfile) ApplySandboxProfileState(*baseProfile);
  if (runtimeProfile && runtimeProfile != baseProfile) ApplySandboxProfileState(*runtimeProfile);
  CaptureSandboxProfileState(m_profileSavedState);

  const auto& runtime = t850::GetRuntimeProfileInfo();
  T8_LOG_INFO("[SceneTemplate] Profile scope='%s' runtime='%s' platform=%s arch=%s gpu='%s' family=%s base=%d runtime=%d",
              embeddedInScene ? m_loadedEditorScenePath.c_str() : m_profileModelKey.c_str(),
              runtime.recommendedProfile.c_str(),
              runtime.platform.c_str(),
              runtime.architecture.c_str(),
              runtime.gpuName.c_str(), runtime.gpuFamily.c_str(), baseProfile ? 1 : 0, runtimeProfile ? 1 : 0);
}

void SceneTemplate::SaveSandboxProfile() {
  if (!m_profileReady) return;

  t850::SandboxProfileDesc current;
  CaptureSandboxProfileState(current);
  t850::SandboxProfileDesc sparse = BuildSparseSandboxProfile(current);
  t850::ApplyProfileTarget(sparse, m_selectedProfileTargetIndex);
  sparse.model = m_profileEmbeddedInScene ? std::string{} : m_profileModelKey;

  t850::SandboxProfileDesc target;
  t850::ApplyProfileTarget(target, m_selectedProfileTargetIndex);

  auto& profiles = m_controlSetup.descriptor.profiles;
  auto profileMatchesSaveTarget = [&](const t850::SandboxProfileDesc& profile) {
    const bool modelMatches = m_profileEmbeddedInScene
        ? profile.model.empty()
        : SandboxProfileModelKey(profile.model) == m_profileModelKey;
    const bool architectureMatches = target.architecture.empty() || profile.architecture == target.architecture;
    return modelMatches && profile.name == target.name &&
           profile.platform == target.platform && architectureMatches &&
           profile.gpu_family == target.gpu_family && profile.gpu_name_contains == target.gpu_name_contains;
  };
  auto existing = std::find_if(profiles.begin(), profiles.end(), profileMatchesSaveTarget);

  bool hasOverrides = !sparse.sliders.empty() || !sparse.checkboxes.empty() || !sparse.selectors.empty() ||
                      !sparse.lights.empty() || !sparse.animations.empty() ||
                      sparse.cubemap_path.has_value() ||
                      sparse.camera.has_value() || sparse.orbit_camera.has_value() || sparse.frustum_culling.has_value() ||
                      sparse.show_culling_debug.has_value() || sparse.current_keyframe.has_value();
  if (hasOverrides) {
    if (existing == profiles.end()) profiles.push_back(sparse);
    else {
      const size_t keepIndex = (size_t)std::distance(profiles.begin(), existing);
      *existing = sparse;
      for (size_t i = profiles.size(); i-- > 0;) {
        if (i != keepIndex && profileMatchesSaveTarget(profiles[i])) {
          profiles.erase(profiles.begin() + (std::ptrdiff_t)i);
        }
      }
    }
  } else if (existing != profiles.end()) {
    profiles.erase(std::remove_if(profiles.begin(), profiles.end(), profileMatchesSaveTarget), profiles.end());
  }

  bool saved = false;
  if (m_profileEmbeddedInScene) {
    t850::scene::EditorSceneFile scene;
    std::string error;
    if (t850::scene::LoadEditorSceneFile(m_loadedEditorScenePath, scene, &error)) {
      scene.profiles = profiles;
      saved = t850::scene::SaveEditorSceneFile(scene, m_loadedEditorScenePath, &error);
      if (!saved && !error.empty()) {
        T8_LOG_ERROR("[SceneTemplate] Failed to save scene profile '%s': %s",
                     m_loadedEditorScenePath.c_str(),
                     error.c_str());
      }
    } else {
      T8_LOG_ERROR("[SceneTemplate] Failed to reload scene '%s' for profile save: %s",
                   m_loadedEditorScenePath.c_str(),
                   error.c_str());
    }
  } else {
    saved = t850::SaveSceneDescriptor("Scenes/SceneTemplate.json", m_controlSetup.descriptor);
  }

  if (saved) {
    m_profileSavedState = current;
    m_profileDirty = false;
    T8_LOG_INFO("[SceneTemplate] Saved profile '%s' for %s '%s'",
                target.name.empty() ? "pc/base" : target.name.c_str(),
                m_profileEmbeddedInScene ? "scene" : "model",
                m_profileEmbeddedInScene ? m_loadedEditorScenePath.c_str() : m_profileModelKey.c_str());
  }
}

void SceneTemplate::OnDraw() {
  T8_TELEMETRY_SCOPE("sandbox.draw");
  SceneProp.ShowCullingDebug = m_showCullStats;
  static float sLuminanceReadbackAccum = 0.0f;
  if (g_sandboxConsoleOpen && SceneProp.DebugLuminanceEnabled) {
    sLuminanceReadbackAccum += DtSecs;
  } else {
    sLuminanceReadbackAccum = 0.0f;
    SceneProp.DebugAdaptedLuminanceValid = false;
  }
  if (g_sandboxConsoleOpen && SceneProp.DebugLuminanceEnabled && sLuminanceReadbackAccum >= 0.25f) {
    sLuminanceReadbackAccum = 0.0f;
    const int adaptedLumRT = m_renderGraph.GetRTHandle("AdaptedLumCurrent");
    float lumRGBA[4] = {};
    {
      T8_TELEMETRY_SCOPE("sandbox.adapted_luminance_readback");
      if (adaptedLumRT >= 0 &&
          pFramework && pFramework->pVideoDriver &&
          pFramework->pVideoDriver->ReadRTColorFloat(adaptedLumRT, BaseDriver::COLOR0_ATTACHMENT, lumRGBA)) {
        const float logLum = lumRGBA[0];
        if (std::isfinite(logLum) && logLum > -20.0f && logLum < 20.0f) {
          SceneProp.DebugAdaptedLuminance = std::exp(logLum);
          SceneProp.DebugAdaptedLuminanceValid = true;
        }
      }
    }
  }

  // FPS logging (every 120 frames)
  static int sFrameCount = 0;
  static float sAccumTime = 0.0f;
  sAccumTime += DtSecs;
  sFrameCount++;
  if (sFrameCount % 120 == 0) {
    float avgFps = (sAccumTime > 0.0f) ? (float)sFrameCount / sAccumTime : 0.0f;
    T8_LOG_INFO("[FPS] %.1f fps (avg over %d frames, dt=%.3f ms)",
                avgFps, sFrameCount, DtSecs * 1000.0f);
  }

  const int drawMeshCount = (std::max)(m_meshCount, Meshes[0].pBase ? 1 : 0);
  UpdateSceneSkeletonsFromRagdollPhysics();
  for (int meshIndex = 0; meshIndex < drawMeshCount; ++meshIndex) {
    if (!Meshes[meshIndex].pBase) continue;
    Meshes[meshIndex].SetParallaxSettings(SceneProp.ParallaxLowSamples,
                                           SceneProp.ParallaxHighSamples,
                                           SceneProp.ParallaxHeight);
    Meshes[meshIndex].SetParallaxEnabled(SceneProp.ToogleParallax != 0);
    Meshes[meshIndex].SetParallaxShadowSettings(SceneProp.ParallaxShadowMinLayers,
                                                 SceneProp.ParallaxShadowMaxLayers,
                                                 SceneProp.ParallaxShadowSoftness,
                                                 SceneProp.ParallaxShadowStrength);
    Meshes[meshIndex].SetParallaxShadowEnabled(SceneProp.ToogleParallaxShadow != 0);
    RenderSkinnedMesh* skinned = Meshes[meshIndex].GetSkinnedMesh();
    if (skinned && skinned->HasSkinData()) {
      if (meshIndex == 0) {
        UpdateSkeletonFromRagdollPhysics();
      }
      skinned->UploadBoneTexture();
    }
  }

  // Execute the render graph (all passes through HDR Composition)
  m_renderGraph.Execute(
    pFramework->pVideoDriver,
    SceneProp,
    Meshes, drawMeshCount,
    Quads,
    &Cam,
    &LightCam,
    nullptr,
    EnvMaps,
    m_finalOutputRT
  );

  // RT Dump via FrameDumper
  if (m_dumper.ShouldDump(DtSecs)) {
    t850::SnapshotSkinnedJson skinnedSnapshot;
    const t850::SnapshotSkinnedJson* skinnedSnapshotPtr = nullptr;
    if (RenderSkinnedMesh* skinned = GetSelectedAnimationMesh()) {
      skinnedSnapshot = CaptureSkinnedSnapshot(skinned, m_showWireframe, m_showSkeleton);
      if (skinnedSnapshot.has_skin)
        skinnedSnapshotPtr = &skinnedSnapshot;
    }

    std::vector<t850::RTDumpEntry> rts = {
      {GBufferPass,           BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Albedo"},
      {GBufferPass,           BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"},
      {GBufferPass,           BaseDriver::COLOR2_ATTACHMENT, "GBuffer_PBR"},
      {GBufferPass,           BaseDriver::COLOR3_ATTACHMENT, "GBuffer_GeoNormal"},
      {GBufferPass,           BaseDriver::COLOR4_ATTACHMENT, "GBuffer_Emissive"},
      {GBufferPass,           BaseDriver::COLOR5_ATTACHMENT, "GBuffer_Sheen"},
      {GBufferPass,           BaseDriver::COLOR6_ATTACHMENT, "GBuffer_SpecularOcclusion"},
      {GBufferPass,           BaseDriver::DEPTH_ATTACHMENT,  "GBuffer_Depth"},
      {DepthPass,             BaseDriver::DEPTH_ATTACHMENT,  "ShadowMap_Depth"},
      {ShadowAccumPass,       BaseDriver::COLOR0_ATTACHMENT, "ShadowAccum"},
      {DeferredPass,          BaseDriver::COLOR0_ATTACHMENT, "Deferred"},
      {Extra16FPass,          BaseDriver::COLOR0_ATTACHMENT, "Extra16F"},
      {ExtraHelperPass,       BaseDriver::COLOR0_ATTACHMENT, "HDR_Final"},
      {BloomAccumPass,        BaseDriver::COLOR0_ATTACHMENT, "Bloom"},
      {AdaptedLumCurrentPass, BaseDriver::COLOR0_ATTACHMENT, "AdaptedLumCurrent"},
    };
    m_dumper.DumpFrame(pFramework->pVideoDriver, Cam, LightCam, SceneProp, rts, DtSecs,
                       nullptr, nullptr, skinnedSnapshotPtr);
    if (m_dumper.ShouldExit()) exit(0);
  }

  const int overlayW = RenderViewportWidth();
  const int overlayH = RenderViewportHeight();
  bool containerOverlayTargetBound = false;
  if (m_finalOutputRT >= 0 && pFramework && pFramework->pVideoDriver) {
    pFramework->pVideoDriver->PushRTLoad(m_finalOutputRT);
    pFramework->pVideoDriver->SetViewport(0.0f, 0.0f, static_cast<float>(overlayW), static_cast<float>(overlayH));
    pFramework->pVideoDriver->SetScissorRect(0, 0, overlayW, overlayH);
    containerOverlayTargetBound = true;
  }

  // Normal final output is emitted by the render graph BackBuffer pass.
  if (m_debugRTSelection > 0) {
    int selected = ExtraHelperPass;
    int attachment = BaseDriver::COLOR0_ATTACHMENT;

    switch (m_debugRTSelection) {
    case 1:  selected = GBufferPass;     attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 2:  selected = GBufferPass;     attachment = BaseDriver::COLOR1_ATTACHMENT; break;
    case 3:  selected = GBufferPass;     attachment = BaseDriver::COLOR2_ATTACHMENT; break;
    case 4:  selected = GBufferPass;     attachment = BaseDriver::COLOR3_ATTACHMENT; break;
    case 5:  selected = GBufferPass;     attachment = BaseDriver::DEPTH_ATTACHMENT;  break;
    case 6:  selected = DepthPass;       attachment = BaseDriver::DEPTH_ATTACHMENT;  break;
    case 7:  selected = ShadowAccumPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 8:  selected = DeferredPass;    attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 9:  selected = Extra16FPass;    attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 10: selected = ExtraHelperPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 11: selected = BloomAccumPass;  attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 12: selected = AdaptedLumCurrentPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    }

    pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
    Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(selected, attachment), 0);
    ShaderKey finalKey(0);
    finalKey.setPass(PassType::FSQUAD_1_TEX);
    finalKey.bits |= ShaderKey::HAS_TEXCOORD0;
    Quads[0].SetGlobalKey(finalKey);
    Quads[0].Draw();
  }

  auto drawMeshDebugOverlays = [this, overlayW, overlayH]() {
    const int overlayMeshCount = (std::max)(m_meshCount, Meshes[0].pBase ? 1 : 0);
    for (int meshIndex = 0; meshIndex < overlayMeshCount; ++meshIndex) {
      if (!Meshes[meshIndex].pBase) {
        continue;
      }
      RenderSkinnedMesh* skinned = dynamic_cast<RenderSkinnedMesh*>(Meshes[meshIndex].pBase);
      if (skinned && skinned->HasSkinData()) {
        skinned->transform = Meshes[meshIndex].Final;
        if (m_showWireframe) {
          int gbufHandle = GBufferPass;
          if (gbufHandle >= 0 && gbufHandle < (int)pFramework->pVideoDriver->RTs.size()) {
            auto* gbufRT = pFramework->pVideoDriver->RTs[gbufHandle];
            skinned->SetWireframeDepthTex(gbufRT->pDepthTexture);
          }
          skinned->SetWireframeViewport(overlayW, overlayH);
          pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
          skinned->DrawWireframe();
        }
        if (m_showSkeleton) {
          pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
          pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
          const std::vector<int>* controlledBones = nullptr;
          std::vector<int> previewBones;
          const std::vector<int>* previewBoneList = nullptr;
          const std::vector<int>* pendingBoneList = nullptr;
          if (meshIndex == 0 &&
              m_skeletonEditMode &&
              m_ragdollEditSelectedCapsule >= 0 &&
              m_ragdollEditSelectedCapsule < static_cast<int>(m_ragdollAnimationBinding.controlledBoneIndices.size())) {
            controlledBones = &m_ragdollAnimationBinding.controlledBoneIndices[static_cast<std::size_t>(m_ragdollEditSelectedCapsule)];
            if (m_ragdollEditSelectedUnassignedBone >= 0 &&
                FindRagdollCapsuleControllingBone(m_ragdollEditSelectedUnassignedBone) < 0) {
              previewBones.push_back(m_ragdollEditSelectedUnassignedBone);
              previewBoneList = &previewBones;
            }
            if (m_ragdollBoneSelectionActive && !m_ragdollBoneSelectionPending.empty()) {
              pendingBoneList = &m_ragdollBoneSelectionPending;
            }
          }
          int selectedSkeletonBone = (meshIndex == m_selectedSkinningMeshIndex && m_skeletonEditMode) ? m_skeletonEditSelectedBone : -1;
          if (m_ragdollBoneSelectionActive) {
            selectedSkeletonBone = -1;
          } else if (previewBoneList && selectedSkeletonBone == m_ragdollEditSelectedUnassignedBone) {
            selectedSkeletonBone = -1;
          }
          if (pendingBoneList &&
              std::find(pendingBoneList->begin(), pendingBoneList->end(), selectedSkeletonBone) != pendingBoneList->end()) {
            selectedSkeletonBone = -1;
          }
          skinned->DrawSkeleton(selectedSkeletonBone, controlledBones, previewBoneList, pendingBoneList);
        }
      } else if (m_showWireframe) {
        RenderMesh* mesh = dynamic_cast<RenderMesh*>(Meshes[meshIndex].pBase);
        if (!mesh) {
          continue;
        }
        mesh->transform = Meshes[meshIndex].Final;
        int gbufHandle = GBufferPass;
        if (gbufHandle >= 0 && gbufHandle < (int)pFramework->pVideoDriver->RTs.size()) {
          auto* gbufRT = pFramework->pVideoDriver->RTs[gbufHandle];
          mesh->SetWireframeDepthTex(gbufRT->pDepthTexture);
        }
        mesh->SetWireframeViewport(overlayW, overlayH);
        pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
        mesh->DrawWireframe();
      }
    }

    if (m_showNavMesh && m_navMeshDebugRenderer.IsReady() && EnsureNavMeshBuilt()) {
      Texture* depthTexture = nullptr;
      Texture* secondaryDepthTexture = nullptr;
      if (GBufferPass >= 0 && GBufferPass < (int)pFramework->pVideoDriver->RTs.size()) {
        if (auto* gbufRT = pFramework->pVideoDriver->RTs[GBufferPass]) {
          depthTexture = gbufRT->pDepthTexture;
        }
      }
      if (DeferredPass >= 0 && DeferredPass < (int)pFramework->pVideoDriver->RTs.size()) {
        if (auto* deferredRT = pFramework->pVideoDriver->RTs[DeferredPass]) {
          secondaryDepthTexture = deferredRT->pDepthTexture;
        }
      }
      if (depthTexture || secondaryDepthTexture) {
        m_navMeshDebugRenderer.SetVerticalOffset(m_navMeshDebugOffset);
        m_navMeshDebugRenderer.SetGraphVerticalOffset(m_navMeshDebugOffset + 0.005f);
        m_navMeshDebugRenderer.SetShapeMode(m_navMeshDebugShapeMode == 1
            ? t850::navigation::NavMeshDebugShapeMode::Nodes
            : t850::navigation::NavMeshDebugShapeMode::Geometry);
        m_navMeshDebugRenderer.SetDepthTexture(depthTexture);
        m_navMeshDebugRenderer.SetSecondaryDepthTexture(secondaryDepthTexture);
        m_navMeshDebugRenderer.SetViewport(overlayW, overlayH);
        m_navMeshDebugRenderer.SetFarPlane(Cam.FPlane);
        pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
        pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
        m_navMeshDebugRenderer.Draw(m_navMesh, VP);
      }
    }

    if (m_showPhysics) {
      t850::EngineContext* engineContext = GetEngineContext();
      if (!engineContext) engineContext = &t850::GetEngineContext();
      if (engineContext && engineContext->physics && m_physicsDebugRenderer.IsReady()) {
        Texture* depthTexture = nullptr;
        Texture* secondaryDepthTexture = nullptr;
        if (GBufferPass >= 0 && GBufferPass < (int)pFramework->pVideoDriver->RTs.size()) {
          if (auto* gbufRT = pFramework->pVideoDriver->RTs[GBufferPass]) {
            depthTexture = gbufRT->pDepthTexture;
          }
        }
        if (DeferredPass >= 0 && DeferredPass < (int)pFramework->pVideoDriver->RTs.size()) {
          if (auto* deferredRT = pFramework->pVideoDriver->RTs[DeferredPass]) {
            secondaryDepthTexture = deferredRT->pDepthTexture;
          }
        }
        if (depthTexture || secondaryDepthTexture) {
          m_physicsDebugRenderer.SetDepthTexture(depthTexture);
          m_physicsDebugRenderer.SetSecondaryDepthTexture(secondaryDepthTexture);
          m_physicsDebugRenderer.SetDepthTestEnabled(true);
          m_physicsDebugRenderer.SetViewport(overlayW, overlayH);
          m_physicsDebugRenderer.SetFarPlane(Cam.FPlane);
          pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
          pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
          m_physicsDebugRenderer.Draw(*engineContext->physics, VP);
        }
      }
    }

    if (m_showLightVolumes) {
      pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
      pFramework->pVideoDriver->SetBlendState(BaseDriver::ALPHA_BLEND);

      unsigned int numLights = SceneProp.ActiveLights;
      if (numLights > SceneProp.Lights.size())
        numLights = static_cast<unsigned int>(SceneProp.Lights.size());
      if (numLights > 128u)
        numLights = 128u;

      for (unsigned int i = 0; i < numLights; ++i) {
        const Light& light = SceneProp.Lights[i];
        if (light.Type != LIGHT_POINT || !light.Enabled || !SceneProp.PointLightsEnabled)
          continue;

        const float effectiveIntensity = light.Intensity * (std::max)(0.0f, SceneProp.LightIntensityScale);
        const float effectiveRadius = light.radius * (std::max)(0.0f, SceneProp.LightRadiusScale);
        const float shaderRange = effectiveRadius * 2.0f;
        if (effectiveIntensity <= 0.0f || shaderRange <= 0.0f)
          continue;

        XVECTOR3 color((std::min)(light.Color.x, 1.0f),
                       (std::min)(light.Color.y, 1.0f),
                       (std::min)(light.Color.z, 1.0f),
                       0.65f);
        m_debugSphere.Draw(VP, light.Position, shaderRange, color);
      }

      pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
    }
  };

#ifdef OS_ANDROID
  if (m_showWireframe || m_showSkeleton || m_showPhysics || m_showNavMesh || m_showLightVolumes) {
    pFramework->pVideoDriver->SetPrePresentOverlayCallback(drawMeshDebugOverlays);
  }
#else
  drawMeshDebugOverlays();
#endif

  DrawSelectedDirectionalLightArrow();
  if (m_skeletonEditMode &&
      m_ragdollBoneSelectionActive &&
      m_ragdollBoneMarqueeDragging &&
      ImGui::GetCurrentContext()) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport) {
      ImDrawList* drawList = ImGui::GetBackgroundDrawList(viewport);
      const ImVec2 start(m_ragdollBoneMarqueeStartX, m_ragdollBoneMarqueeStartY);
      const ImVec2 current(m_ragdollBoneMarqueeCurrentX, m_ragdollBoneMarqueeCurrentY);
      drawList->AddRectFilled(start, current, IM_COL32(255, 0, 255, 36));
      drawList->AddRect(start, current, IM_COL32(255, 0, 255, 220), 0.0f, 0, 1.6f);
    }
  }
  if (m_skeletonEditMode &&
      m_ragdollEditSelectionMode == kRagdollSelectCapsules &&
      m_ragdollEditGizmoMode == kRagdollToolEditCapsule &&
      m_ragdollEditSelectedCapsule >= 0 &&
      !IsRagdollCapsuleFrozen(m_ragdollEditSelectedCapsule)) {
    std::array<XVECTOR3, 7> capsuleHandles;
    if (BuildRagdollEditHandlePoints(m_ragdollEditSelectedCapsule, capsuleHandles)) {
      pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
      pFramework->pVideoDriver->SetBlendState(BaseDriver::ALPHA_BLEND);
      const float baseRadius = (std::max)(0.01f, m_modelRadius * 0.014f);
      for (int handleIndex = 0; handleIndex < static_cast<int>(capsuleHandles.size()); ++handleIndex) {
        const float radius = handleIndex == m_ragdollEditSelectedHandle ? baseRadius * 1.45f : baseRadius;
        m_debugSphere.Draw(VP, capsuleHandles[static_cast<std::size_t>(handleIndex)], radius);
      }
      pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
    }
  }
  // Debug: draw wireframe AABBs for visible meshes
  if (m_showAABBs && Meshes[0].pBase) {
    RenderMesh* rm = static_cast<RenderMesh*>(Meshes[0].pBase);
    XVECTOR3 frustumPlanes[6];
    RenderMesh::ExtractFrustumPlanes(Cam.VP, frustumPlanes);

    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
    pFramework->pVideoDriver->SetBlendState(BaseDriver::ALPHA_BLEND);
    for (std::size_t i = 0; i < rm->Info.size(); i++) {
      RenderMesh::AABB& box = rm->Info[i].bounds;
      if (!RenderMesh::AABBInsideFrustum(box, rm->transform, frustumPlanes))
        continue;
      XVECTOR3 center((box.min.x+box.max.x)*0.5f, (box.min.y+box.max.y)*0.5f, (box.min.z+box.max.z)*0.5f);
      float ex = (box.max.x-box.min.x)*0.5f;
      float ey = (box.max.y-box.min.y)*0.5f;
      float ez = (box.max.z-box.min.z)*0.5f;
      float radius = std::sqrt(ex*ex + ey*ey + ez*ez);
      m_debugSphere.Draw(VP, center, radius);
    }
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_DEFAULT);
    pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
  }

  // Debug: on-screen cull stats
  if (m_showCullStats && Meshes[0].pBase) {
    RenderMesh* rm = static_cast<RenderMesh*>(Meshes[0].pBase);
    int w = overlayW;
    int h = overlayH;

    pFramework->pVideoDriver->SetBlendState(BaseDriver::ALPHA_BLEND);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);

    char buf[256];
    XVECTOR3 yellow(1.0f, 1.0f, 0.2f);
    XVECTOR3 gray(0.7f, 0.7f, 0.7f);
    const float statScale = 0.56f;
    const float lineHeight = 34.0f * statScale * ((float)h / 720.0f);
    const float bottomMargin = 26.0f * ((float)h / 720.0f);
    float y = (float)h - bottomMargin - lineHeight * 4.0f;
    auto drawCenteredStat = [&](const XVECTOR3& color, const char* text) {
      float textW = m_debugText.MeasurePixel(text, w, h) * statScale;
      float x = ((float)w - textW) * 0.5f;
      m_debugText.DrawPixelScaled(x, y, statScale, statScale, w, h, color, text);
      y += lineHeight;
    };

    snprintf(buf, sizeof(buf), "Meshes: %d/%d  culled %d",
            rm->m_visibleMeshes, rm->m_totalMeshes, rm->m_culledMeshes);
    drawCenteredStat(yellow, buf);

    snprintf(buf, sizeof(buf), "Subsets: %d/%d  culled %d  drawn %d",
            rm->m_visibleSubsets, rm->m_totalSubsets, rm->m_culledSubsets, rm->m_drawnSubsets);
    drawCenteredStat(yellow, buf);

    snprintf(buf, sizeof(buf), "Clusters: %d/%d  culled %d  drawn %d",
            rm->m_visibleClusters, rm->m_totalClusters, rm->m_culledClusters, rm->m_drawnClusters);
    drawCenteredStat(yellow, buf);

        snprintf(buf, sizeof(buf), "GBuffer indices: %llu/%llu  6/KP6: culling %s  F2: stats  F3: AABBs  K: cam pos",
          rm->m_drawnIndices, rm->m_totalIndices,
          SceneProp.FrustumCullingEnabled ? "ON" : "OFF");
    drawCenteredStat(gray, buf);

    pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_DEFAULT);
  }

  if (containerOverlayTargetBound && pFramework && pFramework->pVideoDriver) {
    pFramework->pVideoDriver->PopRT();
  }
}

void SceneTemplate::DrawDevGui(t850::DevGuiContext& gui) {
  if (m_controlSetup.descriptor.name.empty()) {
    m_controlSetup.Load("Scenes/SceneTemplate.json");
  }

  struct Mapping { const char* name; int settingIndex; };

  static const Mapping sliderMappings[] = {
    {"exposure", CHANGE_EXPOSURE},
    {"bloom_factor", CHANGE_BLOOM_FACTOR},
    {"bloom_threshold", CHANGE_BLOOM_THRESHOLD},
    {"tm_white_level", CHANGE_TM_WHITE_LEVEL},
    {"tm_adapt_tau", CHANGE_TM_ADAPT_TAU},
    {"pcf_radius", CHANGE_PCF_RADIUS},
    {"pcf_samples", CHANGE_PCF_SAMPLES},
    {"ssao_kernel_size", CHANGE_SSAO_KERNEL_SIZE},
    {"ssao_radius", CHANGE_SSAO_RADIUS},
    {"dof_aperture", CHANGE_DOF_APERTURE},
    {"dof_focal_length", CHANGE_DOF_FOCAL_LENGHT},
    {"dof_max_coc", CHANGE_DOF_MAX_COC},
    {"dof_far_samples", CHANGE_DOF_FAR_SAMPLE},
    {"dof_near_samples", CHANGE_DOF_NEAR_SAMPLE},
    {"light_volume_steps", CHANGE_LIGHT_VOLUME_STEPS},
    {"godrays_factor", CHANGE_GODRAYS_FACTOR},
    {"gauss_kernel_radius", CHANGE_GAUSS_KERNEL_RADIUS},
    {"gauss_kernel_deviation", CHANGE_GAUSS_KERNEL_DEVIATION},
    {"fov", CHANGE_FOV},
    {"light_radius_scale", CHANGE_LIGHT_RADIUS_SCALE},
    {"light_intensity_scale", CHANGE_LIGHT_INTENSITY_SCALE},
    {"lightmap_intensity", CHANGE_LIGHTMAP_INTENSITY},
    {"shadow_bias", CHANGE_SHADOW_BIAS},
    {"shadow_min", CHANGE_SHADOW_MIN},
    {"env_factor", CHANGE_ENV_FACTOR},
    {"ibl_factor", CHANGE_IBL_FACTOR},
    {"material_emissive_intensity", CHANGE_MATERIAL_EMISSIVE_INTENSITY},
    {"material_transmission_multiplier", CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER},
    {"material_refraction_strength", CHANGE_MATERIAL_REFRACTION_STRENGTH},
    {"anim_speed", CHANGE_ANIM_SPEED},
  };

  static const Mapping checkboxMappings[] = {
    {"shadow_toggle", CHANGE_PCF_TOOGLE},
    {"ssao_toggle", CHANGLE_SSAO_TOOGLE},
    {"show_wireframe", CHANGE_SHOW_WIREFRAME},
    {"show_skeleton", CHANGE_SHOW_SKELETON},
    {"show_physics", CHANGE_SHOW_PHYSICS},
    {"show_navmesh", CHANGE_SHOW_NAVMESH},
    {"show_light_volumes", CHANGE_SHOW_LIGHT_VOLUMES},
    {"point_lights_enabled", CHANGE_POINT_LIGHTS_ENABLED},
    {"debug_luminance", CHANGE_DEBUG_LUMINANCE},
  };

  static const Mapping selectorMappings[] = {
    {"debug_render_target", CHANGE_DEBUG_RT},
    {"cubemap", CHANGE_CUBEMAP},
    {"gauss_kernel_sample_count", CHANGE_GAUSS_KERNEL_SAMPLE_COUNT},
    {"active_gauss_kernel", CHANGE_ACTIVE_GAUSS_KERNEL},
    {"luminance_mode", CHANGE_LUMINANCE_MODE},
    {"anim_select", CHANGE_ANIM_SELECT},
    {"anim_mode", CHANGE_ANIM_MODE},
  };

  auto findSetting = [](const std::string& name, const Mapping* mappings, int count) {
    for (int i = 0; i < count; ++i) {
      if (name == mappings[i].name) return mappings[i].settingIndex;
    }
    return -1;
  };

  auto activeKernel = [&]() -> GaussFilter* {
    if (ChangeActiveGaussSelection < 0 || ChangeActiveGaussSelection >= (int)SceneProp.pGaussKernels.size()) return nullptr;
    return SceneProp.pGaussKernels[ChangeActiveGaussSelection];
  };

  auto skinnedMesh = [&]() -> RenderSkinnedMesh* {
    m_selectedAnimationMeshIndex = ClampSkinnedMeshSelection(m_selectedAnimationMeshIndex);
    return GetSkinnedMeshForIndex(m_selectedAnimationMeshIndex);
  };

  auto buildAnimationOptions = [&]() {
    std::vector<std::string> options;
    RenderSkinnedMesh* skinned = skinnedMesh();
    if (skinned && skinned->HasSkinData()) {
      int numSets = skinned->GetNumAnimSets();
      for (int i = 0; i < numSets; ++i) {
        if (skinned->xFile && !skinned->xFile->XMeshDataBase.empty()) {
          auto& anims = skinned->xFile->XMeshDataBase[0]->Animation.Animations;
          if (i < (int)anims.size() && !anims[i].Name.empty()) {
            options.push_back(anims[i].Name);
            continue;
          }
        }
        options.push_back("Anim " + std::to_string(i));
      }
    }
    if (options.empty()) options.push_back("None");
    return options;
  };

  auto getSliderValue = [&](int settingIndex, float& value) -> bool {
    GaussFilter* kernel = activeKernel();
    switch (settingIndex) {
    case CHANGE_EXPOSURE: value = SceneProp.Exposure; return true;
    case CHANGE_BLOOM_FACTOR: value = SceneProp.BloomFactor; return true;
    case CHANGE_BLOOM_THRESHOLD: value = SceneProp.BloomThreshold; return true;
    case CHANGE_TM_WHITE_LEVEL: value = SceneProp.ToneMapWhiteLevel; return true;
    case CHANGE_TM_ADAPT_TAU: value = SceneProp.LuminanceTau; return true;
    case CHANGE_PCF_RADIUS: value = SceneProp.PCFScale; return true;
    case CHANGE_PCF_SAMPLES: value = SceneProp.PCFSamples; return true;
    case CHANGE_SSAO_KERNEL_SIZE: value = (float)SceneProp.SSAOKernel.KernelSize; return true;
    case CHANGE_SSAO_RADIUS: value = SceneProp.SSAOKernel.Radius; return true;
    case CHANGE_DOF_APERTURE: value = SceneProp.Aperture; return true;
    case CHANGE_DOF_FOCAL_LENGHT: value = SceneProp.FocalLength; return true;
    case CHANGE_DOF_MAX_COC: value = SceneProp.MaxCoc; return true;
    case CHANGE_DOF_FAR_SAMPLE: value = SceneProp.DOF_Far_Samples_squared; return true;
    case CHANGE_DOF_NEAR_SAMPLE: value = SceneProp.DOF_Near_Samples_squared; return true;
    case CHANGE_LIGHT_VOLUME_STEPS: value = SceneProp.LightVolumeSteps; return true;
    case CHANGE_GODRAYS_FACTOR: value = SceneProp.GodRaysFactor; return true;
    case CHANGE_GAUSS_KERNEL_RADIUS: if (!kernel) return false; value = kernel->radius; return true;
    case CHANGE_GAUSS_KERNEL_DEVIATION: if (!kernel) return false; value = kernel->sigma; return true;
    case CHANGE_FOV: if (!ActiveCam) return false; value = Rad2Deg(ActiveCam->Fov); return true;
    case CHANGE_LIGHT_INTENSITY: if (SceneProp.Lights.empty()) return false; value = SceneProp.Lights[0].Intensity; return true;
    case CHANGE_LIGHT_RADIUS_SCALE: value = SceneProp.LightRadiusScale; return true;
    case CHANGE_LIGHT_INTENSITY_SCALE: value = SceneProp.LightIntensityScale; return true;
    case CHANGE_LIGHTMAP_INTENSITY: value = SceneProp.LightmapIntensity; return true;
    case CHANGE_SHADOW_BIAS: value = SceneProp.ShadowBias; return true;
    case CHANGE_SHADOW_MIN: value = SceneProp.ShadowMin; return true;
    case CHANGE_ENV_FACTOR: value = SceneProp.EnvFactor; return true;
    case CHANGE_IBL_FACTOR: value = SceneProp.IBLFactor; return true;
    case CHANGE_MATERIAL_EMISSIVE_INTENSITY: value = SceneProp.MaterialEmissiveIntensity; return true;
    case CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER: value = SceneProp.MaterialTransmissionMultiplier; return true;
    case CHANGE_MATERIAL_REFRACTION_STRENGTH: value = SceneProp.MaterialRefractionStrength; return true;
    case CHANGE_ANIM_SPEED: if (RenderSkinnedMesh* sk = skinnedMesh()) { value = sk->GetAnimSpeed(); return true; } return false;
    }
    return false;
  };

  auto setSliderValue = [&](int settingIndex, float value) {
    GaussFilter* kernel = activeKernel();
    switch (settingIndex) {
    case CHANGE_EXPOSURE: SceneProp.Exposure = value; break;
    case CHANGE_BLOOM_FACTOR: SceneProp.BloomFactor = value; break;
    case CHANGE_BLOOM_THRESHOLD: SceneProp.BloomThreshold = value; break;
    case CHANGE_TM_WHITE_LEVEL: SceneProp.ToneMapWhiteLevel = value; break;
    case CHANGE_TM_ADAPT_TAU: SceneProp.LuminanceTau = value; break;
    case CHANGE_PCF_RADIUS: SceneProp.PCFScale = value; break;
    case CHANGE_PCF_SAMPLES: SceneProp.PCFSamples = value; break;
    case CHANGE_SSAO_KERNEL_SIZE: SceneProp.SSAOKernel.KernelSize = (int)value; SceneProp.SSAOKernel.Update(); break;
    case CHANGE_SSAO_RADIUS: SceneProp.SSAOKernel.Radius = value; break;
    case CHANGE_DOF_APERTURE: SceneProp.Aperture = value; break;
    case CHANGE_DOF_FOCAL_LENGHT: SceneProp.FocalLength = value; break;
    case CHANGE_DOF_MAX_COC: SceneProp.MaxCoc = value; break;
    case CHANGE_DOF_FAR_SAMPLE: SceneProp.DOF_Far_Samples_squared = value; break;
    case CHANGE_DOF_NEAR_SAMPLE: SceneProp.DOF_Near_Samples_squared = value; break;
    case CHANGE_LIGHT_VOLUME_STEPS: SceneProp.LightVolumeSteps = value; break;
    case CHANGE_GODRAYS_FACTOR: SceneProp.GodRaysFactor = value; break;
    case CHANGE_GAUSS_KERNEL_RADIUS: if (kernel) { kernel->radius = value; kernel->Update(); } break;
    case CHANGE_GAUSS_KERNEL_DEVIATION: if (kernel) { kernel->sigma = value; kernel->Update(); } break;
    case CHANGE_FOV:
      if (ActiveCam) {
        ActiveCam->SetFov(Deg2Rad(value));
        ActiveCam->VP = ActiveCam->View * ActiveCam->Projection;
        VP = ActiveCam->VP;
      }
      break;
    case CHANGE_LIGHT_INTENSITY: if (!SceneProp.Lights.empty()) SceneProp.Lights[0].Intensity = value; break;
    case CHANGE_LIGHT_RADIUS_SCALE: SceneProp.LightRadiusScale = value; break;
    case CHANGE_LIGHT_INTENSITY_SCALE: SceneProp.LightIntensityScale = value; break;
    case CHANGE_LIGHTMAP_INTENSITY: SceneProp.LightmapIntensity = value; break;
    case CHANGE_SHADOW_BIAS: SceneProp.ShadowBias = value; break;
    case CHANGE_SHADOW_MIN: SceneProp.ShadowMin = value; break;
    case CHANGE_ENV_FACTOR: SceneProp.EnvFactor = value; break;
    case CHANGE_IBL_FACTOR: SceneProp.IBLFactor = value; break;
    case CHANGE_MATERIAL_EMISSIVE_INTENSITY: SceneProp.MaterialEmissiveIntensity = value; break;
    case CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER: SceneProp.MaterialTransmissionMultiplier = value; break;
    case CHANGE_MATERIAL_REFRACTION_STRENGTH: SceneProp.MaterialRefractionStrength = value; break;
    case CHANGE_ANIM_SPEED: if (RenderSkinnedMesh* sk = skinnedMesh()) sk->SetAnimSpeed(value); break;
    }
  };

  auto getCheckboxValue = [&](int settingIndex, bool& value) -> bool {
    switch (settingIndex) {
    case CHANGE_PCF_TOOGLE: value = (SceneProp.ToogleShadow != 0); return true;
    case CHANGLE_SSAO_TOOGLE: value = (SceneProp.ToogleSSAO != 0); return true;
    case CHANGE_SHOW_WIREFRAME: value = m_showWireframe; return true;
    case CHANGE_SHOW_SKELETON: value = (skinnedMesh() != nullptr) && m_showSkeleton; return true;
    case CHANGE_SHOW_PHYSICS: value = m_showPhysics; return true;
    case CHANGE_SHOW_NAVMESH: value = m_showNavMesh; return true;
    case CHANGE_SHOW_LIGHT_VOLUMES: value = m_showLightVolumes; return true;
    case CHANGE_POINT_LIGHTS_ENABLED: value = SceneProp.PointLightsEnabled; return true;
    case CHANGE_DEBUG_LUMINANCE: value = SceneProp.DebugLuminanceEnabled; return true;
    }
    return false;
  };

  auto setCheckboxValue = [&](int settingIndex, bool value) {
    switch (settingIndex) {
    case CHANGE_PCF_TOOGLE: SceneProp.ToogleShadow = value ? 1 : 0; break;
    case CHANGLE_SSAO_TOOGLE: SceneProp.ToogleSSAO = value ? 1 : 0; break;
    case CHANGE_SHOW_WIREFRAME: m_showWireframe = value; break;
    case CHANGE_SHOW_SKELETON: m_showSkeleton = value && (skinnedMesh() != nullptr); break;
    case CHANGE_SHOW_PHYSICS: m_showPhysics = value; break;
    case CHANGE_SHOW_NAVMESH:
      m_showNavMesh = value;
      if (m_showNavMesh && !m_navMesh.IsReady()) m_navMeshBuildAttempted = false;
      break;
    case CHANGE_SHOW_LIGHT_VOLUMES: m_showLightVolumes = value; break;
    case CHANGE_POINT_LIGHTS_ENABLED: SceneProp.PointLightsEnabled = value; break;
    case CHANGE_DEBUG_LUMINANCE: SceneProp.DebugLuminanceEnabled = value; if (!value) SceneProp.DebugAdaptedLuminanceValid = false; break;
    }
  };

  auto getSelectorIndex = [&](const t850::SelectorDesc& desc, int settingIndex, int& selectedIndex) -> bool {
    switch (settingIndex) {
    case CHANGE_DEBUG_RT: selectedIndex = m_debugRTSelection; return true;
    case CHANGE_CUBEMAP: selectedIndex = m_currentCubemapIndex; return true;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      GaussFilter* kernel = activeKernel();
      if (!kernel) return false;
      for (int i = 0; i < (int)desc.options.size(); ++i) {
        if (std::atoi(desc.options[i].c_str()) == kernel->kernelSize) { selectedIndex = i; return true; }
      }
      selectedIndex = desc.default_index;
      return true;
    }
    case CHANGE_ACTIVE_GAUSS_KERNEL: selectedIndex = ChangeActiveGaussSelection; return true;
    case CHANGE_LUMINANCE_MODE: selectedIndex = SceneProp.LuminanceMode; return true;
    case CHANGE_ANIM_SELECT: if (RenderSkinnedMesh* sk = skinnedMesh()) { selectedIndex = sk->GetCurrentAnimSet(); return true; } selectedIndex = 0; return true;
    case CHANGE_ANIM_MODE: if (RenderSkinnedMesh* sk = skinnedMesh()) { selectedIndex = sk->GetKeyframeMode() ? 1 : 0; return true; } selectedIndex = 0; return true;
    }
    return false;
  };

  auto setSelectorIndex = [&](const t850::SelectorDesc& desc, const std::vector<std::string>* options, int settingIndex, int selectedIndex) {
    const std::vector<std::string>& sourceOptions = options ? *options : desc.options;
    if (selectedIndex < 0 || selectedIndex >= (int)sourceOptions.size()) return;
    switch (settingIndex) {
    case CHANGE_DEBUG_RT: m_debugRTSelection = selectedIndex; break;
    case CHANGE_CUBEMAP:
      {
        const std::string selectedCubemapPath = "sky/" + sourceOptions[selectedIndex];
        const bool pathChanged = !ResourcePathEquals(selectedCubemapPath, m_currentCubemapPath);
        if (selectedIndex != m_currentCubemapIndex || pathChanged) {
          m_currentCubemapIndex = selectedIndex;
          m_pendingCubemap = selectedCubemapPath;
          T8_LOG_INFO("[SceneTemplate] Cubemap change queued: '%s'", m_pendingCubemap.c_str());
        }
      }
      break;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      GaussFilter* kernel = activeKernel();
      if (kernel) { kernel->kernelSize = std::atoi(sourceOptions[selectedIndex].c_str()); kernel->Update(); }
    } break;
    case CHANGE_ACTIVE_GAUSS_KERNEL: ChangeActiveGaussSelection = selectedIndex; break;
    case CHANGE_LUMINANCE_MODE: SceneProp.LuminanceMode = selectedIndex; break;
    case CHANGE_ANIM_SELECT:
      if (RenderSkinnedMesh* sk = skinnedMesh()) {
        int guard = sk->GetNumAnimSets() + 1;
        while (sk->GetCurrentAnimSet() != selectedIndex && guard-- > 0) {
          sk->NextAnimation();
        }
      }
      break;
    case CHANGE_ANIM_MODE:
      if (RenderSkinnedMesh* sk = skinnedMesh()) {
        bool keyMode = (selectedIndex == 1);
        sk->SetKeyframeMode(keyMode);
        if (keyMode) sk->StepKeyframe(0);
      }
      break;
    }
  };

  auto drawSliderByName = [&](const char* name) -> bool {
    for (const auto& desc : m_controlSetup.descriptor.sliders) {
      if (desc.name != name) continue;
      int settingIndex = findSetting(desc.name, sliderMappings, (int)(sizeof(sliderMappings) / sizeof(sliderMappings[0])));
      if (settingIndex < 0) return false;
      float value = 0.0f;
      if (getSliderValue(settingIndex, value) && gui.Slider(desc, value)) {
        setSliderValue(settingIndex, value);
      }
      return true;
    }
    return false;
  };

  auto drawCheckboxByName = [&](const char* name) -> bool {
    for (const auto& desc : m_controlSetup.descriptor.checkboxes) {
      if (desc.name != name) continue;
      int settingIndex = findSetting(desc.name, checkboxMappings, (int)(sizeof(checkboxMappings) / sizeof(checkboxMappings[0])));
      if (settingIndex < 0) return false;
      bool value = false;
      if (getCheckboxValue(settingIndex, value) && gui.Checkbox(desc, value)) {
        setCheckboxValue(settingIndex, value);
      }
      return true;
    }
    return false;
  };

  auto drawSelectorByName = [&](const char* name) -> bool {
    for (const auto& desc : m_controlSetup.descriptor.selectors) {
      if (desc.name != name) continue;
      int settingIndex = findSetting(desc.name, selectorMappings, (int)(sizeof(selectorMappings) / sizeof(selectorMappings[0])));
      if (settingIndex < 0) return false;
      std::vector<std::string> animOptions;
      const std::vector<std::string>* overrideOptions = nullptr;
      if (settingIndex == CHANGE_ANIM_SELECT) {
        animOptions = buildAnimationOptions();
        overrideOptions = &animOptions;
      }
      int selectedIndex = 0;
      if (getSelectorIndex(desc, settingIndex, selectedIndex) && gui.Combo(desc, selectedIndex, overrideOptions)) {
        setSelectorIndex(desc, overrideOptions, settingIndex, selectedIndex);
      }
      return true;
    }
    return false;
  };

  if (gui.BeginSection("Camera")) {
    std::vector<std::string> cameraOptions = t850::CameraProfileNames();
    t850::SelectorDesc cameraSelector;
    cameraSelector.name = "camera_profile";
    cameraSelector.label = "Camera Profile";
    cameraSelector.options = cameraOptions;
    cameraSelector.default_index = m_cameraProfileSelection;
    int selectedProfile = m_cameraProfileSelection;
    if (gui.Combo(cameraSelector, selectedProfile, &cameraOptions)) {
      SetCameraProfile(t850::CameraProfileTypeFromIndex(selectedProfile));
    }
    std::string activeCameraText = std::string("Active: ") +
        t850::CameraProfileName(m_cameraController.GetActiveProfileType()) +
        " (F9 cycles profiles)";
    gui.Text(activeCameraText.c_str());
    t850::SliderDesc sensitivityXDesc;
    sensitivityXDesc.name = "mouse_sensitivity_x";
    sensitivityXDesc.label = "Mouse sensitivity X";
    sensitivityXDesc.min_val = 0.05f;
    sensitivityXDesc.max_val = 5.0f;
    sensitivityXDesc.step = 0.01f;
    sensitivityXDesc.default_val = 1.0f;
    float sensitivityX = m_mouseSensitivityX;
    if (gui.Slider(sensitivityXDesc, sensitivityX)) {
      m_mouseSensitivityX = ClampMouseSensitivity(sensitivityX);
    }
    t850::SliderDesc sensitivityYDesc;
    sensitivityYDesc.name = "mouse_sensitivity_y";
    sensitivityYDesc.label = "Mouse sensitivity Y";
    sensitivityYDesc.min_val = 0.05f;
    sensitivityYDesc.max_val = 5.0f;
    sensitivityYDesc.step = 0.01f;
    sensitivityYDesc.default_val = 1.0f;
    float sensitivityY = m_mouseSensitivityY;
    if (gui.Slider(sensitivityYDesc, sensitivityY)) {
      m_mouseSensitivityY = ClampMouseSensitivity(sensitivityY);
    }
    ImGui::Checkbox("Show console output", &g_sandboxConsoleOpen);
    ImGui::TextUnformatted("Controls:");
    ImGui::BulletText("Orbit: left mouse rotate, right mouse pan, middle/scroll zoom");
    ImGui::BulletText("Free/Colliding Fly: WASD move, Q/E up/down, Shift sprint, mouse look");
    ImGui::BulletText("Grounded/COD/Q3 FPS: A/D strafe, W/S forward/back, Shift run, Space jump");
    ImGui::BulletText("F10 dumps a frame; Space no longer triggers dump/exit");
    if (ImGui::Button("Unstick camera")) {
      Cam.Eye += XVECTOR3(0.0f, 0.75f, 0.0f, 0.0f);
      Cam.Eye -= Cam.Look * 1.5f;
      Cam.Eye.w = 1.0f;
      Cam.Velocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
      Cam.Update(0.0f);
      VP = Cam.VP;
      T8_LOG_INFO("[SceneTemplate] Camera unstuck to position[%f,%f,%f]", Cam.Eye.x, Cam.Eye.y, Cam.Eye.z);
    }
  }

  if (gui.BeginSection("Rendering")) {
    drawSelectorByName("cubemap");
    drawSelectorByName("active_gauss_kernel");
    drawSelectorByName("gauss_kernel_sample_count");
    drawSelectorByName("luminance_mode");
    drawCheckboxByName("debug_luminance");
    drawSelectorByName("debug_render_target");
    drawCheckboxByName("shadow_toggle");
    drawCheckboxByName("ssao_toggle");
    for (const auto& desc : m_controlSetup.descriptor.sliders) {
      if (desc.name == "anim_speed") continue;
      int settingIndex = findSetting(desc.name, sliderMappings, (int)(sizeof(sliderMappings) / sizeof(sliderMappings[0])));
      if (settingIndex < 0) continue;
      float value = 0.0f;
      if (getSliderValue(settingIndex, value) && gui.Slider(desc, value)) {
        setSliderValue(settingIndex, value);
      }
    }
  }

  if (gui.BeginSection("Animation")) {
    std::vector<int> skinnedMeshIndices;
    std::vector<std::string> skinnedMeshOptions = BuildSkinnedMeshOptions(&skinnedMeshIndices);
    if (!skinnedMeshIndices.empty()) {
      m_selectedAnimationMeshIndex = ClampSkinnedMeshSelection(m_selectedAnimationMeshIndex);
      int selectedSkinnedOption = 0;
      for (int optionIndex = 0; optionIndex < static_cast<int>(skinnedMeshIndices.size()); ++optionIndex) {
        if (skinnedMeshIndices[static_cast<std::size_t>(optionIndex)] == m_selectedAnimationMeshIndex) {
          selectedSkinnedOption = optionIndex;
          break;
        }
      }
      t850::SelectorDesc skinnedSelector;
      skinnedSelector.name = "animation_model";
      skinnedSelector.label = "Animated Model";
      if (gui.Combo(skinnedSelector, selectedSkinnedOption, &skinnedMeshOptions) &&
          selectedSkinnedOption >= 0 &&
          selectedSkinnedOption < static_cast<int>(skinnedMeshIndices.size())) {
        m_selectedAnimationMeshIndex = skinnedMeshIndices[static_cast<std::size_t>(selectedSkinnedOption)];
      }
    }

    if (RenderSkinnedMesh* sk = skinnedMesh()) {
      if (m_skeletonEditMode) ImGui::BeginDisabled();
      if (gui.Button(sk->IsPlaying() ? "Pause Animation" : "Resume Animation")) {
        if (sk->IsPlaying()) sk->PauseAnimation();
        else sk->PlayAnimation();
      }
      if (m_skeletonEditMode) ImGui::EndDisabled();
    }
    drawSelectorByName("anim_select");
    drawSelectorByName("anim_mode");
    drawSliderByName("anim_speed");
  }

  if (gui.BeginSection("NavMesh")) {
    bool navRebuildRequested = false;
    auto requestNavRebuild = [&]() {
      navRebuildRequested = true;
    };
    auto rebuildNavMeshNow = [&](bool resetAgentsToSceneStart) {
      m_gameLogic.Navigation().PrepareForNavMeshMutation();
      m_navMesh.Clear();
      m_navMeshBuildAttempted = false;
      m_navMeshDebugRenderer.Invalidate();
      m_navTestInitialized = false;
      m_navTestAgents.clear();
      const bool rebuilt = EnsureNavMeshBuilt();
      if (rebuilt && resetAgentsToSceneStart) {
        InitializeNavTestAgents();
      }
      T8_LOG_INFO("[Navigation] Auto re-create requested: result=%s elapsed=%.2fms source=%s",
                  rebuilt ? "ok" : "failed",
                  m_navMeshLastBuildMs,
                  m_navMeshLastBuildFromCache ? "cache" : "build");
    };
    auto savedFloat = [&](const char* name, float fallback) {
      if (const auto* value = FindFloatOverride(m_profileSavedState.sliders, name)) {
        return value->value;
      }
      return fallback;
    };
    auto savedBool = [&](const char* name, bool fallback) {
      if (const auto* value = FindBoolOverride(m_profileSavedState.checkboxes, name)) {
        return value->value;
      }
      return fallback;
    };
    auto savedInt = [&](const char* name, int fallback) {
      if (const auto* value = FindIntOverride(m_profileSavedState.selectors, name)) {
        return value->value;
      }
      return fallback;
    };
    auto restoreSavedNavSettings = [&]() {
      t850::navigation::NavMeshBuildSettings defaults;
      m_navMeshDebugOffset = savedFloat("navmesh_debug_offset", 0.01f);
      m_navTestSpeed = savedFloat("nav_agent_speed_multiplier", 3.0f);
      m_navMeshBuildSettings.cellSize = savedFloat("navmesh_cell_size", defaults.cellSize);
      m_navMeshBuildSettings.cellHeight = savedFloat("navmesh_cell_height", defaults.cellHeight);
      m_navMeshBuildSettings.agentHeight = savedFloat("navmesh_agent_height", defaults.agentHeight);
      m_navMeshBuildSettings.agentRadius = savedFloat("navmesh_agent_radius", defaults.agentRadius);
      m_navMeshBuildSettings.agentMaxClimb = savedFloat("navmesh_agent_max_climb", defaults.agentMaxClimb);
      m_navMeshBuildSettings.agentMaxSlope = savedFloat("navmesh_agent_max_slope", defaults.agentMaxSlope);
      m_navMeshBuildSettings.regionMinSize = savedFloat("navmesh_region_min_size", defaults.regionMinSize);
      m_navMeshBuildSettings.regionMergeSize = savedFloat("navmesh_region_merge_size", defaults.regionMergeSize);
      m_navMeshBuildSettings.edgeMaxLen = savedFloat("navmesh_edge_max_len", defaults.edgeMaxLen);
      m_navMeshBuildSettings.edgeMaxError = savedFloat("navmesh_edge_max_error", defaults.edgeMaxError);
      m_navMeshBuildSettings.detailSampleDist = savedFloat("navmesh_detail_sample_dist", defaults.detailSampleDist);
      m_navMeshBuildSettings.detailSampleMaxError = savedFloat("navmesh_detail_sample_max_error", defaults.detailSampleMaxError);
      m_navMeshBuildSettings.queryExtents.x = savedFloat("navmesh_query_extent_x", defaults.queryExtents.x);
      m_navMeshBuildSettings.queryExtents.y = savedFloat("navmesh_query_extent_y", defaults.queryExtents.y);
      m_navMeshBuildSettings.queryExtents.z = savedFloat("navmesh_query_extent_z", defaults.queryExtents.z);
      m_navMeshBuildSettings.queryExtents.w = 0.0f;
      m_navMeshBuildSettings.dropLinkMinHeight = savedFloat("navmesh_drop_min_height", defaults.dropLinkMinHeight);
      m_navMeshBuildSettings.dropLinkMaxHeight = savedFloat("navmesh_drop_max_height", defaults.dropLinkMaxHeight);
      m_navMeshBuildSettings.dropLinkMaxHorizontalDistance = savedFloat("navmesh_drop_max_horizontal", defaults.dropLinkMaxHorizontalDistance);
      m_navMeshBuildSettings.dropLinkSampleSpacing = savedFloat("navmesh_drop_sample_spacing", defaults.dropLinkSampleSpacing);
      m_navMeshBuildSettings.dropLinkRadius = savedFloat("navmesh_drop_link_radius", defaults.dropLinkRadius);
      m_navMeshBuildSettings.jumpLinkMaxHorizontalDistance = savedFloat("navmesh_jump_max_horizontal", defaults.jumpLinkMaxHorizontalDistance);
      m_navMeshBuildSettings.jumpLinkSampleSpacing = savedFloat("navmesh_jump_sample_spacing", defaults.jumpLinkSampleSpacing);
      m_navMeshBuildSettings.jumpLinkRadius = savedFloat("navmesh_jump_link_radius", defaults.jumpLinkRadius);
      m_navMeshBuildSettings.enableAutoDropLinks = savedBool("navmesh_auto_drop_links", defaults.enableAutoDropLinks);
      m_navMeshBuildSettings.enableAutoJumpLinks = savedBool("navmesh_auto_jump_links", defaults.enableAutoJumpLinks);
      m_navMeshBuildSettings.enableHybridJumpLinks = savedBool("navmesh_hybrid_jump_links", defaults.enableHybridJumpLinks);
      m_navTestMode = ClampNavTestMode(savedInt("nav_agent_mode", kNavTestModeFollowPlayer));
      m_navMeshDebugShapeMode = std::clamp(savedInt("navmesh_debug_shape", 0), 0, 1);
      m_navMeshBuildSettings.vertsPerPoly = std::clamp(savedInt("navmesh_verts_per_poly", defaults.vertsPerPoly), 3, 12);
      m_navMeshBuildSettings.hybridJumpMaxLinks = (std::max)(0, savedInt("navmesh_hybrid_max_links", defaults.hybridJumpMaxLinks));
    };
    auto navSlider = [&](const char* label, float& value, float minValue, float maxValue, const char* format = "%.3f") {
      ImGui::SetNextItemWidth(240.0f);
      const bool changed = ImGui::SliderFloat(label, &value, minValue, maxValue, format);
      if (changed) {
        value = std::clamp(value, minValue, maxValue);
      }
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        requestNavRebuild();
      }
    };
    auto navSliderInt = [&](const char* label, int& value, int minValue, int maxValue) {
      ImGui::SetNextItemWidth(240.0f);
      const bool changed = ImGui::SliderInt(label, &value, minValue, maxValue);
      if (changed) {
        value = std::clamp(value, minValue, maxValue);
      }
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        requestNavRebuild();
      }
    };
    if (gui.Button(m_showNavMesh ? "Display: On" : "Display: Off")) {
      m_showNavMesh = !m_showNavMesh;
      if (m_showNavMesh && !m_navMesh.IsReady()) m_navMeshBuildAttempted = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Default")) {
      restoreSavedNavSettings();
      navRebuildRequested = true;
      rebuildNavMeshNow(true);
      navRebuildRequested = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Q3 preset")) {
      m_navMeshBuildSettings.cellSize = 0.10f;
      m_navMeshBuildSettings.cellHeight = 0.05f;
      m_navMeshBuildSettings.agentHeight = 56.0f / 32.0f;
      m_navMeshBuildSettings.agentRadius = 15.0f / 32.0f;
      m_navMeshBuildSettings.agentMaxClimb = 18.0f / 32.0f;
      m_navMeshBuildSettings.agentMaxSlope = 45.0f;
      m_navMeshBuildSettings.regionMinSize = 4.0f;
      m_navMeshBuildSettings.regionMergeSize = 12.0f;
      m_navMeshBuildSettings.edgeMaxLen = 4.0f;
      m_navMeshBuildSettings.edgeMaxError = 0.35f;
      m_navMeshBuildSettings.detailSampleDist = 1.0f;
      m_navMeshBuildSettings.detailSampleMaxError = 0.25f;
      m_navMeshBuildSettings.queryExtents = XVECTOR3(2.0f, 4.0f, 2.0f, 0.0f);
      navRebuildRequested = true;
    }
    if (m_navMesh.IsReady()) {
      const t850::navigation::NavMeshBuildStats& stats = m_navMesh.GetStats();
      ImGui::Text("Ready: polys=%d verts=%d tris=%d offMesh=%d drop=%d jump=%d jumpPad=%d",
                  stats.polygonCount,
                  stats.vertexCount,
                  stats.triangleCount,
                  stats.offMeshLinkCount,
                  stats.dropLinkCount,
                  stats.jumpLinkCount,
                  stats.jumpPadLinkCount);
      ImGui::Text("Last %s: %.2f ms", m_navMeshLastBuildFromCache ? "cache load" : "build", m_navMeshLastBuildMs);
    } else {
      ImGui::TextDisabled("NavMesh is not built. Change a value, press Default/Q3 preset, or enable display.");
    }
    ImGui::TextDisabled("Slider changes rebuild when released; cache keys include these values. Default restores saved profile values.");

    const char* navModeOptions[] = { "Furthest loop", "Random nodes", "Follow player" };
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("Bot behavior", &m_navTestMode, navModeOptions, 3)) {
      m_navTestMode = ClampNavTestMode(m_navTestMode);
      m_navTestInitialized = false;
      m_navTestAgents.clear();
    }
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::SliderFloat("Bot speed multiplier", &m_navTestSpeed, 0.0f, 10.0f, "%.2fx")) {
      m_navTestSpeed = (std::max)(0.0f, (std::min)(10.0f, m_navTestSpeed));
    }
    const char* navShapeOptions[] = { "Geometry", "Nodes" };
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("Debug shape", &m_navMeshDebugShapeMode, navShapeOptions, 2);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::SliderFloat("Debug vertical offset", &m_navMeshDebugOffset, 0.0f, 0.25f, "%.3f")) {
      m_navMeshDebugOffset = (std::max)(0.0f, (std::min)(0.25f, m_navMeshDebugOffset));
    }

    if (ImGui::CollapsingHeader("Rasterization", ImGuiTreeNodeFlags_DefaultOpen)) {
      navSlider("Cell size", m_navMeshBuildSettings.cellSize, 0.0f, 1.0f);
      navSlider("Cell height", m_navMeshBuildSettings.cellHeight, 0.0f, 1.0f);
    }
    if (ImGui::CollapsingHeader("Agent", ImGuiTreeNodeFlags_DefaultOpen)) {
      navSlider("Agent height", m_navMeshBuildSettings.agentHeight, 0.1f, 5.0f);
      navSlider("Agent radius", m_navMeshBuildSettings.agentRadius, 0.0f, 2.0f);
      navSlider("Agent max climb", m_navMeshBuildSettings.agentMaxClimb, 0.0f, 3.0f);
      navSlider("Agent max slope", m_navMeshBuildSettings.agentMaxSlope, 0.0f, 89.0f, "%.1f");
    }
    if (ImGui::CollapsingHeader("Regions and contours")) {
      navSlider("Region min size", m_navMeshBuildSettings.regionMinSize, 0.0f, 128.0f, "%.1f");
      navSlider("Region merge size", m_navMeshBuildSettings.regionMergeSize, 0.0f, 128.0f, "%.1f");
      navSlider("Edge max len", m_navMeshBuildSettings.edgeMaxLen, 0.0f, 64.0f, "%.1f");
      navSlider("Edge max error", m_navMeshBuildSettings.edgeMaxError, 0.0f, 8.0f, "%.2f");
      navSliderInt("Verts per poly", m_navMeshBuildSettings.vertsPerPoly, 3, 12);
    }
    if (ImGui::CollapsingHeader("Detail and queries")) {
      navSlider("Detail sample dist", m_navMeshBuildSettings.detailSampleDist, 0.0f, 16.0f, "%.2f");
      navSlider("Detail max error", m_navMeshBuildSettings.detailSampleMaxError, 0.0f, 8.0f, "%.2f");
      ImGui::SetNextItemWidth(300.0f);
      const bool queryChanged = ImGui::SliderFloat3("Query extents", &m_navMeshBuildSettings.queryExtents.x, 0.05f, 16.0f, "%.2f");
      if (queryChanged) {
        m_navMeshBuildSettings.queryExtents.x = (std::max)(0.05f, m_navMeshBuildSettings.queryExtents.x);
        m_navMeshBuildSettings.queryExtents.y = (std::max)(0.05f, m_navMeshBuildSettings.queryExtents.y);
        m_navMeshBuildSettings.queryExtents.z = (std::max)(0.05f, m_navMeshBuildSettings.queryExtents.z);
      }
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        requestNavRebuild();
      }
      m_navMeshBuildSettings.queryExtents.w = 0.0f;
    }
    ImGui::TextDisabled("Traversal links are not generated in SceneTemplate; use authored links once available.");
    if (navRebuildRequested) {
      rebuildNavMeshNow(false);
    }
  }

  if (gui.BeginSection("Debug Views")) {
    drawCheckboxByName("show_wireframe");
    drawCheckboxByName("show_skeleton");
    drawCheckboxByName("show_physics");
    drawCheckboxByName("show_light_volumes");
  }

  if (gui.BeginSection("Lights")) {
    t850::CheckboxDesc pointLightsDesc;
    pointLightsDesc.name = "point_lights_enabled";
    pointLightsDesc.label = "Dynamic point lights";
    bool pointLightsEnabled = SceneProp.PointLightsEnabled;
    if (gui.Checkbox(pointLightsDesc, pointLightsEnabled)) {
      SceneProp.PointLightsEnabled = pointLightsEnabled;
    }

    EnsureLightRuntimeState();
    if (SceneProp.Lights.empty()) {
      gui.Text("No lights");
    } else {
      std::vector<std::string> lightOptions;
      lightOptions.reserve(SceneProp.Lights.size());
      for (int i = 0; i < (int)SceneProp.Lights.size(); ++i) {
        const char* typeName = SceneProp.Lights[i].Type == LIGHT_DIRECTIONAL ? "Directional" : "Point";
        lightOptions.push_back(std::string(typeName) + " " + std::to_string(i + 1));
      }

      t850::SelectorDesc lightSelector;
      lightSelector.name = "active_light";
      lightSelector.label = "Light";
      int selectedLight = m_selectedLightIndex;
      if (gui.Combo(lightSelector, selectedLight, &lightOptions)) {
        m_selectedLightIndex = selectedLight;
      }
      EnsureLightRuntimeState();

      Light& light = SceneProp.Lights[m_selectedLightIndex];
      ImGui::PushID(m_selectedLightIndex);

      float color[3] = {light.Color.x, light.Color.y, light.Color.z};
      if (ImGui::ColorEdit3("Color", color, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_PickerHueBar)) {
        light.Color = XVECTOR3(color[0], color[1], color[2]);
      }

      const struct { const char* name; float rgb[3]; } palette[] = {
        {"White", {1.0f, 1.0f, 1.0f}},
        {"Warm", {1.0f, 0.84f, 0.58f}},
        {"Cool", {0.62f, 0.74f, 1.0f}},
        {"Amber", {1.0f, 0.52f, 0.18f}},
        {"Red", {1.0f, 0.18f, 0.15f}},
        {"Green", {0.3f, 1.0f, 0.42f}},
        {"Blue", {0.2f, 0.45f, 1.0f}}
      };
      const int paletteCount = (int)(sizeof(palette) / sizeof(palette[0]));
      for (int paletteIndex = 0; paletteIndex < paletteCount; ++paletteIndex) {
        if (paletteIndex > 0) ImGui::SameLine();
        ImGui::PushID(paletteIndex);
        ImVec4 swatch(palette[paletteIndex].rgb[0], palette[paletteIndex].rgb[1], palette[paletteIndex].rgb[2], 1.0f);
        if (ImGui::ColorButton(palette[paletteIndex].name, swatch, ImGuiColorEditFlags_NoTooltip, ImVec2(20.0f, 20.0f))) {
          light.Color = XVECTOR3(palette[paletteIndex].rgb[0], palette[paletteIndex].rgb[1], palette[paletteIndex].rgb[2]);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", palette[paletteIndex].name);
        ImGui::PopID();
      }

      t850::SliderDesc intensityDesc;
      intensityDesc.name = "light_intensity";
      intensityDesc.label = "Intensity";
      intensityDesc.min_val = 0.0f;
      intensityDesc.max_val = 50.0f;
      intensityDesc.step = 0.1f;
      intensityDesc.default_val = light.Intensity;
      float intensity = light.Intensity;
      if (gui.Slider(intensityDesc, intensity)) light.Intensity = intensity;

      if (light.Type == LIGHT_DIRECTIONAL) {
        t850::CheckboxDesc drawDirectionDesc;
        drawDirectionDesc.name = "draw_direction";
        drawDirectionDesc.label = "Draw direction";
        bool drawDirection = m_drawLightDirection;
        if (gui.Checkbox(drawDirectionDesc, drawDirection)) m_drawLightDirection = drawDirection;

        float direction[3] = {light.Direction.x, light.Direction.y, light.Direction.z};
        if (ImGui::DragFloat3("Direction", direction, 0.01f, -1.0f, 1.0f, "%.3f")) {
          XVECTOR3 newDirection(direction[0], direction[1], direction[2]);
          if (newDirection.Length() > 0.0001f) {
            newDirection.Normalize();
            light.Direction = newDirection;
            SyncLightCameraFromDirectionalLight();
          }
        }
      } else {
        t850::CheckboxDesc attachDesc;
        attachDesc.name = "attach_to_camera";
        attachDesc.label = "Attach to camera";
        bool attachToCamera = m_lightAttachToCamera[m_selectedLightIndex];
        if (gui.Checkbox(attachDesc, attachToCamera)) {
          m_lightAttachToCamera[m_selectedLightIndex] = attachToCamera;
          UpdateAttachedLights();
        }

        float position[3] = {light.Position.x, light.Position.y, light.Position.z};
        if (attachToCamera) ImGui::BeginDisabled();
        if (ImGui::DragFloat3("Position", position, 0.05f, 0.0f, 0.0f, "%.3f")) {
          light.Position = XVECTOR3(position[0], position[1], position[2]);
        }
        if (attachToCamera) ImGui::EndDisabled();

        t850::SliderDesc diameterDesc;
        diameterDesc.name = "light_diameter";
        diameterDesc.label = "Diameter";
        diameterDesc.min_val = 0.01f;
        diameterDesc.max_val = 2000.0f;
        diameterDesc.step = 0.1f;
        diameterDesc.default_val = light.radius * 2.0f;
        float diameter = light.radius * 2.0f;
        if (gui.Slider(diameterDesc, diameter)) light.radius = (std::max)(0.001f, diameter * 0.5f);
      }

      ImGui::PopID();
    }
  }

  if (m_profileReady) {
    t850::SandboxProfileDesc currentProfileState;
    CaptureSandboxProfileState(currentProfileState);
    m_profileDirty = !SandboxProfileStatesEqual(currentProfileState, m_profileSavedState);
    if (gui.BeginSection("Profile")) {
      std::string profileText = m_profileEmbeddedInScene
          ? ("Scene profile: " + (m_loadedEditorScenePath.empty() ? std::string("none") : m_loadedEditorScenePath))
          : ("Model profile: " + (m_profileModelKey.empty() ? std::string("none") : m_profileModelKey));
      gui.Text(profileText.c_str());
      const auto& runtime = t850::GetRuntimeProfileInfo();
      std::string gpuText = runtime.gpuName.empty() ? runtime.gpuFamily : runtime.gpuName;
      if (gpuText.empty()) gpuText = "unknown GPU";
      else if (!runtime.gpuFamily.empty() && runtime.gpuFamily != runtime.gpuName) gpuText += " (" + runtime.gpuFamily + ")";
      std::string runtimeText = "Runtime: " + runtime.platform + " / " + runtime.architecture + " / " + gpuText;
      gui.Text(runtimeText.c_str());

      t850::SelectorDesc targetDesc;
      targetDesc.name = "profile_target";
      targetDesc.label = "Save target";
      for (const auto& target : t850::GetProfileTargets()) targetDesc.options.push_back(target.label);
      targetDesc.default_index = m_selectedProfileTargetIndex;
      int targetIndex = m_selectedProfileTargetIndex;
      if (gui.Combo(targetDesc, targetIndex)) {
        m_selectedProfileTargetIndex = targetIndex;
      }
      bool canSaveProfile = m_profileDirty || m_selectedProfileTargetIndex != t850::DefaultProfileTargetIndex();
      if (gui.Button("Save Profile", canSaveProfile)) {
        SaveSandboxProfile();
      }
    }
  }

  if (gui.BeginSection("Culling")) {
    t850::CheckboxDesc cullingDesc;
    cullingDesc.name = "frustum_culling";
    cullingDesc.label = "Frustum culling";
    cullingDesc.enabled = SceneProp.FrustumCullingToggleAllowed;
    bool cullingEnabled = SceneProp.FrustumCullingEnabled;
    if (gui.Checkbox(cullingDesc, cullingEnabled)) {
      if (!cullingEnabled || !Meshes[0].pBase || static_cast<RenderMesh*>(Meshes[0].pBase)->EnsureCullingMetadata()) {
        SceneProp.FrustumCullingEnabled = cullingEnabled;
      }
    }

    t850::CheckboxDesc statsDesc;
    statsDesc.name = "show_culling_debug";
    statsDesc.label = "Culling stats and frustum";
    statsDesc.enabled = SceneProp.FrustumCullingToggleAllowed;
    bool showCulling = m_showCullStats;
    if (gui.Checkbox(statsDesc, showCulling)) {
      m_showCullStats = showCulling;
      SceneProp.ShowCullingDebug = showCulling;
    }
  }

  DrawRagdollPhysicsSimulationPanel(gui);

  if (gui.BeginPanel("Game Logic")) {
    const t850::game::GameLogicStats& stats = m_gameLogic.Stats();
    char summary[256];
    std::snprintf(summary, sizeof(summary),
                  "Objects: %zu  Components: %zu  Tick: %llu",
                  stats.objectCount,
                  stats.componentCount,
                  static_cast<unsigned long long>(stats.tickIndex));
    gui.Text(summary);
    t850::CheckboxDesc pauseDesc;
    pauseDesc.name = "game_logic_paused";
    pauseDesc.label = "Pause fixed tick";
    bool gamePaused = m_gameLogic.Paused();
    if (gui.Checkbox(pauseDesc, gamePaused)) m_gameLogic.SetPaused(gamePaused);
    gui.Separator();
    for (const t850::game::GameObject& object : m_gameLogic.Registry().Objects()) {
      char line[512];
      std::snprintf(line, sizeof(line),
                    "#%u  %s [%s] id=%s components=%zu mesh=%d physics=%s",
                    object.runtimeId,
                    object.name.c_str(),
                    object.kind.c_str(),
                    object.sceneId.c_str(),
                    object.components.size(),
                    object.links.meshSlot,
                    object.links.primaryBody.IsValid() ? "resolved" : "none");
      gui.Text(line);
    }
    if (gui.BeginSection("Recent Events", false)) {
      const std::span<const t850::game::GameEvent> events = m_gameLogic.Events().RecentEvents();
      if (events.empty()) {
        gui.Text("No events dispatched.");
      } else {
        for (const t850::game::GameEvent& event : events) {
          char eventLine[512];
          std::snprintf(eventLine, sizeof(eventLine),
                        "tick=%llu seq=%llu %s source=%s target=%s",
                        static_cast<unsigned long long>(event.tick),
                        static_cast<unsigned long long>(event.sequence),
                        event.type.c_str(),
                        event.sourceEntityId.c_str(),
                        event.targetEntityId.c_str());
          gui.Text(eventLine);
        }
      }
    }
    std::vector<std::string> behaviorObjects;
    std::vector<t850::game::GameObject*> behaviorObjectPointers;
    for (t850::game::GameObject& object : m_gameLogic.Registry().Objects()) {
      if (!object.behavior) continue;
      behaviorObjects.push_back(object.name.empty() ? object.sceneId : object.name);
      behaviorObjectPointers.push_back(&object);
    }
    if (!behaviorObjects.empty() && gui.BeginSection("Force Transition", false)) {
      m_gameLogicSelectedObject = std::clamp(
          m_gameLogicSelectedObject, 0, static_cast<int>(behaviorObjects.size()) - 1);
      t850::SelectorDesc objectSelector;
      objectSelector.name = "game_logic_object";
      objectSelector.label = "Object";
      gui.Combo(objectSelector, m_gameLogicSelectedObject, &behaviorObjects);

      t850::game::GameObject* selected = behaviorObjectPointers[static_cast<std::size_t>(m_gameLogicSelectedObject)];
      std::vector<std::string> states(
          selected->behavior->StateNames().begin(), selected->behavior->StateNames().end());
      if (!states.empty()) {
        m_gameLogicForceState = std::clamp(m_gameLogicForceState, 0, static_cast<int>(states.size()) - 1);
        t850::SelectorDesc stateSelector;
        stateSelector.name = "game_logic_force_state";
        stateSelector.label = "State";
        gui.Combo(stateSelector, m_gameLogicForceState, &states);
        if (gui.Button("Force Transition")) {
          selected->behavior->ForceTransition(states[static_cast<std::size_t>(m_gameLogicForceState)]);
        }
      }
    }
  }
  gui.EndPanel();

  const RenderMesh* consoleCullMesh = Meshes[0].pBase ? static_cast<const RenderMesh*>(Meshes[0].pBase) : nullptr;
  DrawSandboxConsolePanel(m_cameraController.GetActiveProfileType(), Cam.Eye, SceneProp, consoleCullMesh);
}
