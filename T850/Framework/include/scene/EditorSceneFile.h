#pragma once

#include <optional>
#include <scene/SceneDescriptor.h>
#include <string>
#include <vector>

namespace t850::scene {

struct Vec3f {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct SceneObjectDesc {
  std::string name;
  std::string mesh;
  std::string ragdoll;
  Vec3f position;
  Vec3f rotation;
  Vec3f scale = {1.0f, 1.0f, 1.0f};
  bool visible = true;
  bool frozen = false;
  bool show_wire = false;
};

struct SceneCameraDesc {
  std::string name = "Camera";
  int type = 0; // 0=perspective, 1=ortho
  Vec3f position = {0.0f, 5.0f, -10.0f};
  Vec3f target = {0.0f, 0.0f, 0.0f};
  float fov_deg = 50.0f;
  float ortho_w = 20.0f;
  float ortho_h = 15.0f;
  float near_plane = 0.1f;
  float far_plane = 1000.0f;
  bool visible = true;
  bool frozen = false;
};

struct SceneQ3LightDesc {
  std::string source;
  std::string classname = "light";
  float light = 0.0f;
  std::optional<float> radius;
  std::string target;
  bool targeted = false;
  std::string spawnflags;
  std::string angle;
  std::string origin;
  std::string color;
};

struct SceneLightDesc {
  std::string name = "Light";
  int type = 0; // 0=directional, 1=omni/point
  Vec3f position = {0.0f, 10.0f, 0.0f};
  Vec3f direction = {0.0f, -1.0f, 0.0f};
  Vec3f color = {1.0f, 1.0f, 1.0f};
  float intensity = 1.5f;
  float radius = 10.0f;
  bool enabled = true;
  bool visible = true;
  bool frozen = false;
  std::optional<SceneQ3LightDesc> q3;
};

struct EditorStateDesc {
  Vec3f camera_target;
  float camera_yaw = -0.75f;
  float camera_pitch = 0.4f;
  float camera_distance = 30.0f;
  bool show_skybox = true;
  bool show_wireframe = false;
};

struct EditorSceneFile {
  int version = 1;
  std::string collision;
  EditorStateDesc editor;
  std::vector<SceneObjectDesc> objects;
  std::vector<SceneCameraDesc> cameras;
  std::vector<SceneLightDesc> lights;
  std::vector<::t850::SandboxProfileDesc> profiles;
};

bool LoadEditorSceneFile(const std::string& path, EditorSceneFile& scene, std::string* error = nullptr);
bool SaveEditorSceneFile(const EditorSceneFile& scene, const std::string& path, std::string* error = nullptr);

} // namespace t850::scene
