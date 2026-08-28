#include <pch.h>
#include <scene/ShadowSystem.h>
#include <scene/SceneProp.h>
#include <utils/xMaths.h>
#include <cmath>
#include <algorithm>

namespace t850 {

  void ShadowRuntimeState::Reset() {
    projections.clear();
    projectionById.clear();
  }

  ShadowTechnique ShadowSystem::ResolveTechnique(const std::string& name) {
    if (name == "csm") return ShadowTechnique::DirectionalCascaded;
    if (name == "point_cube") return ShadowTechnique::PointCube;
    if (name == "point_dual_paraboloid") return ShadowTechnique::PointDualParaboloid;
    return ShadowTechnique::DirectionalSingle;  // "directional" and default
  }

  // ── Atlas layout ──
  // 1 -> 1x1, 2 -> 2x1, 3 -> 2x2, 4 -> 2x2, 5 -> 3x2, 6 -> 3x2
  void ShadowSystem::ComputeAtlasLayout(int viewCount, int& columns, int& rows) {
    columns = 1;
    rows = 1;
    if (viewCount <= 1) { columns = 1; rows = 1; }
    else if (viewCount == 2) { columns = 2; rows = 1; }
    else if (viewCount <= 4) { columns = 2; rows = 2; }
    else { columns = 3; rows = 2; }
  }

  // ── Light index resolution: ID first, then legacy index ──
  int ShadowSystem::ResolveLightIndex(const SceneProps& props, const ShadowProjectionDesc& desc) {
    if (!desc.light_id.empty()) {
      for (size_t i = 0; i < props.Lights.size(); ++i) {
        if (props.Lights[i].Id == desc.light_id)
          return static_cast<int>(i);
      }
    }
    if (desc.legacy_light_index >= 0 &&
        desc.legacy_light_index < static_cast<int>(props.Lights.size())) {
      return desc.legacy_light_index;
    }
    return -1;
  }

  // ── Resolve descriptors into runtime projection records ──
  bool ShadowSystem::ResolveDescriptors(
    const RenderGraphDesc& graph,
    SceneProps& props,
    ShadowRuntimeState& runtime,
    std::string* error) {
    runtime.Reset();

    for (const auto& desc : graph.shadow_projections) {
      if (!desc.enabled)
        continue;

      // Validate ID uniqueness
      if (runtime.projectionById.count(desc.id)) {
        if (error) *error = "Duplicate shadow projection id '" + desc.id + "'";
        return false;
      }

      // Validate target exists
      bool targetFound = false;
      for (const auto& rt : graph.render_targets) {
        if (rt.name == desc.target) { targetFound = true; break; }
      }
      if (!targetFound) {
        if (error) *error = "Shadow projection '" + desc.id + "' references missing target '" + desc.target + "'";
        return false;
      }

      // Validate cascade count for CSM
      ShadowTechnique technique = ResolveTechnique(desc.technique);
      if (technique == ShadowTechnique::DirectionalCascaded) {
        if (desc.cascade_count < 1 || desc.cascade_count > 6) {
          if (error) *error = "Shadow projection '" + desc.id + "' cascade_count out of range 1..6";
          return false;
        }
      }

      ShadowProjectionRuntime pr;
      pr.resolvedDesc = desc;
      pr.resolvedLightIndex = ResolveLightIndex(props, desc);

      // Derive view count from technique
      switch (technique) {
        case ShadowTechnique::DirectionalSingle: pr.viewCount = 1; break;
        case ShadowTechnique::DirectionalCascaded: pr.viewCount = desc.cascade_count; break;
        case ShadowTechnique::PointCube: pr.viewCount = 6; break;
        case ShadowTechnique::PointDualParaboloid: pr.viewCount = 2; break;
      }

      // Atlas layout
      ComputeAtlasLayout(pr.viewCount, pr.atlasColumns, pr.atlasRows);

      // Tile resolution
      int tileRes = desc.resolution > 0 ? desc.resolution : static_cast<int>(props.ShadowMapResolution);
      pr.atlasWidth = pr.atlasColumns * tileRes;
      pr.atlasHeight = pr.atlasRows * tileRes;

      // Per-view viewport + atlas scale/bias
      for (int v = 0; v < pr.viewCount; ++v) {
        int tileX = v % pr.atlasColumns;
        int tileY = v / pr.atlasColumns;
        pr.views[v].viewport = { tileX * tileRes, tileY * tileRes, tileRes, tileRes };
        pr.views[v].kind = (pr.viewCount > 1) ? ShadowViewKind::AtlasTile : ShadowViewKind::WholeTexture2D;
        pr.views[v].atlasScaleBias = {
          (float)tileRes / (float)pr.atlasWidth,
          (float)tileRes / (float)pr.atlasHeight,
          (float)tileX * (float)tileRes / (float)pr.atlasWidth,
          (float)tileY * (float)tileRes / (float)pr.atlasHeight};
      }

      int idx = static_cast<int>(runtime.projections.size());
      runtime.projections.push_back(pr);
      runtime.projectionById[desc.id] = idx;
    }
    return true;
  }

