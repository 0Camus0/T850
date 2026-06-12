#include <pch.h>

#include <physics/Q3BspCollision.h>

#include <utils/Log.h>
#include <utils/ResourceLocator.h>
#include <debug/RuntimeTelemetry.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244 4267)
#endif
#include <glaze/glaze.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <optional>

namespace t850 {

struct Q3ClipVec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct Q3ClipPlaneDesc {
  Q3ClipVec3 normal;
  float dist = 0.0f;
};

struct Q3ClipBrushDesc {
  std::vector<Q3ClipPlaneDesc> planes;
};

struct Q3ClipPatchFacetDesc {
  Q3ClipPlaneDesc surface;
  std::vector<Q3ClipPlaneDesc> borders;
  Q3ClipVec3 mins;
  Q3ClipVec3 maxs;
};

struct Q3ClipJumpPadDesc {
  int entity_id = 0;
  Q3ClipVec3 mins;
  Q3ClipVec3 maxs;
  std::optional<Q3ClipVec3> target_position;
  Q3ClipVec3 velocity;
};

struct Q3ClipReachabilityDesc {
  int source_area = 0;
  int target_area = 0;
  int face = 0;
  int edge = 0;
  Q3ClipVec3 start;
  Q3ClipVec3 end;
  std::string travel_type;
  int travel_type_id = 0;
  int travel_flags = 0;
  int travel_time = 0;
};

struct Q3ClipFileDesc {
  int version = 1;
  std::string source;
  std::string aas_source;
  float unit_scale = 1.0f;
  std::vector<Q3ClipBrushDesc> brushes;
  std::vector<Q3ClipPatchFacetDesc> patch_facets;
  std::vector<Q3ClipJumpPadDesc> jump_pads;
  std::vector<Q3ClipReachabilityDesc> reachabilities;
};

namespace {

constexpr float kQ3SurfaceClipEpsilon = 0.125f;
constexpr float kQ3EntityLinkEpsilon = 1.0f;
constexpr float kDefaultQ3UnitScale = 1.0f / 32.0f;
constexpr float kMinTraceEpsilon = 0.0001f;

float ClampFloat(float value, float minValue, float maxValue) {
  return (std::max)(minValue, (std::min)(value, maxValue));
}

float Dot3(const XVECTOR3& a, const XVECTOR3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

float LengthSq3(const XVECTOR3& value) {
  return Dot3(value, value);
}

float Length3(const XVECTOR3& value) {
  return std::sqrt(LengthSq3(value));
}

XVECTOR3 Cross3(const XVECTOR3& a, const XVECTOR3& b) {
  return XVECTOR3(
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x,
      0.0f);
}

XVECTOR3 NormalizeOr(XVECTOR3 value, const XVECTOR3& fallback) {
  const float length = Length3(value);
  if (length <= 0.000001f) {
    return fallback;
  }
  value.x /= length;
  value.y /= length;
  value.z /= length;
  value.w = 0.0f;
  return value;
}

XVECTOR3 BoxHalfExtentsForCapsuleApprox(const CharacterCollisionSweep& sweep) {
  return XVECTOR3(sweep.radius, sweep.halfHeight + sweep.radius, sweep.radius, 0.0f);
}

bool OverlapsAabb(const CharacterTriggerQuery& query, const Q3BspCollisionWorld::JumpPad& jumpPad, float slop) {
  const XVECTOR3 queryMin = query.center - query.halfExtents;
  const XVECTOR3 queryMax = query.center + query.halfExtents;
  return queryMax.x >= jumpPad.mins.x - slop &&
      queryMin.x <= jumpPad.maxs.x + slop &&
      queryMax.y >= jumpPad.mins.y - slop &&
      queryMin.y <= jumpPad.maxs.y + slop &&
      queryMax.z >= jumpPad.mins.z - slop &&
      queryMin.z <= jumpPad.maxs.z + slop;
}

bool OverlapsAabb(const XVECTOR3& firstMin,
                  const XVECTOR3& firstMax,
                  const XVECTOR3& secondMin,
                  const XVECTOR3& secondMax) {
  return firstMax.x >= secondMin.x &&
      firstMin.x <= secondMax.x &&
      firstMax.y >= secondMin.y &&
      firstMin.y <= secondMax.y &&
      firstMax.z >= secondMin.z &&
      firstMin.z <= secondMax.z;
}

void BuildSweptBoxAabb(const XVECTOR3& start,
                       const XVECTOR3& end,
                       const XVECTOR3& halfExtents,
                       float epsilon,
                       XVECTOR3& outMin,
                       XVECTOR3& outMax) {
  outMin = XVECTOR3(
      (std::min)(start.x, end.x) - halfExtents.x - epsilon,
      (std::min)(start.y, end.y) - halfExtents.y - epsilon,
      (std::min)(start.z, end.z) - halfExtents.z - epsilon,
      1.0f);
  outMax = XVECTOR3(
      (std::max)(start.x, end.x) + halfExtents.x + epsilon,
      (std::max)(start.y, end.y) + halfExtents.y + epsilon,
      (std::max)(start.z, end.z) + halfExtents.z + epsilon,
      1.0f);
}

bool IntersectPlanes(const Q3BspCollisionWorld::Plane& a,
                     const Q3BspCollisionWorld::Plane& b,
                     const Q3BspCollisionWorld::Plane& c,
                     XVECTOR3& outPoint) {
  const XVECTOR3 bc = Cross3(b.normal, c.normal);
  const float denom = Dot3(a.normal, bc);
  if (std::fabs(denom) <= 0.000001f) {
    return false;
  }

  const XVECTOR3 ca = Cross3(c.normal, a.normal);
  const XVECTOR3 ab = Cross3(a.normal, b.normal);
  outPoint = (bc * a.dist + ca * b.dist + ab * c.dist) * (1.0f / denom);
  outPoint.w = 1.0f;
  return std::isfinite(outPoint.x) && std::isfinite(outPoint.y) && std::isfinite(outPoint.z);
}

bool ComputeBrushBounds(const std::vector<Q3BspCollisionWorld::Plane>& planes,
                        float tolerance,
                        XVECTOR3& outMin,
                        XVECTOR3& outMax) {
  outMin = XVECTOR3(1.0e30f, 1.0e30f, 1.0e30f, 1.0f);
  outMax = XVECTOR3(-1.0e30f, -1.0e30f, -1.0e30f, 1.0f);
  bool havePoint = false;

  for (std::size_t i = 0; i < planes.size(); ++i) {
    for (std::size_t j = i + 1; j < planes.size(); ++j) {
      for (std::size_t k = j + 1; k < planes.size(); ++k) {
        XVECTOR3 point;
        if (!IntersectPlanes(planes[i], planes[j], planes[k], point)) {
          continue;
        }

        bool inside = true;
        for (const Q3BspCollisionWorld::Plane& plane : planes) {
          if (Dot3(point, plane.normal) - plane.dist > tolerance) {
            inside = false;
            break;
          }
        }
        if (!inside) {
          continue;
        }

        havePoint = true;
        outMin.x = (std::min)(outMin.x, point.x);
        outMin.y = (std::min)(outMin.y, point.y);
        outMin.z = (std::min)(outMin.z, point.z);
        outMax.x = (std::max)(outMax.x, point.x);
        outMax.y = (std::max)(outMax.y, point.y);
        outMax.z = (std::max)(outMax.z, point.z);
      }
    }
  }

  if (!havePoint) {
    return false;
  }

  outMin.x -= tolerance;
  outMin.y -= tolerance;
  outMin.z -= tolerance;
  outMax.x += tolerance;
  outMax.y += tolerance;
  outMax.z += tolerance;
  return outMin.x <= outMax.x && outMin.y <= outMax.y && outMin.z <= outMax.z;
}

bool TraceBrush(const Q3BspCollisionWorld::Brush& brush,
                const XVECTOR3& start,
                const XVECTOR3& end,
                const XVECTOR3& halfExtents,
                float surfaceClipEpsilon,
                float& inOutFraction,
                XVECTOR3& outNormal,
                bool& inOutAllSolid) {
  float enterFraction = -1.0f;
  float leaveFraction = 1.0f;
  XVECTOR3 enterNormal(0.0f, 1.0f, 0.0f, 0.0f);
  bool getOut = false;
  bool startOut = false;

  for (const Q3BspCollisionWorld::Plane& plane : brush.planes) {
    const XVECTOR3& normal = plane.normal;
    const float offset =
        std::fabs(normal.x * halfExtents.x) +
        std::fabs(normal.y * halfExtents.y) +
        std::fabs(normal.z * halfExtents.z);
    const float expandedDist = plane.dist + offset;
    const float startDist = Dot3(start, normal) - expandedDist;
    const float endDist = Dot3(end, normal) - expandedDist;

    if (endDist > 0.0f) {
      getOut = true;
    }

    if (startDist > 0.0f) {
      startOut = true;
    }

    if (startDist > 0.0f && (endDist >= surfaceClipEpsilon || endDist >= startDist)) {
      return false;
    }

    if (startDist <= 0.0f && endDist <= 0.0f) {
      continue;
    }

    if (startDist > endDist) {
      float fraction = (startDist - surfaceClipEpsilon) / (startDist - endDist);
      if (fraction < 0.0f) {
        fraction = 0.0f;
      }
      if (fraction > enterFraction) {
        enterFraction = fraction;
        enterNormal = normal;
      }
    } else {
      float fraction = (startDist + surfaceClipEpsilon) / (startDist - endDist);
      if (fraction > 1.0f) {
        fraction = 1.0f;
      }
      leaveFraction = (std::min)(leaveFraction, fraction);
    }

    if (enterFraction > leaveFraction) {
      return false;
    }
  }

  if (!startOut) {
    if (!getOut && inOutFraction > 0.0f) {
      inOutFraction = 0.0f;
      outNormal = NormalizeOr(start - end, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
      inOutAllSolid = true;
    }
    return true;
  }

  if (enterFraction < 0.0f || enterFraction > inOutFraction) {
    return false;
  }

  inOutFraction = ClampFloat(enterFraction, 0.0f, 1.0f);
  outNormal = enterNormal;
  return true;
}

bool TracePatchFacet(const Q3BspCollisionWorld::PatchFacet& facet,
                     const XVECTOR3& start,
                     const XVECTOR3& end,
                     const XVECTOR3& halfExtents,
                     float surfaceClipEpsilon,
                     float& inOutFraction,
                     XVECTOR3& outNormal) {
  XVECTOR3 sweepMin;
  XVECTOR3 sweepMax;
  BuildSweptBoxAabb(start, end, halfExtents, surfaceClipEpsilon, sweepMin, sweepMax);
  if (!OverlapsAabb(sweepMin, sweepMax, facet.mins, facet.maxs)) {
    return false;
  }

  const XVECTOR3& normal = facet.surface.normal;
  const float offset =
      std::fabs(normal.x * halfExtents.x) +
      std::fabs(normal.y * halfExtents.y) +
      std::fabs(normal.z * halfExtents.z);
  const float expandedDist = facet.surface.dist + offset;
  const float startDist = Dot3(start, normal) - expandedDist;
  const float endDist = Dot3(end, normal) - expandedDist;
  if (startDist <= 0.0f || startDist <= endDist) {
    return false;
  }

  const float denom = startDist - endDist;
  if (denom <= 0.000001f) {
    return false;
  }

  float fraction = (startDist - surfaceClipEpsilon) / denom;
  fraction = ClampFloat(fraction, 0.0f, 1.0f);
  if (fraction > inOutFraction) {
    return false;
  }

  const XVECTOR3 hitCenter = start + (end - start) * fraction;
  for (const Q3BspCollisionWorld::Plane& border : facet.borders) {
    const XVECTOR3& borderNormal = border.normal;
    const float borderOffset =
        std::fabs(borderNormal.x * halfExtents.x) +
        std::fabs(borderNormal.y * halfExtents.y) +
        std::fabs(borderNormal.z * halfExtents.z);
    const float borderDist = border.dist + borderOffset + surfaceClipEpsilon;
    if (Dot3(hitCenter, borderNormal) > borderDist) {
      return false;
    }
  }

  inOutFraction = fraction;
  outNormal = normal;
  return true;
}

} // namespace

bool Q3BspCollisionWorld::Load(const std::string& resourcePath, std::string* error) {
  Clear();

  std::string content;
  if (!ResourceLocator::Instance().ReadText(resourcePath, content)) {
    if (error) *error = "Q3 collision file not found or unreadable: " + resourcePath;
    return false;
  }

  Q3ClipFileDesc desc;
  auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(desc, content);
  if (err) {
    if (error) *error = glz::format_error(err, content);
    return false;
  }

  const float unitScale = std::isfinite(desc.unit_scale) && desc.unit_scale > 0.0f
      ? std::fabs(desc.unit_scale)
      : kDefaultQ3UnitScale;
  m_surfaceClipEpsilon = (std::max)(kQ3SurfaceClipEpsilon * unitScale, kMinTraceEpsilon);
  m_triggerTouchSlop = (std::max)(kQ3EntityLinkEpsilon * unitScale, m_surfaceClipEpsilon);

  m_brushes.reserve(desc.brushes.size());
  for (const Q3ClipBrushDesc& brushDesc : desc.brushes) {
    Brush brush;
    brush.planes.reserve(brushDesc.planes.size());
    for (const Q3ClipPlaneDesc& planeDesc : brushDesc.planes) {
      Plane plane;
      plane.normal = NormalizeOr(
          XVECTOR3(planeDesc.normal.x, planeDesc.normal.y, planeDesc.normal.z, 0.0f),
          XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
      plane.dist = planeDesc.dist;
      if (std::isfinite(plane.dist)) {
        brush.planes.push_back(plane);
      }
    }

    if (brush.planes.size() >= 4) {
      brush.hasBounds = ComputeBrushBounds(brush.planes,
                                           (std::max)(m_surfaceClipEpsilon * 2.0f, 0.0005f),
                                           brush.mins,
                                           brush.maxs);
      m_brushes.push_back(std::move(brush));
    }
  }

  m_patchFacets.reserve(desc.patch_facets.size());
  for (const Q3ClipPatchFacetDesc& facetDesc : desc.patch_facets) {
    PatchFacet facet;
    facet.surface.normal = NormalizeOr(
        XVECTOR3(facetDesc.surface.normal.x, facetDesc.surface.normal.y, facetDesc.surface.normal.z, 0.0f),
        XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
    facet.surface.dist = facetDesc.surface.dist;
    if (!std::isfinite(facet.surface.dist)) {
      continue;
    }

    facet.borders.reserve(facetDesc.borders.size());
    for (const Q3ClipPlaneDesc& borderDesc : facetDesc.borders) {
      Plane border;
      border.normal = NormalizeOr(
          XVECTOR3(borderDesc.normal.x, borderDesc.normal.y, borderDesc.normal.z, 0.0f),
          XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
      border.dist = borderDesc.dist;
      if (std::isfinite(border.dist)) {
        facet.borders.push_back(border);
      }
    }

    facet.mins = XVECTOR3(facetDesc.mins.x, facetDesc.mins.y, facetDesc.mins.z, 1.0f);
    facet.maxs = XVECTOR3(facetDesc.maxs.x, facetDesc.maxs.y, facetDesc.maxs.z, 1.0f);
    if (facet.mins.x > facet.maxs.x) std::swap(facet.mins.x, facet.maxs.x);
    if (facet.mins.y > facet.maxs.y) std::swap(facet.mins.y, facet.maxs.y);
    if (facet.mins.z > facet.maxs.z) std::swap(facet.mins.z, facet.maxs.z);
    if (facet.borders.size() >= 3 &&
        std::isfinite(facet.mins.x) && std::isfinite(facet.mins.y) && std::isfinite(facet.mins.z) &&
        std::isfinite(facet.maxs.x) && std::isfinite(facet.maxs.y) && std::isfinite(facet.maxs.z)) {
      m_patchFacets.push_back(std::move(facet));
    }
  }

  m_jumpPads.reserve(desc.jump_pads.size());
  for (const Q3ClipJumpPadDesc& jumpPadDesc : desc.jump_pads) {
    JumpPad jumpPad;
    jumpPad.entityId = static_cast<uint32_t>((std::max)(0, jumpPadDesc.entity_id));
    jumpPad.mins = XVECTOR3(jumpPadDesc.mins.x, jumpPadDesc.mins.y, jumpPadDesc.mins.z, 1.0f);
    jumpPad.maxs = XVECTOR3(jumpPadDesc.maxs.x, jumpPadDesc.maxs.y, jumpPadDesc.maxs.z, 1.0f);
    if (jumpPadDesc.target_position) {
      jumpPad.targetPosition = XVECTOR3(
          jumpPadDesc.target_position->x,
          jumpPadDesc.target_position->y,
          jumpPadDesc.target_position->z,
          1.0f);
      jumpPad.hasTargetPosition =
          std::isfinite(jumpPad.targetPosition.x) &&
          std::isfinite(jumpPad.targetPosition.y) &&
          std::isfinite(jumpPad.targetPosition.z);
    }
    jumpPad.velocity = XVECTOR3(jumpPadDesc.velocity.x, jumpPadDesc.velocity.y, jumpPadDesc.velocity.z, 0.0f);
    if (jumpPad.mins.x > jumpPad.maxs.x) std::swap(jumpPad.mins.x, jumpPad.maxs.x);
    if (jumpPad.mins.y > jumpPad.maxs.y) std::swap(jumpPad.mins.y, jumpPad.maxs.y);
    if (jumpPad.mins.z > jumpPad.maxs.z) std::swap(jumpPad.mins.z, jumpPad.maxs.z);
    if (std::isfinite(jumpPad.velocity.x) &&
    std::isfinite(jumpPad.velocity.y) &&
    std::isfinite(jumpPad.velocity.z)) {
      m_jumpPads.push_back(jumpPad);
    }
  }

  m_reachabilities.reserve(desc.reachabilities.size());
  for (const Q3ClipReachabilityDesc& reachabilityDesc : desc.reachabilities) {
    Reachability reachability;
    reachability.sourceArea = static_cast<uint32_t>((std::max)(0, reachabilityDesc.source_area));
    reachability.targetArea = static_cast<uint32_t>((std::max)(0, reachabilityDesc.target_area));
    reachability.face = reachabilityDesc.face;
    reachability.edge = reachabilityDesc.edge;
    reachability.start = XVECTOR3(
        reachabilityDesc.start.x,
        reachabilityDesc.start.y,
        reachabilityDesc.start.z,
        1.0f);
    reachability.end = XVECTOR3(
        reachabilityDesc.end.x,
        reachabilityDesc.end.y,
        reachabilityDesc.end.z,
        1.0f);
    reachability.travelType = reachabilityDesc.travel_type;
    reachability.travelTypeId = static_cast<uint32_t>((std::max)(0, reachabilityDesc.travel_type_id));
    reachability.travelFlags = static_cast<uint32_t>((std::max)(0, reachabilityDesc.travel_flags));
    reachability.travelTime = static_cast<uint32_t>((std::max)(0, reachabilityDesc.travel_time));
    if (std::isfinite(reachability.start.x) &&
        std::isfinite(reachability.start.y) &&
        std::isfinite(reachability.start.z) &&
        std::isfinite(reachability.end.x) &&
        std::isfinite(reachability.end.y) &&
        std::isfinite(reachability.end.z) &&
        !reachability.travelType.empty()) {
      m_reachabilities.push_back(std::move(reachability));
    }
  }

  if (m_brushes.empty()) {
    if (error) *error = "Q3 collision file has no valid brushes: " + resourcePath;
    Clear();
    return false;
  }

  m_resourcePath = resourcePath;
  T8_LOG_INFO(
      "[Q3BspCollision] Loaded %s brushes=%zu patchFacets=%zu jumpPads=%zu reachabilities=%zu",
      resourcePath.c_str(),
      m_brushes.size(),
      m_patchFacets.size(),
      m_jumpPads.size(),
      m_reachabilities.size());
  return true;
}

void Q3BspCollisionWorld::Clear() {
  m_resourcePath.clear();
  m_brushes.clear();
  m_patchFacets.clear();
  m_jumpPads.clear();
  m_reachabilities.clear();
  m_surfaceClipEpsilon = kQ3SurfaceClipEpsilon * kDefaultQ3UnitScale;
  m_triggerTouchSlop = kQ3EntityLinkEpsilon * kDefaultQ3UnitScale;
}

bool Q3BspCollisionWorld::SweepCapsule(const CharacterCollisionSweep& sweep, CharacterCollisionHit& outHit) const {
  CharacterBoxSweep boxSweep;
  boxSweep.startCenter = sweep.startCenter;
  boxSweep.displacement = sweep.displacement;
  boxSweep.halfExtents = BoxHalfExtentsForCapsuleApprox(sweep);
  return SweepBox(boxSweep, outHit);
}

bool Q3BspCollisionWorld::SweepBox(const CharacterBoxSweep& sweep, CharacterCollisionHit& outHit) const {
  T8_TELEMETRY_SCOPE("character.q3_sweep_box");
  RuntimeTelemetry::AddCounter("character.q3SweepBox.count", 1.0);
  outHit = CharacterCollisionHit{};
  if (m_brushes.empty() || LengthSq3(sweep.displacement) <= 0.00000001f) {
    return false;
  }

  const XVECTOR3 end = sweep.startCenter + sweep.displacement;
  XVECTOR3 sweepMin;
  XVECTOR3 sweepMax;
  BuildSweptBoxAabb(sweep.startCenter, end, sweep.halfExtents, m_surfaceClipEpsilon, sweepMin, sweepMax);
  float hitFraction = 1.0f;
  XVECTOR3 hitNormal(0.0f, 1.0f, 0.0f, 0.0f);
  bool allSolid = false;
  int brushCandidates = 0;
  int patchCandidates = 0;

  for (const Brush& brush : m_brushes) {
    if (brush.hasBounds && !OverlapsAabb(sweepMin, sweepMax, brush.mins, brush.maxs)) {
      continue;
    }
    ++brushCandidates;
    TraceBrush(
        brush,
        sweep.startCenter,
        end,
        sweep.halfExtents,
        m_surfaceClipEpsilon,
        hitFraction,
        hitNormal,
        allSolid);
  }

  for (const PatchFacet& facet : m_patchFacets) {
    if (!OverlapsAabb(sweepMin, sweepMax, facet.mins, facet.maxs)) {
      continue;
    }
    ++patchCandidates;
    TracePatchFacet(
        facet,
        sweep.startCenter,
        end,
        sweep.halfExtents,
        m_surfaceClipEpsilon,
        hitFraction,
        hitNormal);
  }
  RuntimeTelemetry::AddCounter("character.q3SweepBox.brushCandidates", static_cast<double>(brushCandidates));
  RuntimeTelemetry::AddCounter("character.q3SweepBox.patchCandidates", static_cast<double>(patchCandidates));

  if (hitFraction >= 1.0f && !allSolid) {
    return false;
  }

  outHit.hit = true;
  outHit.fraction = ClampFloat(hitFraction, 0.0f, 1.0f);
  outHit.normal = NormalizeOr(hitNormal, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  outHit.position = sweep.startCenter + sweep.displacement * outHit.fraction;
  outHit.position.w = 1.0f;
  return true;
}

bool Q3BspCollisionWorld::QueryTriggerTouch(const CharacterTriggerQuery& query, CharacterTriggerTouch& outTouch) const {
  outTouch = CharacterTriggerTouch{};
  if (m_jumpPads.empty()) {
    return false;
  }

  for (const JumpPad& jumpPad : m_jumpPads) {
    if (!OverlapsAabb(query, jumpPad, m_triggerTouchSlop)) {
      continue;
    }

    outTouch.type = CharacterTriggerTouch::Type::JumpPad;
    outTouch.entityId = jumpPad.entityId;
    outTouch.velocity = jumpPad.velocity;
    return true;
  }

  return false;
}

} // namespace t850
