#ifndef T800_RENDERGRAPH_DESCRIPTOR_H
#define T800_RENDERGRAPH_DESCRIPTOR_H

#include <string>
#include <vector>
#include <array>
#include <scene/ShadowDescriptor.h>

namespace t850 {

  // ---- Render Target declaration ----

  struct RTDesc {
    std::string name;
    int color_count = 1;
    std::string color_format = "RGBA8";    // default for all (used when color_formats is empty)
    std::vector<std::string> color_formats; // per-attachment formats (overrides color_format if non-empty)
    std::string depth_format = "NONE";     // F32, CUBE_F32, NONE
    std::array<int, 2> size = {0, 0};      // [0,0] = screen size
    bool linear_filter = true;
    bool generate_mips = false;
    std::string size_ref;                  // e.g. "$shadow_resolution", "$god_rays_resolution"
    std::string shadow_projection;          // source JSON ID; empty for ordinary targets
  };

  // ---- Texture input (edge in the graph) ----

  struct TextureInput {
    std::string source;   // "GBuffer:DEPTH", "GBuffer:COLOR4", "@ssao_noise", "@environment_map"
    int slot = 0;
  };

  // ---- State overrides ----

  struct StateDesc {
    std::string depth_stencil;   // "READ_WRITE", "READ", ""
    std::string cull_face;       // "BACK_FACES", "FRONT_FACES", ""
    std::string blend;           // "ALPHA_BLEND", "BLEND_DEFAULT", ""
  };

  // ---- Draw command within a pass ----

  struct DrawCmd {
    std::string type = "fullscreen_quad";   // "fullscreen_quad", "mesh", "final_quad", "callback"
    std::vector<int> mesh_indices;          // for "mesh" type
    std::string signature;                  // Signature enum name
    std::vector<std::string> extra_signatures;  // OR'd with main (e.g. "USE_OMNIDIRECTIONAL_SHADOWS")
    std::string callback;                   // callback name when type == "callback"
  };

  // ---- Pass kind (replaces exact-name checks) ----
  enum class RenderPassKind : uint8_t {
    Normal,
    ShadowDepth
  };

  // ---- Render Pass node ----

  struct RenderPassDesc {
    std::string name;             // Human-readable label (becomes node ID)
    std::string target;           // RT name to push, or "" for no push (continuation)
    bool clear = false;
    std::array<float, 4> clear_color = {0, 0, 0, 0};  // RGBA clear color (used when clear=true)
    float clear_depth = 1.0f;                           // Depth clear value

    // State changes before this pass
    StateDesc state;
    // State restoration after this pass
    StateDesc post_state;

    // Camera selection for geometry passes
    std::string camera;           // "main", "light", "omni[N]", ""

    // Active gauss kernel for blur passes (-1 = don't change)
    int gauss_kernel = -1;

    // Active light camera index (-1 = don't change)
    int active_light_camera = -1;

    // Texture inputs (edges from other passes' outputs)
    std::vector<TextureInput> inputs;

    // Whether to bind environment map on the quad
    bool bind_environment_map = false;

    // Draw commands (usually 1, but geometry passes may draw multiple mesh groups)
    std::vector<DrawCmd> draws;

    // Pop the RT after drawing? (false for Night's deferred->volumetric continuation)
    bool pop = true;

    // Push the RT before drawing? (false to reuse the RT already on the stack from a previous pop=false pass)
    bool push = true;

    // Cubemap loop: draw this pass once per cube face
    int cube_faces = 0;           // 0 = not a cubemap loop, 6 = cubemap
    std::string per_face_camera;  // "omni" = use omni light cameras 0..5

    // Generated shadow metadata (filled by expansion; not authored in JSON)
    std::string shadow_projection;      // source JSON ID; empty for ordinary passes
    std::string kind;                   // "normal" (default) or "shadow_depth"
    int shadow_projection_index = -1;
    int shadow_view_index = -1;
    ShadowViewKind shadow_view_kind = ShadowViewKind::WholeTexture2D;
    int shadow_subresource = -1;
    std::array<int, 4> viewport = {-1, -1, -1, -1};  // x,y,w,h; -1 = full target
  };

  // ---- The full render graph ----

  struct RenderGraphDesc {
    std::vector<ShadowProjectionDesc> shadow_projections;
    std::vector<RTDesc> render_targets;
    std::vector<RenderPassDesc> passes;
  };

  bool LoadRenderGraphDescriptor(const std::string& path, RenderGraphDesc& desc);

} // namespace t850

#endif