  // ── Resolve light bindings after runtime lights exist ──
  bool ShadowSystem::ResolveLightBindings(SceneProps& props, ShadowRuntimeState& runtime, std::string* error) {
    for (auto& pr : runtime.projections) {
      pr.resolvedLightIndex = ResolveLightIndex(props, pr.resolvedDesc);
      if (pr.resolvedLightIndex < 0) {
        if (error) *error = "Shadow projection '" + pr.resolvedDesc.id +
          "' has no matching light (id='" + pr.resolvedDesc.light_id +
          "', legacy index=" + std::to_string(pr.resolvedDesc.legacy_light_index) + ")";
        return false;
      }
    }
    return true;
  }

  // ── Build directional cascades ──
  bool ShadowSystem::BuildDirectionalCascades(
    ShadowProjectionRuntime& projection,
    const SceneProps& props,
    const Camera& mainCamera,
    int tileResolution,
    std::string* error) {
    const auto& desc = projection.resolvedDesc;
    const int N = projection.viewCount;
    if (N < 1 || N > 6) {
      if (error) *error = "Invalid cascade count " + std::to_string(N);
      return false;
    }

    // Clamp near/far to main camera range (avoid Windows near/far macros).
    float nearZ = std::max(desc.near_distance, mainCamera.NPlane);
    float farZ = std::min(desc.far_distance, mainCamera.FPlane);
    if (!std::isfinite(nearZ) || !std::isfinite(farZ) || farZ <= nearZ) {
      if (error) *error = "Invalid near/far for shadow projection '" + desc.id + "'";
      return false;
    }

    // Split boundaries (N-1 boundaries)
    const float lambda = desc.split_lambda;
    for (int i = 1; i < N; ++i) {
      float t = (float)i / (float)N;
      float logSplit = nearZ * std::pow(farZ / nearZ, t);
      float linSplit = nearZ + (farZ - nearZ) * t;
      projection.splitBoundaries[i - 1] = lambda * logSplit + (1.0f - lambda) * linSplit;
    }

    // Light direction from the resolved light
    if (projection.resolvedLightIndex < 0 ||
        projection.resolvedLightIndex >= (int)props.Lights.size()) {
      if (error) *error = "Shadow projection '" + desc.id + "' has no resolved light";
      return false;
    }
    const Light& light = props.Lights[projection.resolvedLightIndex];
    XVECTOR3 lightDir = light.Direction;
    lightDir.Normalize();
    if (lightDir.Length() < 1e-6f) {
      if (error) *error = "Shadow projection '" + desc.id + "' has zero light direction";
      return false;
    }

    // Build camera basis from light direction
    XVECTOR3 up(0.0f, 1.0f, 0.0f, 0.0f);
    float upDot;
    XVecDot(upDot, lightDir, up);
    if (std::fabs(upDot) > 0.99f)
      up = XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);

