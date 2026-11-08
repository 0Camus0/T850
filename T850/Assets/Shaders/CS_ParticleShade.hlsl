// T850 particle screen-space fluid shading.
//
// Reads the smoothed front-surface depth, reconstructs view-space normals from the depth
// field (section 3.5 of van der Laan et al.), and shades with Fresnel reflection, screen-
// space refraction, Beer-Lambert absorption by accumulated thickness and a sun specular
// term. Writes the same (colour, coverage) contract the splat composite already consumes.
//
// `Mode == 0` (Splat) makes the pass a no-op: it does not touch the target, so the splat
// chain's output stays intact. That is how the runtime Splat / Screen-space switch works
// without rebuilding the render graph.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer ParticleShadeConstants : register(b0)
{
    uint  OutputWidth;
    uint  OutputHeight;
    uint  Mode;             // 0 splat, 1 screen space
    uint  DebugView;        // 0 shaded, 1 normals, 2 depth, 3 thickness, 4 coverage

    float ProjScaleY;
    float Opacity;
    float DepthBias;
    float Pad0;

    float ThicknessScale;
    float Refraction;
    float Specular;
    float FresnelPower;

    float NormalScale;
    float Foam;
    float TimeSeconds;
    float NormalStep;   // pixels between finite-difference samples

    float4 FluidTint;       // rgb = per-metre absorption coefficients
    float4 SunDirection;    // view space, toward the light
    float4 SunColor;        // rgb * intensity

    float4 ViewProjection[4];

    float4 CameraRight;
    float4 CameraUp;
    float4 CameraForward;
    float4 ReflectionColor;   // sky/ambient colour for reflected rays above the horizon
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture2D<float4> SurfaceDepth : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
Texture2D<float4> SceneColor : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
SamplerState LinearSampler : register(s1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> OutputTexture : register(u0);

#if defined(T850_GL)
  #define T850_IMAGE_Y(y, h) ((uint)(h) - 1u - (uint)(y))
  #define T850_UV(uv) float2((uv).x, 1.0f - (uv).y)
#else
  #define T850_IMAGE_Y(y, h) (uint)(y)
  #define T850_UV(uv) (uv)
#endif

float4 LoadDepth(int2 coordinate)
{
    const int2 clamped = clamp(coordinate, int2(0, 0),
                               int2(int(OutputWidth) - 1, int(OutputHeight) - 1));
    return SurfaceDepth.Load(int3(clamped.x, int(T850_IMAGE_Y(clamped.y, OutputHeight)), 0));
}

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (Mode == 0u) return;   // Splat mode: leave the splat target untouched.
    if (id.x >= OutputWidth || id.y >= OutputHeight) return;

    const uint2 pixel = uint2(id.x, id.y);
    const uint2 imagePixel = uint2(id.x, T850_IMAGE_Y(id.y, OutputHeight));
    const float4 centre = LoadDepth(int2(id.xy));
    if (centre.r <= 0.0f || centre.a <= 0.001f) {
        // No screen-space surface here. Do NOT write zero: the splat resolve's output is
        // already in this target and is the intended fallback (and the edge feather).
        return;
    }

    const float z = centre.r;
    const float thickness = max(centre.g, 0.0f) * max(ThicknessScale, 0.0f);
    const float2 size = float2(float(OutputWidth), float(OutputHeight));
    const float2 uv = (float2(float(id.x), float(id.y)) + 0.5f) / size;

    // Paper section 3.5: when a neighbour crosses a silhouette, use the smaller absolute
    // finite difference so the normal does not point into the discontinuity.
    const int2 base = int2(id.xy);
    // A one-pixel stencil on a particle splat is dominated by the lattice, not the surface.
    // Sample `NormalStep` pixels away so the reconstructed normal describes the smoothed
    // fluid surface instead of individual splats.
    const int step = clamp(int(NormalStep + 0.5f), 1, 8);
    const float invStep = 1.0f / float(step);
    const float4 left = LoadDepth(base + int2(-step, 0));
    const float4 right = LoadDepth(base + int2(step, 0));
    const float4 up = LoadDepth(base + int2(0, -step));
    const float4 down = LoadDepth(base + int2(0, step));

    const float dl = left.r > 0.0f ? (z - left.r) : 1.0e9f;
    const float dr = right.r > 0.0f ? (right.r - z) : 1.0e9f;
    const float du = up.r > 0.0f ? (z - up.r) : 1.0e9f;
    const float dd = down.r > 0.0f ? (down.r - z) : 1.0e9f;

    const float dzdx = (abs(dr) <= abs(dl) ? dr : -dl) * invStep;
    const float dzdy = (abs(dd) <= abs(du) ? dd : -du) * invStep;

    // Focal lengths in pixels: Fy = H * P11 / 2, Fx = Fy for square pixels.
    // The surface point is P(x,y) = ((x - W/2) * z / Fx, (y - H/2) * z / Fy, z), so the
    // depth-derived normal is proportional to (-Fx*dz/dx, -Fy*dz/dy, -z). Using
    // W/(2Fx) (the paper's Cx) leaves the gradient ~400x too small for pixel-space
    // derivatives, which made every surface reconstruct as fronto-parallel.
    const float fy = 0.5f * float(OutputHeight) * max(ProjScaleY, 1e-4f);
    const float fx = fy;
    const float gx = dzdx * NormalScale;
    const float gy = dzdy * NormalScale;

    float3 normal = normalize(float3(-fx * gx, -fy * gy, -z));
    const float ndcX = uv.x * 2.0f - 1.0f;
    const float ndcY = 1.0f - uv.y * 2.0f;
    const float aspect = float(OutputWidth) / max(float(OutputHeight), 1.0f);
    const float3 viewVector = normalize(float3(ndcX * aspect * z / max(ProjScaleY, 1e-4f),
                                                 ndcY * z / max(ProjScaleY, 1e-4f), z));
    const float3 toCamera = -viewVector;
    if (dot(normal, toCamera) < 0.0f) normal = -normal;

    if (DebugView == 1u) { OutputTexture[imagePixel] = float4(normal * 0.5f + 0.5f, 1.0f); return; }
    if (DebugView == 2u) { OutputTexture[imagePixel] = float4(saturate(z / 12.0f).xxx, 1.0f); return; }
    if (DebugView == 3u) { OutputTexture[imagePixel] = float4(saturate(thickness).xxx, 1.0f); return; }
    if (DebugView == 4u) { OutputTexture[imagePixel] = float4(centre.a.xxx, 1.0f); return; }

    // Refraction: offset the background lookup along the projected normal.
    const float beta = saturate(thickness * 0.8f);
    const float2 refractUV = saturate(uv + normal.xy * Refraction * (0.25f + beta));
    const float3 background = SceneColor.Sample(LinearSampler, T850_UV(refractUV)).rgb;

    // Beer-Lambert absorption. FluidTint holds the per-metre coefficients.
    const float3 transmittance = exp(-thickness * max(FluidTint.rgb, 0.0f));
    const float3 scattering = (1.0f - transmittance) * float3(0.10f, 0.30f, 0.42f);
    const float3 refracted = background * transmittance + scattering;

    // Screen-space reflection: a second, wider scene lookup for curved surfaces, blended
    // with a sky colour for rays that leave the screen upward. A flat water plane viewed
    // at a grazing angle must reflect the sky/house, not the pixel directly behind it;
    // without this the surface looked like a pale band of the pool wall.
    const float2 reflectUV = saturate(uv - normal.xy * Refraction * 1.5f);
    const float3 sceneReflection = SceneColor.Sample(LinearSampler, T850_UV(reflectUV)).rgb;
    const float3 normalWorld = normalize(CameraRight.xyz * normal.x +
                                          CameraUp.xyz * normal.y +
                                          CameraForward.xyz * normal.z);
    const float3 viewWorld = normalize(CameraRight.xyz * viewVector.x +
                                        CameraUp.xyz * viewVector.y +
                                        CameraForward.xyz * viewVector.z);
    const float3 reflectedWorld = reflect(viewWorld, normalWorld);
    const float skyMix = saturate(reflectedWorld.y * 0.75f + 0.25f);
    const float3 reflected = lerp(sceneReflection, ReflectionColor.rgb, skyMix);

    const float fresnel = 0.02f + 0.98f * pow(1.0f - saturate(dot(normal, toCamera)),
                                              max(FresnelPower, 1.0f));
    float3 color = lerp(refracted, reflected, saturate(fresnel));

    // Sun specular (Blinn-Phong half vector).
    const float3 sun = normalize(SunDirection.xyz);
    const float3 halfVector = normalize(sun + toCamera);
    const float specular = pow(saturate(dot(normal, halfVector)), 96.0f) * Specular;
    color += SunColor.rgb * specular;

    // Thin-film foam: brighten where the fluid is shallow, as in the paper's foam term.
    color += Foam * (1.0f - transmittance) * 0.25f;

    const float alpha = saturate(centre.a * Opacity);
    OutputTexture[imagePixel] = float4(color, alpha);
}
