// T850 screen-space fluid shading.
//
// Inputs (the fluid targets may be at a lower resolution than the output; they are upsampled
// here with a depth-aware bilinear filter and re-tested against the full-resolution scene):
//   FluidDepthA     F32      view-space front surface, smoothed (0 = empty)
//   FluidThickness  RGBA16F  r = metres of water along the ray (chord sum), a = coverage
//   AuxBits         per fluid pixel: foam (max), aeration, agitation, spray (chord-weighted)
//   WhitewaterBits  per output pixel: secondary spray, foam, bubbles, sparkle; read AND
//                   cleared here (CS_FluidWhitewater accumulates them each frame)
//   SceneColor / SceneDepth / G-buffer albedo, normal / ShadowAccum: the opaque scene
//   EnvironmentMap  the scene's sky cube, for reflected rays the screen cannot resolve
//   CausticField    focusing of the simulated surface per basin column (CS_PbfFields)
//   Wetness         world-space wetness of the deck and coping (CS_PbfWhitewater/Diffuse)
//
// Optics per water pixel (van der Laan et al. 2009, Eq. 13, with physically scaled terms):
//   normal       from the smoothed depth, plus world-space wind ripples on up-facing water whose
//                slope follows the local flow; the slope variance of ripples too fine for the
//                pixel widens the sun glint and the reflection (Toksvig), instead of vanishing.
//   refraction   Snell (1/1.33). The refracted ray is MARCHED through the depth buffer to the
//                floor, wall or step it meets. When that surface is hidden (under the coping lip)
//                the analytic pool box gives the path and the last visible sample the colour.
//                Water in the air (a sheet over the pool) sees the pool surface behind it,
//                which is shaded here as a second interface.
//   light below  the G-buffer rebuilds the direct sun on the submerged surface (albedo, N.L,
//                ShadowAccum); that part is absorbed along the refracted sun path and carries
//                the caustics (procedural web x simulated-surface focusing). Ambient is
//                absorbed too. Shadowed or sun-averted surfaces get no caustics.
//   in-scatter   sun (Henyey-Greenstein phase, surface shadow) and sky light scattered toward
//                the eye, filling what Beer-Lambert absorbs; entrained air turns it milky.
//   reflection   screen-space trace (hits on surfaces facing away from the ray are rejected),
//                continued from the exit pixel, else the environment cube.
//   Fresnel, normalised Blinn-Phong glint, backlit crests, meniscus at walls, foam lace,
//   spray and bubbles.
// Opaque pixels: wet deck/coping (darker, glossy) and secondary spray in the air.
//
// The shaded colour already contains the background, so alpha is only coverage.
// `Mode == 0` leaves the splat target untouched. `ShadeParams.x == 1` writes zero on empty pixels.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer ParticleShadeConstants : register(b0)
{
    uint  OutputWidth;
    uint  OutputHeight;
    uint  Mode;               // 0 splat, 1 screen space
    uint  DebugView;          // see ParticleScreenSpaceDesc::debug_view

    float ProjScaleY;         // Projection.m[1][1]
    float ProjA;              // Projection.m[2][2], scene depth linearisation
    float ProjB;              // Projection.m[3][2]
    float MaxTravel;          // longest refracted path, metres

    float ThicknessScale;
    float Refraction;         // 0..1 strength of the refracted offset (1 = Snell)
    float Specular;
    float FresnelPower;

    float NormalScale;
    float Foam;               // whitewater strength for isolated fluid particles (spray)
    float ReflectionDistance; // screen-space reflection range in metres, 0 = off
    float NormalStep;

    float4 FluidTint;         // per-metre absorption * absorption scale
    float4 SunDirection;      // view space, toward the light
    float4 SunColor;          // rgb * intensity

    float4 CameraRight;       // camera basis in world space; w = camera position x
    float4 CameraUp;          // w = camera position y
    float4 CameraForward;     // w = camera position z
    float4 ReflectionColor;   // zenith of the fallback sky gradient

    float4 ShadeParams;       // x = write empty pixels, y = particle radius (m), z = time (s),
                              // w = sun glint exponent
    float4 FluidColor;        // single-scattering colour of the water
    float4 SkyHorizon;        // horizon of the fallback sky gradient
    float4 RippleParams;      // x = slope, y = longest wavelength (m), z = speed, w = fade distance (m)
    float4 CausticParams;     // x = strength, y = pattern scale (m), z = speed, w = depth fade (m)

    float4 EnvParams;         // x = environment intensity (0 = gradient)
    float4 PoolMin;           // basin interior min (y = floor), w = water plane y
    float4 PoolMax;           // basin interior max (y = coping top), w = deck y
    float4 LightParams;       // x = sun attenuation, y = caustic focus, z = contact line, w = backlight
    float4 ScatterParams;     // x = sun in-scatter, y = sky in-scatter, z = HG g, w = aeration
    float4 FoamParams;        // x = foam, y = foam cell (m), z = bubbles, w = secondary spray
    float4 WetParams;         // x, y = wetness origin (world x, z), z = texel (m), w = strength
    uint4  GridParams;        // x, y = wetness texels, z, w = surface columns
    float4 RippleFlow;        // x = agitation gain, y = calm multiplier, z = Toksvig (0/1)
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture2D<float> SurfaceDepth : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
Texture2D<float4> FluidThickness : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
Texture2D<float4> SceneColor : register(t2);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
SamplerState LinearSampler : register(s2);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> OutputTexture : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(6, 0)]]
#endif
Texture2D<float> SceneDepth : register(t3);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(7, 0)]]
#endif
TextureCube<float4> EnvironmentMap : register(t4);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(8, 0)]]
#endif
Texture2D<float4> SceneAlbedo : register(t5);    // G-buffer COLOR0: albedo, specular weight

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(9, 0)]]
#endif
Texture2D<float4> SceneNormal : register(t6);    // G-buffer COLOR1: world normal * 0.5 + 0.5

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(10, 0)]]
#endif
Texture2D<float4> SceneShadow : register(t7);    // ShadowAccum: r = sun visibility

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(11, 0)]]
#endif
StructuredBuffer<uint> AuxBits : register(t8);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(12, 0)]]
#endif
StructuredBuffer<float> CausticField : register(t9);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(13, 0)]]
#endif
StructuredBuffer<uint> Wetness : register(t10);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(14, 0)]]
#endif
RWStructuredBuffer<uint> WhitewaterBits : register(u1);

