#pragma once

// Generic particle systems.
//
// WHY THIS EXISTS
//
// Before this, particle support in the engine was ~20 loose SceneProps fields plus one
// hardcoded kernel (CS_TorchParticles) that generated its particles procedurally inside
// the shader. That is fine for a torch flame and useless for anything whose particles are
// produced elsewhere, such as a fluid solver that owns real simulated positions.
//
// A ParticleSystem is a *renderable* description of a particle cloud. It says where the
// particles come from (a procedural emitter, or an external GPU buffer the system does not
// own) and how they are drawn (shape, size, colour, opacity, blend). It says nothing about
// how the particles move: that belongs to whoever owns them.
//
// Two sources cover the cases the engine needs:
//
//   Procedural  The system generates particles in the shader from an emitter + time.
//               Fire, sparks, smoke puffs, dust. No simulation, no state.
//               This is CS_TorchParticles' model generalised.
//
//   Buffer      The system draws an external StructuredBuffer<float4> of positions
//               (xyz + w). The owner fills it. Fluid, debris, custom simulations.
//
// Scenes describe systems in JSON and reference them by name from a render-graph pass, so
// "add fire" is a data change rather than a new kernel and a new constants builder.

#include <vector>
#include <array>
#include <string>

namespace t850 {

  class ComputeBuffer;

  // How the system obtains particle positions.
  enum class ParticleSourceKind {
    Procedural,   // generated in-shader from an emitter
    Buffer        // read from an external StructuredBuffer
  };

  // Splat shape. Every shape is evaluated analytically in screen space, so a system never
  // needs its own geometry.
  enum class ParticleShape {
    SoftSphere,   // radial falloff; water, smoke
    Sprite,       // square falloff; embers, dust
    Streak        // elongated along the velocity direction; sparks, rain
  };

  // Blend applied when the system is composited over the scene.
  enum class ParticleBlend {
    Additive,     // light-emitting: fire, sparks
    Alpha         // occluding: water, smoke, debris
  };

  // How the splat itself is drawn. This is a look/debug knob, independent of Shape
  // (which selects the falloff kernel) and Blend.
  enum class ParticleStyle {
    Shaded,       // soft analytic splat (the fluid look)
    Wireframe,    // ring at the splat edge: shows particle placement and coverage
    Flat          // hard-edged filled disc
  };

  // Surface reconstruction for a Buffer-source fluid.
  //   Splat       the accumulated weighted-average splat (the original renderer)
  //   ScreenSpace screen-space fluid: front-surface depth, curvature-flow smoothing,
  //               normals from the depth field, Fresnel + refraction + absorption
  //               (van der Laan et al., "Screen Space Fluid Rendering with Curvature Flow").
  enum class ParticleRenderMode {
    Splat,
    ScreenSpace
  };

  // Screen-space fluid tuning. All of these are data: a scene configures them in JSON and
  // the Dev GUI edits the live values.
  struct ParticleScreenSpaceDesc {
    // Curvature-flow passes. The render graph contains a fixed maximum of four
    // ping-pong passes; iterations above that are clamped by the graph.
    int smoothing_iterations = 4;
    // Per-pass smoothing step in [0,1]. 0 disables smoothing for that pass.
    float smoothing_strength = 0.85f;
    // Weighted-smoothing radius in pixels. A particle splat is ~10-20 pixels across, so a
    // 3x3 stencil is not enough to remove the lattice.
    float smoothing_radius = 10.0f;
    // Depth discontinuity guard, in metres. Neighbours whose view depth differs by more
    // than this are not blended (silhouettes must not merge).
    float depth_tolerance = 0.35f;
    // Multiplier on the accumulated per-pixel thickness (metres of fluid along the ray).
    float thickness_scale = 1.0f;
    // Per-metre absorption coefficients (Beer-Lambert), RGB.
    std::array<float, 3> absorption = {{3.0f, 1.0f, 0.5f}};
    // Sky/ambient colour used for reflected rays that leave the screen upward.
    std::array<float, 3> reflection = {{0.45f, 0.62f, 0.85f}};
    // Screen-space refraction offset, in UV units, scaled by the normal.
    float refraction = 0.02f;
    // Sun specular strength.
    float specular = 1.0f;
    // Fresnel exponent.
    float fresnel_power = 5.0f;
    // Amplifies the reconstructed depth gradient before computing normals.
    float normal_scale = 0.7f;
    // Pixel distance between the depth samples used for the normal stencil (1..8).
    float normal_step = 4.0f;
    // Grey/white foam added where the fluid is thin (0 disables).
    float foam = 0.0f;
    // 0 = shaded, 1 = normals, 2 = depth, 3 = thickness, 4 = coverage.
    int debug_view = 0;
  };

