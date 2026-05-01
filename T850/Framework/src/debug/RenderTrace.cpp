#include <pch.h>
#include <debug/RenderTrace.h>

#ifdef T850_RENDER_TRACE

#include <video/BaseDriver.h>
#include <Descriptors.h>
#include <utils/Log.h>

#pragma warning(push)
#pragma warning(disable: 4267 4244)
#include <glaze/glaze.hpp>
#pragma warning(pop)

#include <fstream>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <algorithm>

namespace t850 {

  // ── glaze meta: tells the library which fields to serialize and under
  //   what JSON keys. We list explicitly so unused TraceEvent fields with
  //   sentinel values are still emitted (mechanical diff trumps prettiness).

} // namespace t850

template <>
struct glz::meta<t850::TraceTextureRec> {
  using T = t850::TraceTextureRec;
  static constexpr auto value = object(
    "id", &T::id, "name", &T::name, "filepath", &T::filepath,
    "width", &T::width, "height", &T::height, "mipmaps", &T::mipmaps,
    "channels", &T::channels, "props", &T::props, "params", &T::params,
    "kind", &T::kind, "format", &T::format_str, "generation", &T::generation);
};

template <>
struct glz::meta<t850::TraceRTRec> {
  using T = t850::TraceRTRec;
  static constexpr auto value = object(
    "id", &T::id, "name", &T::name, "w", &T::w, "h", &T::h,
    "color_count", &T::color_count, "color_formats", &T::color_formats,
    "depth_format", &T::depth_format, "gen_mips", &T::gen_mips,
    "color_texture_ids", &T::color_texture_ids, "depth_texture_id", &T::depth_texture_id,
    "generation", &T::generation);
};

template <>
struct glz::meta<t850::TraceShaderAttr> {
  using T = t850::TraceShaderAttr;
  static constexpr auto value = object(
    "semantic", &T::semantic, "format", &T::format,
    "location", &T::location, "input_slot", &T::input_slot,
    "offset", &T::offset, "size_bytes", &T::size_bytes);
};

template <>
struct glz::meta<t850::TraceShaderRec> {
  using T = t850::TraceShaderRec;
  static constexpr auto value = object(
    "id", &T::id, "key_bits", &T::key_bits, "key_hex", &T::key_hex,
    "pass", &T::pass, "vs_name", &T::vs_name, "fs_name", &T::fs_name,
    "defines", &T::defines,
    "vertex_stride", &T::vertex_stride, "input_attrs", &T::input_attrs);
};

template <>
struct glz::meta<t850::TracePSORec> {
  using T = t850::TracePSORec;
  static constexpr auto value = object(
    "id", &T::id, "backend", &T::backend, "shader_id", &T::shader_id,
    "shader_key_bits", &T::shader_key_bits, "blend", &T::blend, "depth", &T::depth,
    "cull", &T::cull, "topology", &T::topology,
    "num_color_attachments", &T::num_color_attachments,
    "color_formats", &T::color_formats, "depth_format", &T::depth_format,
    "vertex_stride", &T::vertex_stride, "render_pass", &T::render_pass);
};

template <>
struct glz::meta<t850::TraceSamplerRec> {
  using T = t850::TraceSamplerRec;
  static constexpr auto value = object(
    "id", &T::id, "filter", &T::filter,
    "address_u", &T::address_u, "address_v", &T::address_v, "address_w", &T::address_w,
    "anisotropy", &T::anisotropy, "lod_bias", &T::lod_bias,
    "compare", &T::compare, "border_color", &T::border_color);
};

template <>
struct glz::meta<t850::TraceTextureViewRec> {
  using T = t850::TraceTextureViewRec;
  static constexpr auto value = object(
    "id", &T::id, "texture_id", &T::texture_id,
    "base_mip", &T::base_mip, "mip_count", &T::mip_count,
    "base_layer", &T::base_layer, "layer_count", &T::layer_count,
    "aspect", &T::aspect, "view_format", &T::view_format);
};

template <>
struct glz::meta<t850::TraceEvent> {
  using T = t850::TraceEvent;
  static constexpr auto value = object(
    "seq", &T::seq, "type", &T::type,
    "i0", &T::i0, "i1", &T::i1, "i2", &T::i2, "i3", &T::i3, "i4", &T::i4, "i5", &T::i5,
    "u0", &T::u0, "u1", &T::u1,
    "f0", &T::f0, "f1", &T::f1, "f2", &T::f2, "f3", &T::f3,
    "s0", &T::s0, "s1", &T::s1);
};

template <>
struct glz::meta<t850::TraceTextureBind> {
  using T = t850::TraceTextureBind;
  static constexpr auto value = object(
    "slot", &T::slot, "texture_id", &T::texture_id,
    "view_id", &T::view_id, "sampler_id", &T::sampler_id,
    "shader_name", &T::shader_name, "stage", &T::stage);
};

