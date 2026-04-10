#include <scene/SceneSetup.h>
#include <cstdio>

namespace t800 {

bool SceneSetup::Load(const std::string& jsonPath) {
  if (!LoadSceneDescriptor(jsonPath, descriptor)) {
    printf("[SceneSetup] Failed to load '%s'\n", jsonPath.c_str());
    return false;
  }

  name = descriptor.name;
  meshPaths = descriptor.meshes;
  environmentMap = descriptor.environment_map;

  // ── Build cameras ──
  cameras.resize(descriptor.cameras.size());
  for (size_t i = 0; i < descriptor.cameras.size(); i++) {
    auto& cd = descriptor.cameras[i];
    XVECTOR3 pos(cd.position[0], cd.position[1], cd.position[2]);
    if (cd.ortho)
      cameras[i].InitOrtho(pos, cd.width, cd.height, cd.near_plane, cd.far_plane, cd.left_handed);
    else
      cameras[i].InitPerspective(pos, cd.fov, cd.aspect, cd.near_plane, cd.far_plane, cd.left_handed);
    cameras[i].Speed = cd.speed;
    cameras[i].Eye = XVECTOR3(cd.eye[0], cd.eye[1], cd.eye[2]);
    cameras[i].Pitch = cd.pitch;
    cameras[i].Roll = cd.roll;
    cameras[i].Yaw = cd.yaw;
    cameras[i].Update(0.0f);
  }

  // ── Build light cameras ──
  lightCameras.resize(descriptor.light_cameras.size());
  for (size_t i = 0; i < descriptor.light_cameras.size(); i++) {
    auto& lc = descriptor.light_cameras[i];
    XVECTOR3 pos(lc.position[0], lc.position[1], lc.position[2]);
    if (lc.ortho)
      lightCameras[i].InitOrtho(pos, lc.width, lc.height, lc.near_plane, lc.far_plane, lc.left_handed);
    else
      lightCameras[i].InitPerspective(pos, lc.fov, lc.aspect, lc.near_plane, lc.far_plane, lc.left_handed);
    lightCameras[i].Speed = lc.speed;
    lightCameras[i].Eye = XVECTOR3(lc.eye[0], lc.eye[1], lc.eye[2]);
    lightCameras[i].Pitch = lc.pitch;
    lightCameras[i].Roll = lc.roll;
    lightCameras[i].Yaw = lc.yaw;
    lightCameras[i].Update(0.0f);
  }

  // ── Build gauss filters ──
  gaussFilters.resize(descriptor.gauss_filters.size());
  for (size_t i = 0; i < descriptor.gauss_filters.size(); i++) {
    gaussFilters[i].kernelSize = descriptor.gauss_filters[i].kernel_size;
    gaussFilters[i].radius = descriptor.gauss_filters[i].radius;
    gaussFilters[i].sigma = descriptor.gauss_filters[i].sigma;
    gaussFilters[i].Update();
  }

  // ── Build splines and agents ──
  splines.resize(descriptor.splines.size());
  agents.resize(descriptor.splines.size());
  for (size_t i = 0; i < descriptor.splines.size(); i++) {
    auto& sp = descriptor.splines[i];
    for (auto& pt : sp.points) {
      splines[i].m_points.push_back(SplinePoint(pt.position[0], pt.position[1], pt.position[2]));
      splines[i].m_points.back().m_velocity = pt.velocity;
    }
    splines[i].m_looped = sp.looped;
    splines[i].Init();

    agents[i].SetOffset(static_cast<float>(sp.agent_offset));
    agents[i].m_pSpline = &splines[i];
    agents[i].m_moving = true;
    agents[i].m_velocity = sp.agent_velocity;
  }

  printf("[SceneSetup] Built '%s': %zu cam, %zu lightcam, %zu lights, %zu gauss, %zu splines\n",
         name.c_str(), cameras.size(), lightCameras.size(),
         descriptor.lights.size(), gaussFilters.size(), splines.size());

  return true;
}

void SceneSetup::Apply(SceneProps& props) {
  // Wire cameras
  for (auto& cam : cameras)
    props.AddCamera(&cam);
  for (auto& lcam : lightCameras)
    props.AddLightCamera(&lcam);

  // Add lights
  for (auto& ld : descriptor.lights) {
    props.AddLight(
      XVECTOR3(ld.position[0], ld.position[1], ld.position[2]),
      XVECTOR3(ld.color[0], ld.color[1], ld.color[2]),
      ld.radius, ld.enabled);
  }

  // Wire gauss filters
  for (auto& gf : gaussFilters)
    props.AddGaussKernel(&gf);

  // Quality settings
  auto& q = descriptor.quality;
  props.ShadowMapResolution = q.shadow_map_resolution;
  props.GoodRaysResolution = q.god_rays_resolution;
  props.PCFScale = q.pcf_scale;
  props.PCFSamples = q.pcf_samples;
  props.ParallaxLowSamples = q.parallax_low_samples;
  props.ParallaxHighSamples = q.parallax_high_samples;
  props.ParallaxHeight = q.parallax_height;
  props.LightVolumeSteps = q.light_volume_steps;
  props.SSAOKernel.Radius = q.ssao_radius;
  props.SSAOKernel.KernelSize = q.ssao_kernel_size;
  props.SSAOKernel.Update();
  props.DOF_Near_Samples_squared = q.dof_near_samples;
  props.DOF_Far_Samples_squared = q.dof_far_samples;

  // Scene settings
  auto& s = descriptor.settings;
  props.Exposure = s.exposure;
  props.BloomFactor = s.bloom_factor;
  props.Aperture = s.aperture;
  props.FocalLength = s.focal_length;
  props.MaxCoc = s.max_coc;
  auto& ac = s.ambient_color;
  props.AmbientColor = XVECTOR3(ac[0], ac[1], ac[2]);
  props.ActiveLights = s.active_lights;
  props.ToogleShadow = s.shadow_enabled;
  props.ToogleSSAO = s.ssao_enabled;
  props.AutoFocus = s.auto_focus;
  props.ActiveGaussKernel = 0;
}

Camera* SceneSetup::GetCamera(int index) {
  return (index >= 0 && index < (int)cameras.size()) ? &cameras[index] : nullptr;
}

Camera* SceneSetup::GetLightCamera(int index) {
  return (index >= 0 && index < (int)lightCameras.size()) ? &lightCameras[index] : nullptr;
}

GaussFilter* SceneSetup::GetGaussFilter(int index) {
  return (index >= 0 && index < (int)gaussFilters.size()) ? &gaussFilters[index] : nullptr;
}

Spline* SceneSetup::GetSpline(int index) {
  return (index >= 0 && index < (int)splines.size()) ? &splines[index] : nullptr;
}

SplineAgent* SceneSetup::GetAgent(int index) {
  return (index >= 0 && index < (int)agents.size()) ? &agents[index] : nullptr;
}

} // namespace t800
