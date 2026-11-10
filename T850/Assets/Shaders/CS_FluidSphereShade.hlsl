// T850 particle renderer: blended water, or lit sphere impostors as a debug view.
//
// CS_FluidSplat (ScreenSpace = 0) leaves, per pixel, the packed key of the nearest particle
// sphere in front of the opaque scene and the summed chord length (thickness) of every particle
// along the ray.
//
// Style 0, water: the particles blend through their thickness field. A 5x5 binomial smooths it
// for absorption and alpha; the derivative of a dense 9x9 binomial gives a blob-like surface
// normal, so overlapping particles merge the way metaballs do. Colour is the paper's Eq. 13
// without depth reconstruction: the scene seen through the refracted ray, Beer-Lambert
// absorption over the thickness, a Fresnel sky reflection and the sun's highlight. Thin edges
// fade out.
//
// Style 3, spheres: decode the nearest particle and light its sphere (sun, sky/ground
// hemisphere, rim). A z-buffered field of opaque spheres that shows every particle.
//
// The old splat resolve averaged every overlapping disc into one constant colour with an alpha
// that saturated after three particles, so the whole fluid drew as one flat, unlit tint with no
// thickness, refraction or highlight. Neither style loops over particles.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer ParticleSphereConstants : register(b0)
{
    uint  OutputWidth;
    uint  OutputHeight;
    uint  ParticleCount;
    uint  Style;            // 0 water, 3 lit spheres

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

// The sky as the deferred pass drew it, from the top row of the frame.
float3 SkyEstimate()
{
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float count = 0.0f;
    [unroll]
    for (int k = 1; k <= 3; ++k) {
        const int2 probe = int2(int(OutputWidth) * k / 4, 1);
        if (LinearSceneDepth(probe) > 1.0e8f) {
            sum += SampleScene(float2(probe) + 0.5f);
            count += 1.0f;
        }
    }
    return count > 0.0f ? sum / count : SkyColor.rgb;
}

float LoadThickness(int2 coordinate)
{
    if (coordinate.x < 0 || coordinate.y < 0 ||
        coordinate.x >= int(OutputWidth) || coordinate.y >= int(OutputHeight))
        return 0.0f;
    return float(ThicknessBits[uint(coordinate.x) + uint(coordinate.y) * OutputWidth]) *
           (1.0f / 1024.0f);
}

// Smoothed thickness (x) and its gradient in metres per pixel (yz): a 5x5 binomial kernel and
// its derivative, taps `spacing` pixels apart.
float3 ThicknessField(int2 base, int spacing)
{
    float t = 0.0f;
    float gx = 0.0f;
    float gy = 0.0f;
    [unroll]
    for (int j = -2; j <= 2; ++j) {
        const float by = j == 0 ? 6.0f : (abs(j) == 1 ? 4.0f : 1.0f);
        const float dy = j == 0 ? 0.0f : float(sign(j)) * (abs(j) == 1 ? 2.0f : 1.0f);
        [unroll]
        for (int i = -2; i <= 2; ++i) {
            const float bx = i == 0 ? 6.0f : (abs(i) == 1 ? 4.0f : 1.0f);
            const float dx = i == 0 ? 0.0f : float(sign(i)) * (abs(i) == 1 ? 2.0f : 1.0f);
            const float v = LoadThickness(base + int2(i, j) * spacing);
            t += bx * by * v;
            gx += dx * by * v;
            gy += bx * dy * v;
        }
    }
    const float derivative = 1.0f / (128.0f * float(spacing));
    return float3(t * (1.0f / 256.0f), gx * derivative, gy * derivative);
}

// The same with a 9x9 binomial kernel. The normal needs a footprint of about one particle radius
// but dense taps: a sparse kernel aliases the particle lattice of the deeper layers (seen through
// the water as moire), while this one removes every period longer than about two taps.
static const float kBinomial8[9] = { 1.0f, 8.0f, 28.0f, 56.0f, 70.0f, 56.0f, 28.0f, 8.0f, 1.0f };
static const float kBinomial8Slope[9] = { -1.0f, -6.0f, -14.0f, -14.0f, 0.0f, 14.0f, 14.0f, 6.0f, 1.0f };

float2 ThicknessGradient9(int2 base, int spacing)
{
    float gx = 0.0f;
    float gy = 0.0f;
    [unroll]
    for (int j = 0; j < 9; ++j) {
        [unroll]
        for (int i = 0; i < 9; ++i) {
            const float v = LoadThickness(base + int2(i - 4, j - 4) * spacing);
            gx += kBinomial8Slope[i] * kBinomial8[j] * v;
            gy += kBinomial8[i] * kBinomial8Slope[j] * v;
        }
    }
    return float2(gx, gy) * (1.0f / (128.0f * 256.0f * float(spacing)));
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

float4 ShadeWater(int2 base, float2 pixelCentre, float rPx, float zFront)
{
    // Thickness and coverage blend over about a quarter of a particle radius per tap: enough to
    // merge overlapping particles, little enough to keep droplets apart. The normal is smoothed
    // over about one particle radius, which keeps the shape of the body of water.
    const int spacing = clamp(int(rPx * 0.25f + 0.5f), 1, 16);
    const float thickness = max(ThicknessField(base, spacing).x, 0.0f) * max(ThicknessScale, 0.0f);
    const float2 gradient = ThicknessGradient9(base, spacing);

    // The thickness field behaves like twice the height of a surface facing the camera.
    const float metresPerPixel = zFront / Focal();
    const float2 slope = 0.5f * gradient / metresPerPixel;
    const float3 normal = normalize(float3(-slope.x, slope.y, -1.0f));   // pixel y grows down

    const float3 p = ViewPosition(pixelCentre, zFront);
    const float3 view = normalize(p);
    const float3 toCamera = -view;
    const float2 size = float2(float(OutputWidth), float(OutputHeight));

    // Through deep water the gradient is mostly the noise of many summed particles, which the
    // refracted ray would magnify into lumps; bend it less there. Glints and Fresnel keep the
    // full normal.
    const float2 refractSlope = slope / (1.0f + thickness / (8.0f * Radius));
    float3 transmitted = refract(view, normalize(float3(-refractSlope.x, refractSlope.y, -1.0f)), 1.0f / 1.33f);
    if (dot(transmitted, transmitted) < 1e-6f) transmitted = view;
    const float3 refractEnd = p + transmitted * min(thickness * max(Refraction, 0.0f), 2.0f);
    float2 refractPixel = refractEnd.z > 0.05f ? ProjectToPixel(refractEnd) : pixelCentre;
    refractPixel = clamp(refractPixel, float2(0.5f, 0.5f), size - 0.5f);
    if (LinearSceneDepth(int2(refractPixel)) < zFront) refractPixel = pixelCentre;
    const float3 background = SampleScene(refractPixel);

    const float3 transmittance = exp(-thickness * max(FluidTint.rgb, 0.0f));
    const float3 refracted = background * transmittance + (1.0f - transmittance) * FluidColor.rgb;

    const float3 sky = SkyEstimate();
    const float elevation = dot(reflect(view, normal), WorldUp.xyz);
    const float3 environment = lerp(sky * 0.25f, sky, smoothstep(-0.1f, 0.2f, elevation));
    const float fresnel = 0.02f + 0.98f * pow(1.0f - saturate(dot(normal, toCamera)),
                                              max(FresnelPower, 1.0f));
    float3 color = lerp(refracted, environment, saturate(fresnel));
    const float3 halfVector = normalize(normalize(SunDirection.xyz) + toCamera);
    color += SunColor.rgb * pow(saturate(dot(normal, halfVector)), 96.0f) * Specular;

    // Fade only where the water is thinner than a third of one particle's centre chord.
    const float edge = max(0.7f * Radius * InvPackingFactor, 1.0e-4f);
    return float4(color, smoothstep(0.0f, edge, thickness) * saturate(Opacity));
}

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OutputWidth || id.y >= OutputHeight) return;

    const uint2 imagePixel = uint2(id.x, T850_IMAGE_Y(id.y, OutputHeight));
    const uint key = Keys[id.x + id.y * OutputWidth];
    const uint index = key & KEY_INDEX_MASK;
    if (key == 0xFFFFFFFFu || index >= ParticleCount) {
        OutputTexture[imagePixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    const float4 proj = Projected[index];
    const float2 size = float2(float(OutputWidth), float(OutputHeight));
    const float2 pixelCentre = float2(id.xy) + 0.5f;
    const float r = min(proj.z, MAX_SPLAT_PX);
    const float2 offset = (pixelCentre - proj.xy * size) / max(r, 1.0e-3f);
    const float rho2 = dot(offset, offset);
    if (proj.z <= 0.0f || rho2 > 1.0f) {
        OutputTexture[imagePixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    const float bulge = sqrt(1.0f - rho2);

    if (Style == 3u) {
        // View space: x right, y up, z away from the camera. Pixel y grows down.
        const float3 normal = normalize(float3(offset.x, -offset.y, -bulge));
        const float3 toCamera = -normalize(ViewPosition(pixelCentre, 1.0f));
        OutputTexture[imagePixel] = ShadeSphere(index, normal, toCamera);
        return;
    }
    OutputTexture[imagePixel] = ShadeWater(int2(id.xy), pixelCentre, r,
                                           max(proj.w - Radius * bulge, 0.05f));
}
