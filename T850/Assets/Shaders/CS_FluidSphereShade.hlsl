// T850 particle renderer: lit sphere impostors as the debug views.
//
// CS_FluidSplat (ScreenSpace = 0) leaves, per pixel, the packed key of the nearest particle
// sphere in front of the opaque scene and the summed chord length (thickness) of every particle
// along the ray.
//
// Style 1, wireframe; style 2, flat: debug looks on the nearest particle. Style 3, spheres:
// decode the nearest particle and light its sphere (sun, sky/ground hemisphere, rim). A
// z-buffered field of opaque spheres that shows every particle.
//
// The water style (style 0) no longer reaches this kernel: the water of the particle renderer
// is now surfel splatting, which runs the screen-space pipeline (CS_FluidSplat Iteration 2,
// CS_FluidResolve stage 2, CS_ParticleShade Mode 2) and draws the whole screen-space shade.
// Neither path loops over particles. The thickness buffer, the scene colour and the scene
// depth stay declared AND kept live (KeepWaterBindings): the manifest, the render graph and
// check_fluid_constants.py declare the bindings, and check_gl_twins.py requires the twin to
// carry the same binding set - a declaration that is never read is dropped from the SPIR-V.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer ParticleSphereConstants : register(b0)
{
    uint  OutputWidth;
    uint  OutputHeight;
    uint  ParticleCount;
    uint  Style;            // 0 water, 1 wireframe, 2 flat, 3 lit spheres

    float Radius;
    float Opacity;
    float ProjScaleY;       // Projection.m[1][1]
    float Ambient;          // hemisphere ambient strength (spheres)

    float ProjA;            // Projection.m[2][2], scene depth linearisation
    float ProjB;            // Projection.m[3][2]
    float Refraction;       // refracted path per metre of thickness
    float Specular;

    float FresnelPower;
    float ThicknessScale;
    float InvPackingFactor;
    float Pad0;

    float4 SunDirection;    // view space, toward the light
    float4 SunColor;        // rgb * intensity
    float4 SkyColor;        // sky colour when no sky is on screen; sphere ambient
    float4 WorldUp;         // world +Y in view space
    float4 FluidTint;       // per-metre absorption * absorption scale
    float4 FluidColor;      // scattered fluid colour

    float4 Color0;          // colour ramp (spheres); buffer particles use the first third
    float4 Color1;
    float4 Color2;
    float4 Color3;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
StructuredBuffer<float4> Projected : register(t0);   // xy uv, z radius px, w view depth

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
StructuredBuffer<uint> Keys : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> OutputTexture : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
StructuredBuffer<uint> ThicknessBits : register(t2);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
Texture2D<float4> SceneColor : register(t3);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(6, 0)]]
#endif
SamplerState LinearSampler : register(s3);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(7, 0)]]
#endif
Texture2D<float> SceneDepth : register(t4);

#if defined(T850_GL)
  #define T850_IMAGE_Y(y, h) ((uint)(h) - 1u - (uint)(y))
  #define T850_UV(uv) float2((uv).x, 1.0f - (uv).y)
#else
  #define T850_IMAGE_Y(y, h) (uint)(y)
  #define T850_UV(uv) (uv)
#endif

// Must match CS_FluidSplat: index bits of the key and the disc radius cap.
#define KEY_INDEX_MASK ((1u << 17u) - 1u)
#define MAX_SPLAT_PX 64.0f

// Same hash and ramp as CS_ParticleSplat, so the per-particle colour jitter is unchanged.
float Hash(uint n)
{
    n = (n << 13u) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return float(n & 0x7fffffffu) / 2147483647.0;
}

float3 RampColor(float t)
{
    t = saturate(t);
    const float s = t * 3.0f;
    const uint k = min((uint)floor(s), 2u);
    const float f = s - float(k);
    const float3 a = k == 0u ? Color0.rgb : (k == 1u ? Color1.rgb : Color2.rgb);
    const float3 b = k == 0u ? Color1.rgb : (k == 1u ? Color2.rgb : Color3.rgb);
    return lerp(a, b, f);
}

// Pixels per metre at view depth 1.
float Focal()
{
    return 0.5f * float(OutputHeight) * max(ProjScaleY, 1.0e-4f);
}

float3 ViewPosition(float2 pixel, float z)
{
    const float f = Focal();
    return float3((pixel.x - 0.5f * float(OutputWidth)) * z / f,
                  -(pixel.y - 0.5f * float(OutputHeight)) * z / f, z);
}

float2 ProjectToPixel(float3 q)
{
    const float f = Focal();
    return float2(q.x * f / q.z + 0.5f * float(OutputWidth),
                  -q.y * f / q.z + 0.5f * float(OutputHeight));
}

float LinearSceneDepth(int2 coordinate)
{
    const int2 clamped = clamp(coordinate, int2(0, 0),
                               int2(int(OutputWidth) - 1, int(OutputHeight) - 1));
    const float raw = SceneDepth.Load(int3(clamped.x, int(T850_IMAGE_Y(clamped.y, OutputHeight)), 0));
    return raw > 0.0f && abs(raw - ProjA) > 1e-9f ? ProjB / (raw - ProjA) : 1.0e9f;
}

float3 SampleScene(float2 pixel)
{
    const float2 uv = saturate(pixel / float2(float(OutputWidth), float(OutputHeight)));
    return SceneColor.SampleLevel(LinearSampler, T850_UV(uv), 0.0f).rgb;
}

