// T850 screen-space fluid shading.
//
// Inputs are now split:
//   FluidDepthA     F32      view-space front surface (0 = empty)
//   FluidThickness  RGBA16F  r = metres of water, a = silhouette coverage
//   SceneColor      the opaque scene the water refracts and reflects
//   SceneDepth      GBuffer depth, for the refraction occlusion test and the reflection trace
//
// Normals are reconstructed by taking the cross product of one-sided view-space position
// differences (the paper's silhouette rule), not by hand-deriving component signs from depth
// gradients.
//
// Colour follows the paper's Eq. 13, C = a(1 - F) + bF + ks(n.h)^alpha, with:
//   a  the opaque scene seen along the REFRACTED ray (Snell, eta 1/1.33) and attenuated by
//      Beer-Lambert over the thickness. The refracted sample is rejected when it lands on
//      geometry in front of the water, so the coping and the posts never appear inside it.
//   b  a screen-space reflection traced against the scene depth; rays that leave the screen use
//      the sky as it is drawn at the top of the frame (or `ReflectionColor` when no sky shows).
// The shaded colour already contains the background, so alpha is only the silhouette coverage.
//
// `Mode == 0` leaves the splat target untouched. `ShadeParams.x == 1` writes zero on empty
// pixels, which is required when the splat resolve underneath is disabled.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer ParticleShadeConstants : register(b0)
{
    uint  OutputWidth;
    uint  OutputHeight;
    uint  Mode;               // 0 splat, 1 screen space
    uint  DebugView;          // 0 shaded, 1 normals, 2 depth, 3 thickness, 4 coverage

    float ProjScaleY;         // Projection.m[1][1]
    float ProjA;              // Projection.m[2][2], scene depth linearisation
    float ProjB;              // Projection.m[3][2]
    float MaxTravel;          // longest refracted path, metres

    float ThicknessScale;
    float Refraction;         // refracted path length per metre of thickness
    float Specular;
    float FresnelPower;

    float NormalScale;
    float Foam;               // whitewater strength for isolated droplets (spray)
    float ReflectionDistance; // screen-space reflection range in metres, 0 = off
    float NormalStep;

    float4 FluidTint;         // per-metre absorption * absorption scale
    float4 SunDirection;      // view space, toward the light
    float4 SunColor;          // rgb * intensity

    float4 CameraRight;       // camera basis in world space
    float4 CameraUp;
    float4 CameraForward;
    float4 ReflectionColor;   // sky for reflected rays when no sky is on screen

    float4 ShadeParams;       // x = write empty pixels, y = particle radius (m)
    float4 FluidColor;        // scattered fluid colour
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

#if defined(T850_GL)
  #define T850_IMAGE_Y(y, h) ((uint)(h) - 1u - (uint)(y))
  #define T850_UV(uv) float2((uv).x, 1.0f - (uv).y)
#else
  #define T850_IMAGE_Y(y, h) (uint)(y)
  #define T850_UV(uv) (uv)
#endif

// Pixels per metre at view depth 1. Square pixels, so the same focal length serves x and y.
float Focal()
{
    return 0.5f * float(OutputHeight) * max(ProjScaleY, 1e-4f);
}

float LoadDepth(int2 coordinate)
{
    const int2 clamped = clamp(coordinate, int2(0, 0),
                               int2(int(OutputWidth) - 1, int(OutputHeight) - 1));
    return SurfaceDepth.Load(int3(clamped.x, int(T850_IMAGE_Y(clamped.y, OutputHeight)), 0));
}

float4 LoadThickness(int2 coordinate)
{
    const int2 clamped = clamp(coordinate, int2(0, 0),
                               int2(int(OutputWidth) - 1, int(OutputHeight) - 1));
    return FluidThickness.Load(int3(clamped.x, int(T850_IMAGE_Y(clamped.y, OutputHeight)), 0));
}

// Linear view depth of the opaque scene; the sky (cleared depth) reads as very far.
float LinearSceneDepth(int2 coordinate)
{
    const int2 clamped = clamp(coordinate, int2(0, 0),
                               int2(int(OutputWidth) - 1, int(OutputHeight) - 1));
    const float raw = SceneDepth.Load(int3(clamped.x, int(T850_IMAGE_Y(clamped.y, OutputHeight)), 0));
    return raw > 0.0f && abs(raw - ProjA) > 1e-9f ? ProjB / (raw - ProjA) : 1.0e9f;
}

// View-space position of a pixel at view depth z. Pixel y grows down, view y grows up.
float3 ViewPosition(int2 pixel, float z)
{
    const float f = Focal();
    return float3((float(pixel.x) + 0.5f - 0.5f * float(OutputWidth)) * z / f,
                  -(float(pixel.y) + 0.5f - 0.5f * float(OutputHeight)) * z / f,
                  z);
}

// Inverse of ViewPosition: continuous pixel coordinates of a view-space point.
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

float3 SampleScene(float2 pixel)
{
    const float2 uv = saturate(pixel / float2(float(OutputWidth), float(OutputHeight)));
    return SceneColor.SampleLevel(LinearSampler, T850_UV(uv), 0.0f).rgb;
}

// Surface tangent along `offset`: the central difference inside the surface, the one-sided
// difference next to a depth jump larger than `edge` (the paper's silhouette rule), zero when
// both neighbours are off the surface. Picking the smaller one-sided difference at every pixel
// turned the residual particle bumps into pixel-scale normal noise (glitter).
float3 Tangent(int2 base, float3 p, float z, int2 offset, float edge)
{
    const float zf = LoadDepth(base + offset);
    const float zb = LoadDepth(base - offset);
    const bool front = zf > 0.0f && abs(zf - z) < edge;
    const bool back = zb > 0.0f && abs(z - zb) < edge;
    const float3 df = front ? ViewPosition(base + offset, zf) - p : float3(0.0f, 0.0f, 0.0f);
    const float3 db = back ? p - ViewPosition(base - offset, zb) : float3(0.0f, 0.0f, 0.0f);
    if (front && back) return 0.5f * (df + db);
    return front ? df : db;
}

// The sky as the deferred pass drew it, from the top row of the frame. Falls back to
// ReflectionColor when geometry covers all three probes.
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
    return count > 0.0f ? sum / count : ReflectionColor.rgb;
}

