#pragma once
// ─── T8 RenderTracer: Per-frame API instrumentation for cross-backend diffing ────
//
// Goal: capture every resource the engine creates and every state change /
// bind / draw it makes, write to JSON next to the FrameDumper RT dumps so two
// API runs (e.g. D3D12 vs Vulkan) can be diffed mechanically when their
// rendered output differs.
//
// Guarded by T850_RENDER_TRACE. When undefined, all macros expand to no-ops.
//
// Two layers of bind events are recorded per draw:
//   * "request" events when engine code calls Texture::Set / CB::Set / etc.
//   * "commit" events when the backend actually plumbs bindings to the GPU
//     (Vulkan: BindPendingDescriptors before vkCmdDrawIndexed; D3D12: same
//     site as request since SetGraphicsRootDescriptorTable is synchronous).
//
// Both are needed because Vulkan delays texture binds until draw time, so a
// "request" alone doesn't prove anything about what the GPU actually saw.
//
// Each draw_indexed event also embeds a denormalized snapshot of the
// effective bind state (PSO id, RT id, shader id, VB/IB ids, all bound
// textures with view+sampler ids, all bound CB slices with update_version)
// so a consumer doesn't have to scan backwards through events to reconstruct
// state.

#ifdef T850_RENDER_TRACE

#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <cstdint>
#include <mutex>

namespace t850 {

  class BaseDriver;
  class Texture;
  class BaseRT;
  class ShaderBase;
  class VertexBuffer;
  class IndexBuffer;
  class ConstantBuffer;

  // ── Resource records (catalog) ─────────────────────────────────────────

  struct TraceTextureRec {
    int id = -1;
    std::string name;       // optname (basename if file-loaded)
    std::string filepath;
    int width = 0, height = 0;
    int mipmaps = 0;
    int channels = 0;
    uint32_t props = 0;     // CIL props bitfield
    uint32_t params = 0;
    std::string kind;       // "tex2d" | "cubemap" | "float2d" | "floatcube"
    std::string format_str; // best-effort backend format string
    int generation = 0;
  };

  struct TraceRTRec {
    int id = -1;
    std::string name;
    int w = 0, h = 0;
    int color_count = 0;
    std::vector<int> color_formats;     // BaseRT::FORMAT enum values
    int depth_format = 0;
    bool gen_mips = false;
    std::vector<int> color_texture_ids; // matching BaseDriver::Textures ids
    int depth_texture_id = -1;
    int generation = 0;
  };

  struct TraceShaderRec {
    int id = -1;
    uint64_t key_bits = 0;
    std::string key_hex;
    int pass = 0;
    std::string vs_name;
    std::string fs_name;
    std::vector<std::string> defines;
  };

  // Backend-specific PSO record. Holds the exact key used for the backend
  // pipeline cache so two traces can be compared field-by-field.
  struct TracePSORec {
    int id = -1;
    std::string backend;    // "d3d12" | "vulkan"
    int shader_id = -1;
    uint64_t shader_key_bits = 0;
    int blend = 0;
    int depth = 0;
    int cull = 0;
    int topology = 0;
    int num_color_attachments = 0;
    std::vector<uint32_t> color_formats; // backend native enum values (DXGI_FORMAT or VkFormat)
    uint32_t depth_format = 0;
    uint32_t vertex_stride = 0;          // vulkan only (other backends: 0)
    uint64_t render_pass = 0;            // vulkan handle (other backends: 0)
  };

  struct TraceSamplerRec {
    int id = -1;
    std::string filter;      // "linear" | "anisotropic" | "nearest" | ...
    std::string address_u, address_v, address_w;
    float anisotropy = 1.0f;
    float lod_bias = 0.0f;
    std::string compare;
    std::array<float, 4> border_color = { 0, 0, 0, 0 };
  };

  struct TraceTextureViewRec {
    int id = -1;
    int texture_id = -1;
    int base_mip = 0;
    int mip_count = 0;
    int base_layer = 0;
    int layer_count = 0;
    std::string aspect;     // "color" | "depth" | "stencil"
    std::string view_format;
  };

  // ── Event records ───────────────────────────────────────────────────────

  // Per-event base. Use a discriminated map of fields kept as JSON-friendly
  // primitives so glaze can serialize without needing a sum type.
  struct TraceEvent {
    int seq = 0;
    std::string type;                  // see EventType strings below

    // Common payload fields (most are unused for any one event type — left
    // at sentinel values; glaze writes them all unconditionally for
    // mechanical diffability).
    int     i0 = -1, i1 = -1, i2 = -1, i3 = -1, i4 = -1, i5 = -1;
    uint64_t u0 = 0, u1 = 0;
    float   f0 = 0, f1 = 0, f2 = 0, f3 = 0;
    std::string s0;                    // primary name / pass / context
    std::string s1;                    // secondary string (e.g. shader binding name)
  };