#if defined(T850_GL)
  #define T850_IMAGE_Y(y, h) ((uint)(h) - 1u - (uint)(y))
  #define T850_UV(uv) float2((uv).x, 1.0f - (uv).y)
#else
  #define T850_IMAGE_Y(y, h) (uint)(y)
  #define T850_UV(uv) (uv)
#endif

#define PI 3.14159265f

// Set once per thread at the top of CS().
static uint2 FluidSize;
static float EnvMaxMip;

// ---- Projection and scene access ----------------------------------------------------------

// Pixels per metre at view depth 1. Square pixels, so the same focal length serves x and y.
float Focal()
{
    return 0.5f * float(OutputHeight) * max(ProjScaleY, 1e-4f);
}

float3 ViewPosition(float2 pixel, float z)
{
    const float f = Focal();
    return float3((pixel.x - 0.5f * float(OutputWidth)) * z / f,
                  -(pixel.y - 0.5f * float(OutputHeight)) * z / f,
                  z);
}

float2 ProjectToPixel(float3 q)
{
    const float f = Focal();
    return float2(q.x * f / q.z + 0.5f * float(OutputWidth),
                  -q.y * f / q.z + 0.5f * float(OutputHeight));
}

bool OnScreen(float2 pixel)
{
    return pixel.x >= 0.0f && pixel.y >= 0.0f &&
           pixel.x < float(OutputWidth) && pixel.y < float(OutputHeight);
}

int2 ClampPixel(int2 p)
{
    return clamp(p, int2(0, 0), int2(int(OutputWidth) - 1, int(OutputHeight) - 1));
}

// Full-resolution scene texel (G-buffer, shadow, depth).
int3 SceneTexel(int2 p)
{
    const int2 c = ClampPixel(p);
    return int3(c.x, int(T850_IMAGE_Y(c.y, OutputHeight)), 0);
}

// Linear view depth of the opaque scene; the sky (cleared depth) reads as very far.
float LinearSceneDepth(int2 p)
{
    const float raw = SceneDepth.Load(SceneTexel(p));
    return raw > 0.0f && abs(raw - ProjA) > 1e-9f ? ProjB / (raw - ProjA) : 1.0e9f;
}

float3 SampleScene(float2 pixel)
{
    const float2 uv = saturate(pixel / float2(float(OutputWidth), float(OutputHeight)));
    return SceneColor.SampleLevel(LinearSampler, T850_UV(uv), 0.0f).rgb;
}

float3 SceneNormalWorld(int2 p)
{
    const float3 n = SceneNormal.Load(SceneTexel(p)).xyz * 2.0f - 1.0f;
    const float len = length(n);
    return len > 1.0e-3f ? n / len : float3(0.0f, 1.0f, 0.0f);
}

float3 ToWorld(float3 v)
{
    return CameraRight.xyz * v.x + CameraUp.xyz * v.y + CameraForward.xyz * v.z;
}

float3 ToView(float3 w)
{
    return float3(dot(w, CameraRight.xyz), dot(w, CameraUp.xyz), dot(w, CameraForward.xyz));
}

float3 CameraPosition()
{
    return float3(CameraRight.w, CameraUp.w, CameraForward.w);
}

