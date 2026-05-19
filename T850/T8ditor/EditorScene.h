/*********************************************************
 * T8ditor — Scene file format and save/load.
 *
 * Defines the JSON scene format used by both the editor
 * and the game runtime. Uses the glaze library for
 * serialization (plain aggregates, no macros needed).
 *********************************************************/

#ifndef T8DITOR_EDITOR_SCENE_H
#define T8DITOR_EDITOR_SCENE_H

#include <scene/EditorSceneFile.h>

#include <string>

namespace t8ditor {

// ── Scene file format ────────────────────────────────

using Vec3f = t850::scene::Vec3f;
using SceneObjectDesc = t850::scene::SceneObjectDesc;
using SceneCameraDesc = t850::scene::SceneCameraDesc;
using SceneLightDesc = t850::scene::SceneLightDesc;
using EditorStateDesc = t850::scene::EditorStateDesc;
using SceneFile = t850::scene::EditorSceneFile;

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
