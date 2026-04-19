/*********************************************************
* T8ditor — orbit/pan/zoom editor camera. See header.
*********************************************************/

#include "EditorCamera.h"

#include <utils/InputManager.h>
#include <utils/xMaths.h>   // xPI
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

void EditorCamera::ResetToDefault() {
  m_target   = XVECTOR3(0.0f, 0.0f, 0.0f);
  m_yaw      = 0.0f;
  m_pitch    = -0.4f;
  m_distance = FrameDistance;
}

void EditorCamera::Update(float dtSecs, InputManager& im, float wheelDelta) {
  // ── Mouse-driven controls ──
  // SDL middle = button index 1, right = index 2 (Win32Framework.cpp).
  const bool middleDown = im.PressedMouseButton(1);
  const bool rightDown  = im.PressedMouseButton(2);
  const bool shiftDown  = im.PressedKey(T800K_LSHIFT) || im.PressedKey(T800K_RSHIFT);

  if (middleDown || rightDown) {
    const float dx = (float)im.xDelta;
    const float dy = (float)im.yDelta;

    if (middleDown && shiftDown) {
      // Pan: slide target on the camera's right/up plane. Scale by distance
      // so panning feels consistent at all zoom levels.
      const float panScale = PanSpeed * m_distance;
      m_target.x -= m_cam.Right.x * dx * panScale;
      m_target.y -= m_cam.Right.y * dx * panScale;
      m_target.z -= m_cam.Right.z * dx * panScale;
      m_target.x += m_cam.Up.x * dy * panScale;
      m_target.y += m_cam.Up.y * dy * panScale;
      m_target.z += m_cam.Up.z * dy * panScale;
    } else {
      // Orbit
      m_yaw   += dx * OrbitSpeed;
      m_pitch += dy * OrbitSpeed;
    }
  }

  // ── Keyboard fallback (works without a 3-button mouse) ──
  if (im.PressedKey(T800K_LEFT))  m_yaw   -= KeyOrbitRate * dtSecs;
  if (im.PressedKey(T800K_RIGHT)) m_yaw   += KeyOrbitRate * dtSecs;
  if (im.PressedKey(T800K_UP))    m_pitch -= KeyOrbitRate * dtSecs;
  if (im.PressedKey(T800K_DOWN))  m_pitch += KeyOrbitRate * dtSecs;

  // Zoom via +/- (no SDL mouse-wheel routing in Win32Framework yet).
  if (im.PressedKey(T800K_PLUS) || im.PressedKey(T800K_EQUALS) || im.PressedKey(T800K_KP_PLUS))
    m_distance -= KeyZoomRate * dtSecs * std::max(1.0f, m_distance * 0.1f);
  if (im.PressedKey(T800K_MINUS) || im.PressedKey(T800K_KP_MINUS))
    m_distance += KeyZoomRate * dtSecs * std::max(1.0f, m_distance * 0.1f);

  // Mouse wheel zoom (captured via SDL event watcher in EditorImGui).
  if (wheelDelta != 0.0f)
    m_distance *= std::pow(ZoomSpeed, -wheelDelta);

  // Frame selection.
  if (im.PressedOnceKey(T800K_f))
    Frame();

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