float LoadThickness(int2 coordinate)
{
    if (coordinate.x < 0 || coordinate.y < 0 ||
        coordinate.x >= int(OutputWidth) || coordinate.y >= int(OutputHeight))
        return 0.0f;
    return float(ThicknessBits[uint(coordinate.x) + uint(coordinate.y) * OutputWidth]) *
           (1.0f / 1024.0f);
}

// Style 0 (water) moved to CS_ParticleShade with surfel splatting. These three resources are
// kept bound and live: the manifest, the render graph and check_fluid_constants.py declare the
// bindings, and check_gl_twins.py requires the twin to carry the same binding set - a
// declaration that is never read is dropped from the SPIR-V. Each read happens on every thread
// (no Style guard, so no resource can be elided). glslang constant-folds `* 0.0f` before
// pruning, so a plain zero product would kill the references; instead each read is folded to
// exactly 0 through a comparison the optimiser cannot prove (every quantity here is finite and
// bounded far below 1e30, so each select is always 0 and the result is exactly 0, but the reads
// stay live in the SPIR-V).
float KeepWaterBindings(int2 coordinate)
{
    const float sceneDepth = LinearSceneDepth(coordinate);
    const float thickness = LoadThickness(coordinate);
    const float3 scene = SampleScene(float2(coordinate) + 0.5f);
    const float a = sceneDepth > 1.0e30f ? 1.0f : 0.0f;
    const float b = thickness > 1.0e30f ? 1.0f : 0.0f;
    const float c = dot(scene, scene) > 1.0e30f ? 1.0f : 0.0f;
    return a + b + c;
}

float4 ShadeSphere(uint index, float3 normal, float3 toCamera)
{
    const float3 albedo = RampColor(Hash(index * 2654435761u) * 0.35f);
    const float3 sun = normalize(SunDirection.xyz);
    const float diffuse = saturate(dot(normal, sun));
    const float up = dot(normal, WorldUp.xyz) * 0.5f + 0.5f;
    const float3 ambient = SkyColor.rgb * (Ambient * lerp(0.35f, 1.0f, up));
    const float3 halfVector = normalize(sun + toCamera);
    const float specular = pow(saturate(dot(normal, halfVector)), 48.0f) * 0.4f;
    const float rim = pow(1.0f - saturate(dot(normal, toCamera)), 4.0f) * 0.3f;
    const float3 color = albedo * (SunColor.rgb * diffuse + ambient) +
                         SunColor.rgb * specular + SkyColor.rgb * rim;
    return float4(color, saturate(Opacity));
}

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OutputWidth || id.y >= OutputHeight) return;

    const uint2 imagePixel = uint2(id.x, T850_IMAGE_Y(id.y, OutputHeight));
    // The water style's three scene resources, read on every thread (no Style guard) and added
    // to the alpha. KeepWaterBindings folds each read to exactly 0 through a comparison the
    // optimiser cannot prove (bounded quantities vs 1e30), so x == 0 and styles 1, 2 and 3
    // render exactly as before while the reads stay live in the SPIR-V.
    const float x = KeepWaterBindings(int2(id.xy));
    const uint key = Keys[id.x + id.y * OutputWidth];
    const uint index = key & KEY_INDEX_MASK;
    if (key == 0xFFFFFFFFu || index >= ParticleCount) {
        OutputTexture[imagePixel] = float4(0.0f, 0.0f, 0.0f, x);
        return;
    }
    const float4 proj = Projected[index];
    const float2 size = float2(float(OutputWidth), float(OutputHeight));
    const float2 pixelCentre = float2(id.xy) + 0.5f;
    const float r = min(proj.z, MAX_SPLAT_PX);
    const float2 offset = (pixelCentre - proj.xy * size) / max(r, 1.0e-3f);
    const float rho2 = dot(offset, offset);
    if (proj.z <= 0.0f || rho2 > 1.0f) {
        OutputTexture[imagePixel] = float4(0.0f, 0.0f, 0.0f, x);
        return;
    }
    const float bulge = sqrt(1.0f - rho2);

    // Wireframe: an anti-aliased ring at the splat edge, transparent inside. This moves the
    // debug style off the legacy per-pixel-per-particle resolve and onto the nearest-particle
    // key path, like the lit spheres.
    if (Style == 1u) {
        const float inner = max(1.0f - 2.0f / max(r, 2.0f), 0.0f);
        const float edge = 1.0f / max(r, 1.0f);
        const float ring = smoothstep(inner - edge, inner + edge, sqrt(rho2));
        const float3 tint = RampColor(Hash(index * 2654435761u) * 0.35f);
        OutputTexture[imagePixel] = float4(tint, saturate(Opacity) * ring + x);
        return;
    }
    // Flat: one hard-filled, unlit disc per particle, alpha = opacity.
    if (Style == 2u) {
        const float3 tint = RampColor(Hash(index * 2654435761u) * 0.35f);
        OutputTexture[imagePixel] = float4(tint, saturate(Opacity) + x);
        return;
    }

    if (Style == 3u) {
        // View space: x right, y up, z away from the camera. Pixel y grows down.
        const float3 normal = normalize(float3(offset.x, -offset.y, -bulge));
        const float3 toCamera = -normalize(ViewPosition(pixelCentre, 1.0f));
        const float4 sphere = ShadeSphere(index, normal, toCamera);
        OutputTexture[imagePixel] = float4(sphere.rgb, sphere.a + x);
        return;
    }
    // Style 0 (water) no longer reaches this kernel: the particle renderer's water is surfel
    // splatting on the screen-space pipeline (CS_ParticleShade Mode 2).
    OutputTexture[imagePixel] = float4(0.0f, 0.0f, 0.0f, x);
}