template <>
struct glz::meta<t850::TraceCBufferBind> {
  using T = t850::TraceCBufferBind;
  static constexpr auto value = object(
    "slot", &T::slot, "buffer_id", &T::buffer_id,
    "offset", &T::offset, "size", &T::size,
    "update_version", &T::update_version, "hash", &T::hash,
    "data_hex", &T::data_hex);
};

template <>
struct glz::meta<t850::TraceBufferUpdate> {
  using T = t850::TraceBufferUpdate;
  static constexpr auto value = object(
    "version", &T::version, "size", &T::size, "offset", &T::offset,
    "hash", &T::hash, "truncated", &T::truncated, "data_hex", &T::data_hex);
};

template <>
struct glz::meta<t850::TraceBufferRec> {
  using T = t850::TraceBufferRec;
  static constexpr auto value = object(
    "id", &T::id, "kind", &T::kind, "name", &T::name,
    "updates", &T::updates);
};

template <>
struct glz::meta<t850::TraceDrawSnapshot> {
  using T = t850::TraceDrawSnapshot;
  static constexpr auto value = object(
    "seq", &T::seq, "rt_id", &T::rt_id, "shader_id", &T::shader_id, "pso_id", &T::pso_id,
    "vertex_buffer_id", &T::vertex_buffer_id, "vb_stride", &T::vb_stride,
    "index_buffer_id", &T::index_buffer_id, "ib_format", &T::ib_format,
    "topology", &T::topology,
    "blend", &T::blend, "depth", &T::depth, "cull", &T::cull,
    "viewport_x", &T::viewport_x, "viewport_y", &T::viewport_y,
    "viewport_w", &T::viewport_w, "viewport_h", &T::viewport_h,
    "scissor_x", &T::scissor_x, "scissor_y", &T::scissor_y,
    "scissor_w", &T::scissor_w, "scissor_h", &T::scissor_h,
    "vertex_count", &T::vertex_count, "start_index", &T::start_index, "start_vertex", &T::start_vertex,
    "vertex_buffer_version", &T::vertex_buffer_version,
    "index_buffer_version", &T::index_buffer_version,
    "context_mesh", &T::context_mesh, "context_material", &T::context_material,
    "context_entity", &T::context_entity, "context_pass", &T::context_pass,
    "textures", &T::textures, "cbuffers", &T::cbuffers);
};

template <>
struct glz::meta<t850::TraceFrame> {
  using T = t850::TraceFrame;
  static constexpr auto value = object(
    "frame", &T::frame, "scene", &T::scene, "api", &T::api, "timestamp", &T::timestamp,
    "textures", &T::textures, "texture_views", &T::texture_views, "samplers", &T::samplers,
    "rts", &T::rts, "shaders", &T::shaders, "psos", &T::psos,
    "events", &T::events, "draws", &T::draws, "buffers", &T::buffers);
};

namespace t850 {

  RenderTracer* g_renderTracer = nullptr;

  // ── Init / lifecycle ─────────────────────────────────────────────────

  void RenderTracer::Init(BaseDriver* driver) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_driver = driver;
    m_active = true;
    m_nextSeq = 0;
    m_frame = TraceFrame{};
    m_pending = TraceDrawSnapshot{};
  }

  void RenderTracer::Destroy() {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_active = false;
    m_driver = nullptr;
    m_frame = TraceFrame{};
    m_texMap.clear();
    m_rtMap.clear();
    m_shMap.clear();
    m_pendingInputs.clear();
    m_bufferStore.clear();
    m_bufferLatestVersion.clear();
    m_cbLast.clear();
    m_bufMap.clear();
    m_nextBufferVersion = 1;
    m_nextBufferId = 0;
  }

  void RenderTracer::ResetFrame(int frameIndex) {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_active) return;
    // Clear per-frame events / draw snapshots / buffer payloads. Keep the
    // resource catalog (textures/RTs/shaders/PSOs/samplers/views) and the
    // buffer-id map (m_bufMap) so cross-frame ids stay stable. Clear the
    // CB last-update stash and buffer payload store too — each frame should
    // only show updates that happened that frame.
    m_frame.events.clear();
    m_frame.draws.clear();
    m_frame.buffers.clear();
    m_cbLast.clear();
    m_bufferStore.clear();
    m_bufferLatestVersion.clear();
    m_frame.frame = frameIndex;
    m_nextSeq = 0;
    m_pending = TraceDrawSnapshot{};
  }

  // ── Save: write trace.json next to RT_Dump_*.ppm + snapshot.json ──────

  void RenderTracer::Save(const std::string& dirPath) {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_active) return;

    // Copy the API name from the driver so the trace is self-describing.
    if (m_driver) {
      m_frame.api = (m_driver->m_currentAPI == GraphicsApi::D3D12)  ? "d3d12"
                  : (m_driver->m_currentAPI == GraphicsApi::D3D11)  ? "d3d11"
                  : (m_driver->m_currentAPI == GraphicsApi::VULKAN) ? "vulkan"
                  : "gl";
    }

    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    struct tm lt;