  // Per-draw denormalized snapshot. Embedded at draw time so a consumer
  // doesn't have to scan backwards.
  struct TraceTextureBind {
    int slot = -1;
    int texture_id = -1;
    int view_id = -1;
    int sampler_id = -1;
    std::string shader_name;
    std::string stage;                 // "ps" | "vs"
  };

  struct TraceCBufferBind {
    int slot = -1;
    int buffer_id = -1;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint64_t update_version = 0;       // monotonically increasing per UpdateFromBuffer call
    uint64_t hash = 0;
  };

  struct TraceDrawSnapshot {
    int seq = 0;
    int rt_id = -1;
    int shader_id = -1;
    int pso_id = -1;
    int vertex_buffer_id = -1;
    uint32_t vb_stride = 0;
    int index_buffer_id = -1;
    int ib_format = 0;                 // 0=R16, 1=R32
    int topology = 0;
    int blend = 0, depth = 0, cull = 0;
    float viewport_x = 0, viewport_y = 0, viewport_w = 0, viewport_h = 0;
    int scissor_x = 0, scissor_y = 0, scissor_w = 0, scissor_h = 0;
    unsigned vertex_count = 0, start_index = 0, start_vertex = 0;
    std::string context_mesh;
    std::string context_material;
    std::string context_entity;
    std::string context_pass;
    std::vector<TraceTextureBind> textures;
    std::vector<TraceCBufferBind> cbuffers;
  };

  // Top-level frame trace.
  struct TraceFrame {
    int  frame = 0;
    int  scene = 0;
    std::string api;
    std::string timestamp;

    std::vector<TraceTextureRec>     textures;
    std::vector<TraceTextureViewRec> texture_views;
    std::vector<TraceSamplerRec>     samplers;
    std::vector<TraceRTRec>          rts;
    std::vector<TraceShaderRec>      shaders;
    std::vector<TracePSORec>         psos;

    std::vector<TraceEvent>          events;
    std::vector<TraceDrawSnapshot>   draws;
  };

  // ── RenderTracer (singleton accessed via g_renderTracer) ────────────────

  class RenderTracer {
  public:
    void Init(BaseDriver* driver);
    void Destroy();

    // Frame lifecycle: call at start/end of every rendered frame. Events
    // are cleared at ResetFrame. Draws/snapshots and the resource catalog
    // persist between ResetFrame and the matching Save call so the dump
    // captures everything submitted that frame.
    void ResetFrame(int frameIndex);
    void Save(const std::string& dirPath); // writes <dir>/trace.json

    // Resource registration. Idempotent — if pointer already known returns
    // existing id. Engine code should call these from BaseDriver::Create*.
    int RegisterTexture(const Texture* tex, const char* kind);
    int RegisterRT(const BaseRT* rt, const char* name, int idHint);
    int RegisterShader(const ShaderBase* sh, uint64_t keyBits, const std::string& vsName, const std::string& fsName);

    int RegisterSampler(const TraceSamplerRec& rec);
    int RegisterTextureView(const TraceTextureViewRec& rec);
    int RegisterPSO(TracePSORec rec);  // returns id; rec.id is overwritten

    int LookupTextureId(const Texture* tex) const;
    int LookupRTId(const BaseRT* rt) const;
    int LookupShaderId(const ShaderBase* sh) const;

    // Generic pointer-keyed id assignment (used for cbuffers, vertex/index
    // buffers — anything BaseDriver doesn't track in a vector). Returns a
    // stable monotonic id; first call for a pointer assigns a new id, later
    // calls return the same id.
    int EnsureBufferId(const void* ptr, const char* kind);

    // Draw-context push: scene code calls before issuing draws so each
    // draw_snapshot gets readable mesh/material/entity/pass strings.
    void PushContext(const char* mesh, const char* material, const char* entity, const char* pass);
    void PopContext();

    // ── State events ──────────────────────────────────────────────────────
    void EvBeginFrame();
    void EvEndFrame();
    void EvPushRT(int rtId, bool load);
    void EvPopRT();
    void EvSetBlend(int state);
    void EvSetDepth(int state);
    void EvSetCull(int state);
    void EvSetViewport(float x, float y, float w, float h);
    void EvSetScissor(int x, int y, int w, int h);
    void EvSetTopology(int topology);
    void EvClear();
    void EvClearColor(float r, float g, float b, float a);

    // ── Bind requests (engine-side) ───────────────────────────────────────
    void EvBindShader(int shaderId, uint64_t keyBits);
    void EvBindTextureRequest(int slot, int texId, const std::string& name, const char* stage);
    void EvBindSamplerRequest(int slot);
    void EvBindIndexBufferRequest(int bufferId, int format, uint32_t offset);
    void EvBindVertexBufferRequest(int bufferId, uint32_t stride, uint32_t offset);
    void EvBindCBufferRequest(int bufferId);

    // ── Backend commit events (called from per-API descriptor flush) ──────
    void EvBindTextureCommit(int slot, int texId, int viewId, int samplerId, const std::string& name, const char* stage);
    void EvBindCBufferCommit(int slot, int bufferId);  // looks up last update for bufferId
    void EvBindPSO(int psoId);

