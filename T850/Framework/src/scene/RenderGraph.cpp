#include <pch.h>
#include <scene/RenderGraph.h>
#include <scene/RenderGraphDescriptor.h>
#include <scene/SceneProp.h>
#include <scene/ShadowSystem.h>
#include <scene/PrimitiveInstance.h>
#include <scene/RenderQueue.h>     // MeshDrawStateTracker
#include <utils/Camera.h>
#include <video/BaseDriver.h>
#include <Descriptors.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4267 4244)
#endif
#include <glaze/glaze.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <debug/Profiler.h>
#include <debug/LoadingProgress.h>
#include <debug/RuntimeTelemetry.h>

#include <fstream>
#include <sstream>
#include <cstdio>
#include <unordered_map>
#include <algorithm>
#include <cmath>

namespace t850 {

// ---- JSON loading via glaze ----

bool LoadRenderGraphDescriptor(const std::string& path, RenderGraphDesc& desc) {
  LoadingProgress::ScopedStep loadingStep("Loading render graph", path, 1.0f);
  std::string json;
  if (!ResourceLocator::Instance().ReadText(path, json)) {
    T8_LOG_ERROR("[RenderGraph] Cannot open '%s'", path.c_str());
    return false;
  }

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
  {"DEFERRED_LIGHT_VOLUME_PASS", PassType::DEFERRED_LIGHT_VOLUME},
  {"CASCADE_DEBUG_PASS",    PassType::CASCADE_DEBUG},
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

RenderPassKind RenderGraph::ResolvePassKind(const std::string& name) {
  if (name == "shadow_depth") return RenderPassKind::ShadowDepth;
  return RenderPassKind::Normal;
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
  m_disabledPasses.clear();
  m_sourceDesc = RenderGraphDesc{};
  m_effectiveDesc = RenderGraphDesc{};
  m_nodes.clear();
  m_edges.clear();
  m_rtHandles.clear();

  if (!LoadRenderGraphDescriptor(path, m_sourceDesc))
    return false;
  m_effectiveDesc = m_sourceDesc;

  // Graph is built after CreateRenderTargets (needs RT handle map)
  return true;
}

// ---- Shadow configuration & pass expansion ----

void RenderGraph::MergeShadowOverrides(
  const std::vector<ShadowProjectionOverrideDesc>& baseOverrides,
  const std::vector<ShadowProjectionOverrideDesc>& runtimeOverrides) {
  // Apply base overrides first, then runtime overrides (runtime wins).
  auto apply = [&](const std::vector<ShadowProjectionOverrideDesc>& overrides) {
    for (const auto& ov : overrides) {
      for (auto& proj : m_effectiveDesc.shadow_projections) {
        if (proj.id != ov.projection_id)
          continue;
        if (ov.enabled.has_value()) proj.enabled = *ov.enabled;
        if (ov.resolution.has_value()) proj.resolution = *ov.resolution;
        if (ov.cascade_count.has_value()) proj.cascade_count = *ov.cascade_count;
        if (ov.split_lambda.has_value()) proj.split_lambda = *ov.split_lambda;
        if (ov.near_distance.has_value()) proj.near_distance = *ov.near_distance;
        if (ov.far_distance.has_value()) proj.far_distance = *ov.far_distance;
        if (ov.caster_depth_padding.has_value()) proj.caster_depth_padding = *ov.caster_depth_padding;
        if (ov.blend_fraction.has_value()) proj.blend_fraction = *ov.blend_fraction;
      }
    }
  };
  apply(baseOverrides);
  apply(runtimeOverrides);
}

void RenderGraph::SizeShadowTarget(RTDesc& rt, const SceneProps& props) {
  // Find the effective projection by ID.
  for (const auto& proj : m_effectiveDesc.shadow_projections) {
    if (proj.id != rt.shadow_projection)
      continue;
    int tileRes = proj.resolution > 0 ? proj.resolution : static_cast<int>(props.ShadowMapResolution);
    int viewCount = 1;
    ShadowTechnique technique = ShadowSystem::ResolveTechnique(proj.technique);
    switch (technique) {
      case ShadowTechnique::DirectionalSingle: viewCount = 1; break;
      case ShadowTechnique::DirectionalCascaded: viewCount = proj.cascade_count; break;
      case ShadowTechnique::PointCube: viewCount = 6; break;
      case ShadowTechnique::PointDualParaboloid: viewCount = 2; break;
    }
    int columns = 1, rows = 1;
    ShadowSystem::ComputeAtlasLayout(viewCount, columns, rows);
    if (technique == ShadowTechnique::PointCube) {
      rt.size = { tileRes, tileRes };
    } else if (technique == ShadowTechnique::PointDualParaboloid) {
      rt.size = { 2 * tileRes, tileRes };
    } else {
      rt.size = { columns * tileRes, rows * tileRes };
    }
    return;
  }
}

bool RenderGraph::ExpandDirectionalProjection(
  SceneProps& props,
  const ShadowProjectionDesc& projection,
  int projectionIndex,
  std::string* error) {
  if (!projection.enabled)
    return true;

  ShadowTechnique technique = ShadowSystem::ResolveTechnique(projection.technique);
  const int N = (technique == ShadowTechnique::DirectionalCascaded)
    ? projection.cascade_count : 1;
  if (N < 1 || N > 6) {
    if (error) *error = "Shadow projection '" + projection.id + "' cascade_count out of range 1..6";
    return false;
  }

  // Find the authored shadow-depth template pass for this projection, or the
  // first legacy "Shadow Depth" pass for compatibility.
  int templateIndex = -1;
  for (int i = 0; i < static_cast<int>(m_effectiveDesc.passes.size()); ++i) {
    const auto& p = m_effectiveDesc.passes[i];
    if (ResolvePassKind(p.kind) == RenderPassKind::ShadowDepth && p.shadow_projection == projection.id) {
      templateIndex = i;
      break;
    }
  }
  if (templateIndex < 0) {
    for (int i = 0; i < static_cast<int>(m_effectiveDesc.passes.size()); ++i) {
      if (m_effectiveDesc.passes[i].name == "Shadow Depth") {
        templateIndex = i;
        break;
      }
    }
  }
  if (templateIndex < 0) {
    if (error) *error = "Shadow projection '" + projection.id + "' has no shadow-depth template pass";
    return false;
  }

  const RenderPassDesc templatePass = m_effectiveDesc.passes[templateIndex];

  // Compute atlas layout for viewports.
  int columns = 1, rows = 1;
  ShadowSystem::ComputeAtlasLayout(N, columns, rows);
  int tileRes = projection.resolution > 0 ? projection.resolution : static_cast<int>(props.ShadowMapResolution);

  // Build N generated passes.
  std::vector<RenderPassDesc> generated;
  generated.reserve(N);
  for (int v = 0; v < N; ++v) {
    RenderPassDesc p = templatePass;
    p.name = "Shadow Depth " + std::to_string(v);
    p.kind = "shadow_depth";
    p.shadow_projection = projection.id;
    p.shadow_projection_index = projectionIndex;
    p.shadow_view_index = v;
    p.shadow_view_kind = (N > 1) ? ShadowViewKind::AtlasTile : ShadowViewKind::WholeTexture2D;
    p.shadow_subresource = -1;

    int tileX = v % columns;
    int tileY = v / columns;
    p.viewport = { tileX * tileRes, tileY * tileRes, tileRes, tileRes };

    // Grouped push/pop/clear policy:
    //   pass 0: push true, pop false (clears)
    //   middle: push false, pop false
    //   last:   push false, pop true
    p.push = (v == 0);
    p.pop = (v == N - 1);
    p.clear = (v == 0);

    generated.push_back(p);
  }

  // Replace the template pass with the generated passes.
  m_effectiveDesc.passes.erase(m_effectiveDesc.passes.begin() + templateIndex);
  m_effectiveDesc.passes.insert(m_effectiveDesc.passes.begin() + templateIndex,
                                generated.begin(), generated.end());
  return true;
}

bool RenderGraph::Configure(
  SceneProps& props,
  const std::vector<ShadowProjectionOverrideDesc>& baseOverrides,
  const std::vector<ShadowProjectionOverrideDesc>& runtimeOverrides,
  std::string* error) {
  // Always restart from the source descriptor.
  m_effectiveDesc = m_sourceDesc;

  // Merge typed shadow overrides into projection baselines.
  MergeShadowOverrides(baseOverrides, runtimeOverrides);

  // Validate + initialize runtime projection records.
  if (!ShadowSystem::ResolveDescriptors(m_effectiveDesc, props, props.Shadows, error))
    return false;

  // Expand each enabled directional projection into generated passes.
  // Iterate over a copy of the projection list (indices shift as passes expand).
  const auto projections = m_effectiveDesc.shadow_projections;
  for (int pi = 0; pi < static_cast<int>(projections.size()); ++pi) {
    const auto& proj = projections[pi];
    if (!proj.enabled)
      continue;
    ShadowTechnique technique = ShadowSystem::ResolveTechnique(proj.technique);
    if (technique != ShadowTechnique::DirectionalSingle &&
        technique != ShadowTechnique::DirectionalCascaded) {
      if (error) *error = "Shadow projection '" + proj.id + "' uses an unsupported technique";
      return false;
    }
    if (!ExpandDirectionalProjection(props, proj, pi, error))
      return false;
  }

  return true;
}

void RenderGraph::CreateRenderTargets(BaseDriver* driver, const SceneProps& props) {
  CreateRenderTargets(driver, props, 0, 0);
}

void RenderGraph::CreateRenderTargets(BaseDriver* driver, const SceneProps& props, int widthOverride, int heightOverride) {
  m_rtHandles.clear();

#if defined(OS_ANDROID)
  constexpr float kAndroidScreenRenderScale = 0.5f;
  bool loggedAndroidRenderScale = false;
#endif

  for (auto& rt : m_effectiveDesc.render_targets) {
    int cf = ResolveColorFormat(rt.color_format);
    int df = ResolveDepthFormat(rt.depth_format);

    int w = rt.size[0];
    int h = rt.size[1];

    // Shadow-referenced targets are sized from their projection.
    if (!rt.shadow_projection.empty()) {
      SizeShadowTarget(rt, props);
      w = rt.size[0];
      h = rt.size[1];
    }

    if (rt.shadow_projection.empty() && !rt.size_ref.empty()) {
      if (rt.size_ref == "$shadow_resolution") {
        w = h = static_cast<int>(props.ShadowMapResolution);
      } else if (rt.size_ref == "$god_rays_resolution") {
        w = h = static_cast<int>(props.GoodRaysResolution);
      }
    }

#if defined(OS_ANDROID)
    const bool usesBackbufferSize = rt.size[0] == 0 && rt.size[1] == 0 && rt.size_ref.empty();
    if (usesBackbufferSize && driver && driver->width > 0 && driver->height > 0) {
      w = (std::max)(1, static_cast<int>(std::round(static_cast<float>(driver->width) * kAndroidScreenRenderScale)));
      h = (std::max)(1, static_cast<int>(std::round(static_cast<float>(driver->height) * kAndroidScreenRenderScale)));
      if (!loggedAndroidRenderScale) {
        T8_LOG_INFO("[RenderGraph] Android screen render scale %.2f: %dx%d -> %dx%d",
                    kAndroidScreenRenderScale, driver->width, driver->height, w, h);
        loggedAndroidRenderScale = true;
      }
    }
#endif

    if (rt.size[0] == 0 && rt.size[1] == 0 && rt.size_ref.empty()) {
      if (widthOverride > 0) w = widthOverride;
      if (heightOverride > 0) h = heightOverride;
    }

    // CreateRT's final argument controls mip generation, not linear filtering.
    // Keep it opt-in per target so bloom/intermediate passes never sample
    // implicit mips unexpectedly.
    const bool generateMips = rt.generate_mips;
    int handle;
    if (!rt.color_formats.empty()) {
      // Per-attachment formats specified in JSON
      std::vector<int> perCF;
      for (const auto& fmt : rt.color_formats)
        perCF.push_back(ResolveColorFormat(fmt));
      handle = driver->CreateRT(rt.color_count, perCF, df, w, h, generateMips);
    } else {
      handle = driver->CreateRT(rt.color_count, cf, df, w, h, generateMips);
    }
    auto applyFilter = [&](Texture* tex) {
      if (!tex) return;
      tex->params &= ~(TextBasicParams::NEAREST_FILTER | TextBasicParams::LINEAR_FILTER);
      tex->params |= rt.linear_filter ? TextBasicParams::LINEAR_FILTER : TextBasicParams::NEAREST_FILTER;
      tex->SetTextureParams();
    };
    for (int attachment = 0; attachment < rt.color_count; ++attachment) {
      if (Texture* tex = driver->GetRTTexture(handle, attachment)) {
        applyFilter(tex);
        tex->mipmaps = 1;
        if (generateMips &&
            (driver->m_currentAPI == GraphicsApi::D3D11 || driver->m_currentAPI == GraphicsApi::OPENGL)) {
          unsigned int maxDim = (tex->x > tex->y) ? tex->x : tex->y;
          unsigned int levels = 1;
          while (maxDim > 1) { maxDim >>= 1; ++levels; }
          tex->mipmaps = levels;
        }
      }
    }
    applyFilter(driver->GetRTTexture(handle, BaseDriver::DEPTH_ATTACHMENT));
    m_rtHandles[rt.name] = handle;

    T8_LOG_INFO("[RenderGraph] Created RT '%s' -> handle %d (%dx%d, %d colors, cf=%s, df=%s)",
                rt.name.c_str(), handle, w, h, rt.color_count,
                rt.color_format.c_str(), rt.depth_format.c_str());
  }

  // Now that RT handles are resolved, build the DAG
  BuildGraph();
}

void RenderGraph::DestroyRenderTargets(BaseDriver* driver) {
  if (!driver) {
    m_rtHandles.clear();
    m_nodes.clear();
    m_edges.clear();
    return;
  }
  std::vector<int> handles;
  handles.reserve(m_rtHandles.size());
  for (const auto& entry : m_rtHandles) {
    if (entry.second >= 0) {
      handles.push_back(entry.second);
    }
  }
  std::sort(handles.begin(), handles.end());
  handles.erase(std::unique(handles.begin(), handles.end()), handles.end());
  for (int handle : handles) {
    driver->DestroyRT(handle);
  }
  m_rtHandles.clear();
  m_nodes.clear();
  m_edges.clear();
}

void RenderGraph::BuildGraph() {
  m_nodes.clear();
  m_edges.clear();

  // Map pass names to indices for dependency tracking
  std::unordered_map<std::string, int> passIndex;
  for (int i = 0; i < static_cast<int>(m_effectiveDesc.passes.size()); i++) {
    passIndex[m_effectiveDesc.passes[i].name] = i;
  }

  // Build a map: RT name -> index of the last pass that wrote to it
  // (updated as we scan passes in order)
  std::unordered_map<std::string, int> lastWriter;

  m_nodes.resize(m_effectiveDesc.passes.size());

  for (int i = 0; i < static_cast<int>(m_effectiveDesc.passes.size()); i++) {
    const auto& passDesc = m_effectiveDesc.passes[i];
    auto& node = m_nodes[i];
    node.index = i;
    node.desc = &m_effectiveDesc.passes[i];

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
  const EnvironmentMapSet& envMaps,
  int finalOutputRT,
  CustomDrawCallback customDraw)
{
  const bool shadowsEnabled = props.ToogleShadow != 0;
  const bool ssaoEnabled = props.ToogleSSAO != 0;
  props.DeferredLightVolumesEnabled = false;
  for (const auto& node : m_nodes) {
    if (m_disabledPasses.count(node.desc->name)) continue;
    for (const auto& draw : node.desc->draws) {
      if (draw.signature == "DEFERRED_LIGHT_VOLUME_PASS") {
        props.DeferredLightVolumesEnabled = true;
        break;
      }
    }
    if (props.DeferredLightVolumesEnabled) break;
  }

  for (const auto& node : m_nodes) {
    if (m_disabledPasses.count(node.desc->name)) continue;
    if (!shadowsEnabled && ResolvePassKind(node.desc->kind) == RenderPassKind::ShadowDepth) continue;
    if (!shadowsEnabled && !ssaoEnabled &&
        (node.desc->name == "Shadow Accumulation" ||
         node.desc->name == "Shadow Blur V" ||
         node.desc->name == "Shadow Blur H")) {
      continue;
    }
    ExecutePass(node, driver, props, meshes, meshCount, quads,
                mainCam, lightCam, omniCams, envMaps, finalOutputRT, customDraw);
  }
  if (mainCam && !props.pCameras.empty()) {
    props.SetPrimaryCamera(mainCam);
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
  const EnvironmentMapSet& envMaps,
  int finalOutputRT,
  const CustomDrawCallback& customDraw)
{
  const auto& pass = *node.desc;
  ScopedPrimaryCameraOverride cameraScope(props);
  T8_PROFILE_SCOPE(t850::g_profiler, pass.name.c_str());
  RuntimeTelemetry::ScopedTimer telemetryPass("render.pass." + pass.name);
  RuntimeTelemetry::AddCounter("render.pass.count", 1.0);
  const bool shadowsEnabled = props.ToogleShadow != 0;
  const bool ssaoEnabled = props.ToogleSSAO != 0;

  auto shouldSkipTextureInput = [&](const TextureInput& input) -> bool {
    if (!shadowsEnabled && input.source == "DepthPass:DEPTH") {
      return true;
    }
    if (pass.name == "Shadow Accumulation" && !ssaoEnabled && input.source == "GBuffer:COLOR3") {
      return true;
    }
    if (!ssaoEnabled && input.source == "@ssao_noise") {
      return true;
    }
    if (!shadowsEnabled && !ssaoEnabled && input.source.rfind("ShadowAccum:", 0) == 0) {
      return true;
    }
    return false;
  };

  auto clearTextureSlot = [](PrimitiveInst& primitive, int slot) {
    if (slot >= 0 && slot < MaxPrimitiveTextures) {
      primitive.SetTexture(nullptr, slot);
    }
  };

  // Pre-pass state changes
  int ds = ResolveDepthStencilState(pass.state.depth_stencil);
  if (ds >= 0) driver->SetDepthStencilState(static_cast<BaseDriver::DepthStencilStates>(ds));

  int cf = ResolveCullFace(pass.state.cull_face);
  if (cf >= 0) driver->SetCullFace(static_cast<BaseDriver::FaceCulling>(cf));

  int bs = ResolveBlendState(pass.state.blend);
  if (bs >= 0) driver->SetBlendState(static_cast<BaseDriver::BlendStates>(bs));

  // Camera selection: generated shadow passes use their dedicated cascade camera.
  if (ResolvePassKind(pass.kind) == RenderPassKind::ShadowDepth &&
      pass.shadow_projection_index >= 0 &&
      pass.shadow_view_index >= 0 &&
      pass.shadow_projection_index < static_cast<int>(props.Shadows.projections.size())) {
    auto& projection = props.Shadows.projections[pass.shadow_projection_index];
    if (pass.shadow_view_index < projection.viewCount) {
      props.SetPrimaryCamera(&projection.views[pass.shadow_view_index].camera);
    }
  } else if (pass.camera == "light" && lightCam) {
    props.SetPrimaryCamera(lightCam);
  } else if (pass.camera == "main" && mainCam) {
    props.SetPrimaryCamera(mainCam);
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
        props.SetPrimaryCamera(&omniCams[face]);
      }
      driver->RTs[node.rt_handle]->ChangeCubeDepthTexture(face);

      // Phase C step 3: open a pass-scoped state tracker so the
      // per-RenderMesh state (IBL textures, EnvMap, IB pool, shader)
      // dedupes across the multiple mesh draws in this pass.
      MeshDrawStateTracker::Get().Begin();
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
      MeshDrawStateTracker::Get().End();
    }

    driver->PopRT();

    // Restore camera after cubemap faces
    if (pass.per_face_camera == "omni" && mainCam) {
      props.SetPrimaryCamera(mainCam);
    }
  }
  else {
    // Standard pass: push RT, bind textures, draw, pop

    // Push RT (if we have a target and push is enabled)
    const bool finalOutputPass = node.rt_handle < 0 && finalOutputRT >= 0;
    if (finalOutputPass && pass.push) {
      driver->PushRT(finalOutputRT);
    } else if (node.rt_handle >= 0 && pass.push) {
      driver->PushRT(node.rt_handle);
    } else if (node.rt_handle >= 0 && !pass.push && driver->CurrentRT != node.rt_handle) {
      driver->PushRTLoad(node.rt_handle);
    } else if (node.rt_handle < 0 && !finalOutputPass && driver->IsOffscreenEnabled() && !driver->IsCurrentOffscreenTarget()) {
      driver->BindOffscreenTarget(false);
    }

    if (pass.clear) {
      driver->ClearWithColor(pass.clear_color[0], pass.clear_color[1],
                             pass.clear_color[2], pass.clear_color[3]);
    }

    // Per-pass viewport/scissor (generated shadow tiles).
    if (pass.viewport[2] > 0 && pass.viewport[3] > 0) {
      driver->SetViewport((float)pass.viewport[0], (float)pass.viewport[1],
                          (float)pass.viewport[2], (float)pass.viewport[3]);
      driver->SetScissorRect(pass.viewport[0], pass.viewport[1],
                             pass.viewport[2], pass.viewport[3]);
    } else if (node.rt_handle >= 0) {
      // Reset to the full target viewport for non-tile passes.
      const int tw = driver->RTs[node.rt_handle]->w;
      const int th = driver->RTs[node.rt_handle]->h;
      driver->SetViewport(0.0f, 0.0f, (float)tw, (float)th);
      driver->SetScissorRect(0, 0, tw, th);
    } else if (finalOutputPass && finalOutputRT >= 0) {
      const int tw = driver->RTs[finalOutputRT]->w;
      const int th = driver->RTs[finalOutputRT]->h;
      driver->SetViewport(0.0f, 0.0f, (float)tw, (float)th);
      driver->SetScissorRect(0, 0, tw, th);
    }

    for (int slot = 0; slot < MaxPrimitiveTextures; ++slot) {
      clearTextureSlot(quads[0], slot);
    }
    quads[0].SetEnvironmentMap(nullptr);

    // Bind input textures
    for (const auto& input : pass.inputs) {
      if (shouldSkipTextureInput(input)) {
        clearTextureSlot(quads[0], input.slot);
        continue;
      }

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
      for (int slot = 0; slot < MaxPrimitiveTextures; ++slot) {
        clearTextureSlot(mesh, slot);
      }
      mesh.SetEnvironmentMap(nullptr);

      for (const auto& input : pass.inputs) {
        if (shouldSkipTextureInput(input)) {
          clearTextureSlot(mesh, input.slot);
          continue;
        }

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
    // Phase C step 3: bracket the multi-mesh draw section with a
    // pass-scoped state tracker.
    bool meshTrackerOpened = false;
    auto openMeshTracker = [&]() {
      if (!meshTrackerOpened) {
        MeshDrawStateTracker::Get().Begin();
        meshTrackerOpened = true;
      }
    };
    for (const auto& draw : pass.draws) {
      if (draw.type == "callback") {
        if (meshTrackerOpened) { MeshDrawStateTracker::Get().End(); meshTrackerOpened = false; }
        if (customDraw && !draw.callback.empty())
          customDraw(draw.callback);
        continue;
      }

      ShaderKey sig = ResolveSignature(draw.signature);
      for (const auto& extraSig : draw.extra_signatures) {
        ShaderKey extra = ResolveSignature(extraSig);
        sig.bits |= extra.bits;
      }

      if (draw.type == "mesh") {
        openMeshTracker();
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
        // Quad draws have a different binding model — close the
        // mesh tracker scope before non-mesh work to avoid
        // contaminating its assumptions.
        if (meshTrackerOpened) { MeshDrawStateTracker::Get().End(); meshTrackerOpened = false; }
        driver->SetDepthStencilState(BaseDriver::DepthStencilStates::NONE);
        for (const auto& input : pass.inputs) {
          if (shouldSkipTextureInput(input)) {
            clearTextureSlot(quads[0], input.slot);
            continue;
          }

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
    if (meshTrackerOpened) { MeshDrawStateTracker::Get().End(); meshTrackerOpened = false; }

    // Pop RT
    bool didPush = ((node.rt_handle >= 0 || finalOutputPass) && pass.push);
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