float Luminance(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

// ---- Fluid targets (possibly lower resolution) --------------------------------------------

int2 FluidTexelOf(float2 pixel)
{
    const float2 scaled = pixel * float2(FluidSize) / float2(float(OutputWidth), float(OutputHeight));
    return clamp(int2(floor(scaled)), int2(0, 0), int2(FluidSize) - 1);
}

float LoadFluidDepthTexel(int2 t)
{
    const int2 c = clamp(t, int2(0, 0), int2(FluidSize) - 1);
    return SurfaceDepth.Load(int3(c.x, int(T850_IMAGE_Y(c.y, FluidSize.y)), 0));
}

float4 LoadFluidThicknessTexel(int2 t)
{
    const int2 c = clamp(t, int2(0, 0), int2(FluidSize) - 1);
    return FluidThickness.Load(int3(c.x, int(T850_IMAGE_Y(c.y, FluidSize.y)), 0));
}

// Depth-aware bilinear upsample at a full-resolution pixel position. Texels of the nearest
// layer (within 0.25 m of the front-most) are blended; the coverage is the plain bilinear
// blend so silhouettes stay anti-aliased. At full resolution this is the texel itself.
void SampleFluid(float2 pixel, out float z, out float4 thickness)
{
    const float2 f = pixel * float2(FluidSize) / float2(float(OutputWidth), float(OutputHeight)) - 0.5f;
    const int2 t0 = int2(floor(f));
    const float2 w = f - float2(t0);
    const float d00 = LoadFluidDepthTexel(t0);
    const float d10 = LoadFluidDepthTexel(t0 + int2(1, 0));
    const float d01 = LoadFluidDepthTexel(t0 + int2(0, 1));
    const float d11 = LoadFluidDepthTexel(t0 + int2(1, 1));
    const float4 bw = float4((1.0f - w.x) * (1.0f - w.y), w.x * (1.0f - w.y), (1.0f - w.x) * w.y, w.x * w.y);
    const float4 d = float4(d00, d10, d01, d11);
    float front = 1.0e9f;
    [unroll]
    for (int k = 0; k < 4; ++k)
        if (d[k] > 0.0f) front = min(front, d[k]);
    float4 valid = float4(0.0f, 0.0f, 0.0f, 0.0f);
    [unroll]
    for (int j = 0; j < 4; ++j)
        valid[j] = (d[j] > 0.0f && d[j] < front + max(0.25f, 0.03f * front)) ? bw[j] : 0.0f;
    const float sum = valid.x + valid.y + valid.z + valid.w;
    if (sum <= 1.0e-4f) {
        z = 0.0f;
        thickness = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    z = dot(valid, d) / sum;
    const float4 t00 = LoadFluidThicknessTexel(t0);
    const float4 t10 = LoadFluidThicknessTexel(t0 + int2(1, 0));
    const float4 t01 = LoadFluidThicknessTexel(t0 + int2(0, 1));
    const float4 t11 = LoadFluidThicknessTexel(t0 + int2(1, 1));
    thickness.rgb = (t00.rgb * valid.x + t10.rgb * valid.y + t01.rgb * valid.z + t11.rgb * valid.w) / sum;
    thickness.a = dot(bw, float4(t00.a, t10.a, t01.a, t11.a));
}

float LoadDepth(int2 p)
{
    float z;
    float4 unused;
    SampleFluid(float2(ClampPixel(p)) + 0.5f, z, unused);
    return z;
}

// The whitewater channels of the fluid texel under a pixel.
float4 LoadAux(float2 pixel)
{
    const int2 t = FluidTexelOf(pixel);
    const uint base = (uint(t.x) + uint(t.y) * FluidSize.x) * 4u;
    return float4(float(AuxBits[base + 0u]) / 65535.0f,
                  float(AuxBits[base + 1u]) / 1024.0f,
                  float(AuxBits[base + 2u]) / 1024.0f,
                  float(AuxBits[base + 3u]) / 1024.0f);
}

// True where the opaque scene seen at `pixel` lies under the water: inside the silhouette (not
// its anti-aliased rim) and clearly below that pixel's surface.
bool UnderWater(int2 pixel)
{
    const int2 t = FluidTexelOf(float2(pixel) + 0.5f);
    const float water = LoadFluidDepthTexel(t);
    return water > 0.0f && LoadFluidThicknessTexel(t).a > 0.5f && LinearSceneDepth(pixel) >= water + 0.05f;
}

// ---- Sky and environment ------------------------------------------------------------------

// Fallback sky for a world-space direction: horizon to zenith, dimmed below the horizon.
float3 SkyGradient(float3 directionWorld)
{
    const float elevation = directionWorld.y;
    const float3 sky = lerp(SkyHorizon.rgb, ReflectionColor.rgb, smoothstep(0.0f, 0.65f, elevation));
    return lerp(SkyHorizon.rgb * 0.35f, sky, smoothstep(-0.12f, 0.02f, elevation));
}

// Radiance of the scene's sky cube along a world direction (the cube is stored with x and z
// mirrored, as FS_Mesh and the deferred pass sample it).
float3 EnvRadiance(float3 directionWorld, float mip)
{
    if (EnvParams.x <= 0.0f) return SkyGradient(directionWorld);
    const float3 d = float3(-directionWorld.x, directionWorld.y, -directionWorld.z);
    return EnvironmentMap.SampleLevel(LinearSampler, d, clamp(mip, 0.0f, EnvMaxMip)).rgb * EnvParams.x;
}

// Sky light falling on an up-facing surface: a blurred cosine-weighted average.
float3 SkyIrradiance()
{
    const float mip = max(EnvMaxMip - 1.0f, 0.0f);
    float3 sum = EnvRadiance(float3(0.0f, 1.0f, 0.0f), mip) * 2.0f;
    sum += EnvRadiance(normalize(float3(0.7f, 0.7f, 0.0f)), mip);
    sum += EnvRadiance(normalize(float3(-0.7f, 0.7f, 0.0f)), mip);
    sum += EnvRadiance(normalize(float3(0.0f, 0.7f, 0.7f)), mip);
    sum += EnvRadiance(normalize(float3(0.0f, 0.7f, -0.7f)), mip);
    return sum / 6.0f;
}

// Henyey-Greenstein phase relative to isotropic scattering (1 = isotropic).
float PhaseRelative(float cosine, float g)
{
    const float g2 = g * g;
    return (1.0f - g2) / pow(max(1.0f + g2 - 2.0f * g * cosine, 1.0e-4f), 1.5f);
}

// ---- Surface geometry ---------------------------------------------------------------------

// Surface tangent along `offset` (full-resolution pixels): the central difference inside the
// surface, the one-sided difference next to a depth jump larger than `edge` (the paper's
// silhouette rule), zero when both neighbours are off the surface.
float3 Tangent(int2 base, float3 p, float z, int2 offset, float edge)
{
    const float zf = LoadDepth(base + offset);
    const float zb = LoadDepth(base - offset);
    const bool front = zf > 0.0f && abs(zf - z) < edge;
    const bool back = zb > 0.0f && abs(z - zb) < edge;
    const float3 df = front ? ViewPosition(float2(base + offset) + 0.5f, zf) - p : float3(0.0f, 0.0f, 0.0f);
    const float3 db = back ? p - ViewPosition(float2(base - offset) + 0.5f, zb) : float3(0.0f, 0.0f, 0.0f);
    if (front && back) return 0.5f * (df + db);
    return front ? df : db;
}

// ---- Reflection ---------------------------------------------------------------------------

// The reflected image averaged over a narrow cone: sub-pixel ripples make water a slightly
// glossy mirror, and an exact mirror copies every texel of a busy wall texture as speckle.
float3 SampleSceneGlossy(float2 pixel, float radiusPx)
{
    const float r = clamp(radiusPx, 0.0f, 12.0f);
    const float h = 0.5f * r;
    float3 c = 2.0f * SampleScene(pixel);
    c += SampleScene(pixel + float2(h, h));
    c += SampleScene(pixel + float2(-h, h));
    c += SampleScene(pixel + float2(h, -h));
    c += SampleScene(pixel + float2(-h, -h));
    c += SampleScene(pixel + float2(r, 0.0f));
    c += SampleScene(pixel + float2(-r, 0.0f));
    c += SampleScene(pixel + float2(0.0f, r));
    c += SampleScene(pixel + float2(0.0f, -r));
    return c * 0.1f;
}

// Screen-space reflection: march the reflected ray against the opaque depth with a geometric
// step, bisect the crossing, and accept it only if the surface there faces the ray (a crossing
// behind a pillar or under the coping is the ray passing behind the object, not hitting it).
// Returns the weight of `hitColor` over the environment. A ray that leaves the frame continues
// the object at its exit pixel, trusted less the further it still has to travel; a ray coming
// back toward the camera reflects what is behind it, which the screen does not have.
float TraceReflection(float3 origin, float3 direction, float coneTan, out float3 hitColor)
{
    hitColor = float3(0.0f, 0.0f, 0.0f);
    if (ReflectionDistance <= 0.0f) return 0.0f;
    const float facing = smoothstep(-0.02f, 0.12f, direction.z);
    if (facing <= 0.0f) return 0.0f;

    const float focal = Focal();
    float stepLength = max(0.004f * origin.z, 0.01f);
    float t = 0.5f * stepLength;
    float lastT = -1.0f;
    bool exited = false;
    [loop]
    for (int k = 0; k < 80; ++k) {
        const float previous = t;
        t += stepLength;
        stepLength *= 1.07f;
        if (t > ReflectionDistance) return 0.0f;
        const float3 q = origin + direction * t;
        if (q.z <= 0.05f) return 0.0f;
        const float2 pixel = ProjectToPixel(q);
        if (!OnScreen(pixel)) { exited = true; break; }
        lastT = t;
        const float sceneZ = LinearSceneDepth(int2(pixel));
        if (sceneZ < 1.0e8f && q.z > sceneZ && q.z - sceneZ < max(2.0f * stepLength, 0.25f)) {
            float lo = previous;
            float hi = t;
            [unroll]
            for (int r = 0; r < 6; ++r) {
                const float mid = 0.5f * (lo + hi);
                const float3 m = origin + direction * mid;
                if (m.z > LinearSceneDepth(int2(ProjectToPixel(m)))) hi = mid;
                else lo = mid;
            }
            const float3 h = origin + direction * hi;
            const float2 hitPixel = ProjectToPixel(h);
            if (dot(ToView(SceneNormalWorld(int2(hitPixel))), direction) > 0.1f) continue;
            hitColor = SampleSceneGlossy(hitPixel, coneTan * hi * focal / max(h.z, 0.05f));
            return facing;
        }
    }
    if (!exited || lastT < 0.0f) return 0.0f;

    const float3 q = origin + direction * lastT;
    const float2 exitPixel = ProjectToPixel(q);
    const float exitZ = LinearSceneDepth(int2(exitPixel));
    hitColor = SampleSceneGlossy(exitPixel, coneTan * lastT * focal / max(q.z, 0.05f));
    if (exitZ > 1.0e8f) return facing;               // on-screen sky along the ray
    const float ahead = exitZ - q.z;                 // view depth still to cover
    if (ahead <= 0.0f) return facing;                // already level with the object
    return facing * exp(-(ahead / max(direction.z, 1.0e-3f)) / 3.0f);
}

// ---- Ripples ------------------------------------------------------------------------------

uint Pcg(uint v)
{
    const uint state = v * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float Hash01(uint v)
{
    return float(Pcg(v) >> 8u) * (1.0f / 16777216.0f);
}

// Wind ripples: six directional waves with hashed directions and phases and irregularly
// spaced wavelengths (no visible lattice when the camera is still). Returns the height
// gradient (dh/dx, dh/dz). Waves shorter than about six pixels fade out; their slope variance
// is returned in `lostVariance` so the glint and the reflection widen instead.
float2 RippleGradient(float2 xz, float time, float metresPerPixel, float slope, out float lostVariance)
{
    float2 gradient = float2(0.0f, 0.0f);
    lostVariance = 0.0f;
    const float longest = max(RippleParams.y, 0.01f);
    const float speed = RippleParams.z;
    const float ratios[6] = { 1.0f, 0.613f, 0.377f, 0.239f, 0.149f, 0.093f };
    [unroll]
    for (int k = 0; k < 6; ++k) {
        const float wavelength = longest * ratios[k];
        const float amplitude = slope * pow(0.78f, float(k));
        const float resolve = saturate(wavelength / max(metresPerPixel * 6.0f, 1.0e-5f) - 1.0f);
        const float angle = 0.6f + 6.2831853f * Hash01(uint(k) * 7919u + 17u);
        const float2 direction = float2(cos(angle), sin(angle));
        const float waveNumber = 6.2831853f / wavelength;
        const float omega = sqrt(9.81f * waveNumber) * speed;   // deep-water dispersion
        const float phase = dot(direction, xz) * waveNumber + omega * time + 6.2831853f * Hash01(uint(k) * 104729u + 3u);
        gradient += direction * (amplitude * resolve * cos(phase));
        lostVariance += 0.5f * amplitude * amplitude * (1.0f - resolve * resolve);
    }
    return gradient;
}

// ---- Caustics, wetness, foam --------------------------------------------------------------

// Caustic web: two drifting layers of ridged sine interference whose product leaves thin
// bright filaments where the ridges cross.
float CausticPattern(float2 xz, float time)
{
    const float scale = 6.2831853f / max(CausticParams.y, 0.05f);
    const float speed = CausticParams.z;
    float2 p = xz * scale;
    const float2 warp = float2(sin(p.y * 0.73f + time * 0.9f * speed),
                               sin(p.x * 0.61f - time * 1.1f * speed));
    p += 0.9f * warp;
    const float a = 1.0f - abs(sin(p.x + time * 0.6f * speed) * sin(p.y * 1.13f - time * 0.4f * speed));
    const float b = 1.0f - abs(sin((p.x + p.y) * 0.71f - time * 0.5f * speed) *
                               sin((p.x - p.y) * 0.83f + time * 0.7f * speed));
    return pow(saturate(a * b), 6.0f);
}

// Focusing of the simulated surface over the basin (CS_PbfFields), bilinear; 1 = neutral.
float CausticFocus(float2 xz)
{
    const uint sw = GridParams.z;
    const uint sd = GridParams.w;
    if (sw < 2u || sd < 2u) return 1.0f;
    const float2 g = (xz - PoolMin.xz) / max(PoolMax.xz - PoolMin.xz, float2(1.0e-3f, 1.0e-3f)) *
                     float2(float(sw - 1u), float(sd - 1u));
    const float2 c = clamp(g, float2(0.0f, 0.0f), float2(float(sw - 1u), float(sd - 1u)) - 1.0e-3f);
    const uint2 t = uint2(c);
    const float2 f = c - float2(t);
    const uint i = t.x + t.y * sw;
    const float a = lerp(CausticField[i], CausticField[i + 1u], f.x);
    const float b = lerp(CausticField[i + sw], CausticField[i + sw + 1u], f.x);
    return lerp(a, b, f.y);
}

// Wetness of the deck and coping at a world position (0 dry .. 1 soaked), bilinear.
float WetnessAt(float2 xz)
{
    const uint ww = GridParams.x;
    const uint wh = GridParams.y;
    if (ww < 2u || wh < 2u || WetParams.z <= 0.0f) return 0.0f;
    const float2 g = (xz - WetParams.xy) / WetParams.z - 0.5f;
    if (any(g < 0.0f) || g.x >= float(ww - 1u) || g.y >= float(wh - 1u)) return 0.0f;
    const uint2 t = uint2(g);
    const float2 f = g - float2(t);
    const uint i = t.x + t.y * ww;
    const float a = lerp(float(Wetness[i]), float(Wetness[i + 1u]), f.x);
    const float b = lerp(float(Wetness[i + ww]), float(Wetness[i + ww + 1u]), f.x);
    return lerp(a, b, f.y) / 65535.0f;
}

// Foam lace: the films between the bubbles of a slowly churning cellular pattern.
float FoamCells(float2 p, float time)
{
    const float2 cell = floor(p);
    const float2 f = p - cell;
    float d1 = 8.0f;
    float d2 = 8.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    [unroll]
    for (int x = -1; x <= 1; ++x) {
        const float2 o = float2(float(x), float(y));
        const int2 c = int2(cell + o);
        const uint h = Pcg(uint(c.x + 4096) * 1973u + uint(c.y + 4096) * 9277u + 1u);
        const float2 r = float2(float(h & 0xFFFFu), float(h >> 16u)) / 65535.0f;
        const float2 jitter = 0.5f + 0.42f * sin(time * 0.5f + 6.2831853f * r);
        const float d = length(o + jitter - f);
        if (d < d1) { d2 = d1; d1 = d; }
        else if (d < d2) { d2 = d; }
    }
    return 1.0f - smoothstep(0.0f, 0.2f, d2 - d1);
}

float FoamLace(float2 xz, float time)
{
    const float2 p = xz / max(FoamParams.y, 0.02f);
    return max(FoamCells(p, time), 0.7f * FoamCells(p * 2.3f + 7.1f, time * 1.3f));
}

// ---- Refraction ---------------------------------------------------------------------------

// Distance along a world ray from inside the basin to its floor or walls (the analytic pool
// box, steps ignored). Outside the footprint the ray is not in the pool: a large distance.
float PoolBoxExit(float3 o, float3 d)
{
    if (o.x < PoolMin.x || o.x > PoolMax.x || o.z < PoolMin.z || o.z > PoolMax.z) return 1.0e3f;
    float t = 1.0e3f;
    if (d.x > 1.0e-4f) t = min(t, (PoolMax.x - o.x) / d.x);
    else if (d.x < -1.0e-4f) t = min(t, (PoolMin.x - o.x) / d.x);
    if (d.z > 1.0e-4f) t = min(t, (PoolMax.z - o.z) / d.z);
    else if (d.z < -1.0e-4f) t = min(t, (PoolMin.z - o.z) / d.z);
    if (d.y < -1.0e-4f) t = min(t, (PoolMin.y - o.y) / d.y);
    return max(t, 0.0f);
}

struct RefractionHit
{
    float2 pixel;      // where the refracted ray meets the opaque scene (or the stand-in)
    float sceneZ;
    float distance;    // metres along the refracted ray
    float visible;     // 1 = the hit is on screen, 0 = hidden (analytic distance, stand-in colour)
};

// March the refracted ray through the depth buffer and bisect the first crossing behind an
// opaque surface that lies under the water. A crossing behind anything that is NOT under the
// water (the coping lip, the deck) means the true hit is hidden from the camera.
RefractionHit MarchRefraction(float3 origin, float3 direction, float maxDistance, float3 originWorld,
                              float2 startPixel)
{
    RefractionHit hit;
    hit.pixel = startPixel;
    hit.sceneZ = LinearSceneDepth(int2(startPixel));
    hit.distance = 0.0f;
    hit.visible = 0.0f;
    float2 lastGood = startPixel;
    float lastGoodZ = hit.sceneZ;
    float previous = 0.0f;
    [loop]
    for (int k = 1; k <= 16; ++k) {
        const float s = float(k) / 16.0f;
        const float t = maxDistance * s * sqrt(s);
        const float3 q = origin + direction * t;
        if (q.z <= 0.05f) break;
        const float2 px = ProjectToPixel(q);
        if (!OnScreen(px)) break;
        const float sceneZ = LinearSceneDepth(int2(px));
        if (q.z >= sceneZ) {
            if (!UnderWater(int2(px))) break;   // hidden behind something above the water
            float lo = previous;
            float hi = t;
            [unroll]
            for (int r = 0; r < 5; ++r) {
                const float mid = 0.5f * (lo + hi);
                const float3 m = origin + direction * mid;
                if (m.z >= LinearSceneDepth(int2(ProjectToPixel(m)))) hi = mid;
                else lo = mid;
            }
            hit.pixel = clamp(ProjectToPixel(origin + direction * hi), float2(0.5f, 0.5f),
                              float2(float(OutputWidth), float(OutputHeight)) - 0.5f);
            hit.sceneZ = LinearSceneDepth(int2(hit.pixel));
            hit.distance = hi;
            hit.visible = 1.0f;
            return hit;
        }
        // Both corners of the bilinear footprint, or the filter blends in the coping lip.
        if (UnderWater(int2(px - 0.5f)) && UnderWater(int2(px + 0.5f))) {
            lastGood = px;
            lastGoodZ = sceneZ;
        }
        previous = t;
    }
    hit.pixel = lastGood;
    hit.sceneZ = lastGoodZ;
    hit.distance = min(PoolBoxExit(originWorld, ToWorld(direction)), maxDistance);
    return hit;
}

// ---- Light under and in the water ---------------------------------------------------------

// Colour of the opaque surface at a refraction hit, with the light that reaches it corrected
// for the water above. The deferred pass lit it as if in air: the direct sun part (rebuilt from
// the G-buffer: albedo, N.L, shadow) is absorbed along the refracted sun path and carries the
// caustics; the ambient part is absorbed over the depth. `lightInfo` feeds debug view 8.
float3 SubmergedRadiance(RefractionHit hit, float surfaceY, float3 sunWorld, float3 sunDown,
                         float time, out float3 lightInfo)
{
    const float3 colour = SampleScene(hit.pixel);
    lightInfo = float3(0.0f, 1.0f, 0.5f);
    if (hit.visible < 0.5f || hit.sceneZ > 1.0e8f) return colour;
    const int2 hp = int2(hit.pixel);
    const float3 floorWorld = CameraPosition() + ToWorld(ViewPosition(hit.pixel, hit.sceneZ));
    const float depth = surfaceY - floorWorld.y;
    if (depth <= 0.01f) return colour;
    const float3 albedo = SceneAlbedo.Load(SceneTexel(hp)).rgb;
    const float3 n = SceneNormalWorld(hp);
    const float shadow = SceneShadow.Load(SceneTexel(hp)).r;
    const float3 direct = albedo * (saturate(dot(n, sunWorld)) * shadow * 0.96f / PI) * SunColor.rgb;
    const float directFraction = saturate(Luminance(direct) / max(Luminance(colour), 1.0e-4f));
    const float cosine = max(-sunDown.y, 0.2f);
    const float3 one = float3(1.0f, 1.0f, 1.0f);
    const float3 sunTransmit = lerp(one, exp(-FluidTint.rgb * depth / cosine), LightParams.x);
    const float3 ambientTransmit = lerp(one, exp(-FluidTint.rgb * depth * 1.2f), LightParams.x);
    float caustic = 1.0f;
    if (CausticParams.x > 0.0f && directFraction > 0.0f) {
        // The sun entered the surface above this point along the refracted sun ray.
        const float2 entry = floorWorld.xz - sunDown.xz / cosine * depth;
        const float fade = exp(-depth / max(CausticParams.w, 0.05f)) * smoothstep(0.02f, 0.15f, depth);
        const float web = max(1.0f + CausticParams.x * fade * (CausticPattern(entry, time) * 2.4f - 0.3f), 0.0f);
        caustic = web * pow(max(CausticFocus(entry), 0.05f), LightParams.y);
    }
    lightInfo = float3(directFraction, Luminance(sunTransmit), 0.5f * caustic);
    return colour * ((1.0f - directFraction) * ambientTransmit + directFraction * sunTransmit * caustic);
}

// Sun visibility at a point of the water surface: the floor point on the same refracted sun
// ray is shadowed by the same occluder above the water, and ShadowAccum knows that point when
// the camera sees it.
float SurfaceSunVisibility(float3 surfaceWorld, float3 sunDown)
{
    const float cosine = max(-sunDown.y, 0.2f);
    const float depth = max(surfaceWorld.y - PoolMin.y, 0.05f);
    const float3 v = ToView(surfaceWorld + sunDown * (depth / cosine) - CameraPosition());
    if (v.z <= 0.1f) return 1.0f;
    const float2 px = ProjectToPixel(v);
    if (!OnScreen(px)) return 1.0f;
    if (abs(LinearSceneDepth(int2(px)) - v.z) > 0.15f * v.z + 0.1f) return 1.0f;
    return SceneShadow.Load(SceneTexel(int2(px))).r;
}

// Sun and sky light scattered toward the eye inside the water (the scene-7 volumetric term,
// with a Henyey-Greenstein phase): the colour Beer-Lambert absorption is replaced by.
float3 InScatter(float3 viewWorld, float3 sunWorld, float3 sunDown, float sunVisibility, float3 skyAmbient)
{
    const float phase = PhaseRelative(dot(sunDown, -viewWorld), ScatterParams.z);
    const float3 light = skyAmbient * ScatterParams.y +
                         SunColor.rgb * (ScatterParams.x * 0.25f * phase * saturate(sunWorld.y) * sunVisibility);
    return FluidColor.rgb * light;
}

// Diffuse white (foam, spray, aerated water) lit like the deferred pass lights an up-facing
// surface of albedo 0.85.
float3 WhitewaterLight(float3 sunWorld, float sunVisibility, float3 skyAmbient)
{
    return 0.85f * (SunColor.rgb * (saturate(sunWorld.y) * sunVisibility / PI) + skyAmbient);
}

// A wet porous surface: darker, with a thin glossy film reflecting the sky.
float3 WetSurface(float3 colour, float3 normal, float3 viewWorld, float wet)
{
    const float cosine = saturate(dot(normal, -viewWorld));
    const float fresnel = 0.02f + 0.98f * pow(1.0f - cosine, 5.0f);
    return colour * lerp(1.0f, 0.55f, wet) + EnvRadiance(reflect(viewWorld, normal), 1.0f) * (fresnel * wet * 0.8f);
}

// The pool surface seen through a splash sheet, a second interface at the resting water plane:
// its own reflection, and the floor refracted and absorbed below it.
float3 PoolSurfaceBehind(float3 pointWorld, float3 directionWorld, float3 sunWorld, float3 sunDown,
                         float time, float3 skyAmbient)
{
    float lost;
    const float distance = length(pointWorld - CameraPosition());
    const float2 g = RippleGradient(pointWorld.xz, time, distance / Focal(), RippleParams.x * RippleFlow.y, lost);
    const float3 n = normalize(float3(-g.x, 1.0f, -g.y));
    const float3 pView = ToView(pointWorld - CameraPosition());
    const float3 rWorld = reflect(directionWorld, n);
    float3 hitColour;
    const float hitWeight = TraceReflection(pView, ToView(rWorld), 0.02f, hitColour);
    const float3 reflected = lerp(EnvRadiance(rWorld, 0.0f), hitColour, hitWeight);
    float3 tWorld = refract(directionWorld, n, 1.0f / 1.33f);
    if (dot(tWorld, tWorld) < 1.0e-6f) tWorld = directionWorld;
    const RefractionHit hit = MarchRefraction(pView, ToView(tWorld), 4.0f, pointWorld, ProjectToPixel(pView));
    float3 lightInfo;
    const float3 below = SubmergedRadiance(hit, pointWorld.y, sunWorld, sunDown, time, lightInfo);
    const float3 transmit = exp(-FluidTint.rgb * max(hit.distance, 0.05f));
    const float3 refracted = below * transmit +
                             InScatter(directionWorld, sunWorld, sunDown, 1.0f, skyAmbient) * (1.0f - transmit);
    const float fresnel = 0.02f + 0.98f * pow(1.0f - saturate(dot(n, -directionWorld)), max(FresnelPower, 1.0f));
    return lerp(refracted, reflected, fresnel);
}

// ---- Main ---------------------------------------------------------------------------------

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (Mode == 0u) return;   // Splat mode: leave the splat target untouched.
    if (id.x >= OutputWidth || id.y >= OutputHeight) return;

    SurfaceDepth.GetDimensions(FluidSize.x, FluidSize.y);
    uint envWidth, envHeight, envLevels;
    EnvironmentMap.GetDimensions(0, envWidth, envHeight, envLevels);
    EnvMaxMip = float(max(envLevels, 1u) - 1u);

    const uint2 imagePixel = uint2(id.x, T850_IMAGE_Y(id.y, OutputHeight));
    const int2 base = int2(id.xy);
    const float2 pixelCentre = float2(id.xy) + 0.5f;
    const float particleRadius = max(ShadeParams.y, 1.0e-3f);
    const float time = ShadeParams.z;

    // Secondary particles splatted this frame: read, then clear for the next frame.
    const uint wwBase = (id.x + id.y * OutputWidth) * 4u;
    const float sprayCover = float(WhitewaterBits[wwBase + 0u]) / 4096.0f;
    const float foamCover = float(WhitewaterBits[wwBase + 1u]) / 4096.0f;
    const float bubbleCover = float(WhitewaterBits[wwBase + 2u]) / 4096.0f;
    const float sparkle = float(WhitewaterBits[wwBase + 3u]) / 65535.0f;
    WhitewaterBits[wwBase + 0u] = 0u;
    WhitewaterBits[wwBase + 1u] = 0u;
    WhitewaterBits[wwBase + 2u] = 0u;
    WhitewaterBits[wwBase + 3u] = 0u;

    const float3 sunWorld = normalize(ToWorld(normalize(SunDirection.xyz)));
    float3 sunDown = refract(-sunWorld, float3(0.0f, 1.0f, 0.0f), 1.0f / 1.33f);
    if (dot(sunDown, sunDown) < 1.0e-6f || sunWorld.y <= 0.0f) sunDown = float3(0.0f, -1.0f, 0.0f);
    const float3 skyAmbient = SkyIrradiance();
    const float sprayAlpha = saturate(1.0f - exp(-2.5f * sprayCover)) * saturate(FoamParams.w);

    float z;
    float4 thicknessData;
    SampleFluid(pixelCentre, z, thicknessData);
    const float sceneZ = LinearSceneDepth(base);
    const bool hasWater = z > 0.0f && thicknessData.a > 0.001f && z < sceneZ - 0.002f;

    if (!hasWater) {
        // An opaque pixel or the sky: a wet deck, and secondary spray in front of it.
        float wet = 0.0f;
        float3 colour = SampleScene(pixelCentre);
        if (sceneZ < 1.0e8f && WetParams.w > 0.0f) {
            const float3 world = CameraPosition() + ToWorld(ViewPosition(pixelCentre, sceneZ));
            const float3 n = SceneNormalWorld(base);
            if (n.y > 0.6f && world.y > PoolMin.y + 0.2f) {
                wet = saturate(WetnessAt(world.xz) * WetParams.w);
                if (wet > 0.001f) colour = WetSurface(colour, n, normalize(world - CameraPosition()), wet);
            }
        }
        if (DebugView == 9u) { OutputTexture[imagePixel] = float4(wet, wet, wet, 1.0f); return; }
        if (wet <= 0.001f && sprayAlpha <= 0.001f) {
            if (ShadeParams.x > 0.5f) OutputTexture[imagePixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
            return;
        }
        const float3 spray = WhitewaterLight(sunWorld, 1.0f, skyAmbient);
        if (wet > 0.001f) OutputTexture[imagePixel] = float4(lerp(colour, spray, sprayAlpha), 1.0f);
        else OutputTexture[imagePixel] = float4(spray, sprayAlpha);
        return;
    }

    // ---- Geometry ----
    const float3 p = ViewPosition(pixelCentre, z);
    const float3 view = normalize(p);
    const float3 toCamera = -view;
    const float texelPixels = max(float(OutputHeight) / float(max(FluidSize.y, 1u)), 1.0f);
    const int stencil = clamp(int(NormalStep * texelPixels + 0.5f), 1, 16);
    const float edge = 2.0f * particleRadius * max(NormalStep, 1.0f);
    float3 tangentX = Tangent(base, p, z, int2(stencil, 0), edge);
    float3 tangentY = Tangent(base, p, z, int2(0, stencil), edge);
    tangentX.xy *= max(NormalScale, 0.0f);
    tangentY.xy *= max(NormalScale, 0.0f);
    if (length(tangentX) < 1e-6f) tangentX = float3(1.0f, 0.0f, 0.0f);
    if (length(tangentY) < 1e-6f) tangentY = float3(0.0f, -1.0f, 0.0f);
    float3 macroNormal = normalize(cross(tangentX, tangentY));
    if (dot(macroNormal, toCamera) < 0.0f) macroNormal = -macroNormal;

    // Whitewater carried by the fluid particles on this ray.
    const float chordRaw = max(thicknessData.r, 1.0e-4f);
    const float4 aux = LoadAux(pixelCentre);
    const float foamFluid = aux.x;
    const float aeration = saturate(aux.y / chordRaw);
    const float agitation = saturate(aux.z / chordRaw);
    const float sprayFluid = saturate(aux.w / chordRaw);

    // Ripples on water that faces up, as strong as the local flow is agitated.
    const float3 viewWorld = ToWorld(view);
    const float3 surfaceWorld = CameraPosition() + ToWorld(p);
    const float3 macroWorld = ToWorld(macroNormal);
    const float metresPerPixel = z / Focal() / max(abs(dot(macroNormal, toCamera)), 0.08f);
    const float rippleWeight = smoothstep(0.75f, 0.95f, macroWorld.y) *
                               saturate(1.0f - length(p) / max(RippleParams.w, 0.01f));
    const float slope = RippleParams.x * lerp(RippleFlow.y, 1.0f + RippleFlow.x, agitation) * rippleWeight;
    float3 normal = macroNormal;
    float lostVariance = 0.0f;
    if (slope > 0.0f) {
        const float2 g = RippleGradient(surfaceWorld.xz, time, metresPerPixel, slope, lostVariance);
        normal = normalize(ToView(normalize(macroWorld + float3(-g.x, 0.0f, -g.y))));
        if (dot(normal, toCamera) < 0.02f) normal = macroNormal;
    }
    lostVariance *= RippleFlow.z;

    if (DebugView == 1u) { OutputTexture[imagePixel] = float4(normal * 0.5f + 0.5f, 1.0f); return; }
    if (DebugView == 2u) { OutputTexture[imagePixel] = float4(saturate(z / 12.0f).xxx, 1.0f); return; }

    // ---- Water column and refraction ----
    const float gap = sceneZ < 1.0e8f ? max(sceneZ - z, 0.0f) / max(view.z, 0.05f) : 1.0e4f;
    const float chord = chordRaw * max(ThicknessScale, 0.0f);
    // Under the pool surface the particles fill the gap; water in the air is thinner than the
    // gap to the scene behind it.
    const float filled = smoothstep(0.45f, 0.8f, chord / max(gap, 1.0e-3f));
    const float sunVisibility = SurfaceSunVisibility(surfaceWorld, sunDown);

    float3 transmitted = refract(view, normal, 1.0f / 1.33f);
    if (dot(transmitted, transmitted) < 1e-6f) transmitted = view;
    // A thin sheet offsets the background but barely bends it; a body of water bends it fully.
    const float bend = saturate(Refraction) * lerp(0.3f, 1.0f, filled);
    const float3 bent = normalize(lerp(view, transmitted, bend));
    const float marchLength = clamp(gap * 1.2f + 0.2f, 0.3f, max(MaxTravel, 0.3f) * 2.0f);
    const RefractionHit hit = MarchRefraction(p, bent, marchLength, surfaceWorld, pixelCentre);
    float3 lightInfo;
    float3 background = SubmergedRadiance(hit, surfaceWorld.y, sunWorld, sunDown, time, lightInfo);
    const float waterPath = lerp(chord, max(hit.distance, chord), filled);

    // Water in the air over the pool: the pool surface behind it is a second interface.
    if (filled < 0.5f) {
        const float3 bentWorld = ToWorld(bent);
        if (bentWorld.y < -0.02f) {
            const float tPlane = (PoolMin.w - surfaceWorld.y) / bentWorld.y;
            const float3 planePoint = surfaceWorld + bentWorld * tPlane;
            const bool overPool = planePoint.x > PoolMin.x && planePoint.x < PoolMax.x &&
                                  planePoint.z > PoolMin.z && planePoint.z < PoolMax.z;
            if (tPlane > chord && overPool && (hit.visible < 0.5f || tPlane < hit.distance))
                background = lerp(background,
                                  PoolSurfaceBehind(planePoint, bentWorld, sunWorld, sunDown, time, skyAmbient),
                                  saturate(1.0f - 2.0f * filled));
        }
    }

    // Beer-Lambert over the water path; entrained air scatters (milky) and blocks the view.
    const float air = saturate(aeration * ScatterParams.w + bubbleCover * FoamParams.z * 0.3f);
    const float3 transmittance = exp(-waterPath * max(FluidTint.rgb, 0.0f)) * (1.0f - 0.85f * air);
    const float3 whitewaterLight = WhitewaterLight(sunWorld, sunVisibility, skyAmbient);
    const float3 inscatter = lerp(InScatter(viewWorld, sunWorld, sunDown, sunVisibility, skyAmbient),
                                  whitewaterLight * 0.8f, air);
    float3 refracted = background * transmittance + (1.0f - transmittance) * inscatter;
    // Bubbles under the surface catch the light.
    refracted += whitewaterLight * ((sparkle * 1.5f + bubbleCover * 0.15f) * saturate(FoamParams.z));

    if (DebugView == 3u) { OutputTexture[imagePixel] = float4(saturate(waterPath / 4.0f).xxx, 1.0f); return; }
    if (DebugView == 4u) { OutputTexture[imagePixel] = float4(thicknessData.a.xxx, 1.0f); return; }
    if (DebugView == 6u) { OutputTexture[imagePixel] = float4(refracted, 1.0f); return; }
    if (DebugView == 7u) {
        OutputTexture[imagePixel] = float4(saturate(foamFluid + foamCover), air, saturate(sprayFluid + sprayCover), 1.0f);
        return;
    }
    if (DebugView == 8u) { OutputTexture[imagePixel] = float4(lightInfo, 1.0f); return; }

    // ---- Reflection ----
    // Toksvig: the slope variance of unresolved ripples widens the reflection cone and the glint.
    const float coneTan = 0.015f + 2.0f * sqrt(lostVariance);
    const float3 reflectedView = reflect(view, normal);
    const float3 reflectedWorld = ToWorld(reflectedView);
    float3 hitColor;
    const float hitWeight = TraceReflection(p, reflectedView, coneTan, hitColor);
    const float envMip = clamp(log2(max(coneTan * 256.0f, 1.0f)), 0.0f, EnvMaxMip);
    const float3 reflected = lerp(EnvRadiance(reflectedWorld, envMip), hitColor, hitWeight);
    if (DebugView == 5u) { OutputTexture[imagePixel] = float4(reflected, 1.0f); return; }

    // Fresnel from a normal half-way between the surface and the ripples: a ripple facet must
    // not flip a grazing pixel to total reflection.
    const float3 fresnelNormal = normalize(macroNormal + normal);
    const float cosine = saturate(dot(fresnelNormal, toCamera));
    const float fresnel = 0.02f + 0.98f * pow(1.0f - cosine, max(FresnelPower, 1.0f));
    float3 color = lerp(refracted, reflected, saturate(fresnel));

    // Sun glint: normalised Blinn-Phong whose lobe widens by the unresolved slope variance, so
    // distant water shows a glitter path instead of a vanishing highlight.
    const float3 sun = normalize(SunDirection.xyz);
    const float3 halfVector = normalize(sun + toCamera);
    const float baseExponent = max(ShadeParams.w, 8.0f);
    const float roughness2 = 2.0f / (baseExponent + 2.0f) + 2.0f * lostVariance;
    const float exponent = max(2.0f / roughness2 - 2.0f, 4.0f);
    const float glint = pow(saturate(dot(normal, halfVector)), exponent) * (exponent + 8.0f) / (8.0f * PI);
    color += SunColor.rgb * glint * Specular * fresnel * sunVisibility * saturate(dot(normal, sun) * 4.0f);

    // Thin crests and sheets seen toward the sun glow: sunlight passing through the water.
    const float thin = (1.0f - filled) * exp(-chord * 1.5f) * (1.0f - exp(-chord * 6.0f));
    const float forward = PhaseRelative(dot(sunWorld, viewWorld), 0.6f);
    color += SunColor.rgb * FluidColor.rgb * (LightParams.w * 0.6f * thin * forward * sunVisibility * (1.0f - fresnel));

    // The meniscus where the water meets a wall.
    const float contact = (1.0f - smoothstep(0.004f, 0.035f, gap)) * filled * LightParams.z;
    color = lerp(color, color * 1.25f + skyAmbient * 0.08f, saturate(contact));

    // Surface foam: the solver's carried foam plus secondary foam particles, drawn as lace.
    const float foamCoverage = saturate(foamFluid * 1.2f + foamCover * 0.6f);
    if (foamCoverage > 0.01f && FoamParams.x > 0.0f) {
        const float lace = FoamLace(surfaceWorld.xz, time);
        const float foamMask = saturate((foamCoverage - (1.0f - lace)) * 2.5f + foamCoverage * foamCoverage) *
                               saturate(FoamParams.x);
        color = lerp(color, whitewaterLight * lerp(0.8f, 1.0f, lace), foamMask);
    }

    // Spray: isolated fluid particles (classified by the solver) and secondary droplets.
    const float spray = saturate(sprayFluid * saturate(Foam));
    color = lerp(color, whitewaterLight, spray * 0.85f);
    color = lerp(color, whitewaterLight, sprayAlpha);

    const float alpha = saturate(thicknessData.a) * (1.0f - 0.35f * spray);
    OutputTexture[imagePixel] = float4(color, max(alpha, sprayAlpha));
}
