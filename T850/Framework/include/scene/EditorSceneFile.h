#pragma once

#include <cstdint>
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

struct SceneObjectPhysicsDesc {
  bool enabled = false;
  std::string body_type = "none";
  std::string motion = "static";
  std::string collision_layer = "world";
  bool generate_collision = false;
  std::string collision_asset;
};

struct SceneObjectNavigationDesc {
  bool include = true;
  bool walkable = true;
  bool static_object = true;
  std::string area = "walkable";
  float cost = 1.0f;
};

struct SceneObjectRagdollDesc {
  bool enabled = false;
  std::string asset;
  bool preview = false;
  bool drive_from_animation = true;
  std::string runtime_motion = "disabled";
};

struct SceneObjectDesc {
  std::string name;
  std::string mesh;
  std::string ragdoll;
  Vec3f position;
  Vec3f rotation;
  Vec3f scale = {1.0f, 1.0f, 1.0f};
  bool visible = true;
  std::optional<bool> mobile_visible;
  bool frozen = false;
  bool show_wire = false;
  bool show_orientation = false;
  std::optional<float> nav_agent_front_yaw_offset_deg;
  std::optional<float> nav_agent_face_yaw_sign;
  std::string nav_agent_target_mode = "direct";
  float nav_agent_follow_distance = 0.0f;
  float nav_agent_side_offset = 0.0f;
  float nav_agent_formation_depth_step = 0.0f;
  int nav_agent_slot = -1;
  std::optional<SceneObjectPhysicsDesc> physics;
  std::optional<SceneObjectNavigationDesc> navigation;
  std::optional<SceneObjectRagdollDesc> ragdoll_authoring;
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

struct SceneLightCameraDesc {
  std::string name = "Light Camera";
  int type = 1; // 0=perspective, 1=ortho
  Vec3f position = {25.0f, 100.0f, 0.0f};
  Vec3f target = {0.0f, 0.0f, 0.0f};
  float fov_deg = 45.0f;
  float ortho_w = 130.0f;
  float ortho_h = 130.0f;
  float near_plane = 0.1f;
  float far_plane = 600.0f;
  float yaw_rate = 0.0f;
  int attached_light = 0;
  bool enabled = true;
  bool visible = true;
  bool frozen = false;
};

struct SceneGodRaysVolumeDesc {
  std::string name = "God Rays Volume";
  Vec3f position = {0.0f, 50.0f, 0.0f};
  Vec3f half_extents = {65.0f, 65.0f, 65.0f};
  bool enabled = true;
  bool clip_enabled = false;
  bool visible = true;
  bool frozen = false;
  bool show_wire = true;
};

struct SceneCameraAnimationKeyframeDesc {
  float time = 0.0f;
  std::optional<Vec3f> position;
  std::optional<Vec3f> target;
  std::optional<Vec3f> rotation;
  std::optional<float> fov_deg;
  std::optional<float> ortho_w;
  std::optional<float> ortho_h;
};

struct SceneCameraAnimationDesc {
  std::string name = "Camera Animation";
  std::string target = "camera"; // camera or light_camera
  int camera = 0;
  bool enabled = true;
  bool play_on_timeline = true;
  bool loop = true;
  float start_time = 0.0f;
  float duration = 0.0f;
  Vec3f linear_velocity = {0.0f, 0.0f, 0.0f};
  Vec3f target_velocity = {0.0f, 0.0f, 0.0f};
  Vec3f angular_velocity = {0.0f, 0.0f, 0.0f};
  std::vector<SceneCameraAnimationKeyframeDesc> keyframes;
};

struct ScenePhysicsCookSettingsDesc {
  uint32_t max_triangles_per_leaf = 8;
  std::string build_quality = "runtime_performance";
  float active_edge_cos_threshold_angle = 0.996195f;
  bool per_triangle_user_data = false;
  bool use_disk_cache = true;
};

struct ScenePhysicsCharacterDesc {
  std::string runtime_path = "kinematic";
  std::string implementation = "virtual";
  float bot_radius = 2.0f;
  float mass = 70.0f;
  float max_strength = 100.0f;
  float max_slope_angle_deg = 50.0f;
  bool enhanced_internal_edge_removal = true;
  float supporting_volume_offset = -1.0e10f;
  Vec3f shape_offset;
  std::string back_face_mode = "collide";
  float predictive_contact_distance = 0.1f;
  int max_collision_iterations = 5;
  int max_constraint_iterations = 15;
  float min_time_remaining = 1.0e-4f;
  float collision_tolerance = 1.0e-3f;
  float character_padding = 0.02f;
  int max_num_hits = 256;
  float hit_reduction_cos_max_angle = 0.999f;
  float penetration_recovery_speed = 1.0f;
  float gravity_factor = 1.0f;
  bool allow_translation_x = true;
  bool allow_translation_y = true;
  bool allow_translation_z = true;
  bool inner_body = false;
};

struct SceneNavMeshBuildSettingsDesc {
  float cell_size = 0.30f;
  float cell_height = 0.20f;
  float agent_height = 2.0f;
  float agent_radius = 0.6f;
  float agent_max_climb = 0.9f;
  float agent_max_slope = 45.0f;
  float region_min_size = 8.0f;
  float region_merge_size = 20.0f;
  float edge_max_len = 12.0f;
  float edge_max_error = 1.3f;
  int verts_per_poly = 6;
  float detail_sample_dist = 6.0f;
  float detail_sample_max_error = 1.0f;
  Vec3f query_extents = {2.0f, 4.0f, 2.0f};
  bool auto_drop_links = true;
  float drop_min_height = 1.0f;
  float drop_max_height = 24.0f;
  float drop_max_horizontal = 2.4f;
  float drop_sample_spacing = 1.2f;
  float drop_link_radius = 0.75f;
  bool auto_jump_links = true;
  float jump_max_horizontal = 7.0f;
  float jump_sample_spacing = 1.2f;
  float jump_link_radius = 0.75f;
  bool hybrid_jump_links = true;
  int hybrid_max_links = 192;
  uint64_t off_mesh_link_validation_key = 0;
};

struct SceneNavMeshLinkDesc {
  std::string name = "Nav Link";
  std::string type = "jump";
  int start_node = -1;
  int end_node = -1;
  Vec3f start;
  Vec3f end;
  float radius = 0.75f;
  bool bidirectional = false;
  float cost = 1.0f;
  bool enabled = true;
  bool visible = true;
  bool frozen = false;
  bool show_wire = true;
};

struct SceneNavigationMeshDesc {
  std::string name = "NavMesh";
  bool enabled = false;
  bool visible = true;
  bool frozen = false;
  bool show_wire = true;
  float debug_offset = 0.01f;
  int debug_shape_mode = 0;
  SceneNavMeshBuildSettingsDesc build_settings;
  std::vector<SceneNavMeshLinkDesc> authored_links;
};

struct SceneSplinePointDesc {
  Vec3f position;
  float velocity = 7.0f;
  Vec3f rotation;
  bool look_at_center = true;
};

struct SceneSplineDesc {
  std::string name = "Spline";
  std::vector<SceneSplinePointDesc> points;
  bool looped = false;
  float agent_velocity = 15.0f;
  float agent_offset = 0.0f;
  int attached_camera = 0;
  bool play_on_start = true;
  bool visible = true;
  bool frozen = false;
  bool show_wire = true;
};

struct ScenePhysicsEntityDesc {
  std::string name;
  std::string type = "static_triangle_mesh";
  std::string source_object;
  Vec3f position;
  Vec3f rotation;
  Vec3f scale = {1.0f, 1.0f, 1.0f};
  bool visible = true;
  bool frozen = false;
  bool show_wire = true;
  bool show_orientation = false;
  std::string shape = "box";
  Vec3f half_extents = {16.0f, 32.0f, 16.0f};
  float radius = 16.0f;
  float half_height = 24.0f;
  float friction = 0.6f;
  float restitution = 0.0f;
  bool sensor = false;
  ScenePhysicsCookSettingsDesc cook_settings;
  ScenePhysicsCharacterDesc character;
};

struct SceneGameEntityDesc {
  std::string name = "Game Entity";
  std::string kind = "generic";
  std::string mesh_object;
  std::string primary_physics_entity;
  std::vector<std::string> physics_entities;
  std::string camera;
  std::string ragdoll_object;
  std::string ai;
  bool visible = true;
  bool frozen = false;
  bool show_wire = true;
};

struct EditorStateDesc {
  Vec3f camera_target;
  float camera_yaw = -0.75f;
  float camera_pitch = 0.4f;
  float camera_distance = 30.0f;
  bool show_skybox = true;
  bool show_wireframe = false;
  bool allow_custom_layout = false;
  std::string imgui_layout;
};

struct EditorSceneFile {
  int version = 1;
  std::string collision;
  std::string render_graph;
  EditorStateDesc editor;
  std::vector<SceneObjectDesc> objects;
  std::vector<SceneGameEntityDesc> game_entities;
  std::vector<ScenePhysicsEntityDesc> physics_entities;
  std::optional<SceneNavigationMeshDesc> navigation_mesh;
  std::vector<SceneSplineDesc> splines;
  std::vector<SceneCameraDesc> cameras;
  std::vector<SceneLightCameraDesc> light_cameras;
  std::vector<SceneCameraAnimationDesc> camera_animations;
  std::optional<SceneGodRaysVolumeDesc> god_rays_volume;
  std::vector<SceneLightDesc> lights;
  std::vector<::t850::SandboxProfileDesc> profiles;
};

bool LoadEditorSceneFile(const std::string& path, EditorSceneFile& scene, std::string* error = nullptr);
bool SaveEditorSceneFile(const EditorSceneFile& scene, const std::string& path, std::string* error = nullptr);

} // namespace t850::scene
