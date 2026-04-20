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

struct EditorStateDesc {
  Vec3f camera_target;
  float camera_yaw      = -0.75f;
  float camera_pitch    =  0.4f;
  float camera_distance = 30.0f;
  bool  show_skybox     = true;
  bool  show_wireframe  = false;
};

struct SceneFile {
  int                           version = 1;
  EditorStateDesc               editor;
  std::vector<SceneObjectDesc>  objects;
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