    XVECTOR3 right, fwd;
    XVecCross(right, up, lightDir);   // right = up x lightDir
    right.Normalize();
    XVecCross(fwd, lightDir, right);  // fwd = lightDir x right (orthogonal)
    fwd.Normalize();

    // Main camera basis (normalized)
    XVECTOR3 camLook = mainCamera.Look; camLook.Normalize();
    XVECTOR3 camRight = mainCamera.Right; camRight.Normalize();
    XVECTOR3 camUp = mainCamera.Up; camUp.Normalize();

    const bool ortho = mainCamera.Ortho;
    const float aspect = mainCamera.AspectRatio > 0.0f ? mainCamera.AspectRatio : 1.0f;
    const float halfFovTan = std::tan(mainCamera.Fov * 0.5f);

    // For each cascade, build the view-slice corners and fit an ortho light camera
    for (int c = 0; c < N; ++c) {
      float sliceNear = (c == 0) ? nearZ : projection.splitBoundaries[c - 1];
      float sliceFar = (c == N - 1) ? farZ : projection.splitBoundaries[c];

      // Build 8 world-space corners of the view slice
      XVECTOR3 corners[8];
      if (ortho) {
        float halfW = mainCamera.Width * 0.5f;
        float halfH = mainCamera.Height * 0.5f;
        // near plane
        XVECTOR3 cn = mainCamera.Eye + camLook * sliceNear;
        corners[0] = cn - camRight * halfW - camUp * halfH;
        corners[1] = cn + camRight * halfW - camUp * halfH;
        corners[2] = cn + camRight * halfW + camUp * halfH;
        corners[3] = cn - camRight * halfW + camUp * halfH;
        // far plane
        XVECTOR3 cf = mainCamera.Eye + camLook * sliceFar;
        corners[4] = cf - camRight * halfW - camUp * halfH;
        corners[5] = cf + camRight * halfW - camUp * halfH;
        corners[6] = cf + camRight * halfW + camUp * halfH;
        corners[7] = cf - camRight * halfW + camUp * halfH;
      } else {
        float nearHalfH = halfFovTan * sliceNear;
        float nearHalfW = nearHalfH * aspect;
        float farHalfH = halfFovTan * sliceFar;
        float farHalfW = farHalfH * aspect;
        XVECTOR3 cn = mainCamera.Eye + camLook * sliceNear;
        corners[0] = cn - camRight * nearHalfW - camUp * nearHalfH;
        corners[1] = cn + camRight * nearHalfW - camUp * nearHalfH;
        corners[2] = cn + camRight * nearHalfW + camUp * nearHalfH;
        corners[3] = cn - camRight * nearHalfW + camUp * nearHalfH;
        XVECTOR3 cf = mainCamera.Eye + camLook * sliceFar;
        corners[4] = cf - camRight * farHalfW - camUp * farHalfH;
        corners[5] = cf + camRight * farHalfW - camUp * farHalfH;
        corners[6] = cf + camRight * farHalfW + camUp * farHalfH;
        corners[7] = cf - camRight * farHalfW + camUp * farHalfH;
      }

      // Store corners for debug
      for (int k = 0; k < 8; ++k)
        projection.views[c].frustumCorners[k] = corners[k];

      // Compute receiver center and bounding-sphere radius
      XVECTOR3 center(0.0f, 0.0f, 0.0f, 1.0f);
      for (int k = 0; k < 8; ++k) center += corners[k];
      center = center / 8.0f;

      float maxDistSq = 0.0f;
      for (int k = 0; k < 8; ++k) {
        XVECTOR3 d = corners[k] - center;
        float dsq = d.x * d.x + d.y * d.y + d.z * d.z;
        if (dsq > maxDistSq) maxDistSq = dsq;
      }
      float radius = std::sqrt(maxDistSq);
      // Round up to a small increment to prevent extent oscillation
      const float kExtentIncrement = 16.0f;
      radius = std::ceil(radius / kExtentIncrement) * kExtentIncrement;

      // Light-space center (project center onto light basis)
      float cx, cy, cz;
      XVecDot(cx, center, right);
      XVecDot(cy, center, fwd);
      XVecDot(cz, center, lightDir);

      // Texel snapping
      float extent = 2.0f * radius;
      float worldUnitsPerTexel = extent / (float)tileResolution;
      float snappedX = std::round(cx / worldUnitsPerTexel) * worldUnitsPerTexel;
      float snappedY = std::round(cy / worldUnitsPerTexel) * worldUnitsPerTexel;

      XVECTOR3 snappedCenter = right * snappedX + fwd * snappedY + lightDir * cz;

      // Fit receiver depth relative to the snapped center, then place the eye
      // before the nearest receiver along the light-ray direction. This keeps
      // the generated orthographic camera on a positive near/far interval.
      float minRelativeZ = 1e30f, maxRelativeZ = -1e30f;
      for (int k = 0; k < 8; ++k) {
        float relativeZ;
        XVecDot(relativeZ, corners[k] - snappedCenter, lightDir);
        if (relativeZ < minRelativeZ) minRelativeZ = relativeZ;
        if (relativeZ > maxRelativeZ) maxRelativeZ = relativeZ;
      }

      const float casterPadding = std::max(desc.caster_depth_padding, 0.0f);
      XVECTOR3 eye = snappedCenter + lightDir * (minRelativeZ - casterPadding);
      const float nearPlane = 0.1f;
      const float farPlane = std::max(
        nearPlane + 1.0f,
        (maxRelativeZ - minRelativeZ) + casterPadding + 1.0f);

      // Configure the generated ortho camera
      Camera& cam = projection.views[c].camera;
      cam.Eye = eye;
      cam.Look = lightDir;
      cam.Right = right;
      cam.Up = fwd;
      cam.Ortho = true;
      cam.Width = extent;
      cam.Height = extent;
      cam.NPlane = nearPlane;
      cam.FPlane = farPlane;
      cam.LeftHanded = mainCamera.LeftHanded;
      cam.CreatePojectionOrtho();

      XMATRIX44 view;
      const XVECTOR3 target = eye + lightDir;
      if (cam.LeftHanded)
        XMatViewLookAtLH(view, eye, target, fwd);
      else
        XMatViewLookAtRH(view, eye, target, fwd);

      cam.View = view;
      cam.VP = view * cam.Projection;
      projection.views[c].viewProjection = cam.VP;
    }