#ifdef OS_WINDOWS
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    char tsBuf[64];
    std::snprintf(tsBuf, sizeof(tsBuf), "%04d%02d%02d_%02d%02d%02d",
                  lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                  lt.tm_hour, lt.tm_min, lt.tm_sec);
    m_frame.timestamp = tsBuf;

    // Materialize the per-buffer payload store into the catalog before
    // serializing. Stable id order makes cross-trace diffs deterministic.
    m_frame.buffers.clear();
    m_frame.buffers.reserve(m_bufferStore.size());
    for (auto& kv : m_bufferStore) {
      TraceBufferRec rec;
      rec.id   = kv.first;
      rec.kind = kv.second.kind;
      rec.name = kv.second.name;
      rec.updates = kv.second.updates;
      m_frame.buffers.push_back(std::move(rec));
    }
    std::sort(m_frame.buffers.begin(), m_frame.buffers.end(),
              [](const TraceBufferRec& a, const TraceBufferRec& b) { return a.id < b.id; });

    auto result = glz::write<glz::opts{.prettify = true}>(m_frame);
    if (!result) {
      T8_LOG_ERROR("[RenderTracer] glaze serialization failed");
      return;
    }
    std::string outPath = dirPath + "/trace.json";
    std::ofstream f(outPath, std::ios::out | std::ios::trunc);
    if (!f.is_open()) {
      T8_LOG_ERROR("[RenderTracer] cannot open '%s' for writing", outPath.c_str());
      return;
    }
    f << result.value();
    T8_LOG_INFO("[RenderTracer] wrote %s (%zu events, %zu draws, %zu textures, %zu shaders, %zu psos)",
                outPath.c_str(),
                m_frame.events.size(), m_frame.draws.size(),
                m_frame.textures.size(), m_frame.shaders.size(), m_frame.psos.size());
  }

  // ── Resource registration ────────────────────────────────────────────

  static std::string FormatHex64(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(v));
    return buf;
  }

  static std::string SafeName(const char* buf, std::size_t maxLen = 64) {
    if (!buf || !buf[0]) return std::string();
    std::string out;
    out.reserve(32);
    for (std::size_t i = 0; i < maxLen; ++i) {
      char c = buf[i];
      if (c == '\0') break;
      // JSON-safe: drop control chars and non-ASCII to avoid corrupted JSON
      // when the source buffer wasn't initialized (legacy float-cube textures
      // skip optname).
      if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return std::string();
      out.push_back(c);
    }
    return out;
  }

  // FNV-1a 64-bit hash. Stable across runs/APIs/architectures so two traces
  // can be diffed by hash even when the raw payloads are gigabytes.
  static uint64_t FNV1a64(const void* data, std::size_t size) {
    uint64_t h = 0xcbf29ce484222325ull;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
      h ^= p[i];
      h *= 0x100000001b3ull;
    }
    return h;
  }

  // Lowercase, no-separator hex encoder. Returns "" if data is null.
  // Caller is responsible for any size cap; we don't truncate here.
  static std::string HexEncode(const void* data, std::size_t size) {
    if (!data || size == 0) return std::string();
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(size * 2);
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
      out[i * 2 + 0] = kHex[(p[i] >> 4) & 0xF];
      out[i * 2 + 1] = kHex[p[i] & 0xF];
    }
    return out;
  }

  // Per-buffer raw payload cap to keep trace.json from blowing up on huge
  // skinned meshes (some go several MB). Cap is enforced in raw bytes; the
  // hex string is 2x. Buffers exceeding this get the prefix recorded with
  // truncated=true so the JSON is still well-formed and diffable.
  static constexpr std::size_t kMaxBufferRawBytes = 1 << 20; // 1 MiB raw / 2 MiB hex

  int RenderTracer::RegisterTexture(const Texture* tex, const char* kind) {
    if (!tex) return -1;
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_active) return -1;
    auto it = m_texMap.find(tex);
    if (it != m_texMap.end()) return it->second;
    TraceTextureRec rec;
    rec.id        = (int)m_frame.textures.size();
    rec.name      = SafeName(tex->optname);
    rec.filepath  = tex->filepath;
    rec.width     = (int)tex->x;
    rec.height    = (int)tex->y;
    rec.mipmaps   = (int)tex->mipmaps;
    rec.channels  = (int)tex->m_channels;
    rec.props     = tex->props;
    rec.params    = tex->params;
    rec.kind      = kind ? kind : "tex2d";
    m_frame.textures.push_back(std::move(rec));
    m_texMap[tex] = (int)m_frame.textures.size() - 1;
    return m_texMap[tex];
  }

  int RenderTracer::RegisterRT(const BaseRT* rt, const char* name, int idHint) {
    if (!rt) return -1;
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_active) return -1;
    auto it = m_rtMap.find(rt);
    if (it != m_rtMap.end()) return it->second;
    TraceRTRec rec;
    rec.id           = (int)m_frame.rts.size();
    rec.name         = name ? name : (idHint >= 0 ? ("RT_" + std::to_string(idHint)) : std::string());
    rec.w            = rt->w;
    rec.h            = rt->h;
    rec.color_count  = rt->number_RT;
    rec.depth_format = rt->depth_format;
    rec.gen_mips     = rt->GenMips;
    if (rt->perColorFormats.empty()) {
      for (int i = 0; i < rt->number_RT; ++i)
        rec.color_formats.push_back(rt->color_format);
    } else {
      rec.color_formats = rt->perColorFormats;
    }
    // Map associated color textures + depth back to texture ids if already
    // registered; ids are -1 otherwise (RT-internal textures may not be
    // separately catalogued).
    for (auto* ct : rt->vColorTextures) {
      auto tit = m_texMap.find(ct);
      rec.color_texture_ids.push_back(tit != m_texMap.end() ? tit->second : -1);
    }
    if (rt->pDepthTexture) {
      auto tit = m_texMap.find(rt->pDepthTexture);
      rec.depth_texture_id = (tit != m_texMap.end() ? tit->second : -1);
    }
    m_frame.rts.push_back(std::move(rec));
    m_rtMap[rt] = (int)m_frame.rts.size() - 1;
    return m_rtMap[rt];
  }

  int RenderTracer::RegisterShader(const ShaderBase* sh, uint64_t keyBits,
                                   const std::string& vsName, const std::string& fsName) {
    if (!sh) return -1;
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_active) return -1;
    auto it = m_shMap.find(sh);
    if (it != m_shMap.end()) return it->second;
    TraceShaderRec rec;
    rec.id        = (int)m_frame.shaders.size();
    rec.key_bits  = keyBits;
    rec.key_hex   = FormatHex64(keyBits);
    rec.pass      = (int)((keyBits >> ShaderKey::PASS_SHIFT) & 0x3F);
    rec.vs_name   = vsName;
    rec.fs_name   = fsName;
    // Decode active flags into human-readable defines for diffability.
    auto checkFlag = [&](uint64_t flag, const char* name) {
      if (keyBits & flag) rec.defines.push_back(name);
    };
    checkFlag(ShaderKey::HAS_NORMALS, "HAS_NORMALS");
    checkFlag(ShaderKey::HAS_TANGENTS, "HAS_TANGENTS");
    checkFlag(ShaderKey::HAS_BINORMALS, "HAS_BINORMALS");
    checkFlag(ShaderKey::HAS_TEXCOORD0, "HAS_TEXCOORD0");
    checkFlag(ShaderKey::HAS_TEXCOORD1, "HAS_TEXCOORD1");
    checkFlag(ShaderKey::DIFFUSE_MAP, "DIFFUSE_MAP");
    checkFlag(ShaderKey::SPECULAR_MAP, "SPECULAR_MAP");
    checkFlag(ShaderKey::GLOSS_MAP, "GLOSS_MAP");
    checkFlag(ShaderKey::NORMAL_MAP, "NORMAL_MAP");
    checkFlag(ShaderKey::REFLECT_MAP, "REFLECT_MAP");
    checkFlag(ShaderKey::HEIGHT_MAP, "HEIGHT_MAP");
    checkFlag(ShaderKey::METALLIC_MAP, "METALLIC_MAP");
    checkFlag(ShaderKey::NO_LIGHT, "NO_LIGHT");
    checkFlag(ShaderKey::FRESNEL, "FRESNEL");
    checkFlag(ShaderKey::OMNI_SHADOWS, "OMNI_SHADOWS");
    checkFlag(ShaderKey::PARALLAX, "PARALLAX");
    checkFlag(ShaderKey::SHADOWS, "SHADOWS");
    checkFlag(ShaderKey::SSAO, "SSAO");
    checkFlag(ShaderKey::AUTO_FOCUS, "AUTO_FOCUS");
    checkFlag(ShaderKey::GOD_RAYS, "GOD_RAYS");
    checkFlag(ShaderKey::PARALLAX_SHADOW, "PARALLAX_SHADOW");
    checkFlag(ShaderKey::GLTF_TANGENT_SPACE, "GLTF_TANGENT_SPACE");
    checkFlag(ShaderKey::HAS_SKINNING, "HAS_SKINNING");
    checkFlag(ShaderKey::HAS_SKINNING_QT, "HAS_SKINNING_QT");
    checkFlag(ShaderKey::HAS_SKINNING_TEX, "HAS_SKINNING_TEX");
    checkFlag(ShaderKey::CLEARCOAT_MAP, "CLEARCOAT_MAP");
    checkFlag(ShaderKey::SHEEN_COLOR_MAP, "SHEEN_COLOR_MAP");
    checkFlag(ShaderKey::SHEEN_ROUGHNESS_MAP, "SHEEN_ROUGHNESS_MAP");
    checkFlag(ShaderKey::CLEARCOAT_ROUGHNESS_MAP, "CLEARCOAT_ROUGHNESS_MAP");
    checkFlag(ShaderKey::OCCLUSION_MAP, "OCCLUSION_MAP");
    checkFlag(ShaderKey::SPECULAR_FACTOR_MAP, "SPECULAR_FACTOR_MAP");
    checkFlag(ShaderKey::SPECULAR_COLOR_MAP, "SPECULAR_COLOR_MAP");
    checkFlag(ShaderKey::TRANSMISSION_MAP, "TRANSMISSION_MAP");
    checkFlag(ShaderKey::HAS_TEXCOORD2, "HAS_TEXCOORD2");
    checkFlag(ShaderKey::HAS_TEXCOORD3, "HAS_TEXCOORD3");
    // Merge any input-layout info that the per-API CreateShader stashed by
    // pointer before BaseDriver got a chance to assign this shader an id.
    auto pi = m_pendingInputs.find(sh);
    if (pi != m_pendingInputs.end()) {
      rec.vertex_stride = pi->second.vertex_stride;
      rec.input_attrs   = std::move(pi->second.attrs);
      m_pendingInputs.erase(pi);
    }
    m_frame.shaders.push_back(std::move(rec));
    m_shMap[sh] = (int)m_frame.shaders.size() - 1;
    return m_shMap[sh];
  }

  void RenderTracer::RegisterShaderInputsForPtr(const ShaderBase* sh,
                                                uint32_t vertexStride,
                                                std::vector<TraceShaderAttr> attrs) {
    if (!sh) return;
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_active) return;
    // If the shader already has a tracer id (unlikely, but possible if the
    // backend is re-entered), merge directly into the existing rec.
    auto sit = m_shMap.find(sh);
    if (sit != m_shMap.end()) {
      auto& rec = m_frame.shaders[sit->second];
      rec.vertex_stride = vertexStride;
      rec.input_attrs   = std::move(attrs);
      return;
    }
    PendingInputs pi;
    pi.vertex_stride = vertexStride;
    pi.attrs         = std::move(attrs);
    m_pendingInputs[sh] = std::move(pi);
  }

  int RenderTracer::RegisterSampler(const TraceSamplerRec& rec) {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_active) return -1;
    TraceSamplerRec copy = rec;
    copy.id = m_nextSamplerId++;
    m_frame.samplers.push_back(std::move(copy));
    return m_frame.samplers.back().id;
  }

  int RenderTracer::RegisterTextureView(const TraceTextureViewRec& rec) {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_active) return -1;
    TraceTextureViewRec copy = rec;
    copy.id = m_nextViewId++;
    m_frame.texture_views.push_back(std::move(copy));
    return m_frame.texture_views.back().id;
  }

  int RenderTracer::RegisterPSO(TracePSORec rec) {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_active) return -1;
    rec.id = m_nextPSOId++;
    m_frame.psos.push_back(std::move(rec));
    return m_frame.psos.back().id;
  }

  int RenderTracer::LookupTextureId(const Texture* tex) const {
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_texMap.find(tex);
    return it != m_texMap.end() ? it->second : -1;
  }
  int RenderTracer::LookupRTId(const BaseRT* rt) const {
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_rtMap.find(rt);
    return it != m_rtMap.end() ? it->second : -1;
  }
  int RenderTracer::LookupShaderId(const ShaderBase* sh) const {
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_shMap.find(sh);
    return it != m_shMap.end() ? it->second : -1;
  }

  int RenderTracer::EnsureBufferId(const void* ptr, const char* /*kind*/) {
    if (!ptr) return -1;
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_active) return -1;
    auto it = m_bufMap.find(ptr);
    if (it != m_bufMap.end()) return it->second;
    int id = m_nextBufferId++;
    m_bufMap[ptr] = id;
    return id;
  }

  uint64_t RenderTracer::RecordBufferUpdate(int bufferId, const void* data, uint32_t size,
                                            const char* kind, const char* name) {
    if (bufferId < 0 || size == 0) return 0;
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_active) return 0;
    uint64_t version = m_nextBufferVersion++;
    auto& store = m_bufferStore[bufferId];
    if (store.kind.empty() && kind) store.kind = kind;
    if (store.name.empty() && name) store.name = name;
    TraceBufferUpdate upd;
    upd.version = version;
    upd.size    = size;
    upd.offset  = 0;
    upd.hash    = data ? FNV1a64(data, size) : 0;
