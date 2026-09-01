#pragma once

#include <cstdint>
#include <array>
#include <map>
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
  std::string id;
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
  std::string attached_light_id;
  int type = 1; // 0=perspective, 1=ortho
  Vec3f position = {25.0f, 100.0f, 0.0f};
  Vec3f target = {0.0f, 0.0f, 0.0f};
  float fov_deg = 45.0f;
  float ortho_w = 130.0f;
  float ortho_h = 130.0f;
  float near_plane = 0.1f;
  float far_plane = 600.0f;
  float yaw_rate = 0.0f;
  int attached_light = 0; // legacy fallback
  bool enabled = true;
  bool visible = true;
  bool frozen = false;
};

struct SceneGodRaysVolumeDesc {
  std::string name = "God Rays Volume";
  Vec3f position = {0.0f, 50.0f, 0.0f};
  Vec3f half_extents = {65.0f, 65.0f, 65.0f};
  int light_camera = 0;
  bool authored = false;
  bool enabled = false;
  bool clip_enabled = false;
  bool visible = false;
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

struct SceneNavMeshVolumeDesc {
  std::string name = "Nav Volume";
  std::string type = "exclude"; // include_bounds, exclude, area_cost, link_include, link_exclude
  std::string shape = "box";
  Vec3f position = {0.0f, 0.0f, 0.0f};
  Vec3f rotation = {0.0f, 0.0f, 0.0f};
  Vec3f half_extents = {8.0f, 4.0f, 8.0f};
  std::string area = "walkable";
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
  std::string runtime_mode = "build_cached"; // build_cached, build, baked_asset
  std::string baked_asset;
  SceneNavMeshBuildSettingsDesc build_settings;
  std::vector<SceneNavMeshVolumeDesc> volumes;
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

struct SceneComponentDesc {
  std::string id;
  std::string type;
  bool enabled = true;
  std::map<std::string, std::string> params;
  std::string config_json;
};

struct SceneControlDesc {
  std::string mode = "none";
  std::string controller;
  int player_slot = 0;
};

struct SceneStateDesc {
  std::string name;
  std::map<std::string, std::string> params;
  float default_duration = -1.0f;
};

struct SceneTransitionDesc {
  std::string from_state;
  std::string to_state;
  std::string condition;
  float priority = 0.0f;
  float cooldown = 0.0f;
};

struct SceneStateMachineDesc {
  std::string initial_state = "idle";
  std::vector<SceneStateDesc> states;
  std::vector<SceneTransitionDesc> transitions;
};

struct SceneFlockConfigDesc {
  float separation_weight = 1.0f;
  float alignment_weight = 0.8f;
  float cohesion_weight = 0.6f;
  float separation_radius = 2.0f;
  float neighbor_radius = 5.0f;
  float max_speed = 10.0f;
};

struct SceneFormationConfigDesc {
  std::string type = "wedge";
  float spacing = 3.0f;
  float depth_step = 3.0f;
  std::string leader_entity_id;
};

struct SceneGroupDesc {
  std::string id;
  std::string name;
  std::string strategy = "formation";
  std::vector<std::string> member_entity_ids;
  SceneFlockConfigDesc flock;
  SceneFormationConfigDesc formation;
};

struct SceneSpatialGridSettingsDesc {
  bool enabled = false;
  float cell_size = 4.0f;
  int grid_width = 256;
  int grid_depth = 256;
};

struct SceneGameLogicSettingsDesc {
  int schema_version = 2;
  float fixed_delta_seconds = 1.0f / 60.0f;
  int max_steps_per_frame = 4;
  SceneSpatialGridSettingsDesc spatial_grid;
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
  std::string id;
  int team = -1;
  SceneControlDesc control;
  std::string group_id;
  std::vector<SceneComponentDesc> components;
  std::optional<SceneStateMachineDesc> behavior;
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

struct SceneVoxelBlockDesc {
  std::string name;
  bool opaque = true;
  bool solid = true;
  // Face order: +X, -X, +Y, -Y, +Z, -Z. Each pair is atlas tile u,v.
  std::array<int, 12> tiles = {};
  std::array<int, 4> color = {255, 255, 255, 255};
};

struct SceneVoxelOreDesc {
  std::string block;
  float threshold = 1.0f;
  int min_depth = 0;
};

struct SceneVoxelTerrainDesc {
  float base_frequency = 0.01f;
  int base_octaves = 4;
  float base_lacunarity = 2.0f;
  float base_gain = 0.5f;
  int base_height = 20;
  float base_amplitude = 20.0f;
  float mountain_frequency = 0.005f;
  int mountain_octaves = 3;
  float mountain_lacunarity = 2.0f;
  float mountain_gain = 0.5f;
  float mountain_amplitude = 20.0f;
  float cave_frequency = 0.08f;
  float cave_threshold = 0.72f;
  float tree_threshold = 0.985f;
  int tree_min_height = 4;
  int tree_height_variation = 3;
  int tree_max_surface_height = 50;
  int surface_depth = 3;
  int cave_min_y = 2;
  std::string air_block = "air";
  std::string bedrock_block = "bedrock";
  std::string stone_block = "stone";
  std::string dirt_block = "dirt";
  std::string grass_block = "grass";
  std::string sand_block = "sand";
  std::string water_block = "water";
  std::string log_block = "log";
  std::string leaves_block = "leaves";
  std::vector<SceneVoxelOreDesc> ores;
};

struct SceneVoxelPlayerDesc {
  Vec3f spawn = {0.5f, 40.0f, 0.5f};
  float walk_speed = 4.3f;
  float sprint_speed = 5.6f;
  float ground_acceleration = 30.0f;
  float air_acceleration = 3.0f;
  float friction = 8.0f;
  float stop_speed = 2.0f;
  float gravity = 24.0f;
  float jump_speed = 8.0f;
  float capsule_radius = 0.3f;
  float capsule_half_height = 0.9f;
  float eye_height = 1.62f;
  float ground_probe_distance = 0.25f;
  float step_height = 0.5f;
  float min_walk_normal_y = 0.70f;
  bool allow_sprint = true;
  bool air_control = true;
  float mouse_sensitivity = 0.0025f;
  float debug_camera_speed = 50.0f;
  float look_pitch_limit = 1.55f;
  float collision_sweep_step = 0.25f;
};

struct SceneVoxelDayNightDesc {
  bool enabled = true;
  bool trajectory_paused = false;
  float time_of_day = 0.25f;
  float day_length_seconds = 120.0f;
  float animation_speed = 0.1f;
  float orbit_phase = 0.0f;
  float orbit_radius = 140.0f;
  Vec3f orbit_center = {0.0f, 40.0f, 0.0f};
  Vec3f orbit_horizontal = {1.0f, 0.0f, 0.0f};
  Vec3f orbit_vertical = {0.0f, 1.0f, 0.0f};
  float horizon_offset = 0.15f;
  float ambient_night = 0.12f;
  float ambient_day = 0.47f;
  Vec3f ambient_tint = {0.6f, 0.7f, 1.0f};
  Vec3f manual_ambient = {0.282f, 0.329f, 0.47f};
  float sun_intensity_night = 2.0f;
  float sun_intensity_day = 6.0f;
  Vec3f sun_color_low = {1.0f, 0.6f, 0.4f};
  Vec3f sun_color_high = {1.0f, 1.0f, 1.0f};
};

struct SceneVoxelBoxPartDesc {
  Vec3f min;
  Vec3f max;
  std::string block;
};

struct SceneVoxelMobDesc {
  Vec3f spawn = {24.5f, 40.0f, 24.5f};
  int count = 1;
  float move_speed = 1.8f;
  float repath_seconds = 1.0f;
  float waypoint_distance = 0.2f;
  float player_avoidance_radius = 1.35f;
  float visual_ground_clearance = 0.001f;
  float half_width = 0.25f;
  float height = 1.4f;
  float vertical_follow_speed = 8.0f;
  std::vector<SceneVoxelBoxPartDesc> parts;
};

struct SceneVoxelMaterialDesc {
  std::string diffuse_texture = "lens1.png";
  float roughness = 0.9f;
  float specular = 0.04f;
};

struct SceneVoxelDofDesc {
  bool normalized_focus = true;
  float focus_range = 0.5f;
  float focus_falloff = 8.0f;
  float auto_focus_radius = 0.05f;
};

struct SceneVoxelDebugTargetDesc {
  std::string label;
  std::string source;
};

struct SceneVoxelWeaponDesc {
  float scale = 0.30f;
  float offset_right = 0.45f;
  float offset_down = 0.45f;
  float offset_forward = 0.55f;
  float bob_speed = 8.0f;
  float bob_vertical = 0.03f;
  float bob_horizontal = 0.02f;
  float swing_speed = 6.0f;
  float swing_angle = 1.2f;
  std::vector<SceneVoxelBoxPartDesc> parts;
};

struct SceneVoxelInteractionDesc {
  float reach = 8.0f;
  float break_cooldown = 0.25f;
  float place_cooldown = 0.25f;
};

struct SceneVoxelWorldDesc {
  int schema_version = 1;
  int seed = 1337;
  int chunk_size = 16;
  int world_height = 64;
  int water_level = 32;
  int render_distance = 4;
  int streaming_recenter_threshold = 2;
  int max_uploads_per_frame = 2;
  bool async_streaming = true;
  int atlas_size = 256;
  int atlas_tiles_per_axis = 16;
  // When non-empty, the voxel renderer samples this image file (under
  // Assets/Textures/) as the block atlas instead of generating a solid-color
  // one. atlas_tile_px is the logical tile size. atlas_pixelation_factor
  // optionally reduces source detail and nearest-expands it while preserving
  // that grid.
  std::string atlas_texture;
  int atlas_tile_px = 16;
  int atlas_pixelation_factor = 1;
  float navmesh_rebuild_seconds = 0.5f;
  std::string environment_map = "sky/CubeMap_SkyWater.dds";
  std::vector<std::string> environment_options;
  bool show_physics = false;
  bool show_chunk_bounds = false;
  bool show_lights = false;
  float sun_debug_size = 2.0f;
  Vec3f collision_debug_color = {1.0f, 0.85f, 0.1f};
  Vec3f chunk_debug_color = {0.1f, 0.85f, 1.0f};
  bool show_navmesh = false;
  bool frustum_culling = true;
  bool show_culling_debug = false;
  bool show_cascade_debug = true;
  int cascade_debug_mode = 0;
  float cascade_debug_opacity = 0.12f;
  std::vector<Vec3f> cascade_debug_colors = {
    {1.0f, 0.2f, 0.2f}, {0.2f, 1.0f, 0.2f}, {0.2f, 0.4f, 1.0f},
    {1.0f, 1.0f, 0.2f}, {1.0f, 0.4f, 1.0f}, {0.2f, 1.0f, 1.0f}
  };
  int camera_mode = 0;
  int debug_cascade_index = 0;
  int debug_render_target = 0;
  std::vector<SceneVoxelDebugTargetDesc> debug_render_targets;
  int active_lights = 1;
  SceneVoxelTerrainDesc terrain;
  SceneVoxelPlayerDesc player;
  SceneVoxelDayNightDesc day_night;
  SceneVoxelMaterialDesc material;
  SceneVoxelDofDesc dof;
  SceneVoxelMobDesc mob;
  SceneVoxelWeaponDesc weapon;
  SceneVoxelInteractionDesc interaction;
  std::vector<SceneVoxelBlockDesc> blocks;
  std::vector<std::string> hotbar;
};

struct EditorSceneFile {
  int version = 2;  // v2 adds stable light IDs; v1 still loads (migrated in memory)
  std::string collision;
  std::string render_graph;
  std::string control_descriptor;
  EditorStateDesc editor;
  std::vector<SceneObjectDesc> objects;
  std::vector<SceneGameEntityDesc> game_entities;
  std::vector<SceneGroupDesc> game_groups;
  std::optional<SceneGameLogicSettingsDesc> game_logic_settings;
  std::vector<ScenePhysicsEntityDesc> physics_entities;
  std::optional<SceneNavigationMeshDesc> navigation_mesh;
  std::vector<SceneSplineDesc> splines;
  std::vector<SceneCameraDesc> cameras;
  std::vector<SceneLightCameraDesc> light_cameras;
  std::vector<SceneCameraAnimationDesc> camera_animations;
  std::optional<SceneGodRaysVolumeDesc> god_rays_volume;
  std::optional<SceneVoxelWorldDesc> voxel_world;
  std::vector<SceneLightDesc> lights;
  std::vector<::t850::SandboxProfileDesc> profiles;
};

bool LoadEditorSceneFile(const std::string& path, EditorSceneFile& scene, std::string* error = nullptr);
bool SaveEditorSceneFile(const EditorSceneFile& scene, const std::string& path, std::string* error = nullptr);

} // namespace t850::scene