    return true;
  }

  // ── Update projection (dispatch by technique) ──
  bool ShadowSystem::UpdateProjection(
    ShadowProjectionRuntime& projection,
    const SceneProps& props,
    const Camera& mainCamera,
    int tileResolution,
    std::string* error) {
    switch (ResolveTechnique(projection.resolvedDesc.technique)) {
      case ShadowTechnique::DirectionalSingle:
      case ShadowTechnique::DirectionalCascaded:
        return BuildDirectionalCascades(projection, props, mainCamera, tileResolution, error);
      case ShadowTechnique::PointCube:
        return BuildPointCubeViews(projection, props, mainCamera, tileResolution, error);
      case ShadowTechnique::PointDualParaboloid:
        return BuildDualParaboloidViews(projection, props, mainCamera, tileResolution, error);
    }
    if (error) *error = "Unsupported shadow technique";
    return false;
  }

  // ── Future techniques (not implemented in CSM milestone) ──
  bool ShadowSystem::BuildPointCubeViews(ShadowProjectionRuntime&, const SceneProps&, const Camera&, int, std::string* error) {
    if (error) *error = "Point cube shadows not yet supported";
    return false;
  }
  bool ShadowSystem::BuildDualParaboloidViews(ShadowProjectionRuntime&, const SceneProps&, const Camera&, int, std::string* error) {
    if (error) *error = "Dual paraboloid shadows not yet supported";
    return false;
  }

} // namespace t850