#ifdef T850_TRACE_GEOMETRY
    if (data) {
      // Cap the encoded payload at the per-buffer raw cap so trace.json
      // stays usable even with multi-MB vertex pools.
      const std::size_t take = (size > kMaxBufferRawBytes) ? kMaxBufferRawBytes : (std::size_t)size;
      upd.truncated = (take < (std::size_t)size);
      upd.data_hex  = HexEncode(data, take);
    }
#else
    (void)data;
#endif
    store.updates.push_back(std::move(upd));
    m_bufferLatestVersion[bufferId] = version;
    TraceEvent ev{0, "buffer_update"};
    ev.i0 = bufferId;
    ev.u0 = ((uint64_t)0 /*offset*/) | ((uint64_t)size << 32);
    ev.u1 = version;
    ev.s0 = FormatHex64(store.updates.back().hash);
    ev.s1 = store.kind;
    AppendEvent(std::move(ev));
    return version;
  }

  uint64_t RenderTracer::LatestBufferVersion(int bufferId) const {
    if (bufferId < 0) return 0;
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_bufferLatestVersion.find(bufferId);
    return it != m_bufferLatestVersion.end() ? it->second : 0;
  }

  // ── Context stack ────────────────────────────────────────────────────

  void RenderTracer::PushContext(const char* mesh, const char* material,
                                 const char* entity, const char* pass) {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_active) return;
    CtxFrame f;
    f.mesh     = mesh ? mesh : "";
    f.material = material ? material : "";
    f.entity   = entity ? entity : "";
    f.pass     = pass ? pass : "";
    m_ctxStack.push_back(std::move(f));
  }
  void RenderTracer::PopContext() {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_active) return;
    if (!m_ctxStack.empty()) m_ctxStack.pop_back();
  }

  // ── Event helpers ────────────────────────────────────────────────────

  void RenderTracer::AppendEvent(TraceEvent ev) {
    if (!m_active) return;
    ev.seq = m_nextSeq++;
    m_frame.events.push_back(std::move(ev));
  }

  void RenderTracer::EvBeginFrame() {
    std::lock_guard<std::mutex> lk(m_mtx);
    AppendEvent(TraceEvent{0, "begin_frame"});
  }
  void RenderTracer::EvEndFrame() {
    std::lock_guard<std::mutex> lk(m_mtx);
    AppendEvent(TraceEvent{0, "end_frame"});
  }

  void RenderTracer::EvPushRT(int rtId, bool load) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, load ? "push_rt_load" : "push_rt"};
    ev.i0 = rtId;
    ev.i1 = load ? 1 : 0;
    AppendEvent(std::move(ev));
    m_pending.rt_id = rtId;
  }
  void RenderTracer::EvPopRT() {
    std::lock_guard<std::mutex> lk(m_mtx);
    AppendEvent(TraceEvent{0, "pop_rt"});
    m_pending.rt_id = -1;
  }

  void RenderTracer::EvSetBlend(int state) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "set_blend"};
    ev.i0 = state; AppendEvent(std::move(ev));
    m_pending.blend = state;
  }
  void RenderTracer::EvSetDepth(int state) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "set_depth"};
    ev.i0 = state; AppendEvent(std::move(ev));
    m_pending.depth = state;
  }
  void RenderTracer::EvSetCull(int state) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "set_cull"};
    ev.i0 = state; AppendEvent(std::move(ev));
    m_pending.cull = state;
  }
  void RenderTracer::EvSetViewport(float x, float y, float w, float h) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "set_viewport"};
    ev.f0 = x; ev.f1 = y; ev.f2 = w; ev.f3 = h;
    AppendEvent(std::move(ev));
    m_pending.viewport_x = x; m_pending.viewport_y = y;
    m_pending.viewport_w = w; m_pending.viewport_h = h;
  }
  void RenderTracer::EvSetScissor(int x, int y, int w, int h) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "set_scissor"};
    ev.i0 = x; ev.i1 = y; ev.i2 = w; ev.i3 = h;
    AppendEvent(std::move(ev));
    m_pending.scissor_x = x; m_pending.scissor_y = y;
    m_pending.scissor_w = w; m_pending.scissor_h = h;
  }
  void RenderTracer::EvSetTopology(int topology) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "set_topology"};
    ev.i0 = topology; AppendEvent(std::move(ev));
    m_pending.topology = topology;
  }
  void RenderTracer::EvClear() {
    std::lock_guard<std::mutex> lk(m_mtx);
    AppendEvent(TraceEvent{0, "clear"});
  }
  void RenderTracer::EvClearColor(float r, float g, float b, float a) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "clear_color"};
    ev.f0 = r; ev.f1 = g; ev.f2 = b; ev.f3 = a;
    AppendEvent(std::move(ev));
  }

  void RenderTracer::EvBindShader(int shaderId, uint64_t keyBits) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "bind_shader"};
    ev.i0 = shaderId;
    ev.u0 = keyBits;
    AppendEvent(std::move(ev));
    m_pending.shader_id = shaderId;
  }

  void RenderTracer::EvBindTextureRequest(int slot, int texId, const std::string& name, const char* stage) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "bind_tex_req"};
    ev.i0 = slot; ev.i1 = texId; ev.s0 = name; ev.s1 = stage ? stage : "ps";
    AppendEvent(std::move(ev));
  }
  void RenderTracer::EvBindSamplerRequest(int slot) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "bind_sampler_req"};
    ev.i0 = slot;
    AppendEvent(std::move(ev));
  }
  void RenderTracer::EvBindIndexBufferRequest(int bufferId, int format, uint32_t offset) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "bind_ib_req"};
    ev.i0 = bufferId; ev.i1 = format; ev.u0 = offset;
    AppendEvent(std::move(ev));
    m_pending.index_buffer_id = bufferId;
    m_pending.ib_format = format;
  }
  void RenderTracer::EvBindVertexBufferRequest(int bufferId, uint32_t stride, uint32_t offset) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "bind_vb_req"};
    ev.i0 = bufferId; ev.u0 = stride; ev.u1 = offset;
    AppendEvent(std::move(ev));
    m_pending.vertex_buffer_id = bufferId;
    m_pending.vb_stride = stride;
  }
  void RenderTracer::EvBindCBufferRequest(int bufferId) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "bind_cb_req"};
    ev.i0 = bufferId;
    AppendEvent(std::move(ev));
  }

  void RenderTracer::EvBindTextureCommit(int slot, int texId, int viewId, int samplerId,
                                         const std::string& name, const char* stage) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "bind_tex_commit"};
    ev.i0 = slot; ev.i1 = texId; ev.i2 = viewId; ev.i3 = samplerId;
    ev.s0 = name; ev.s1 = stage ? stage : "ps";
    AppendEvent(std::move(ev));
    TraceTextureBind b;
    b.slot = slot; b.texture_id = texId; b.view_id = viewId; b.sampler_id = samplerId;
    b.shader_name = name; b.stage = stage ? stage : "ps";
    // Replace any existing binding to the same slot/stage so the snapshot
    // reflects only the most recent commit per slot.
    for (auto it = m_pending.textures.begin(); it != m_pending.textures.end(); ++it) {
      if (it->slot == slot && it->stage == b.stage) { *it = b; return; }
    }
    m_pending.textures.push_back(std::move(b));
  }
  void RenderTracer::EvBindCBufferCommit(int slot, int bufferId) {
    std::lock_guard<std::mutex> lk(m_mtx);
    CBLast last{};
    auto it = m_cbLast.find(bufferId);
    if (it != m_cbLast.end()) last = it->second;
    TraceEvent ev{0, "bind_cb_commit"};
    ev.i0 = slot; ev.i1 = bufferId;
    ev.u0 = ((uint64_t)last.offset) | ((uint64_t)last.size << 32);
    ev.u1 = last.version;
    ev.s0 = FormatHex64(last.hash);
    AppendEvent(std::move(ev));
    TraceCBufferBind b;
    b.slot = slot; b.buffer_id = bufferId;
    b.offset = last.offset; b.size = last.size;
    b.update_version = last.version; b.hash = last.hash;
#ifdef T850_TRACE_GEOMETRY
    // Snapshot the bytes the GPU saw for this slot at this draw. We copy
    // here (rather than referencing m_cbLast.bytes) so a subsequent
    // EvUpdateCBuffer for the same buffer doesn't retroactively mutate
    // earlier draw snapshots.
    if (!last.bytes.empty()) {
      b.data_hex = HexEncode(last.bytes.data(), last.bytes.size());
    }
#endif
    for (auto bit = m_pending.cbuffers.begin(); bit != m_pending.cbuffers.end(); ++bit) {
      if (bit->slot == slot) { *bit = std::move(b); return; }
    }
    m_pending.cbuffers.push_back(std::move(b));
  }
  void RenderTracer::EvBindPSO(int psoId) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "bind_pso"};
    ev.i0 = psoId;
    AppendEvent(std::move(ev));
    m_pending.pso_id = psoId;
  }

  uint64_t RenderTracer::EvUpdateCBuffer(int bufferId, const void* bytes, uint32_t size, uint32_t allocOffset) {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_active) return 0;
    uint64_t h = FNV1a64(bytes, size);
    uint64_t version = m_nextCBVersion++;
    CBLast last;
    last.offset  = allocOffset;
    last.size    = size;
    last.version = version;
    last.hash    = h;
