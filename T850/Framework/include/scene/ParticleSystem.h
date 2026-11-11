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
    Shaded,       // soft analytic splat; a scene can draw it as blended water instead
    Wireframe,    // ring at the splat edge: shows particle placement and coverage
    Flat,         // hard-edged filled disc
    Spheres       // opaque lit sphere per particle, depth sorted (debug)
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
    // Edge-aware a-trous smoothing passes; the tap spacing doubles every pass. The graph
    // contains eight ping-pong passes; counts below eight make the remaining passes copy.
    int smoothing_iterations = 6;
    // Blend toward the filtered value in [0,1].
    float smoothing_strength = 1.0f;
    // Upper bound, in pixels, for the projected filter radius.
    float smoothing_radius = 64.0f;
    // Filter radius in metres; projected per pixel, so near and far water smooth alike.
    float smoothing_world_radius = 0.3f;
    // Narrow-range threshold in metres: neighbours further behind belong to another layer and
    // are ignored, neighbours further in front are clamped to it.
    float depth_tolerance = 0.3f;
    // Depth sphere radius as a fraction of the particle spacing. Smaller than the thickness
    // kernel so the surface sits on the fluid, not half a radius outside it.
    float depth_radius_scale = 0.70f;
    // Sphere packing factor used to convert a chord sum into metres of water. 4.19 is
    // (4/3)*pi for a cubic lattice with radius == spacing.
    float packing_factor = 4.19f;
    // Multiplier on the accumulated per-pixel thickness (metres of fluid along the ray).
    float thickness_scale = 1.0f;
    // Per-metre absorption coefficients (Beer-Lambert), RGB. Clear water is approximately
    // (0.45, 0.06, 0.02); `absorption_scale` is the artistic control.
    std::array<float, 3> absorption = {{0.45f, 0.06f, 0.02f}};
    float absorption_scale = 4.0f;
    // Colour scattered by the fluid; replaces the old hard-coded teal.
    std::array<float, 3> fluid_color = {{0.10f, 0.30f, 0.42f}};
    // Sky colour for reflected rays when no sky is visible on screen (the zenith), and the
    // particle renderer's ambient.
    std::array<float, 3> reflection = {{0.45f, 0.62f, 0.85f}};
    // Horizon end of the off-screen sky gradient for reflected rays.
    std::array<float, 3> sky_horizon = {{0.85f, 0.80f, 0.74f}};
    // Particle renderer: refracted path length per metre of thickness.
    float refraction = 0.5f;
    // Screen-space renderer: refraction strength, 0..1 (1 bends the transmitted ray by Snell's
    // law for water, n = 1.33, over the water column under the surface).
    float refraction_strength = 1.0f;
    // Screen-space reflection range in metres; 0 reflects only the sky.
    float reflection_distance = 40.0f;
    // Sun specular strength.
    float specular = 1.0f;
    // Exponent of the normalised sun glint (larger = smaller, brighter sparkles).
    float glint_exponent = 900.0f;
    // Wind ripples on near-horizontal water: slope amplitude, longest wavelength (m), speed
    // multiplier on deep-water dispersion, and the distance (m) over which they fade out.
    float ripple_slope = 0.05f;
    float ripple_wavelength = 0.9f;
    float ripple_speed = 0.6f;
    float ripple_distance = 30.0f;
    // Caustics on the submerged floor and walls: strength (0 = off), pattern scale (m),
    // animation speed, and the depth (m) over which they fade.
    float caustics = 0.6f;
    float caustic_scale = 0.9f;
    float caustic_speed = 1.0f;
    float caustic_depth = 1.6f;
    // Fresnel exponent.
    float fresnel_power = 5.0f;
    // Multiplier on the view-space derivatives used to build the normal (1 = geometric).
    float normal_scale = 1.0f;
    // Pixel distance between the depth samples used for the normal stencil (1..8).
    float normal_step = 1.0f;
    // Whitewater strength for isolated droplets (spray), 0..1. 0 draws them as clear water.
    float foam = 0.0f;
    // Keep the splat resolve under the screen-space layer. This masks a broken screen-space
    // backend, but it also re-blends the background at the silhouette and costs a second
    // all-particle pass. Off is the correct setting on Vulkan/GL/D3D12.
    bool splat_fallback = false;
    // Draw each particle as the ellipsoid in `anisotropy_buffer` (when the system has one)
    // instead of a sphere: flat free surfaces and continuous sheets instead of domes.
    bool anisotropy = true;
    // ---- Lighting from the scene (2026-11 realism pass) ----
    // Multiplier on the environment cube for reflected rays the screen-space trace cannot
    // resolve; 0 falls back to the analytic zenith/horizon gradient.
    float env_intensity = 1.0f;
    // Sunlight absorbed on its way DOWN to submerged surfaces (0 = lit as if in air, 1 = full
    // Beer-Lambert over the refracted sun path).
    float sun_attenuation = 1.0f;
    // Caustic focusing by the simulated surface (0 = procedural pattern only).
    float caustic_focus = 1.0f;
    // Brightening of the meniscus where the water meets a wall.
    float contact_line = 0.5f;
    // Thin crests and sheets glowing when seen toward the sun.
    float backlight = 1.0f;
    // In-scattering: sun and sky light scattered toward the eye inside the water.
    float scatter_sun = 0.6f;
    float scatter_sky = 0.8f;
    float scatter_anisotropy = 0.45f;   // Henyey-Greenstein g, 0 = isotropic
    // Ripple slope multipliers: still water, and the extra slope at full local agitation.
    float ripple_calm = 0.6f;
    float ripple_agitation = 1.5f;
    // ---- Whitewater (Ihmsen et al. 2012) ----
    float foam_strength = 1.0f;         // surface foam
    float foam_texture_scale = 0.35f;   // foam lace cell size, metres
    float aeration = 1.0f;              // milky entrained air under the surface
    float bubble_strength = 1.0f;       // bubble sparkles
    float spray_strength = 1.0f;        // secondary spray droplets
    float wetness = 1.0f;               // darkening and sheen of wet deck/coping
    // 0 = shaded, 1 = normals, 2 = depth, 3 = water path, 4 = coverage, 5 = reflection,
    // 6 = refraction, 7 = whitewater (r foam, g aeration, b spray), 8 = sun light under
    // water (r direct fraction, g transmittance, b caustics), 9 = wetness.
    int debug_view = 0;
  };

  // Scene geometry a fluid renderer needs from its simulation (set by the owning scene).
  struct ParticleFluidGeometry {
    std::array<float, 3> pool_min = {{0.0f, 0.0f, 0.0f}};   // basin interior; y = floor
    std::array<float, 3> pool_max = {{0.0f, 0.0f, 0.0f}};   // y = coping top
    float water_plane_y = 0.0f;
    float deck_y = 0.0f;
    // World-space wetness map (texel (0,0) at origin, square texels).
    float wet_origin_x = 0.0f;
    float wet_origin_z = 0.0f;
    float wet_texel = 0.1f;
    int wet_width = 0;
    int wet_height = 0;
    // Caustic focus field over the basin footprint (one value per surface column).
    int surface_width = 0;
    int surface_depth = 0;
    // Secondary (diffuse) particle pool.
    int diffuse_capacity = 0;
    float spray_radius = 0.015f;
    float foam_radius = 0.03f;
    float bubble_radius = 0.01f;
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
    // Optional: an externally published buffer of anisotropic particle shapes (three float4
    // per particle, see CS_PbfAnisotropy.hlsl). The screen-space depth splat ray-casts these
    // ellipsoids instead of spheres when it is set.
    std::string anisotropy_buffer;
    // Optional: further externally published buffers the system's passes bind (whitewater,
    // secondary particles, caustic field, wetness map). Declaring them lets the graph accept
    // the names before the solver publishes them.
    std::vector<std::string> extra_buffers;
    // Optional: per-particle float4 whitewater (foam, aeration, agitation, -) from the solver.
    std::string whitewater_buffer;
    // Optional: secondary particle pool, two float4 per particle (position + life, velocity +
    // class). Its published count is the pool capacity.
    std::string diffuse_buffer;
    ParticleFluidGeometry geometry;
    // Optional: a compute_buffers[] entry of float4 view-projection. When empty the graph
    // creates the camera matrix buffer for this system automatically.
    std::string view_projection_buffer;
    // Element count override; 0 = use the buffer's own element_count.
    int buffer_count = 0;
    // Size of the graph's per-particle scratch (projected table); 0 = buffer_count. Set it to
    // the largest count the system can reach when buffer_count changes after the graph loads.
    int buffer_capacity = 0;

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
