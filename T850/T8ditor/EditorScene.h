/*********************************************************
 * T8ditor — Scene file format and save/load.
 *
 * Defines the JSON scene format used by both the editor
 * and the game runtime. Uses the glaze library for
 * serialization (plain aggregates, no macros needed).
 *********************************************************/

#ifndef T8DITOR_EDITOR_SCENE_H
#define T8DITOR_EDITOR_SCENE_H

#include <string>
#include <vector>

namespace t8ditor {

// ── Scene file format ────────────────────────────────

struct Vec3f { float x = 0, y = 0, z = 0; };

struct SceneObjectDesc {
  std::string name;
  std::string mesh;          // path relative to project root
  Vec3f       position;
  Vec3f       rotation;      // degrees (human-readable)
  Vec3f       scale = {1,1,1};
};

struct SceneCameraDesc {
  std::string name       = "Camera";
  int         type       = 0;  // 0=perspective, 1=ortho
  Vec3f       position   = {0, 5, -10};
  Vec3f       target     = {0, 0, 0};
  float       fov_deg    = 50.0f;
  float       ortho_w    = 20.0f;
  float       ortho_h    = 15.0f;
  float       near_plane = 0.1f;
  float       far_plane  = 1000.0f;
};

struct SceneLightDesc {
  std::string name       = "Light";
  int         type       = 0;  // 0=directional, 1=omni
  Vec3f       position   = {0, 10, 0};
  Vec3f       direction  = {0, -1, 0};
  Vec3f       color      = {1, 1, 1};
  float       intensity  = 1.5f;
  float       radius     = 10.0f;
  bool        enabled    = true;
};

struct EditorStateDesc {
  Vec3f camera_target;
  float camera_yaw      = -0.75f;
  float camera_pitch    =  0.4f;
  float camera_distance = 30.0f;
  bool  show_skybox     = true;
  bool  show_wireframe  = false;
};

struct SceneFile {
  int                            version = 1;
  EditorStateDesc                editor;
  std::vector<SceneObjectDesc>   objects;
  std::vector<SceneCameraDesc>   cameras;
  std::vector<SceneLightDesc>    lights;
};

// ── Save / Load ──────────────────────────────────────

// Save scene to a JSON file. Returns true on success.
bool SaveSceneToFile(const SceneFile& scene, const std::string& path);

// Load scene from a JSON file. Returns true on success, fills `scene`.
bool LoadSceneFromFile(const std::string& path, SceneFile& scene);

// ── File dialog ──────────────────────────────────────

// Opens a native Windows save-file dialog. Returns empty string on cancel.
std::string SaveFileDialog(const wchar_t* filter, const wchar_t* title,
                           const wchar_t* defaultExt = nullptr);

} // namespace t8ditor

#endif // T8DITOR_EDITOR_SCENE_H