#ifdef T850_TRACE_GEOMETRY
    if (bytes && size > 0) {
      last.bytes.assign(static_cast<const uint8_t*>(bytes),
                        static_cast<const uint8_t*>(bytes) + size);
    }
#endif
    m_cbLast[bufferId] = std::move(last);
    TraceEvent ev{0, "update_cbuffer"};
    ev.i0 = bufferId;
    ev.u0 = ((uint64_t)allocOffset) | ((uint64_t)size << 32);
    ev.u1 = version;
    ev.f0 = (float)size;
    ev.s0 = FormatHex64(h);
    AppendEvent(std::move(ev));
    return version;
  }
  void RenderTracer::EvUpdateVB(int bufferId, uint32_t size, uint32_t allocOffset) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "update_vb"};
    ev.i0 = bufferId;
    ev.u0 = ((uint64_t)allocOffset) | ((uint64_t)size << 32);
    AppendEvent(std::move(ev));
  }
  void RenderTracer::EvUpdateIB(int bufferId, uint32_t size, uint32_t allocOffset) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "update_ib"};
    ev.i0 = bufferId;
    ev.u0 = ((uint64_t)allocOffset) | ((uint64_t)size << 32);
    AppendEvent(std::move(ev));
  }
  void RenderTracer::EvCreatePSO(const TracePSORec& rec) {
    // Records the PSO catalog entry; id is assigned by RegisterPSO call.
    int id = RegisterPSO(rec);
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "pso_created"};
    ev.i0 = id;
    ev.i1 = rec.shader_id;
    ev.i2 = rec.blend;
    ev.i3 = rec.depth;
    ev.i4 = rec.cull;
    ev.i5 = rec.num_color_attachments;
    ev.s0 = rec.backend;
    AppendEvent(std::move(ev));
  }

  void RenderTracer::EvDrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) {
    std::lock_guard<std::mutex> lk(m_mtx);
    TraceEvent ev{0, "draw_indexed"};
    ev.u0 = vertexCount;
    ev.u1 = ((uint64_t)startIndex) | ((uint64_t)startVertex << 32);
    AppendEvent(std::move(ev));

    // Snapshot pending state.
    TraceDrawSnapshot snap = m_pending;
    snap.seq = m_nextSeq - 1;
    snap.vertex_count = vertexCount;
    snap.start_index  = startIndex;
    snap.start_vertex = startVertex;
    // Attribute the bound VB/IB to a specific update version so a consumer
    // can find the exact bytes resident at draw submission time.
    if (snap.vertex_buffer_id >= 0) {
      auto vit = m_bufferLatestVersion.find(snap.vertex_buffer_id);
      if (vit != m_bufferLatestVersion.end()) snap.vertex_buffer_version = vit->second;
    }
    if (snap.index_buffer_id >= 0) {
      auto iit = m_bufferLatestVersion.find(snap.index_buffer_id);
      if (iit != m_bufferLatestVersion.end()) snap.index_buffer_version = iit->second;
    }
    if (!m_ctxStack.empty()) {
      const auto& c = m_ctxStack.back();
      snap.context_mesh     = c.mesh;
      snap.context_material = c.material;
      snap.context_entity   = c.entity;
      snap.context_pass     = c.pass;
    }
    m_frame.draws.push_back(std::move(snap));
  }

  // ── Pending-state setters ────────────────────────────────────────────

  void RenderTracer::SetPendingShader(int shaderId, int psoId, uint64_t /*keyBits*/) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_pending.shader_id = shaderId;
    m_pending.pso_id = psoId;
  }
  void RenderTracer::SetPendingRT(int rtId)                          { std::lock_guard<std::mutex> lk(m_mtx); m_pending.rt_id = rtId; }
  void RenderTracer::SetPendingVB(int bufferId, uint32_t stride)     { std::lock_guard<std::mutex> lk(m_mtx); m_pending.vertex_buffer_id = bufferId; m_pending.vb_stride = stride; }
  void RenderTracer::SetPendingIB(int bufferId, int format)          { std::lock_guard<std::mutex> lk(m_mtx); m_pending.index_buffer_id = bufferId; m_pending.ib_format = format; }
  void RenderTracer::SetPendingTopology(int topology)                { std::lock_guard<std::mutex> lk(m_mtx); m_pending.topology = topology; }
  void RenderTracer::SetPendingBlend(int state)                      { std::lock_guard<std::mutex> lk(m_mtx); m_pending.blend = state; }
  void RenderTracer::SetPendingDepth(int state)                      { std::lock_guard<std::mutex> lk(m_mtx); m_pending.depth = state; }
  void RenderTracer::SetPendingCull(int state)                       { std::lock_guard<std::mutex> lk(m_mtx); m_pending.cull = state; }
  void RenderTracer::SetPendingViewport(float x, float y, float w, float h) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_pending.viewport_x = x; m_pending.viewport_y = y;
    m_pending.viewport_w = w; m_pending.viewport_h = h;
  }
  void RenderTracer::SetPendingScissor(int x, int y, int w, int h) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_pending.scissor_x = x; m_pending.scissor_y = y;
    m_pending.scissor_w = w; m_pending.scissor_h = h;
  }
  void RenderTracer::SetPendingTextureBind(const TraceTextureBind& bind) {
    std::lock_guard<std::mutex> lk(m_mtx);
    for (auto it = m_pending.textures.begin(); it != m_pending.textures.end(); ++it) {
      if (it->slot == bind.slot && it->stage == bind.stage) { *it = bind; return; }
    }
    m_pending.textures.push_back(bind);
  }
  void RenderTracer::SetPendingCBufferBind(const TraceCBufferBind& bind) {
    std::lock_guard<std::mutex> lk(m_mtx);
    for (auto it = m_pending.cbuffers.begin(); it != m_pending.cbuffers.end(); ++it) {
      if (it->slot == bind.slot) { *it = bind; return; }
    }
    m_pending.cbuffers.push_back(bind);
  }
  void RenderTracer::ClearPendingTextureBinds() { std::lock_guard<std::mutex> lk(m_mtx); m_pending.textures.clear(); }
  void RenderTracer::ClearPendingCBufferBinds() { std::lock_guard<std::mutex> lk(m_mtx); m_pending.cbuffers.clear(); }

} // namespace t850

#else // T850_RENDER_TRACE not defined ───────────────────────────────────

namespace t850 { class RenderTracer; RenderTracer* g_renderTracer = nullptr; }

#endif // T850_RENDER_TRACE