  struct ParticleColorRamp {
    // Four stops over particle life (or over normalised depth when there is no life).
    std::array<std::array<float, 3>, 4> stops = {{
      {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}},
      {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}
    }};
  };

  struct ParticleSystemDesc {
    std::string name;                     // graph-unique; the pass references this

    ParticleSourceKind source = ParticleSourceKind::Procedural;
    ParticleShape shape = ParticleShape::SoftSphere;
    ParticleBlend blend = ParticleBlend::Alpha;
    ParticleStyle style = ParticleStyle::Shaded;
    ParticleRenderMode render_mode = ParticleRenderMode::Splat;
    ParticleScreenSpaceDesc screen_space;

    // ---- Sizing ----
    // World radius in metres for Buffer sources; a base scale for procedural ones.
    float radius = 0.13f;
    // Multiplier applied to the splat radius, by normalised depth or life. 1 = constant.
    float size_scale_min = 1.0f;
    float size_scale_max = 1.0f;

    // ---- Appearance ----
    ParticleColorRamp ramp;
    float opacity = 0.22f;                // alpha at the splat centre for Alpha blends
    float intensity = 1.0f;               // brightness multiplier for Additive blends
    float softness = 0.35f;               // edge feathered fraction, 0 = hard disc
    // Fade the splat out past this depth difference, so particles do not draw over
    // geometry in front of them. 0 disables the depth test.
    float depth_bias = 0.05f;

    // ---- Procedural source ----
    int count = 0;                        // particles emitted per frame
    float lifetime = 1.0f;                // seconds
    float spread = 0.25f;                 // emitter radius
    float rise = 1.0f;                    // metres travelled over one life
    std::string emitter_prop = "ParticleEmitterPosition";  // SceneProps field to read

    // ---- Buffer source ----
    // Name of a compute_buffers[] entry holding float4 positions (see render-graph.md).
    std::string buffer;
    // Optional: a compute_buffers[] entry of float4 velocities, for streaks and speed tint.
    std::string velocity_buffer;
    // Optional: a compute_buffers[] entry of float4 view-projection. When empty the graph
    // creates the camera matrix buffer for this system automatically.
    std::string view_projection_buffer;
    // Element count override; 0 = use the buffer's own element_count.
    int buffer_count = 0;

    bool enabled = true;
  };

  // A named ComputeBuffer a particle system can reference, owned by whoever filled it.
  // The graph resolves a system's `buffer` / `velocity_buffer` through this table, so a
  // solver keeps ownership of its storage while the renderer stays generic.
  class ParticleBufferRegistry {
  public:
    static ParticleBufferRegistry& Instance();
    void Clear();
    void Set(const std::string& name, ComputeBuffer* buffer, int elementCount);
    ComputeBuffer* Find(const std::string& name, int* elementCount = nullptr) const;
  private:
    struct Entry { std::string name; ComputeBuffer* buffer = nullptr; int count = 0; };
    std::vector<Entry> m_entries;
  };

  // Owns the particle systems the active scene has declared, plus the live state a
  // procedural system needs (its clock). Deliberately render-side only: nothing here
  // simulates anything.
  class ParticleSystemRegistry {
  public:
    static ParticleSystemRegistry& Instance();

    void Clear();
    // Replaces any existing system with the same name. Returns false on an invalid desc.
    bool Add(const ParticleSystemDesc& desc);
    const ParticleSystemDesc* Find(const std::string& name) const;
    const std::vector<ParticleSystemDesc>& Systems() const { return m_systems; }

    // Scene clock, advanced by the owning scene each frame. Procedural systems animate
    // from this; buffer systems ignore it.
    void Advance(float deltaSeconds);
    float Time() const { return m_timeSeconds; }

  private:
    std::vector<ParticleSystemDesc> m_systems;
    float m_timeSeconds = 0.0f;
  };

}  // namespace t850
