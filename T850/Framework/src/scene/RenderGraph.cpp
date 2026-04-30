#include <pch.h>
#include <scene/RenderGraph.h>
#include <scene/RenderGraphDescriptor.h>
#include <scene/SceneProp.h>
#include <scene/PrimitiveInstance.h>
#include <utils/Camera.h>
#include <video/BaseDriver.h>
#include <Descriptors.h>
#include <utils/Log.h>

#pragma warning(push)
#pragma warning(disable: 4267 4244)
#include <glaze/glaze.hpp>
#pragma warning(pop)

#include <debug/Profiler.h>

#include <fstream>
#include <sstream>
#include <cstdio>
#include <unordered_map>

namespace t850 {

// ---- JSON loading via glaze ----

bool LoadRenderGraphDescriptor(const std::string& path, RenderGraphDesc& desc) {
  std::ifstream file(path);
  if (!file.is_open()) {
    T8_LOG_ERROR("[RenderGraph] Cannot open '%s'", path.c_str());
    return false;
  }

  std::stringstream ss;
  ss << file.rdbuf();
  std::string json = ss.str();

  auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(desc, json);
  if (ec) {
    std::string err = glz::format_error(ec, json);
    T8_LOG_ERROR("[RenderGraph] Parse error '%s': %s", path.c_str(), err.c_str());
    return false;
  }

  T8_LOG_INFO("[RenderGraph] Loaded '%s': %zu render targets, %zu passes",
              path.c_str(), desc.render_targets.size(), desc.passes.size());
  return true;
}

// ---- String -> enum resolution tables ----

static const std::unordered_map<std::string, int> s_attachmentMap = {
  {"DEPTH",  BaseDriver::DEPTH_ATTACHMENT},
  {"COLOR0", BaseDriver::COLOR0_ATTACHMENT},
  {"COLOR1", BaseDriver::COLOR1_ATTACHMENT},
  {"COLOR2", BaseDriver::COLOR2_ATTACHMENT},
  {"COLOR3", BaseDriver::COLOR3_ATTACHMENT},
  {"COLOR4", BaseDriver::COLOR4_ATTACHMENT},
  {"COLOR5", BaseDriver::COLOR5_ATTACHMENT},
  {"COLOR6", BaseDriver::COLOR6_ATTACHMENT},
  {"COLOR7", BaseDriver::COLOR7_ATTACHMENT},
};

static const std::unordered_map<std::string, int> s_colorFormatMap = {
  {"NONE",    BaseRT::NOTHING},
  {"RGBA8",   BaseRT::RGBA8},
  {"RGBA16F", BaseRT::RGBA16F},
  {"RGBA32F", BaseRT::RGBA32F},
  {"R8",      BaseRT::R8},
  {"F16",     BaseRT::F16},
  {"RGB8",    BaseRT::RGB8},
};

static const std::unordered_map<std::string, int> s_depthFormatMap = {
  {"NONE",    BaseRT::NOTHING},
  {"F32",     BaseRT::F32},
  {"CUBE_F32", BaseRT::CUBE_F32},
};

// Pass name -> PassType::E
static const std::unordered_map<std::string, uint8_t> s_passMap = {
  {"FORWARD_PASS",          PassType::FORWARD},
  {"GBUFF_PASS",            PassType::GBUFFER},
  {"SHADOW_MAP_PASS",       PassType::SHADOW_MAP},
  {"FSQUAD_1_TEX",          PassType::FSQUAD_1_TEX},
  {"FSQUAD_2_TEX",          PassType::FSQUAD_2_TEX},
  {"FSQUAD_3_TEX",          PassType::FSQUAD_3_TEX},
  {"DEFERRED_PASS",         PassType::DEFERRED},
  {"SHADOW_COMP_PASS",      PassType::SHADOW_COMP},
  {"VERTICAL_BLUR_PASS",    PassType::VERTICAL_BLUR},
  {"HORIZONTAL_BLUR_PASS",  PassType::HORIZONTAL_BLUR},
  {"BRIGHT_PASS",           PassType::BRIGHT},
  {"HDR_COMP_PASS",         PassType::HDR_COMP},
  {"LUMINANCE_MAP_PASS",    PassType::LUMINANCE_MAP},
  {"ADAPT_LUMINANCE_PASS",  PassType::ADAPT_LUMINANCE},
  {"COC_PASS",              PassType::COC},
  {"COMBINE_COC_PASS",      PassType::COMBINE_COC},
  {"DOF_PASS",              PassType::DOF},
  {"DOF_PASS_2",            PassType::DOF_2},
  {"BACKBUFFER_PASS",       PassType::BACKBUFFER},
  {"RAY_MARCH",             PassType::RAY_MARCH},
  {"RADIAL_DEPTH_PASS",     PassType::RADIAL_DEPTH},
  {"LIGHT_RAY_MARCHING",    PassType::LIGHT_RAY_MARCHING},
  {"LIGHT_ADD",             PassType::LIGHT_ADD},
  {"FADE_PASS",             PassType::FADE},
  {"GOD_RAY_CALCULATION_PASS", PassType::GOD_RAY_CALCULATION},
  {"GOD_RAY_BLEND_PASS",    PassType::GOD_RAY_BLEND},
  {"SSAO_PASS",             PassType::SSAO},
  {"DEFERRED_LDR_PASS",     PassType::DEFERRED_LDR},
};

// Feature name -> ShaderKey feature bit
static const std::unordered_map<std::string, uint64_t> s_featureMap = {
  {"USE_OMNIDIRECTIONAL_SHADOWS", ShaderKey::OMNI_SHADOWS},
};

static const std::unordered_map<std::string, int> s_depthStencilMap = {
  {"READ_WRITE", BaseDriver::READ_WRITE},
  {"READ",       BaseDriver::READ},
  {"NONE",       BaseDriver::NONE},
};

static const std::unordered_map<std::string, int> s_cullFaceMap = {
  {"FRONT_FACES",    BaseDriver::FRONT_FACES},
  {"BACK_FACES",     BaseDriver::BACK_FACES},
  {"FRONT_AND_BACK", BaseDriver::FRONT_AND_BACK},
};

static const std::unordered_map<std::string, int> s_blendMap = {
  {"BLEND_DEFAULT",     BaseDriver::BLEND_DEFAULT},
  {"BLEND_OPAQUE",      BaseDriver::BLEND_OPAQUE},
  {"ADDITIVE",          BaseDriver::ADDITIVE},
  {"ALPHA_BLEND",       BaseDriver::ALPHA_BLEND},
  {"NON_PREMULTIPLIED", BaseDriver::NON_PREMULTIPLIED},
};

int RenderGraph::ResolveAttachment(const std::string& name) {
  auto it = s_attachmentMap.find(name);
  return (it != s_attachmentMap.end()) ? it->second : BaseDriver::COLOR0_ATTACHMENT;
}

int RenderGraph::ResolveColorFormat(const std::string& name) {
  auto it = s_colorFormatMap.find(name);
  return (it != s_colorFormatMap.end()) ? it->second : BaseRT::RGBA8;
}

int RenderGraph::ResolveDepthFormat(const std::string& name) {
  auto it = s_depthFormatMap.find(name);
  return (it != s_depthFormatMap.end()) ? it->second : BaseRT::NOTHING;
}

ShaderKey RenderGraph::ResolveSignature(const std::string& name) {
  ShaderKey key(0);
  auto pit = s_passMap.find(name);
  if (pit != s_passMap.end()) {
    key.setPass(pit->second);
    return key;
  }
  auto fit = s_featureMap.find(name);
  if (fit != s_featureMap.end()) {
    key.bits = fit->second;
    return key;
  }
  T8_LOG_ERROR("[RenderGraph] Unknown signature '%s'", name.c_str());
  return key;
}

int RenderGraph::ResolveDepthStencilState(const std::string& name) {
  if (name.empty()) return -1;
  auto it = s_depthStencilMap.find(name);
  return (it != s_depthStencilMap.end()) ? it->second : -1;
}

int RenderGraph::ResolveCullFace(const std::string& name) {
  if (name.empty()) return -1;
  auto it = s_cullFaceMap.find(name);
  return (it != s_cullFaceMap.end()) ? it->second : -1;
}

int RenderGraph::ResolveBlendState(const std::string& name) {
  if (name.empty()) return -1;
  auto it = s_blendMap.find(name);
  return (it != s_blendMap.end()) ? it->second : -1;
}

// ---- Parse "RTName:ATTACHMENT" ----

RenderGraph::ResolvedTexture RenderGraph::ResolveTextureInput(const std::string& source) const {
  ResolvedTexture result{};
  result.rt_handle = -1;
  result.attachment = BaseDriver::COLOR0_ATTACHMENT;
  result.is_builtin = false;

  if (source.empty()) return result;

  // Built-in textures: @ssao_noise, @environment_map
  if (source[0] == '@') {
    result.is_builtin = true;
    result.builtin = source;
    return result;
  }

  // Parse "RTName:ATTACHMENT"
  auto colon = source.find(':');
  if (colon == std::string::npos) {
    // Just an RT name, default to COLOR0
    auto it = m_rtHandles.find(source);
    if (it != m_rtHandles.end()) result.rt_handle = it->second;
    else T8_LOG_ERROR("[RenderGraph] Unknown RT '%s'", source.c_str());
    return result;
  }

  std::string rtName = source.substr(0, colon);
  std::string attName = source.substr(colon + 1);

  auto it = m_rtHandles.find(rtName);
  if (it != m_rtHandles.end()) result.rt_handle = it->second;
  else T8_LOG_ERROR("[RenderGraph] Unknown RT '%s' in '%s'", rtName.c_str(), source.c_str());

  result.attachment = ResolveAttachment(attName);
  return result;
}

// ---- Load & Build ----

bool RenderGraph::Load(const std::string& path) {
  if (!LoadRenderGraphDescriptor(path, m_desc))
    return false;

  // Graph is built after CreateRenderTargets (needs RT handle map)
  return true;
}

void RenderGraph::CreateRenderTargets(BaseDriver* driver, const SceneProps& props) {
  m_rtHandles.clear();

  for (const auto& rt : m_desc.render_targets) {
    int cf = ResolveColorFormat(rt.color_format);
    int df = ResolveDepthFormat(rt.depth_format);

    int w = rt.size[0];
    int h = rt.size[1];

    if (!rt.size_ref.empty()) {
      if (rt.size_ref == "$shadow_resolution") {
        w = h = static_cast<int>(props.ShadowMapResolution);
      } else if (rt.size_ref == "$god_rays_resolution") {
        w = h = static_cast<int>(props.GoodRaysResolution);
      }
    }

    int handle;
    if (!rt.color_formats.empty()) {
      // Per-attachment formats specified in JSON
      std::vector<int> perCF;
      for (const auto& fmt : rt.color_formats)
        perCF.push_back(ResolveColorFormat(fmt));
      handle = driver->CreateRT(rt.color_count, perCF, df, w, h, rt.linear_filter);
    } else {
      handle = driver->CreateRT(rt.color_count, cf, df, w, h, rt.linear_filter);
    }
    m_rtHandles[rt.name] = handle;

    T8_LOG_INFO("[RenderGraph] Created RT '%s' -> handle %d (%dx%d, %d colors, cf=%s, df=%s)",
                rt.name.c_str(), handle, w, h, rt.color_count,
                rt.color_format.c_str(), rt.depth_format.c_str());
  }

  // Now that RT handles are resolved, build the DAG
  BuildGraph();
}

void RenderGraph::BuildGraph() {
  m_nodes.clear();
  m_edges.clear();

  // Map pass names to indices for dependency tracking
  std::unordered_map<std::string, int> passIndex;
  for (int i = 0; i < static_cast<int>(m_desc.passes.size()); i++) {
    passIndex[m_desc.passes[i].name] = i;
  }

  // Build a map: RT name -> index of the last pass that wrote to it
  // (updated as we scan passes in order)
  std::unordered_map<std::string, int> lastWriter;

  m_nodes.resize(m_desc.passes.size());

  for (int i = 0; i < static_cast<int>(m_desc.passes.size()); i++) {
    const auto& passDesc = m_desc.passes[i];
    auto& node = m_nodes[i];
    node.index = i;
    node.desc = &m_desc.passes[i];

    // Resolve RT handle for this pass's target
    if (!passDesc.target.empty()) {
      auto it = m_rtHandles.find(passDesc.target);
      node.rt_handle = (it != m_rtHandles.end()) ? it->second : -1;
    } else {
      node.rt_handle = -1;
    }

    // Scan inputs to build edges
    for (const auto& input : passDesc.inputs) {
      if (input.source.empty() || input.source[0] == '@')
        continue;  // built-in, no graph edge

      // Extract RT name from "RTName:ATTACHMENT"
      std::string rtName = input.source;
      auto colon = rtName.find(':');
      if (colon != std::string::npos)
        rtName = rtName.substr(0, colon);

      auto writerIt = lastWriter.find(rtName);
      if (writerIt != lastWriter.end()) {
        int fromPass = writerIt->second;

        GraphEdge edge;
        edge.from_pass = fromPass;
        edge.to_pass = i;
        edge.rt = rtName;
        edge.attachment = ResolveAttachment(
          (colon != std::string::npos) ? input.source.substr(colon + 1) : "COLOR0");
        edge.slot = input.slot;
        m_edges.push_back(edge);

        // Record adjacency
        if (std::find(node.inputs_from.begin(), node.inputs_from.end(), fromPass) == node.inputs_from.end())
          node.inputs_from.push_back(fromPass);
        if (std::find(m_nodes[fromPass].outputs_to.begin(), m_nodes[fromPass].outputs_to.end(), i) == m_nodes[fromPass].outputs_to.end())
          m_nodes[fromPass].outputs_to.push_back(i);
      }
    }

    // This pass writes to its target RT
    if (!passDesc.target.empty()) {
      lastWriter[passDesc.target] = i;
    }
  }

  T8_LOG_INFO("[RenderGraph] Built graph: %zu nodes, %zu edges", m_nodes.size(), m_edges.size());
}

int RenderGraph::GetRTHandle(const std::string& name) const {
  auto it = m_rtHandles.find(name);
  return (it != m_rtHandles.end()) ? it->second : -1;
}

// ---- Print ----

void RenderGraph::PrintGraph() const {
  if (t850::Log::GetMaxLevel() < t850::Log::LVL_DEBUG) return;
  printf("\n=== Render Graph ===\n");
  for (const auto& node : m_nodes) {
    printf("[%2d] %-30s -> RT: %-20s (handle %d)\n",
           node.index,
           node.desc->name.c_str(),
           node.desc->target.c_str(),
           node.rt_handle);

    if (!node.inputs_from.empty()) {
      printf("       depends on: ");
      for (int dep : node.inputs_from) printf("[%d]%s ", dep, m_nodes[dep].desc->name.c_str());
      printf("\n");
    }
    if (!node.outputs_to.empty()) {
      printf("       feeds into: ");
      for (int out : node.outputs_to) printf("[%d]%s ", out, m_nodes[out].desc->name.c_str());
      printf("\n");
    }
  }

  printf("\n--- Edges ---\n");
  for (const auto& e : m_edges) {
    printf("  [%d]%s -(%s:%d)-> [%d]%s slot %d\n",
           e.from_pass, m_nodes[e.from_pass].desc->name.c_str(),
           e.rt.c_str(), e.attachment,
           e.to_pass, m_nodes[e.to_pass].desc->name.c_str(),
           e.slot);
  }
  printf("====================\n\n");
}

// ---- Execution ----

void RenderGraph::Execute(
  BaseDriver* driver,
  SceneProps& props,
  PrimitiveInst* meshes, int meshCount,
  PrimitiveInst* quads,
  ::Camera* mainCam,
  ::Camera* lightCam,
  ::Camera* omniCams,
      const EnvironmentMapSet& envMaps)
{
  for (const auto& node : m_nodes) {
    if (m_disabledPasses.count(node.desc->name)) continue;
    ExecutePass(node, driver, props, meshes, meshCount, quads,
                mainCam, lightCam, omniCams, envMaps);
  }
}

void RenderGraph::ExecutePass(
  const GraphNode& node,
  BaseDriver* driver,
  SceneProps& props,
  PrimitiveInst* meshes, int meshCount,
  PrimitiveInst* quads,
  ::Camera* mainCam,
  ::Camera* lightCam,
  ::Camera* omniCams,
  const EnvironmentMapSet& envMaps)
{
  const auto& pass = *node.desc;
  T8_PROFILE_SCOPE(t850::g_profiler, pass.name.c_str());

  // Pre-pass state changes
  int ds = ResolveDepthStencilState(pass.state.depth_stencil);
  if (ds >= 0) driver->SetDepthStencilState(static_cast<BaseDriver::DepthStencilStates>(ds));

  int cf = ResolveCullFace(pass.state.cull_face);
  if (cf >= 0) driver->SetCullFace(static_cast<BaseDriver::FaceCulling>(cf));

  int bs = ResolveBlendState(pass.state.blend);
  if (bs >= 0) driver->SetBlendState(static_cast<BaseDriver::BlendStates>(bs));

  // Camera selection
  if (pass.camera == "light" && lightCam) {
    props.pCameras[0] = lightCam;
  } else if (pass.camera == "main" && mainCam) {
    props.pCameras[0] = mainCam;
  }

  // Active light camera
  if (pass.active_light_camera >= 0) {
    props.ActiveLightCamera = pass.active_light_camera;
  }

  // Gauss kernel selection
  if (pass.gauss_kernel >= 0) {
    props.ActiveGaussKernel = pass.gauss_kernel;
  }

  // Cubemap loop pass
  if (pass.cube_faces > 0 && node.rt_handle >= 0) {
    driver->PushRT(node.rt_handle);

    // Clear all faces first
    for (int face = 0; face < pass.cube_faces; face++) {
      driver->RTs[node.rt_handle]->ChangeCubeDepthTexture(face);
      driver->Clear();
    }

    // Draw each face
    for (int face = 0; face < pass.cube_faces; face++) {
      if (pass.per_face_camera == "omni" && omniCams) {
        props.pCameras[0] = &omniCams[face];
      }
      driver->RTs[node.rt_handle]->ChangeCubeDepthTexture(face);

      for (const auto& draw : pass.draws) {
        ShaderKey sig = ResolveSignature(draw.signature);
        for (const auto& extraSig : draw.extra_signatures) {
          ShaderKey extra = ResolveSignature(extraSig);
          sig.bits |= extra.bits;
        }

        for (int mi : draw.mesh_indices) {
          if (mi < meshCount) {
            meshes[mi].SetGlobalKey(sig);
            meshes[mi].Draw();
            ShaderKey fwd(0); fwd.setPass(PassType::FORWARD);
            meshes[mi].SetGlobalKey(fwd);
          }
        }
      }
    }

    driver->PopRT();

    // Restore camera after cubemap faces
    if (pass.per_face_camera == "omni" && mainCam) {
      props.pCameras[0] = mainCam;
    }
  }
  else {
    // Standard pass: push RT, bind textures, draw, pop

    // Push RT (if we have a target and push is enabled)
    if (node.rt_handle >= 0 && pass.push) {
      driver->PushRT(node.rt_handle);
    } else if (node.rt_handle >= 0 && !pass.push && driver->CurrentRT != node.rt_handle) {
      driver->PushRTLoad(node.rt_handle);
    }

    if (pass.clear) {
      driver->ClearWithColor(pass.clear_color[0], pass.clear_color[1],
                             pass.clear_color[2], pass.clear_color[3]);
    }

    // Bind input textures
    for (const auto& input : pass.inputs) {
      auto resolved = ResolveTextureInput(input.source);

      if (resolved.is_builtin) {
        if (resolved.builtin == "@ssao_noise") {
          quads[0].SetTexture(props.SSAOKernel.NoiseTex, input.slot);
        }
        // Other built-ins can be added here
      } else if (resolved.rt_handle >= 0) {
        Texture* tex = driver->GetRTTexture(resolved.rt_handle, resolved.attachment);
        quads[0].SetTexture(tex, input.slot);
      }
    }

    auto textureOrNull = [&](int textureIndex) -> Texture* {
      return textureIndex >= 0 ? driver->GetTexture(textureIndex) : nullptr;
    };

    auto bindEnvironmentResources = [&](PrimitiveInst& primitive) {
      Texture* sky = textureOrNull(envMaps.Sky);
      Texture* diffuse = textureOrNull(envMaps.DiffuseIBL >= 0 ? envMaps.DiffuseIBL : envMaps.Sky);
      Texture* specular = textureOrNull(envMaps.SpecularIBL >= 0 ? envMaps.SpecularIBL : envMaps.Sky);
      Texture* brdfLut = textureOrNull(envMaps.BrdfLUT);
      int charlieIndex = envMaps.CharlieIBL >= 0 ? envMaps.CharlieIBL : (envMaps.SpecularIBL >= 0 ? envMaps.SpecularIBL : envMaps.Sky);
      Texture* charlie = textureOrNull(charlieIndex);
      Texture* charlieLut = textureOrNull(envMaps.CharlieLUT);
      Texture* sheenELut = textureOrNull(envMaps.SheenELUT);
      primitive.SetEnvironmentMap(sky);
      primitive.SetTexture(diffuse, EnvironmentTextureSlot::DiffuseIBL);
      primitive.SetTexture(specular, EnvironmentTextureSlot::SpecularIBL);
      primitive.SetTexture(brdfLut, EnvironmentTextureSlot::BrdfLUT);
      primitive.SetTexture(charlie, EnvironmentTextureSlot::CharlieIBL);
      primitive.SetTexture(charlieLut, EnvironmentTextureSlot::CharlieLUT);
      primitive.SetTexture(sheenELut, EnvironmentTextureSlot::SheenELUT);
    };

    auto clearEnvironmentResources = [](PrimitiveInst& primitive) {
      primitive.SetEnvironmentMap(nullptr);
      primitive.SetTexture(nullptr, EnvironmentTextureSlot::DiffuseIBL);
      primitive.SetTexture(nullptr, EnvironmentTextureSlot::SpecularIBL);
      primitive.SetTexture(nullptr, EnvironmentTextureSlot::BrdfLUT);
      primitive.SetTexture(nullptr, EnvironmentTextureSlot::CharlieIBL);
      primitive.SetTexture(nullptr, EnvironmentTextureSlot::CharlieLUT);
      primitive.SetTexture(nullptr, EnvironmentTextureSlot::SheenELUT);
    };

    if (pass.bind_environment_map) {
      bindEnvironmentResources(quads[0]);
    } else {
      clearEnvironmentResources(quads[0]);
    }

    auto bindMeshPassResources = [&](PrimitiveInst& mesh) {
      for (const auto& input : pass.inputs) {
        auto resolved = ResolveTextureInput(input.source);
        if (!resolved.is_builtin && resolved.rt_handle >= 0 && input.slot >= 0 && input.slot < MaxPrimitiveTextures) {
          mesh.SetTexture(driver->GetRTTexture(resolved.rt_handle, resolved.attachment), input.slot);
        }
      }
      if (pass.bind_environment_map) {
        bindEnvironmentResources(mesh);
      } else {
        clearEnvironmentResources(mesh);
      }
    };

    // Execute draw commands
    for (const auto& draw : pass.draws) {
      ShaderKey sig = ResolveSignature(draw.signature);
      for (const auto& extraSig : draw.extra_signatures) {
        ShaderKey extra = ResolveSignature(extraSig);
        sig.bits |= extra.bits;
      }

      if (draw.type == "mesh") {
        if (draw.mesh_indices.empty()) {
          // Empty array = draw ALL meshes
          for (int mi = 0; mi < meshCount; ++mi) {
            bindMeshPassResources(meshes[mi]);
            meshes[mi].SetGlobalKey(sig);
            meshes[mi].Draw();
            ShaderKey fwd(0); fwd.setPass(PassType::FORWARD);
            meshes[mi].SetGlobalKey(fwd);
          }
        } else {
          for (int mi : draw.mesh_indices) {
            if (mi >= 0 && mi < meshCount) {
              bindMeshPassResources(meshes[mi]);
              meshes[mi].SetGlobalKey(sig);
              meshes[mi].Draw();
              ShaderKey fwd(0); fwd.setPass(PassType::FORWARD);
              meshes[mi].SetGlobalKey(fwd);
            }
          }
        }
      }
      else if (draw.type == "final_quad") {
        driver->SetDepthStencilState(BaseDriver::DepthStencilStates::NONE);
        for (const auto& input : pass.inputs) {
          auto resolved = ResolveTextureInput(input.source);
          if (!resolved.is_builtin && resolved.rt_handle >= 0) {
            quads[0].SetTexture(driver->GetRTTexture(resolved.rt_handle, resolved.attachment), input.slot);
          }
        }
        quads[0].SetGlobalKey(sig);
        quads[0].Draw();
      }
      else {
        // fullscreen_quad (default)
        driver->SetDepthStencilState(BaseDriver::DepthStencilStates::NONE);
        quads[0].SetGlobalKey(sig);
        quads[0].Draw();
      }
    }

    // Pop RT
    bool didPush = (node.rt_handle >= 0 && pass.push);
    bool inheritedRT = (!pass.push && node.rt_handle >= 0);
    if (pass.pop && (didPush || inheritedRT)) {
      driver->PopRT();
    }
  }

  // Post-pass state restoration
  int postDs = ResolveDepthStencilState(pass.post_state.depth_stencil);
  if (postDs >= 0) driver->SetDepthStencilState(static_cast<BaseDriver::DepthStencilStates>(postDs));

  int postCf = ResolveCullFace(pass.post_state.cull_face);
  if (postCf >= 0) driver->SetCullFace(static_cast<BaseDriver::FaceCulling>(postCf));

  int postBs = ResolveBlendState(pass.post_state.blend);
  if (postBs >= 0) driver->SetBlendState(static_cast<BaseDriver::BlendStates>(postBs));
}

} // namespace t850