    // ── Resource updates ──────────────────────────────────────────────────
    // Records hash + remembers (offset,size,version,hash) for bufferId so a
    // subsequent EvBindCBufferCommit can reference it without recomputing.
    // Returns the assigned monotonic version (0 if tracer inactive).
    uint64_t EvUpdateCBuffer(int bufferId, const void* bytes, uint32_t size, uint32_t allocOffset);
    void     EvUpdateVB(int bufferId, uint32_t size, uint32_t allocOffset);
    void     EvUpdateIB(int bufferId, uint32_t size, uint32_t allocOffset);
    void     EvCreatePSO(const TracePSORec& rec);

    // ── Draw event ────────────────────────────────────────────────────────
    void EvDrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex);

    // Pending-state setters (called by per-API binds to populate the next
    // draw_snapshot without spamming new event types).
    void SetPendingShader(int shaderId, int psoId, uint64_t keyBits);
    void SetPendingRT(int rtId);
    void SetPendingVB(int bufferId, uint32_t stride);
    void SetPendingIB(int bufferId, int format);
    void SetPendingTopology(int topology);
    void SetPendingBlend(int state);
    void SetPendingDepth(int state);
    void SetPendingCull(int state);
    void SetPendingViewport(float x, float y, float w, float h);
    void SetPendingScissor(int x, int y, int w, int h);
    void SetPendingTextureBind(const TraceTextureBind& bind);
    void SetPendingCBufferBind(const TraceCBufferBind& bind);
    void ClearPendingTextureBinds();
    void ClearPendingCBufferBinds();

    bool IsActive() const { return m_active; }

  private:
    int NextEventSeq() { return m_nextSeq++; }
    void AppendEvent(TraceEvent ev);

    mutable std::mutex m_mtx;
    bool m_active = false;
    BaseDriver* m_driver = nullptr;
    int m_nextSeq = 0;
    TraceFrame m_frame;

    // Identity maps so resource ids are stable across calls.
    std::unordered_map<const Texture*, int>     m_texMap;
    std::unordered_map<const BaseRT*, int>      m_rtMap;
    std::unordered_map<const ShaderBase*, int>  m_shMap;
    std::unordered_map<const void*, int>        m_bufMap;
    int m_nextBufferId = 0;

    // Monotonic counters.
    int m_nextSamplerId = 0;
    int m_nextViewId = 0;
    int m_nextPSOId = 0;
    uint64_t m_nextCBVersion = 1;

    // Last upload state per cbuffer id (set at EvUpdateCBuffer time, read
    // at EvBindCBufferCommit time so the bind event references the actual
    // bytes that were most recently uploaded for that buffer).
    struct CBLast {
      uint32_t offset = 0;
      uint32_t size = 0;
      uint64_t version = 0;
      uint64_t hash = 0;
    };
    std::unordered_map<int, CBLast> m_cbLast;

    // Pending state (denormalized into next draw_snapshot).
    TraceDrawSnapshot m_pending;

    // Context stack (mesh/material/entity/pass strings).
    struct CtxFrame {
      std::string mesh, material, entity, pass;
    };
    std::vector<CtxFrame> m_ctxStack;
  };

  extern RenderTracer* g_renderTracer;

} // namespace t850

// ── Convenience macros ────────────────────────────────────────────────────
// All macros are no-ops when T850_RENDER_TRACE is undefined; this keeps
// per-API call sites short.

#define T8_TRACE_ACTIVE()                  (::t850::g_renderTracer && ::t850::g_renderTracer->IsActive())
#define T8_TRACE(call)                     do { if (T8_TRACE_ACTIVE()) { ::t850::g_renderTracer->call; } } while(0)

#define T8_TRACE_REGISTER_TEXTURE(t, kind) (T8_TRACE_ACTIVE() ? ::t850::g_renderTracer->RegisterTexture((t), (kind)) : -1)
#define T8_TRACE_REGISTER_RT(rt, name, h)  (T8_TRACE_ACTIVE() ? ::t850::g_renderTracer->RegisterRT((rt), (name), (h)) : -1)
#define T8_TRACE_REGISTER_SHADER(sh, k, v, f) \
  (T8_TRACE_ACTIVE() ? ::t850::g_renderTracer->RegisterShader((sh), (k), (v), (f)) : -1)

#else // T850_RENDER_TRACE not defined ───────────────────────────────────

#define T8_TRACE_ACTIVE()                  false
#define T8_TRACE(call)                     ((void)0)
#define T8_TRACE_REGISTER_TEXTURE(t, kind) (-1)
#define T8_TRACE_REGISTER_RT(rt, name, h)  (-1)
#define T8_TRACE_REGISTER_SHADER(sh, k, v, f) (-1)

namespace t850 {
  // Forward-declared so other headers can write `::t850::RenderTracer*` without
  // fenceposting. The pointer remains nullptr in non-traced builds.
  class RenderTracer;
  extern RenderTracer* g_renderTracer;
}

#endif // T850_RENDER_TRACE
