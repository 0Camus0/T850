#pragma once

#include <physics/CharacterController.h>

#include <string>
#include <vector>

namespace t850 {

class Q3BspCollisionWorld final : public CharacterCollisionWorld {
public:
  bool Load(const std::string& resourcePath, std::string* error = nullptr);
  void Clear();

  bool IsLoaded() const { return !m_brushes.empty(); }
  const std::string& GetResourcePath() const { return m_resourcePath; }
  std::size_t GetBrushCount() const { return m_brushes.size(); }
  std::size_t GetPatchFacetCount() const { return m_patchFacets.size(); }
  std::size_t GetJumpPadCount() const { return m_jumpPads.size(); }
  std::size_t GetReachabilityCount() const { return m_reachabilities.size(); }

  bool SweepCapsule(const CharacterCollisionSweep& sweep, CharacterCollisionHit& outHit) const override;
  bool SweepBox(const CharacterBoxSweep& sweep, CharacterCollisionHit& outHit) const override;
  bool QueryTriggerTouch(const CharacterTriggerQuery& query, CharacterTriggerTouch& outTouch) const override;

  struct Plane {
    XVECTOR3 normal = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
    float dist = 0.0f;
  };

  struct Brush {
    std::vector<Plane> planes;
    XVECTOR3 mins = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 maxs = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    bool hasBounds = false;
  };

  struct PatchFacet {
    Plane surface;
    std::vector<Plane> borders;
    XVECTOR3 mins = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 maxs = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  };

  struct JumpPad {
    uint32_t entityId = 0;
    XVECTOR3 mins = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 maxs = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 targetPosition = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 velocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
    bool hasTargetPosition = false;
  };
  const std::vector<JumpPad>& GetJumpPads() const { return m_jumpPads; }

  struct Reachability {
    uint32_t sourceArea = 0;
    uint32_t targetArea = 0;
    int face = 0;
    int edge = 0;
    XVECTOR3 start = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 end = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    std::string travelType;
    uint32_t travelTypeId = 0;
    uint32_t travelFlags = 0;
    uint32_t travelTime = 0;
  };
  const std::vector<Reachability>& GetReachabilities() const { return m_reachabilities; }

private:
  std::string m_resourcePath;
  std::vector<Brush> m_brushes;
  std::vector<PatchFacet> m_patchFacets;
  std::vector<JumpPad> m_jumpPads;
  std::vector<Reachability> m_reachabilities;
  float m_surfaceClipEpsilon = 0.125f / 32.0f;
  float m_triggerTouchSlop = 1.0f / 32.0f;
};

} // namespace t850
