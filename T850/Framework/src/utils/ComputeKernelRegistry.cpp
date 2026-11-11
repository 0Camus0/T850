#include <pch.h>

#include <utils/ComputeKernelRegistry.h>
#include <scene/ParticleSystem.h>
#include <scene/SceneProp.h>
#include <utils/Camera.h>
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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace t850 {

struct ComputeBindingManifest {
  std::string type;
  int shader_register = 0;
  int binding_index = 0;
  int constant_count = 0;
  std::string storage_format = "unspecified";
  std::string dimension = "2d";
  bool match_output_extent = false;
};

struct ComputeKernelManifestEntry {
  std::string id;
  std::string source;
  std::string entry = "CS";
  std::vector<std::string> permutations;
  std::vector<ComputeBindingManifest> bindings;
  std::string pipeline_configurator;
  std::string constants_builder;
};

struct ComputeKernelManifest {
  int version = 0;
  std::vector<ComputeKernelManifestEntry> kernels;
};

namespace {

constexpr uint32_t kMaxBlurWeights = 24;

struct GodRaysComputeConstants {
  XMATRIX44 WVPInverse;
  XMATRIX44 WVPLight;
  XVECTOR3 CameraPosition;
  XVECTOR3 SunDirectionAndBias;
  XVECTOR3 VolumeCenterAndEnabled;
  XVECTOR3 VolumeHalfExtentsAndFactor;
  XVECTOR3 OutputSizeStepsAndEnabled;
};
static_assert(sizeof(GodRaysComputeConstants) == 208);

struct BlurComputeConstants {
  uint32_t outputWidth;
  uint32_t outputHeight;
  uint32_t kernelSize;
  uint32_t direction;
  float radius;
  float padding[3];
  float weights[kMaxBlurWeights];
};
static_assert(sizeof(BlurComputeConstants) == 128);

struct PostProcessComputeConstants {
  float outputWidth;
  float outputHeight;
  float parameter0;
  float parameter1;
  float parameter2;
  float padding[3];
};
static_assert(sizeof(PostProcessComputeConstants) == 32);

struct ParticleComputeConstants {
  XMATRIX44 viewProjection;
  XVECTOR3 emitterAndTime;
  XVECTOR3 additionalEmitterXZ;
  XVECTOR3 outputSizeCountEnabled;
  XVECTOR3 motion;
  XVECTOR3 color0;
  XVECTOR3 color1;
  XVECTOR3 color2;
  XVECTOR3 shape;
  XVECTOR3 wobble;
  XVECTOR3 fade;
  XVECTOR3 intensity;
};
static_assert(sizeof(ParticleComputeConstants) == 240);

// Mirrors `cbuffer ParticleConstants` in CS_PbfParticles.hlsl: 16 DWORDs.
struct FluidParticleComputeConstants {
  uint32_t particleCount;
  uint32_t outputWidth;
  uint32_t outputHeight;
  uint32_t iteration;
  float particleRadius;
  float depthBias;
  float opacity;
  float pad0;
  float cameraPosition[3];
  float pad1;
  float fluidColor[3];
  float tileCountX;
};
static_assert(sizeof(FluidParticleComputeConstants) == 64);

// Mirrors `cbuffer ParticleConstants` in CS_ParticleSplat.hlsl.
// 4 + 12 + 4 + 12 + 8 floats/units = 160 bytes (40 DWORDs).
struct ParticleSplatConstants {
  uint32_t particleCount;
  uint32_t outputWidth;
  uint32_t outputHeight;
  uint32_t iteration;
  float radius;
  float opacity;
  float intensity;
  float softness;
  float depthBias;
  float lifetime;
  float spread;
  float rise;
  float timeSeconds;
  float sizeScaleMin;
  float sizeScaleMax;
  uint32_t shape;
  uint32_t blend;
  uint32_t source;
  float speedTint;            // reused as the screen-space flag (shader field ScreenSpace)
  float tileCountX;
  float tileCountY;
  uint32_t frameIndex;
  float projScaleY;
  float pad2;                 // style (shader field Style)
  float depthRadiusScale;     // depth sphere radius / spacing
  float invPackingFactor;     // 1 / packing factor
  float projA;                // Projection.m[2][2]
  float projB;                // Projection.m[3][2]
  float color[4][4];          // four 16-byte slots: rgb + pad
  float emitterPosition[4];   // xyz + pad
  float viewProjection[16];   // row-major, 4 x float4
};
static_assert(sizeof(ParticleSplatConstants) == 256);

// Mirrors `cbuffer ParticleSmoothConstants` in CS_ParticleSmooth.hlsl.
struct ParticleSmoothConstants {
  uint32_t outputWidth;
  uint32_t outputHeight;
  float spacing;              // a-trous tap spacing in pixels, 0 = copy
  float worldRadius;          // filter radius in metres
  float strength;
  float tolerance;            // layer threshold in metres
  float radiusPx;             // projected radius cap
  float projScaleY;
};
static_assert(sizeof(ParticleSmoothConstants) == 32);

// Mirrors `cbuffer ParticleShadeConstants` in CS_ParticleShade.hlsl.
struct ParticleShadeConstants {
  uint32_t outputWidth;
  uint32_t outputHeight;
  uint32_t mode;          // 0 splat, 1 screen space
  uint32_t debugView;     // 0 shaded, 1 normals, 2 depth, 3 water path, 4 coverage, 5 reflection, 6 refraction
  float projScaleY;
  float projA;            // Projection.m[2][2]
  float projB;            // Projection.m[3][2]
  float maxTravel;        // longest refracted path, metres
  float thicknessScale;
  float refraction;       // 0..1 refraction strength (1 = Snell)
  float specular;
  float fresnelPower;
  float normalScale;
  float foam;
  float reflectionDistance;   // screen-space reflection range, metres (0 = off)
  float normalStep;
  float fluidTint[4];
  float sunDirection[4];  // view space, toward the light
  float sunColor[4];      // rgb * intensity
  float cameraRight[4];   // w = camera position x
  float cameraUp[4];      // w = camera position y
  float cameraForward[4]; // w = camera position z
  float reflectionColor[4];   // zenith sky
  float shadeParams[4];       // x = write empty pixels, y = particle radius (m), z = time, w = glint exponent
  float fluidColor[4];        // scattered fluid colour
  float skyHorizon[4];        // horizon sky
  float rippleParams[4];      // slope, longest wavelength (m), speed, fade distance (m)
  float causticParams[4];     // strength, scale (m), speed, depth fade (m)
  float envParams[4];         // x = environment intensity (0 = analytic sky gradient)
  float poolMin[4];           // basin interior min (y = floor), w = water plane y
  float poolMax[4];           // basin interior max (y = coping top), w = deck y
  float lightParams[4];       // sun attenuation, caustic focus, contact line, backlight
  float scatterParams[4];     // sun in-scatter, sky in-scatter, HG g, aeration
  float foamParams[4];        // foam, foam cell (m), bubbles, secondary spray
  float wetParams[4];         // wetness origin x, z, texel (m), strength
  uint32_t gridParams[4];     // wetness texels x, y; surface columns x, z
  float rippleFlow[4];        // agitation gain, calm multiplier, Toksvig (0/1)
};
static_assert(sizeof(ParticleShadeConstants) == 400);

// Mirrors `cbuffer WhitewaterSplatConstants` in CS_FluidWhitewater.hlsl.
struct WhitewaterSplatConstants {
  uint32_t outputWidth;
  uint32_t outputHeight;
  uint32_t diffuseCapacity;
  uint32_t enabled;
  float projScaleX;
  float projScaleY;
  float projA;
  float projB;
  float sprayRadius;
  float foamRadius;
  float bubbleRadius;
  float waterAbsorption;
  float viewProjection[16];
};
static_assert(sizeof(WhitewaterSplatConstants) == 112);

// Mirrors `cbuffer ParticleSphereConstants` in CS_FluidSphereShade.hlsl.
struct ParticleSphereConstants {
  uint32_t outputWidth;
  uint32_t outputHeight;
  uint32_t particleCount;
  uint32_t style;         // 0 water, 3 lit spheres
  float radius;
  float opacity;
  float projScaleY;
  float ambient;
  float projA;            // Projection.m[2][2]
  float projB;            // Projection.m[3][2]
  float refraction;
  float specular;
  float fresnelPower;
  float thicknessScale;
  float invPackingFactor;
  float pad0;
  float sunDirection[4];  // view space, toward the light
  float sunColor[4];
  float skyColor[4];
  float worldUp[4];       // world +Y in view space
  float fluidTint[4];     // absorption per metre * scale
  float fluidColor[4];
  float color[4][4];      // ramp stops
};
static_assert(sizeof(ParticleSphereConstants) == 224);

uint32_t ConstantWordCount(const ComputeKernelDefinition& kernel) {
  for (size_t index = 0; index < kernel.bindings.size(); ++index) {
    if (kernel.bindings[index].type == ComputeBindingType::Constants32)
      return kernel.bindings[index].constantCount;
  }
  return 0;
}

void ConfigureArithmeticPipeline(ComputePipelineDesc& pipeline, std::string_view permutation) {
  if (permutation == "read-input")
    pipeline.bindings.push_back({ComputeBindingType::ReadOnlyBuffer, 0, 2, 0});
}

template <typename T>
void CopyWords(const T& value, std::vector<uint32_t>& words) {
  static_assert(sizeof(T) % sizeof(uint32_t) == 0);
  words.resize(sizeof(T) / sizeof(uint32_t));
  std::memcpy(words.data(), &value, sizeof(T));
}

bool BuildGodRaysConstants(const ComputeKernelConstantsContext& context,
                           std::vector<uint32_t>& constants, std::string& error) {
  SceneProps& props = *context.sceneProps;
  Camera* camera = props.GetPrimaryCamera();
  if (!camera) {
    error = "God rays constants require a primary camera";
    return false;
  }
  GodRaysComputeConstants value = {};
  camera->VP.Inverse(&value.WVPInverse);
  value.CameraPosition = camera->Eye;
  XVECTOR3 sunDirection(0.0f, 1.0f, 0.0f, props.ShadowBias);
  const bool hasLightCamera = props.ActiveLightCamera >= 0 &&
    props.ActiveLightCamera < static_cast<int>(props.pLightCameras.size()) &&
    props.pLightCameras[props.ActiveLightCamera];
  if (hasLightCamera) {
    value.WVPLight = props.pLightCameras[props.ActiveLightCamera]->VP;
    XVECTOR3 direction = -props.pLightCameras[props.ActiveLightCamera]->Look;
    direction.Normalize();
    sunDirection.x = direction.x;
    sunDirection.y = direction.y;
    sunDirection.z = direction.z;
  }
  value.SunDirectionAndBias = sunDirection;
  value.VolumeCenterAndEnabled = props.GodRaysVolumeCenter;
  value.VolumeCenterAndEnabled.w = static_cast<float>(props.GodRaysVolumeEnabled);
  value.VolumeHalfExtentsAndFactor = props.GodRaysVolumeHalfExtents;
  value.VolumeHalfExtentsAndFactor.w = props.GodRaysFactor;
  value.OutputSizeStepsAndEnabled = XVECTOR3(
    static_cast<float>(context.outputWidth), static_cast<float>(context.outputHeight),
    (std::max)(props.LightVolumeSteps, 2.0f),
    props.ToogleGodRays && hasLightCamera ? 1.0f : 0.0f);
  CopyWords(value, constants);
  return true;
}

bool BuildBlurConstants(const ComputeKernelConstantsContext& context,
                        std::vector<uint32_t>& constants, std::string& error) {
  SceneProps& props = *context.sceneProps;
  const bool validKernel = props.ActiveGaussKernel >= 0 &&
    props.ActiveGaussKernel < static_cast<int>(props.pGaussKernels.size()) &&
    props.pGaussKernels[props.ActiveGaussKernel] &&
    !props.pGaussKernels[props.ActiveGaussKernel]->vGaussKernel.empty();
  if (!validKernel) {
    error = "Blur constants require the active Gaussian kernel";
    return false;
  }
  const GaussFilter& gaussian = *props.pGaussKernels[props.ActiveGaussKernel];
  const uint32_t kernelSize = static_cast<uint32_t>(gaussian.vGaussKernel[0].x);
  if (!kernelSize || kernelSize > kMaxBlurWeights || gaussian.vGaussKernel.size() <= kernelSize) {
    error = "Blur Gaussian kernel exceeds the supported weight count";
    return false;
  }
  BlurComputeConstants value = {};
  value.outputWidth = context.outputWidth;
  value.outputHeight = context.outputHeight;
  value.kernelSize = kernelSize;
  value.direction = context.permutation == "vertical" ? 1u : 0u;
  value.radius = gaussian.vGaussKernel[0].y;
  for (uint32_t index = 0; index < kernelSize; ++index) {
    const float weight = gaussian.vGaussKernel[index + 1].x;
    value.weights[index] = std::round(weight * 1000000.0f) / 1000000.0f;
  }
  CopyWords(value, constants);
  return true;
}

bool BuildPostProcessConstants(const ComputeKernelConstantsContext& context,
                               bool composite, std::vector<uint32_t>& constants) {
  SceneProps& props = *context.sceneProps;
  PostProcessComputeConstants value = {};
  value.outputWidth = static_cast<float>(context.outputWidth);
  value.outputHeight = static_cast<float>(context.outputHeight);
  value.parameter0 = composite ? props.BloomFactor : props.BloomThreshold;
  value.parameter1 = props.Exposure;
  value.parameter2 = props.ToneMapWhiteLevel;
  CopyWords(value, constants);
  return true;
}

bool BuildBrightConstants(const ComputeKernelConstantsContext& context,
                          std::vector<uint32_t>& constants, std::string&) {
  return BuildPostProcessConstants(context, false, constants);
}

bool BuildHDRCompositeConstants(const ComputeKernelConstantsContext& context,
                                std::vector<uint32_t>& constants, std::string&) {
  return BuildPostProcessConstants(context, true, constants);
}

bool BuildTorchParticleConstants(const ComputeKernelConstantsContext& context,
                                 std::vector<uint32_t>& constants, std::string& error) {
  SceneProps& props = *context.sceneProps;
  Camera* camera = props.GetPrimaryCamera();
  if (!camera) {
    error = "Torch particle constants require a primary camera";
    return false;
  }
  ParticleComputeConstants value = {};
  value.viewProjection = camera->VP;
  value.emitterAndTime = props.ParticleEmitterPosition;
  value.emitterAndTime.w = props.ParticleTimeSeconds;
  value.additionalEmitterXZ = XVECTOR3(
    props.ParticleEmitterPosition1.x, props.ParticleEmitterPosition1.z,
    props.ParticleEmitterPosition2.x, props.ParticleEmitterPosition2.z);
  value.outputSizeCountEnabled = XVECTOR3(
    static_cast<float>(context.outputWidth), static_cast<float>(context.outputHeight),
    static_cast<float>(props.ParticleCount),
    static_cast<float>((std::max)(0, (std::min)(3, props.ParticleEmitterEnabled))));
  value.motion = XVECTOR3(props.ParticleLifetime, props.ParticleRiseHeight,
                          props.ParticleSpread, props.ParticleSize);
  value.color0 = props.ParticleColor0;
  value.color1 = props.ParticleColor1;
  value.color2 = props.ParticleColor2;
  value.shape = props.ParticleShape;
  value.wobble = props.ParticleWobble;
  value.fade = props.ParticleFade;
  value.intensity = XVECTOR3(props.ParticleFadeOutStart, props.ParticleIntensity, 0.0f, 0.0f);
  CopyWords(value, constants);
  return true;
}

bool ResolveBindingType(std::string_view value, ComputeBindingType& type) {
  if (value == "constants") type = ComputeBindingType::Constants32;
  else if (value == "read_only_buffer") type = ComputeBindingType::ReadOnlyBuffer;
  else if (value == "read_write_buffer") type = ComputeBindingType::ReadWriteBuffer;
  else if (value == "sampled_texture") type = ComputeBindingType::ReadOnlyTexture;
  else if (value == "storage_texture") type = ComputeBindingType::ReadWriteTexture;
  else if (value == "sampler") type = ComputeBindingType::Sampler;
  else return false;
  return true;
}

bool ResolveStorageFormat(std::string_view value, ComputeStorageFormat& format) {
  if (value == "unspecified") format = ComputeStorageFormat::Unspecified;
  else if (value == "rgba8unorm") format = ComputeStorageFormat::Rgba8Unorm;
  else if (value == "rgba16float") format = ComputeStorageFormat::Rgba16Float;
  else if (value == "r32float") format = ComputeStorageFormat::R32Float;
  else return false;
  return true;
}

ComputePipelineConfigurator ResolvePipelineConfigurator(std::string_view value) {
  if (value.empty()) return nullptr;
  return value == "arithmetic" ? ConfigureArithmeticPipeline : nullptr;
}

bool BuildFluidParticleConstants(const ComputeKernelConstantsContext& context,
                                 std::vector<uint32_t>& constants, std::string& error) {
  SceneProps& props = *context.sceneProps;
  Camera* camera = props.GetPrimaryCamera();
  if (!camera) {
    error = "Fluid particle constants require a primary camera";
    return false;
  }
  FluidParticleComputeConstants value = {};
  // Particle count and radius come from the scene's fluid particle settings.
  value.particleCount = static_cast<uint32_t>((std::max)(0, props.FluidParticleCount));
  value.outputWidth = context.outputWidth;
  value.outputHeight = context.outputHeight;
  value.iteration = 0;
  value.particleRadius = props.FluidParticleRadius;
  value.depthBias = props.FluidParticleDepthBias;
  value.opacity = props.FluidParticleOpacity;
  value.cameraPosition[0] = camera->Eye.x;
  value.cameraPosition[1] = camera->Eye.y;
  value.cameraPosition[2] = camera->Eye.z;
  value.fluidColor[0] = props.FluidParticleColor.x;
  value.fluidColor[1] = props.FluidParticleColor.y;
  value.fluidColor[2] = props.FluidParticleColor.z;
  // 8x8 pixel tiles, matching [numthreads(8,8,1)] in the stamp pass.
  value.tileCountX = static_cast<float>((context.outputWidth + 7u) / 8u);
  CopyWords(value, constants);
  return true;
}

bool BuildParticleSplatConstants(const ComputeKernelConstantsContext& context,
                                 std::vector<uint32_t>& constants, std::string& error) {
  SceneProps& props = *context.sceneProps;
  Camera* camera = props.GetPrimaryCamera();
  if (!camera) {
    error = "particle splat constants require a primary camera";
    return false;
  }
  // Everything about the system comes from the ParticleSystem descriptor, so one kernel
  // renders fire, smoke, sparks or fluid. The scene no longer needs loose particle globals.
  const ParticleSystemDesc* system = context.particleSystem.empty()
    ? nullptr : ParticleSystemRegistry::Instance().Find(std::string(context.particleSystem));
  if (!system) {
    error = "particle splat constants require a named particle system";
    return false;
  }
  ParticleSplatConstants value = {};
  value.outputWidth  = context.outputWidth;
  value.outputHeight = context.outputHeight;
  value.particleCount = static_cast<uint32_t>((std::max)(0, system->source == ParticleSourceKind::Buffer
      ? (system->buffer_count > 0 ? system->buffer_count : 0)
      : system->count));
  // The splat is two passes in one kernel: stamp projects each particle and marks its
  // 8x8 tile; resolve walks the marked tiles and writes colour. A compute dispatch has no
  // grid-wide barrier, so they cannot be one dispatch -- the graph runs the pass twice with
  // these two permutations.
  value.iteration    = context.permutation == "resolve" ? 1u : 0u;
  value.radius       = system->radius;
  value.opacity      = system->opacity;
  value.intensity    = system->intensity;
  value.softness     = system->softness;
  value.depthBias    = system->depth_bias;
  value.lifetime     = system->lifetime;
  value.spread       = system->spread;
  value.rise         = system->rise;
  value.timeSeconds  = ParticleSystemRegistry::Instance().Time();
  value.sizeScaleMin = system->size_scale_min;
  value.sizeScaleMax = system->size_scale_max;
  value.shape  = static_cast<uint32_t>(system->shape);
  value.blend  = static_cast<uint32_t>(system->blend);
  value.source = static_cast<uint32_t>(system->source);
  // Pad2 carries the particle look style (0 shaded, 1 wireframe, 2 flat). Keeping it in
  // the existing padding slot preserves the 60-DWORD splat cbuffer.
  value.pad2 = static_cast<float>(system->style);
  // SpeedTint is unused by the splat: reuse it as the screen-space flag so the splat
  // resolve and the depth pass can stand down when the other renderer is active.
  value.speedTint = system->render_mode == ParticleRenderMode::ScreenSpace ? 1.0f : 0.0f;
  value.depthRadiusScale = system->screen_space.depth_radius_scale;
  value.invPackingFactor = 1.0f / (std::max)(system->screen_space.packing_factor, 1.0f);
  for (int c = 0; c < 4; ++c) {
    value.color[c][0] = system->ramp.stops[c][0];
    value.color[c][1] = system->ramp.stops[c][1];
    value.color[c][2] = system->ramp.stops[c][2];
  }
  value.tileCountX = static_cast<float>((context.outputWidth + 7u) / 8u);
  value.tileCountY = static_cast<float>((context.outputHeight + 7u) / 8u);
  value.frameIndex = context.frameIndex;
  // P11 = cot(fov/2). Needed for the pixel radius: R * P11 * (H/2) / w.
  value.projScaleY = camera->Projection.m[1][1];
  // Scene depth must be linearised with the PROJECTION terms, not the view-projection
  // rows: VP[2].z includes the view matrix's z row, which introduced a 2.7% depth error
  // that grew with the camera's distance from the world origin.
  value.projA = camera->Projection.m[2][2];
  value.projB = camera->Projection.m[3][2];
  value.emitterPosition[0] = props.ParticleEmitterPosition.x;
  value.emitterPosition[1] = props.ParticleEmitterPosition.y;
  value.emitterPosition[2] = props.ParticleEmitterPosition.z;
  // Padding slots read by CS_FluidSplat: P00 (ProjScaleX) after Color0, and the anisotropy
  // flag after EmitterPosition. The other particle kernels keep them as padding.
  value.color[0][3] = camera->Projection.m[0][0];
  value.emitterPosition[3] =
      system->screen_space.anisotropy && !system->anisotropy_buffer.empty() ? 1.0f : 0.0f;
  // AuxEnabled (after Color1): accumulate the solver's whitewater for the screen-space shade.
  value.color[1][3] = system->render_mode == ParticleRenderMode::ScreenSpace &&
                      !system->whitewater_buffer.empty() ? 1.0f : 0.0f;
  // Row-major view-projection; the kernel multiplies rows against float4(p, 1).
  std::memcpy(value.viewProjection, &camera->VP, sizeof(value.viewProjection));
  CopyWords(value, constants);
  return true;
}

bool BuildParticleSmoothConstants(const ComputeKernelConstantsContext& context,
                                  std::vector<uint32_t>& constants, std::string& error) {
  const ParticleSystemDesc* system = context.particleSystem.empty()
    ? nullptr : ParticleSystemRegistry::Instance().Find(std::string(context.particleSystem));
  if (!system) {
    error = "particle smooth constants require a named particle system";
    return false;
  }
  SceneProps& props = *context.sceneProps;
  Camera* camera = props.GetPrimaryCamera();
  if (!camera) {
    error = "particle smooth constants require a primary camera";
    return false;
  }
  ParticleSmoothConstants value = {};
  value.outputWidth = context.outputWidth;
  value.outputHeight = context.outputHeight;
  const int iterations = (std::min)((std::max)(system->screen_space.smoothing_iterations, 0), 8);
  // Passes beyond the configured iteration count copy the field unchanged. The graph
  // always contains eight ping-pong passes, so the final depth is always in the same target
  // regardless of the runtime iteration count.
  const bool active = context.passStage >= 1 && context.passStage <= iterations &&
                      system->render_mode == ParticleRenderMode::ScreenSpace;
  // Dyadic a-trous spacing: 1, 2, 4, ... pixels; the shader stops each pixel at its radius. The
  // last pass goes back to spacing 1 to remove the grid texture the wide passes leave behind.
  const bool cleanup = context.passStage == iterations && iterations > 1;
  value.spacing = active ? (cleanup ? 1.0f : static_cast<float>(1u << (context.passStage - 1))) : 0.0f;
  value.strength = active ? system->screen_space.smoothing_strength : 0.0f;
  value.worldRadius = system->screen_space.smoothing_world_radius;
  value.tolerance = system->screen_space.depth_tolerance;
  value.radiusPx = system->screen_space.smoothing_radius;
  value.projScaleY = camera->Projection.m[1][1];
  CopyWords(value, constants);
  return true;
}

// The sun as the fluid shaders see it: the first enabled directional light (or the shadow
// camera), transformed to view space and pointing TOWARD the light.
void SunInView(const SceneProps& props, const Camera& camera, float direction[3], float color[3]) {
  XVECTOR3 sunWorld(0.35f, -0.55f, 0.78f);
  XVECTOR3 sunColor(1.0f, 1.0f, 1.0f);
  bool found = false;
  for (const Light& light : props.Lights) {
    if (!light.Enabled || light.Type != LIGHT_DIRECTIONAL) continue;
    sunWorld = light.Direction;
    sunColor = light.Color * light.Intensity;
    found = true;
    break;
  }
  if (!found) {
    // Fall back to the shadow/light camera, exactly as the god-rays builder does.
    const bool hasLightCamera = props.ActiveLightCamera >= 0 &&
      props.ActiveLightCamera < static_cast<int>(props.pLightCameras.size()) &&
      props.pLightCameras[props.ActiveLightCamera];
    if (hasLightCamera) {
      const XVECTOR3 look = props.pLightCameras[props.ActiveLightCamera]->Look;
      sunWorld = XVECTOR3(-look.x, -look.y, -look.z);
    }
  }
  // Row-vector convention: v' = v * View.
  const XMATRIX44& view = camera.View;
  const float sx = sunWorld.x * view.m[0][0] + sunWorld.y * view.m[1][0] + sunWorld.z * view.m[2][0];
  const float sy = sunWorld.x * view.m[0][1] + sunWorld.y * view.m[1][1] + sunWorld.z * view.m[2][1];
  const float sz = sunWorld.x * view.m[0][2] + sunWorld.y * view.m[1][2] + sunWorld.z * view.m[2][2];
  const float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
  const float inv = sl > 1e-6f ? 1.0f / sl : 1.0f;
  direction[0] = -sx * inv;
  direction[1] = -sy * inv;
  direction[2] = -sz * inv;
  color[0] = sunColor.x;
  color[1] = sunColor.y;
  color[2] = sunColor.z;
}

bool BuildParticleShadeConstants(const ComputeKernelConstantsContext& context,
                                 std::vector<uint32_t>& constants, std::string& error) {
  SceneProps& props = *context.sceneProps;
  Camera* camera = props.GetPrimaryCamera();
  if (!camera) {
    error = "particle shade constants require a primary camera";
    return false;
  }
  const ParticleSystemDesc* system = context.particleSystem.empty()
    ? nullptr : ParticleSystemRegistry::Instance().Find(std::string(context.particleSystem));
  if (!system) {
    error = "particle shade constants require a named particle system";
    return false;
  }
  ParticleShadeConstants value = {};
  value.outputWidth = context.outputWidth;
  value.outputHeight = context.outputHeight;
  value.mode = system->render_mode == ParticleRenderMode::ScreenSpace ? 1u : 0u;
  value.debugView = static_cast<uint32_t>((std::max)(system->screen_space.debug_view, 0));
  value.projScaleY = camera->Projection.m[1][1];
  value.projA = camera->Projection.m[2][2];
  value.projB = camera->Projection.m[3][2];
  // Longest refracted path: beyond this the single-offset lookup stops resembling the
  // refracted image.
  value.maxTravel = 3.0f;
  value.thicknessScale = system->screen_space.thickness_scale;
  value.refraction = system->screen_space.refraction_strength;
  value.specular = system->screen_space.specular;
  value.fresnelPower = system->screen_space.fresnel_power;
  value.normalScale = system->screen_space.normal_scale;
  value.normalStep = system->screen_space.normal_step;
  value.foam = system->screen_space.foam;
  value.reflectionDistance = system->screen_space.reflection_distance;
  for (int c = 0; c < 3; ++c) {
    value.fluidTint[c] = system->screen_space.absorption[c] *
                         (std::max)(system->screen_space.absorption_scale, 0.0f);
    value.reflectionColor[c] = system->screen_space.reflection[c];
    value.fluidColor[c] = system->screen_space.fluid_color[c];
    value.skyHorizon[c] = system->screen_space.sky_horizon[c];
  }
  // If the splat resolve is disabled the shade must write every pixel so the composite never
  // samples an unwritten target. With the fallback on, empty pixels are left untouched.
  value.shadeParams[0] = system->screen_space.splat_fallback ? 0.0f : 1.0f;
  value.shadeParams[1] = system->radius;
  value.shadeParams[2] = ParticleSystemRegistry::Instance().Time();
  value.shadeParams[3] = system->screen_space.glint_exponent;
  value.rippleParams[0] = system->screen_space.ripple_slope;
  value.rippleParams[1] = system->screen_space.ripple_wavelength;
  value.rippleParams[2] = system->screen_space.ripple_speed;
  value.rippleParams[3] = system->screen_space.ripple_distance;
  value.causticParams[0] = system->screen_space.caustics;
  value.causticParams[1] = system->screen_space.caustic_scale;
  value.causticParams[2] = system->screen_space.caustic_speed;
  value.causticParams[3] = system->screen_space.caustic_depth;

  SunInView(props, *camera, value.sunDirection, value.sunColor);
  // Camera basis in world space (view axes) plus the eye in w: the shader goes between view
  // and world space for the sky gradient, the ripples and the caustics.
  value.cameraRight[0] = camera->Right.x;
  value.cameraRight[1] = camera->Right.y;
  value.cameraRight[2] = camera->Right.z;
  value.cameraRight[3] = camera->Eye.x;
  value.cameraUp[0] = camera->Up.x;
  value.cameraUp[1] = camera->Up.y;
  value.cameraUp[2] = camera->Up.z;
  value.cameraUp[3] = camera->Eye.y;
  value.cameraForward[0] = camera->Look.x;
  value.cameraForward[1] = camera->Look.y;
  value.cameraForward[2] = camera->Look.z;
  value.cameraForward[3] = camera->Eye.z;

  const ParticleScreenSpaceDesc& ss = system->screen_space;
  const ParticleFluidGeometry& geometry = system->geometry;
  value.envParams[0] = ss.env_intensity * props.IBLFactor;
  for (int c = 0; c < 3; ++c) {
    value.poolMin[c] = geometry.pool_min[c];
    value.poolMax[c] = geometry.pool_max[c];
  }
  value.poolMin[3] = geometry.water_plane_y;
  value.poolMax[3] = geometry.deck_y;
  value.lightParams[0] = (std::min)((std::max)(ss.sun_attenuation, 0.0f), 1.0f);
  value.lightParams[1] = ss.caustic_focus;
  value.lightParams[2] = ss.contact_line;
  value.lightParams[3] = ss.backlight;
  value.scatterParams[0] = ss.scatter_sun;
  value.scatterParams[1] = ss.scatter_sky;
  value.scatterParams[2] = ss.scatter_anisotropy;
  value.scatterParams[3] = ss.aeration;
  value.foamParams[0] = ss.foam_strength;
  value.foamParams[1] = ss.foam_texture_scale;
  value.foamParams[2] = ss.bubble_strength;
  value.foamParams[3] = ss.spray_strength;
  value.wetParams[0] = geometry.wet_origin_x;
  value.wetParams[1] = geometry.wet_origin_z;
  value.wetParams[2] = geometry.wet_texel;
  value.wetParams[3] = ss.wetness;
  value.gridParams[0] = static_cast<uint32_t>((std::max)(geometry.wet_width, 0));
  value.gridParams[1] = static_cast<uint32_t>((std::max)(geometry.wet_height, 0));
  value.gridParams[2] = static_cast<uint32_t>((std::max)(geometry.surface_width, 0));
  value.gridParams[3] = static_cast<uint32_t>((std::max)(geometry.surface_depth, 0));
  value.rippleFlow[0] = ss.ripple_agitation;
  value.rippleFlow[1] = ss.ripple_calm;
  value.rippleFlow[2] = 1.0f;
  CopyWords(value, constants);
  return true;
}

bool BuildWhitewaterSplatConstants(const ComputeKernelConstantsContext& context,
                                   std::vector<uint32_t>& constants, std::string& error) {
  SceneProps& props = *context.sceneProps;
  Camera* camera = props.GetPrimaryCamera();
  if (!camera) {
    error = "whitewater splat constants require a primary camera";
    return false;
  }
  const ParticleSystemDesc* system = context.particleSystem.empty()
    ? nullptr : ParticleSystemRegistry::Instance().Find(std::string(context.particleSystem));
  if (!system) {
    error = "whitewater splat constants require a named particle system";
    return false;
  }
  const ParticleScreenSpaceDesc& ss = system->screen_space;
  WhitewaterSplatConstants value = {};
  value.outputWidth = context.outputWidth;
  value.outputHeight = context.outputHeight;
  value.diffuseCapacity = static_cast<uint32_t>((std::max)(system->geometry.diffuse_capacity, 0));
  value.enabled = system->render_mode == ParticleRenderMode::ScreenSpace && !system->diffuse_buffer.empty() &&
                  (ss.spray_strength > 0.0f || ss.foam_strength > 0.0f || ss.bubble_strength > 0.0f) ? 1u : 0u;
  value.projScaleX = camera->Projection.m[0][0];
  value.projScaleY = camera->Projection.m[1][1];
  value.projA = camera->Projection.m[2][2];
  value.projB = camera->Projection.m[3][2];
  value.sprayRadius = system->geometry.spray_radius;
  value.foamRadius = system->geometry.foam_radius;
  value.bubbleRadius = system->geometry.bubble_radius;
  value.waterAbsorption = ss.absorption[1] * (std::max)(ss.absorption_scale, 0.0f);
  std::memcpy(value.viewProjection, &camera->VP, sizeof(value.viewProjection));
  CopyWords(value, constants);
  return true;
}

bool BuildParticleSphereConstants(const ComputeKernelConstantsContext& context,
                                  std::vector<uint32_t>& constants, std::string& error) {
  SceneProps& props = *context.sceneProps;
  Camera* camera = props.GetPrimaryCamera();
  if (!camera) {
    error = "particle sphere constants require a primary camera";
    return false;
  }
  const ParticleSystemDesc* system = context.particleSystem.empty()
    ? nullptr : ParticleSystemRegistry::Instance().Find(std::string(context.particleSystem));
  if (!system) {
    error = "particle sphere constants require a named particle system";
    return false;
  }
  ParticleSphereConstants value = {};
  value.outputWidth = context.outputWidth;
  value.outputHeight = context.outputHeight;
  value.particleCount = static_cast<uint32_t>((std::max)(0, system->source == ParticleSourceKind::Buffer
      ? system->buffer_count : system->count));
  value.radius = system->radius;
  value.opacity = system->opacity;
  value.projScaleY = camera->Projection.m[1][1];
  value.ambient = 0.6f;
  // 0 water, 1 wireframe, 2 flat, 3 spheres: all four are handled by CS_FluidSphereShade.
  value.style = static_cast<uint32_t>(system->style);
  // The water style shares the screen-space renderer's optics: both draw the same water.
  const ParticleScreenSpaceDesc& optics = system->screen_space;
  value.projA = camera->Projection.m[2][2];
  value.projB = camera->Projection.m[3][2];
  value.refraction = optics.refraction;
  value.specular = optics.specular;
  value.fresnelPower = optics.fresnel_power;
  value.thicknessScale = optics.thickness_scale;
  value.invPackingFactor = 1.0f / (std::max)(optics.packing_factor, 1.0f);
  for (int c = 0; c < 3; ++c) {
    value.fluidTint[c] = optics.absorption[c] * (std::max)(optics.absorption_scale, 0.0f);
    value.fluidColor[c] = optics.fluid_color[c];
  }
  SunInView(props, *camera, value.sunDirection, value.sunColor);
  for (int c = 0; c < 3; ++c) value.skyColor[c] = system->screen_space.reflection[c];
  value.worldUp[0] = camera->Right.y;
  value.worldUp[1] = camera->Up.y;
  value.worldUp[2] = camera->Look.y;
  for (int stop = 0; stop < 4; ++stop) {
    for (int c = 0; c < 3; ++c) value.color[stop][c] = system->ramp.stops[stop][c];
  }
  CopyWords(value, constants);
  return true;
}

ComputeConstantsBuilder ResolveConstantsBuilder(std::string_view value) {
  if (value.empty()) return nullptr;
  if (value == "god_rays") return BuildGodRaysConstants;
  if (value == "blur") return BuildBlurConstants;
  if (value == "bright") return BuildBrightConstants;
  if (value == "hdr_composite") return BuildHDRCompositeConstants;
  if (value == "torch_particles") return BuildTorchParticleConstants;
  if (value == "fluid_particles") return BuildFluidParticleConstants;
  if (value == "particle_splat") return BuildParticleSplatConstants;
  if (value == "particle_smooth") return BuildParticleSmoothConstants;
  if (value == "particle_shade") return BuildParticleShadeConstants;
  if (value == "particle_whitewater") return BuildWhitewaterSplatConstants;
  if (value == "particle_sphere") return BuildParticleSphereConstants;
  return nullptr;
}

const std::vector<ComputeKernelDefinition>& Kernels() {
  static const std::vector<ComputeKernelDefinition> kernels = [] {
    std::vector<ComputeKernelDefinition> result;
    std::string json;
    if (!ResourceLocator::Instance().ReadText("Shaders/compute_kernels.json", json)) {
      T8_LOG_ERROR("[ComputeKernelRegistry] Cannot read Shaders/compute_kernels.json");
      return result;
    }
    ComputeKernelManifest manifest;
    const auto error = glz::read<glz::opts{.error_on_unknown_keys = true}>(manifest, json);
    if (error || manifest.version != 1 || manifest.kernels.empty()) {
      T8_LOG_ERROR("[ComputeKernelRegistry] Invalid manifest: %s",
        error ? glz::format_error(error, json).c_str() : "unsupported version or empty kernel list");
      return result;
    }
    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> sources;
    result.reserve(manifest.kernels.size());
    for (const ComputeKernelManifestEntry& entry : manifest.kernels) {
      ComputeKernelDefinition kernel;
      if (entry.id.empty() || entry.source.empty() || entry.entry.empty() ||
          entry.permutations.empty() || entry.bindings.empty() ||
          !ids.insert(entry.id).second || !sources.insert(entry.source).second) {
        T8_LOG_ERROR("[ComputeKernelRegistry] Invalid or duplicate kernel '%s'", entry.source.c_str());
        return std::vector<ComputeKernelDefinition>{};
      }
      kernel.id = entry.id;
      kernel.sourceName = entry.source;
      kernel.entryPoint = entry.entry;
      kernel.permutations = entry.permutations;
      kernel.configurePipeline = ResolvePipelineConfigurator(entry.pipeline_configurator);
      kernel.buildConstants = ResolveConstantsBuilder(entry.constants_builder);
      if ((!entry.pipeline_configurator.empty() && !kernel.configurePipeline) ||
          (!entry.constants_builder.empty() && !kernel.buildConstants)) {
        T8_LOG_ERROR("[ComputeKernelRegistry] Kernel '%s' names an unknown callback", entry.source.c_str());
        return std::vector<ComputeKernelDefinition>{};
      }
      std::unordered_set<uint64_t> logicalBindings;
      std::unordered_set<uint32_t> nativeBindings;
      for (const ComputeBindingManifest& binding : entry.bindings) {
        ComputeBindingLayoutDesc layout;
        if (binding.shader_register < 0 || binding.binding_index < 0 || binding.constant_count < 0 ||
            !ResolveBindingType(binding.type, layout.type) ||
            !ResolveStorageFormat(binding.storage_format, layout.storageFormat)) {
          T8_LOG_ERROR("[ComputeKernelRegistry] Kernel '%s' has an invalid binding", entry.source.c_str());
          return std::vector<ComputeKernelDefinition>{};
        }
        if (binding.dimension == "3d") layout.dimension = ComputeTextureDimension::Tex3D;
        else if (binding.dimension == "cube" && layout.type == ComputeBindingType::ReadOnlyTexture)
          layout.dimension = ComputeTextureDimension::Cube;
        else if (binding.dimension != "2d") {
          T8_LOG_ERROR("[ComputeKernelRegistry] Kernel '%s' has an invalid binding dimension",
                       entry.source.c_str());
          return std::vector<ComputeKernelDefinition>{};
        }
        layout.shaderRegister = static_cast<uint32_t>(binding.shader_register);
        layout.bindingIndex = static_cast<uint32_t>(binding.binding_index);
        layout.constantCount = static_cast<uint32_t>(binding.constant_count);
        layout.matchOutputExtent = binding.match_output_extent;
        const uint64_t logical = (static_cast<uint64_t>(layout.type) << 32u) | layout.shaderRegister;
        if (!logicalBindings.insert(logical).second || !nativeBindings.insert(layout.bindingIndex).second) {
          T8_LOG_ERROR("[ComputeKernelRegistry] Kernel '%s' has duplicate bindings", entry.source.c_str());
          return std::vector<ComputeKernelDefinition>{};
        }
        kernel.bindings.push_back(layout);
      }
      // D3D12 root-signature budget, visible on every backend so a 4K-only or D3D-only
      // failure is not discovered on device. Root constants cost 1 DWORD per value, root
      // SRV/UAV descriptors 2, descriptor tables 1. The D3D12 backend moves the cbuffer
      // to a root CBV when this exceeds 64.
      uint32_t d3d12RootCost = 0;
      for (const ComputeBindingLayoutDesc& layout : kernel.bindings) {
        switch (layout.type) {
          case ComputeBindingType::Constants32:   d3d12RootCost += layout.constantCount; break;
          case ComputeBindingType::ReadOnlyBuffer:
          case ComputeBindingType::ReadWriteBuffer: d3d12RootCost += 2u; break;
          default:                                 d3d12RootCost += 1u; break;
        }
      }
      if (d3d12RootCost > 64u) {
        T8_LOG_INFO("[ComputeKernelRegistry] Kernel '%s' needs %u D3D12 root DWORDs (>64); "
                    "its cbuffer is bound as a root CBV", kernel.sourceName.c_str(), d3d12RootCost);
      }
      result.push_back(std::move(kernel));
    }
    T8_LOG_INFO("[ComputeKernelRegistry] Loaded %zu kernels from manifest", result.size());
    return result;
  }();
  return kernels;
}

std::string_view FileName(std::string_view path) {
  const size_t separator = path.find_last_of("/\\");
  return separator == std::string_view::npos ? path : path.substr(separator + 1);
}

} // namespace

const ComputeKernelDefinition* FindComputeKernel(std::string_view sourceName) {
  const std::string_view fileName = FileName(sourceName);
  for (const ComputeKernelDefinition& kernel : Kernels()) {
    if (kernel.sourceName == fileName)
      return &kernel;
  }
  return nullptr;
}

bool SupportsComputePermutation(const ComputeKernelDefinition& kernel,
                                std::string_view permutation) {
  return std::find(kernel.permutations.begin(), kernel.permutations.end(), permutation) !=
         kernel.permutations.end();
}

void ConfigureComputePipelineDesc(const ComputeKernelDefinition& kernel,
                                  ComputePipelineDesc& pipeline) {
  pipeline.bindings = kernel.bindings;
  if (kernel.configurePipeline)
    kernel.configurePipeline(pipeline, pipeline.permutationName);
}

bool BuildComputeConstants(const ComputeKernelDefinition& kernel,
                           const ComputeKernelConstantsContext& context,
                           std::vector<uint32_t>& constants,
                           std::string& error) {
  constants.clear();
  if (!context.sceneProps || !context.outputWidth || !context.outputHeight) {
    error = "compute constants require scene properties and a nonzero output extent";
    return false;
  }
  if (!SupportsComputePermutation(kernel, context.permutation)) {
    error = "unsupported compute permutation '" + std::string(context.permutation) + "'";
    return false;
  }

  if (!kernel.buildConstants) {
    error = "kernel does not expose scene-driven constants";
    return false;
  }
  if (!kernel.buildConstants(context, constants, error)) return false;

  if (constants.size() != ConstantWordCount(kernel)) {
    error = "packed constant count does not match the registered shader layout";
    constants.clear();
    return false;
  }
  return true;
}

} // namespace t850