// Screen-space reflection: march the reflected ray against the opaque depth buffer with a
// geometric step, then bisect the crossing. Returns the hit weight (0 = miss, fades near the
// screen edge); a miss that crossed on-screen sky reports that sky in `skyColor`. `jitter`
// offsets the first step per pixel so neighbouring rays do not alias into scanline bands.
float TraceReflection(float3 origin, float3 direction, float jitter, out float3 hitColor,
                      out float3 skyColor, out float skySeen)
{
    hitColor = float3(0.0f, 0.0f, 0.0f);
    skyColor = float3(0.0f, 0.0f, 0.0f);
    skySeen = 0.0f;
    if (ReflectionDistance <= 0.0f) return 0.0f;

    float2 skyPixel = float2(-1.0f, -1.0f);
    float stepLength = max(0.005f * origin.z, 0.01f);
    float t = stepLength * jitter;
    [loop]
    for (int k = 0; k < 64; ++k) {
        const float previous = t;
        t += stepLength;
        if (t > ReflectionDistance) break;
        const float3 q = origin + direction * t;
        if (q.z <= 0.05f) break;
        const float2 pixel = ProjectToPixel(q);
        if (!OnScreen(pixel)) break;
        const float sceneZ = LinearSceneDepth(int2(pixel));
        if (sceneZ > 1.0e8f) {
            skyPixel = pixel;
        } else if (q.z > sceneZ && q.z - sceneZ < max(2.0f * stepLength, 0.25f)) {
            float lo = previous;
            float hi = t;
            [unroll]
            for (int r = 0; r < 5; ++r) {
                const float mid = 0.5f * (lo + hi);
                const float3 m = origin + direction * mid;
                if (m.z > LinearSceneDepth(int2(ProjectToPixel(m)))) hi = mid;
                else lo = mid;
            }
            const float2 hit = ProjectToPixel(origin + direction * hi);
            const float2 border = min(hit, float2(float(OutputWidth), float(OutputHeight)) - hit);
            hitColor = SampleScene(hit);
            return saturate(min(border.x, border.y) / (0.06f * float(OutputHeight)));
        }
        stepLength *= 1.08f;
    }
    if (skyPixel.x >= 0.0f) {
        skyColor = SampleScene(skyPixel);
        skySeen = 1.0f;
    }
    return 0.0f;
}

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (Mode == 0u) return;   // Splat mode: leave the splat target untouched.
    if (id.x >= OutputWidth || id.y >= OutputHeight) return;

    const uint2 imagePixel = uint2(id.x, T850_IMAGE_Y(id.y, OutputHeight));
    const float z = LoadDepth(int2(id.xy));
    const float4 thicknessData = LoadThickness(int2(id.xy));
    if (z <= 0.0f || thicknessData.a <= 0.001f) {
        if (ShadeParams.x > 0.5f) {
            OutputTexture[imagePixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
        return;
    }

    const float thickness = max(thicknessData.r, 0.0f) * max(ThicknessScale, 0.0f);
    const float2 size = float2(float(OutputWidth), float(OutputHeight));
    const float2 pixelCentre = float2(id.xy) + 0.5f;
    const int2 base = int2(id.xy);

    const int stencil = clamp(int(NormalStep + 0.5f), 1, 8);
    const float3 p = ViewPosition(base, z);
    const float edge = max(2.0f * ShadeParams.y, 1.0e-3f) * float(stencil);
    float3 tangentX = Tangent(base, p, z, int2(stencil, 0), edge);
    float3 tangentY = Tangent(base, p, z, int2(0, stencil), edge);
    tangentX.xy *= max(NormalScale, 0.0f);
    tangentY.xy *= max(NormalScale, 0.0f);
    if (length(tangentX) < 1e-6f) tangentX = float3(1.0f, 0.0f, 0.0f);
    if (length(tangentY) < 1e-6f) tangentY = float3(0.0f, -1.0f, 0.0f);

    float3 normal = normalize(cross(tangentX, tangentY));
    const float3 view = normalize(p);          // camera -> surface
    const float3 toCamera = -view;
    if (dot(normal, toCamera) < 0.0f) normal = -normal;

    if (DebugView == 1u) { OutputTexture[imagePixel] = float4(normal * 0.5f + 0.5f, 1.0f); return; }
    if (DebugView == 2u) { OutputTexture[imagePixel] = float4(saturate(z / 12.0f).xxx, 1.0f); return; }
    if (DebugView == 3u) { OutputTexture[imagePixel] = float4(saturate(thickness).xxx, 1.0f); return; }
    if (DebugView == 4u) { OutputTexture[imagePixel] = float4(thicknessData.a.xxx, 1.0f); return; }

    // Refraction: follow the transmitted ray into the water for a thickness-proportional
    // distance and look the opaque scene up where it ends. Entering water bends the ray toward
    // -n, so a flat pool shows its floor displaced toward the viewer, as a real pool does.
    float3 transmitted = refract(view, normal, 1.0f / 1.33f);
    if (dot(transmitted, transmitted) < 1e-6f) transmitted = view;
    const float travel = min(thickness * max(Refraction, 0.0f), max(MaxTravel, 0.0f));
    const float3 refractEnd = p + transmitted * travel;
    float2 refractPixel = refractEnd.z > 0.05f ? ProjectToPixel(refractEnd) : pixelCentre;
    refractPixel = clamp(refractPixel, float2(0.5f, 0.5f), size - 0.5f);
    // Geometry in front of the water surface cannot be seen through it.
    if (LinearSceneDepth(int2(refractPixel)) < z) refractPixel = pixelCentre;
    const float3 background = SceneColor.SampleLevel(
        LinearSampler, T850_UV(refractPixel / size), 0.0f).rgb;

    // Beer-Lambert absorption. FluidTint already includes the artistic scale.
    const float3 transmittance = exp(-thickness * max(FluidTint.rgb, 0.0f));
    const float3 refracted = background * transmittance + (1.0f - transmittance) * FluidColor.rgb;

    // Reflection: the scene where the mirrored ray meets it, else the sky or a dim ground.
    const float3 reflectedView = reflect(view, normal);
    const float3 worldUp = float3(CameraRight.y, CameraUp.y, CameraForward.y);
    const float elevation = dot(reflectedView, worldUp);
    float3 hitColor;
    float3 skyColor;
    float skySeen;
    // Interleaved gradient noise (Jimenez 2014): a stable per-pixel offset in [0, 1).
    const float jitter = frac(52.9829189f * frac(dot(pixelCentre, float2(0.06711056f, 0.00583715f))));
    const float hitWeight = TraceReflection(p, reflectedView, jitter, hitColor, skyColor, skySeen);
    const float3 sky = skySeen > 0.5f ? skyColor : SkyEstimate();
    const float3 environment = lerp(sky * 0.25f, sky, smoothstep(-0.1f, 0.2f, elevation));
    const float3 reflected = lerp(environment, hitColor, hitWeight);

    const float fresnel = 0.02f + 0.98f * pow(1.0f - saturate(dot(normal, toCamera)),
                                              max(FresnelPower, 1.0f));
    float3 color = lerp(refracted, reflected, saturate(fresnel));

    const float3 sun = normalize(SunDirection.xyz);
    const float3 halfVector = normalize(sun + toCamera);
    const float specular = pow(saturate(dot(normal, halfVector)), 96.0f) * Specular;
    color += SunColor.rgb * specular;

    // Spray: a droplet is a pixel whose surroundings 2.5 particle radii out are empty or far
    // behind it. Splashing water reads white (aerated, scattering), so isolated droplets fade
    // toward a lit whitewater colour and become partly transparent instead of drawing as
    // flat refractive discs. The silhouette of a body has about half its ring empty: no spray.
    const float particlePx = ShadeParams.y * Focal() / z;
    const float ringPx = clamp(2.5f * particlePx, 2.0f, 48.0f);
    const float layerGap = 4.0f * ShadeParams.y;
    float emptyCount = 0.0f;
    [unroll]
    for (int k = 0; k < 8; ++k) {
        const float angle = float(k) * 0.7853982f;
        const int2 probe = base + int2(round(float2(cos(angle), sin(angle)) * ringPx));
        const float probeDepth = LoadDepth(probe);
        emptyCount += (probeDepth <= 0.0f || probeDepth > z + layerGap) ? 1.0f : 0.0f;
    }
    const float spray = saturate((emptyCount - 5.0f) * 0.5f) * saturate(Foam);
    const float3 whitewater = (sky + SunColor.rgb * 0.15f) * 0.85f;
    color = lerp(color, whitewater, spray);

    // The colour already contains the scene behind the water, so the composite must not
    // blend it in again: alpha is the anti-aliased silhouette, lowered only for spray.
    OutputTexture[imagePixel] = float4(color, saturate(thicknessData.a) * (1.0f - 0.35f * spray));
}
