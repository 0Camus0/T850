/*********************************************************
* T8ditor — orbit/pan/zoom editor camera.
*
* Wraps Framework's `Camera` for VP composition and feeds it
* eye/look/up positions derived from spherical coordinates
* (yaw, pitch, distance) around a Target point. Mouse:
*   Middle-drag           = orbit (yaw/pitch around Target)
*   Shift + Middle-drag   = pan (slide Target on the camera's right/up plane)
*   Right-drag            = orbit (alternate, ergonomic for 3-button mice)
*   Wheel / +/-           = zoom (distance to Target)
*   F                     = frame Target (reset distance to FrameDistance)
*
* Rotation also reachable via the keyboard (arrow keys) when no mouse
* button is down, so the editor is usable without a 3-button mouse.
*********************************************************/

#ifndef T8DITOR_EDITOR_CAMERA_H
#define T8DITOR_EDITOR_CAMERA_H

#include <utils/Camera.h>
#include <utils/Picking.h>
#include <utils/xMaths.h>

class InputManager;

namespace t8ditor {

class EditorCamera {
public:
  EditorCamera();

  // Set up the underlying perspective camera. Must be called once before
  // any Update() call.
  void Init(int viewportWidth, int viewportHeight,
            float fovDeg = 50.0f,
            float nearPlane = 0.1f,
            float farPlane = 5000.0f);

  // Per-frame: poll the InputManager (mouse + keys), update spherical
  // coordinates, recompute the view/proj on the wrapped Camera.
  // wheelDelta: accumulated mouse wheel ticks since last frame (positive = zoom in).
  // skipMouse: when true, skip mouse orbit/pan (ImGui has focus).
  // skipKeyboard: when true, skip arrow/zoom/frame keys (ImGui has focus).
  void Update(float dtSecs, InputManager& im, float wheelDelta = 0.0f,
              bool skipMouse = false, bool skipKeyboard = false);

  // Notify the camera of a new viewport size (e.g. window resize).
  void SetViewportSize(int w, int h);

  // The orbit target (looked-at point). Assigned by EditorApp to the
  // current selection's center.
  void SetTarget(const XVECTOR3& target);
  const XVECTOR3& GetTarget() const { return m_target; }

  // Frame the target — re-center distance to FrameDistance.
  void Frame();
  void FrameBounds(const XVECTOR3& center, float radius);
  void FrameBounds(const t850::AABB& bounds);

  // Reset camera to the default position and orientation.
  void ResetToDefault();

  // Reset yaw/pitch/distance to defaults but keep the current target.
  void ResetViewAngle();

  // Underlying Framework camera (for VP, Eye, Look). Const access to
  // discourage callers from mutating it directly.
  const ::Camera& GetCamera() const { return m_cam; }
  ::Camera&       GetCameraMutable()      { return m_cam; }

  // Tunables.
  float OrbitSpeed   = 0.005f;  // radians per pixel of mouse delta
  float PanSpeed     = 0.01f;   // world units per pixel * distance
  float ZoomSpeed    = 1.10f;   // multiplicative per scroll tick
  float KeyOrbitRate = 1.5f;    // radians per second when using arrow keys
  float KeyZoomRate  = 4.0f;    // world units per second
  float MinDistance  = 0.5f;
  float MaxDistance  = 10000.0f;
  float MinPitch     = -1.55f;  // clamp just shy of poles
  float MaxPitch     =  1.55f;
  float FrameDistance = 30.0f;

  // Orbit state accessors (for scene save/load)
  float GetYaw()      const { return m_yaw; }
  float GetPitch()    const { return m_pitch; }
  float GetDistance()  const { return m_distance; }
  void  SetOrbitState(float yaw, float pitch, float dist) {
    m_yaw = yaw; m_pitch = pitch; m_distance = dist; RecomputeEye();
  }

private:
  void RecomputeEye();

  ::Camera  m_cam;
  XVECTOR3  m_target;

  // Spherical coords around m_target.
  // 3dsmax-style default: camera in the upper-right quadrant, looking down
  // at the grid from above. Yaw ≈ -43° rotates from +Z toward -X; pitch
  // ≈ +23° elevates the eye above the target.
  float m_yaw      = -0.75f;   // radians, ~-43° — upper-right quadrant
  float m_pitch    =  0.4f;    // radians, +23° — above the grid
  float m_distance = 30.0f;    // world units

  int m_viewportW = 1280;
  int m_viewportH = 720;
};

} // namespace t8ditor

#endif // T8DITOR_EDITOR_CAMERA_H
