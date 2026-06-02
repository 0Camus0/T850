/*********************************************************
* T8ditor — orbit/pan/zoom editor camera. See header.
*********************************************************/

#include "EditorCamera.h"

#include <utils/InputManager.h>
#include <utils/xMaths.h>   // xPI
#include <algorithm>
#include <cmath>

namespace t8ditor {

EditorCamera::EditorCamera() : m_target(0.0f, 0.0f, 0.0f) {}

void EditorCamera::Init(int viewportWidth, int viewportHeight,
                        float fovDeg, float nearPlane, float farPlane) {
  m_viewportW = viewportWidth  > 0 ? viewportWidth  : 1;
  m_viewportH = viewportHeight > 0 ? viewportHeight : 1;

  const float fovRad = fovDeg * (xPI / 180.0f);
  const float aspect = (float)m_viewportW / (float)m_viewportH;

  // Initial eye is computed by RecomputeEye(); pass a placeholder.
  m_cam.InitPerspective(XVECTOR3(0.0f, 0.0f, -1.0f), fovRad, aspect, nearPlane, farPlane);
  RecomputeEye();
  m_cam.Update(0.0f);
}

void EditorCamera::SetViewportSize(int w, int h) {
  if (w <= 0 || h <= 0) return;
  m_viewportW = w;
  m_viewportH = h;
  m_cam.SetRatio((float)w / (float)h);
  m_cam.CreatePojection();
}

void EditorCamera::SetTarget(const XVECTOR3& target) {
  m_target = target;
}

void EditorCamera::Frame() {
  m_distance = FrameDistance;
}

void EditorCamera::FrameBounds(const XVECTOR3& center, float radius) {
  m_target = center;
  const float safeRadius = (std::max)(0.1f, radius);
  const float aspect = m_viewportH > 0 ? static_cast<float>(m_viewportW) / static_cast<float>(m_viewportH) : 1.0f;
  const float verticalFov = (std::max)(0.01f, m_cam.Fov);
  const float horizontalFov = 2.0f * std::atan(std::tan(verticalFov * 0.5f) * (std::max)(0.01f, aspect));
  const float fitFov = (std::max)(0.01f, (std::min)(verticalFov, horizontalFov));
  m_distance = std::clamp((safeRadius / std::tan(fitFov * 0.5f)) * 1.25f, MinDistance, MaxDistance);
  RecomputeEye();
  m_cam.Update(0.0f);
}

void EditorCamera::FrameBounds(const t850::AABB& bounds) {
  if (!bounds.IsValid()) {
    return;
  }

  const XVECTOR3 center(
      (bounds.vMin.x + bounds.vMax.x) * 0.5f,
      (bounds.vMin.y + bounds.vMax.y) * 0.5f,
      (bounds.vMin.z + bounds.vMax.z) * 0.5f,
      1.0f);

  m_target = center;
  const float aspect = m_viewportH > 0
      ? static_cast<float>(m_viewportW) / static_cast<float>(m_viewportH)
      : 1.0f;
  const float verticalFov = (std::max)(0.01f, m_cam.Fov);
  const float horizontalFov = 2.0f * std::atan(std::tan(verticalFov * 0.5f) * (std::max)(0.01f, aspect));
  const float tanHalfV = (std::max)(0.001f, std::tan(verticalFov * 0.5f));
  const float tanHalfH = (std::max)(0.001f, std::tan(horizontalFov * 0.5f));

  RecomputeEye();

  float halfW = 0.0f;
  float halfH = 0.0f;
  float halfD = 0.0f;
  const float bmin[3] = { bounds.vMin.x, bounds.vMin.y, bounds.vMin.z };
  const float bmax[3] = { bounds.vMax.x, bounds.vMax.y, bounds.vMax.z };
  for (int c = 0; c < 8; ++c) {
    const XVECTOR3 corner(
        (c & 1) ? bmax[0] : bmin[0],
        (c & 2) ? bmax[1] : bmin[1],
        (c & 4) ? bmax[2] : bmin[2],
        1.0f);
    const XVECTOR3 delta(corner.x - center.x, corner.y - center.y, corner.z - center.z, 0.0f);
    float right = 0.0f;
    float up = 0.0f;
    float forward = 0.0f;
    XVecDot(right, delta, m_cam.Right);
    XVecDot(up, delta, m_cam.Up);
    XVecDot(forward, delta, m_cam.Look);
    halfW = (std::max)(halfW, std::fabs(right));
    halfH = (std::max)(halfH, std::fabs(up));
    halfD = (std::max)(halfD, std::fabs(forward));
  }

  const float fitDistance = (std::max)(halfW / tanHalfH, halfH / tanHalfV);
  m_distance = std::clamp((fitDistance + halfD) * 1.20f, MinDistance, MaxDistance);
  RecomputeEye();
  m_cam.Update(0.0f);
}

void EditorCamera::ResetToDefault() {
  m_target   = XVECTOR3(0.0f, 0.0f, 0.0f);
  m_yaw      = -0.75f;   // ~-43° — upper-right quadrant (3dsmax style)
  m_pitch    =  0.4f;    // +23° — above the grid looking down
  m_distance = FrameDistance;
}

void EditorCamera::ResetViewAngle() {
  // Reset viewing angle and distance but keep the current target
  m_yaw      = -0.75f;
  m_pitch    =  0.4f;
  m_distance = FrameDistance;
}

void EditorCamera::Update(float dtSecs, InputManager& im, float wheelDelta,
                          bool skipMouse, bool skipKeyboard) {
  // ── Mouse-driven controls (skip when ImGui wants the mouse) ──
  if (!skipMouse) {
    const bool middleDown = im.PressedMouseButton(1);
    const bool rightDown  = im.PressedMouseButton(2);
    const bool leftDown   = im.PressedMouseButton(0);
    const bool altDown    = im.PressedKey(T800K_LALT) || im.PressedKey(T800K_RALT);

    if (middleDown || rightDown || (leftDown && altDown)) {
      const float dx = (float)im.xDelta;
      const float dy = (float)im.yDelta;

      if (middleDown) {
        // Middle-drag = pan (3dsmax style: no modifier needed)
        const float panScale = PanSpeed * m_distance;
        m_target.x -= m_cam.Right.x * dx * panScale;
        m_target.y -= m_cam.Right.y * dx * panScale;
        m_target.z -= m_cam.Right.z * dx * panScale;
        m_target.x += m_cam.Up.x * dy * panScale;
        m_target.y += m_cam.Up.y * dy * panScale;
        m_target.z += m_cam.Up.z * dy * panScale;
      } else {
        // Right-drag or Alt+left-drag = orbit
        m_yaw   += dx * OrbitSpeed;
        m_pitch += dy * OrbitSpeed;
      }
    }
  }

  // ── Keyboard fallback (skip when ImGui wants the keyboard) ──
  if (!skipKeyboard) {
    if (im.PressedKey(T800K_LEFT))  m_yaw   -= KeyOrbitRate * dtSecs;
    if (im.PressedKey(T800K_RIGHT)) m_yaw   += KeyOrbitRate * dtSecs;
    if (im.PressedKey(T800K_UP))    m_pitch -= KeyOrbitRate * dtSecs;
    if (im.PressedKey(T800K_DOWN))  m_pitch += KeyOrbitRate * dtSecs;

    if (im.PressedKey(T800K_PLUS) || im.PressedKey(T800K_EQUALS) || im.PressedKey(T800K_KP_PLUS))
      m_distance -= KeyZoomRate * dtSecs * std::max(1.0f, m_distance * 0.1f);
    if (im.PressedKey(T800K_MINUS) || im.PressedKey(T800K_KP_MINUS))
      m_distance += KeyZoomRate * dtSecs * std::max(1.0f, m_distance * 0.1f);

    if (im.PressedOnceKey(T800K_f))
      Frame();
  }

  // Mouse wheel zoom (always active — the caller already zeros wheelDelta
  // when ImGui wants the mouse, so no extra gate needed here).
  if (wheelDelta != 0.0f)
    m_distance *= std::pow(ZoomSpeed, -wheelDelta);

  // ── Clamp & apply ──
  if (m_pitch < MinPitch) m_pitch = MinPitch;
  if (m_pitch > MaxPitch) m_pitch = MaxPitch;
  if (m_distance < MinDistance) m_distance = MinDistance;
  if (m_distance > MaxDistance) m_distance = MaxDistance;

  RecomputeEye();
  m_cam.Update(dtSecs);
}

void EditorCamera::RecomputeEye() {
  // Spherical -> cartesian. Yaw rotates around world Y, pitch tilts up/down.
  // Eye is `m_target + dir * m_distance`, where `dir` points from target to eye.
  const float cp = std::cos(m_pitch);
  const float sp = std::sin(m_pitch);
  const float cy = std::cos(m_yaw);
  const float sy = std::sin(m_yaw);

  XVECTOR3 dir(sy * cp, sp, cy * cp); // unit, target-to-eye direction
  XVECTOR3 eye(m_target.x + dir.x * m_distance,
               m_target.y + dir.y * m_distance,
               m_target.z + dir.z * m_distance);

  m_cam.Eye  = eye;
  m_cam.Look = m_target;
  m_cam.SetLookAt(m_target);
}

} // namespace t8ditor
