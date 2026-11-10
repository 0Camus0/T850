// T850 screen-space fluid shading.
//
// Inputs:
//   FluidDepthA     F32      view-space front surface, smoothed (0 = empty)
//   FluidThickness  RGBA16F  r = metres of water along the ray (chord sum), a = silhouette coverage
//   SceneColor      the opaque scene the water refracts and reflects
//   SceneDepth      GBuffer depth: refraction occlusion, the water column under the surface, and
//                   the reflection trace
//
// Optics, per pixel (van der Laan et al. 2009, Eq. 13, with physically scaled terms):
//
//   normal      cross product of view-space position differences on the smoothed depth (the
//               paper's silhouette rule), plus small world-space ripples on near-horizontal
//               water so the surface is not a perfect mirror.
//   refraction  Snell (eta 1/1.33). The refracted ray travels through the water column to the
//               opaque scene behind it, so a shallow step barely shifts while the deep end bends;
//               a lookup that lands on anything not under the water there (the coping, the
//               deck) is pulled back toward the pixel.
//   absorption  Beer-Lambert over the metres of water on the ray. Under the pool surface that is
//               the geometric gap to the floor or wall (smooth, exact); for water in the air it is
//               the particle chord sum. The water's in-scattered colour fills what is absorbed.
//   caustics    the sun focused by the moving surface, projected onto the submerged floor and
//               walls the refracted ray lands on.
//   reflection  screen-space trace against the opaque depth over a ~1 degree glossy cone; a ray
//               that leaves the frame continues the object at its exit pixel (weighted by how
//               far it still has to go), else a horizon-to-zenith sky gradient.
//   Fresnel     Schlick, F0 = 0.02.
//   sun glint   normalised Blinn-Phong on the rippled normal.
//
// The shaded colour already contains the background, so alpha is only the silhouette coverage.
// `Mode == 0` leaves the splat target untouched. `ShadeParams.x == 1` writes zero on empty pixels.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer ParticleShadeConstants : register(b0)
{
    uint  OutputWidth;
    uint  OutputHeight;
    uint  Mode;               // 0 splat, 1 screen space
    uint  DebugView;          // 0 shaded, 1 normals, 2 depth, 3 water path, 4 coverage,
                              // 5 reflection, 6 refraction

    float ProjScaleY;         // Projection.m[1][1]
    float ProjA;              // Projection.m[2][2], scene depth linearisation
    float ProjB;              // Projection.m[3][2]
    float MaxTravel;          // longest refracted path, metres

    float ThicknessScale;
    float Refraction;         // 0..1 strength of the refracted offset (1 = Snell)
    float Specular;
    float FresnelPower;

    float NormalScale;
    float Foam;               // whitewater strength for isolated droplets (spray)
    float ReflectionDistance; // screen-space reflection range in metres, 0 = off
    float NormalStep;

    float4 FluidTint;         // per-metre absorption * absorption scale
    float4 SunDirection;      // view space, toward the light
    float4 SunColor;          // rgb * intensity

    float4 CameraRight;       // camera basis in world space; w = camera position x
    float4 CameraUp;          // w = camera position y
    float4 CameraForward;     // w = camera position z
    float4 ReflectionColor;   // zenith sky colour for reflected rays that leave the screen

    float4 ShadeParams;       // x = write empty pixels, y = particle radius (m), z = time (s),
                              // w = sun glint exponent
    float4 FluidColor;        // in-scattered water colour
    float4 SkyHorizon;        // horizon sky colour for reflected rays that leave the screen
    float4 RippleParams;      // x = slope, y = longest wavelength (m), z = speed, w = fade distance (m)
    float4 CausticParams;     // x = strength, y = pattern scale (m), z = speed, w = depth fade (m)
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
float3 ViewPosition(float2 pixel, float z)
{
    const float f = Focal();
    return float3((pixel.x - 0.5f * float(OutputWidth)) * z / f,
                  -(pixel.y - 0.5f * float(OutputHeight)) * z / f,
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

// View-space vector to world space and back (CameraRight/Up/Forward are the view axes in world).
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

// Surface tangent along `offset`: the central difference inside the surface, the one-sided
// difference next to a depth jump larger than `edge` (the paper's silhouette rule), zero when
// both neighbours are off the surface.
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

// Sky for a world-space direction that left the screen: horizon to zenith, dimmed below the
// horizon (the reflected ray then meets the deck, the wall or the ground).
float3 SkyGradient(float3 directionWorld)
{
    const float elevation = directionWorld.y;
    const float3 sky = lerp(SkyHorizon.rgb, ReflectionColor.rgb, smoothstep(0.0f, 0.65f, elevation));
    return lerp(SkyHorizon.rgb * 0.35f, sky, smoothstep(-0.12f, 0.02f, elevation));
}

// True where the opaque scene seen at `pixel` lies under the water: inside the silhouette (not
// its anti-aliased rim) and clearly below that pixel's surface.
bool UnderWater(int2 pixel)
{
    const float water = LoadDepth(pixel);
    return water > 0.0f && LoadThickness(pixel).a > 0.5f && LinearSceneDepth(pixel) >= water + 0.05f;
}

// The reflected image averaged over a narrow cone (about 1 degree): sub-pixel ripples make
// water a slightly glossy mirror, and an exact mirror copies every texel of a busy wall
// texture into the water as speckle.
float3 SampleSceneGlossy(float2 pixel, float radiusPx)
{
    const float r = clamp(radiusPx, 0.0f, 10.0f);
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

// Screen-space reflection: march the reflected ray against the opaque depth buffer with a
// geometric step, then bisect the crossing. Returns the weight of `hitColor` over the sky
// gradient.
//
// A ray that leaves the frame before hitting anything goes on into geometry the camera does
// not see. The object at the exit pixel stands in for it, trusted less the further the ray
// still has to travel to reach that object's depth: a house wall that continues above the
// frame keeps reflecting, open sky above a low wall does not. Without this every ripple that
// tips a ray across the top edge flips it from wall to sky, a hard-edged patchwork.
float TraceReflection(float3 origin, float3 direction, out float3 hitColor)
{
    hitColor = float3(0.0f, 0.0f, 0.0f);
    if (ReflectionDistance <= 0.0f) return 0.0f;
    // A ray coming back toward the camera (a wave face tilted at the viewer) reflects what is
    // behind the camera; the depth buffer only has the front faces of what lies along it, so
    // it would "hit" the top of the near coping from below. It takes the sky instead.
    const float facing = smoothstep(-0.02f, 0.12f, direction.z);
    if (facing <= 0.0f) return 0.0f;

    const float gloss = 0.015f * Focal();   // cone radius in pixels per metre travelled at depth 1
    float stepLength = max(0.004f * origin.z, 0.01f);
    float t = 0.5f * stepLength;
    float lastT = -1.0f;
    bool exited = false;
    [loop]
    for (int k = 0; k < 80; ++k) {
        const float previous = t;
        t += stepLength;
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
            hitColor = SampleSceneGlossy(ProjectToPixel(h), gloss * hi / max(h.z, 0.05f));
            return facing;
        }
        stepLength *= 1.07f;
    }
    if (!exited || lastT < 0.0f) return 0.0f;

    const float3 q = origin + direction * lastT;
    const float2 exitPixel = ProjectToPixel(q);
    const float exitZ = LinearSceneDepth(int2(exitPixel));
    hitColor = SampleSceneGlossy(exitPixel, gloss * lastT / max(q.z, 0.05f));
    if (exitZ > 1.0e8f) return facing;              // on-screen sky along the ray
    const float ahead = exitZ - q.z;                 // view depth still to cover
    if (ahead <= 0.0f) return facing;                // already level with the object
    return facing * exp(-(ahead / max(direction.z, 1.0e-3f)) / 3.0f);
}

// Small wind ripples: a few directional waves of decreasing wavelength. Returns the height
// gradient (dh/dx, dh/dz) in world units; octaves shorter than a few pixels fade out so the
// pattern never aliases at a grazing angle.
float2 RippleGradient(float2 xz, float time, float metresPerPixel)
{
    float2 gradient = float2(0.0f, 0.0f);
    const float slope = RippleParams.x;
    const float longest = max(RippleParams.y, 0.01f);
    const float speed = RippleParams.z;
    [unroll]
    for (int k = 0; k < 5; ++k) {
        const float fk = float(k);
        const float wavelength = longest * pow(0.62f, fk);
        // Octaves need about six pixels per wavelength; below that they only add glitter.
        const float resolve = saturate(wavelength / max(metresPerPixel * 6.0f, 1.0e-5f) - 1.0f);
        const float angle = 0.9f + fk * 2.39996f;   // golden-angle spread of directions
        const float2 direction = float2(cos(angle), sin(angle));
        const float waveNumber = 6.2831853f / wavelength;
        // Deep-water dispersion: phase speed sqrt(g / k).
        const float omega = sqrt(9.81f * waveNumber) * speed;
        const float phase = dot(direction, xz) * waveNumber + omega * time + fk * 1.7f;
        gradient += direction * (slope * pow(0.8f, fk) * resolve * cos(phase));
    }
    return gradient;
}

// Caustic web on the submerged floor: two drifting layers of ridged sine interference whose
// product leaves thin bright filaments where the ridges cross.
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

    const float2 size = float2(float(OutputWidth), float(OutputHeight));
    const float2 pixelCentre = float2(id.xy) + 0.5f;
    const int2 base = int2(id.xy);
    const float particleRadius = max(ShadeParams.y, 1.0e-3f);
    const float time = ShadeParams.z;

    // ---- Geometry ------------------------------------------------------------------------
    const int stencil = clamp(int(NormalStep + 0.5f), 1, 8);
    const float3 p = ViewPosition(pixelCentre, z);
    const float edge = 2.0f * particleRadius * float(stencil);
    float3 tangentX = Tangent(base, p, z, int2(stencil, 0), edge);
    float3 tangentY = Tangent(base, p, z, int2(0, stencil), edge);
    tangentX.xy *= max(NormalScale, 0.0f);
    tangentY.xy *= max(NormalScale, 0.0f);
    if (length(tangentX) < 1e-6f) tangentX = float3(1.0f, 0.0f, 0.0f);
    if (length(tangentY) < 1e-6f) tangentY = float3(0.0f, -1.0f, 0.0f);

    const float3 view = normalize(p);          // camera -> surface
    const float3 toCamera = -view;
    float3 macroNormal = normalize(cross(tangentX, tangentY));
    if (dot(macroNormal, toCamera) < 0.0f) macroNormal = -macroNormal;

    // World-space ripples on water that faces up; splash sheets and droplets keep their shape.
    const float3 surfaceWorld = CameraPosition() + ToWorld(p);
    const float3 macroWorld = ToWorld(macroNormal);
    const float metresPerPixel = z / Focal() / max(abs(dot(macroNormal, toCamera)), 0.08f);
    const float rippleWeight = smoothstep(0.75f, 0.95f, macroWorld.y) *
                               saturate(1.0f - length(p) / max(RippleParams.w, 0.01f));
    float3 normal = macroNormal;
    if (rippleWeight > 0.0f && RippleParams.x > 0.0f) {
        const float2 g = RippleGradient(surfaceWorld.xz, time, metresPerPixel) * rippleWeight;
        normal = normalize(ToView(normalize(macroWorld + float3(-g.x, 0.0f, -g.y))));
        if (dot(normal, toCamera) < 0.02f) normal = macroNormal;
    }

    if (DebugView == 1u) { OutputTexture[imagePixel] = float4(normal * 0.5f + 0.5f, 1.0f); return; }
    if (DebugView == 2u) { OutputTexture[imagePixel] = float4(saturate(z / 12.0f).xxx, 1.0f); return; }

    // ---- Water column and refraction -----------------------------------------------------
    // Metres from the surface to the opaque scene behind it, along the view ray.
    const float sceneBehind = LinearSceneDepth(base);
    const float gap = sceneBehind < 1.0e8f ? max(sceneBehind - z, 0.0f) / max(view.z, 0.05f) : 1.0e4f;
    const float chord = max(thicknessData.r, 0.0f) * max(ThicknessScale, 0.0f);
    // Under the pool surface the particles fill the gap and the geometric gap is the exact,
    // noise-free path. Water in the air (a splash, the falling sphere) is thinner than the gap
    // to the scene behind it, so its own chord sum is the path.
    const float filled = smoothstep(0.45f, 0.8f, chord / max(gap, 1.0e-3f));
    const float waterPath = lerp(chord, min(gap, chord * 1.5f + particleRadius), filled);

    float3 transmitted = refract(view, normal, 1.0f / 1.33f);
    if (dot(transmitted, transmitted) < 1e-6f) transmitted = view;
    // Follow the refracted ray as far as the water column reaches (capped), then look up the
    // scene there. Bent toward -n, a flat pool shows its floor displaced toward the viewer.
    const float travel = min(min(waterPath, gap), max(MaxTravel, 0.0f));
    const float3 bent = normalize(lerp(view, transmitted, saturate(Refraction)));
    const float3 refractEnd = p + bent * travel;
    // The lookup must land on geometry that is under the water at that pixel. The pool floor
    // nearer the camera is legitimate (it is behind its own surface even though it is nearer
    // than this pixel's surface); the coping, the deck or anything in front of the water is
    // not. Pull the offset back toward the pixel until it lands under water.
    float2 offset = (refractEnd.z > 0.05f ? ProjectToPixel(refractEnd) : pixelCentre) - pixelCentre;
    float2 refractPixel = pixelCentre;
    float refractedSceneZ = sceneBehind;
    [loop]
    for (int attempt = 0; attempt < 4; ++attempt) {
        const float2 candidate = clamp(pixelCentre + offset, float2(0.5f, 0.5f), size - 0.5f);
        // Both corners of the bilinear footprint, or the filter blends in the coping lip.
        if (UnderWater(int2(candidate - 0.5f)) && UnderWater(int2(candidate + 0.5f))) {
            refractPixel = candidate;
            refractedSceneZ = LinearSceneDepth(int2(candidate));
            break;
        }
        offset *= 0.5f;
    }
    float3 background = SceneColor.SampleLevel(LinearSampler, T850_UV(refractPixel / size), 0.0f).rgb;

    // Caustics on what the refracted ray lands on, when that is under the water surface.
    if (CausticParams.x > 0.0f && refractedSceneZ < 1.0e8f) {
        const float3 floorWorld = CameraPosition() + ToWorld(ViewPosition(refractPixel, refractedSceneZ));
        const float depthBelow = surfaceWorld.y - floorWorld.y;
        if (depthBelow > 0.02f) {
            // Project along the sun so the pattern sits where the light actually lands.
            const float3 sunWorld = ToWorld(normalize(SunDirection.xyz));
            const float2 shift = sunWorld.y > 0.1f ? sunWorld.xz / sunWorld.y * depthBelow : float2(0.0f, 0.0f);
            const float caustic = CausticPattern(floorWorld.xz + shift, time);
            const float fade = exp(-depthBelow / max(CausticParams.w, 0.05f)) * smoothstep(0.02f, 0.15f, depthBelow);
            const float sunLevel = saturate(dot(SunColor.rgb, float3(0.2126f, 0.7152f, 0.0722f)) * 0.5f);
            background *= 1.0f + CausticParams.x * fade * sunLevel * (caustic * 2.2f - 0.25f);
        }
    }

    // Beer-Lambert absorption; the absorbed light is replaced by the water's scattered colour.
    const float3 transmittance = exp(-waterPath * max(FluidTint.rgb, 0.0f));
    const float3 refracted = background * transmittance + (1.0f - transmittance) * FluidColor.rgb;

    if (DebugView == 3u) { OutputTexture[imagePixel] = float4(saturate(waterPath / 4.0f).xxx, 1.0f); return; }
    if (DebugView == 4u) { OutputTexture[imagePixel] = float4(thicknessData.a.xxx, 1.0f); return; }
    if (DebugView == 6u) { OutputTexture[imagePixel] = float4(refracted, 1.0f); return; }

    // ---- Reflection ----------------------------------------------------------------------
    const float3 reflectedView = reflect(view, normal);
    const float3 reflectedWorld = ToWorld(reflectedView);
    float3 hitColor;
    const float hitWeight = TraceReflection(p, reflectedView, hitColor);
    const float3 reflected = lerp(SkyGradient(reflectedWorld), hitColor, hitWeight);
    if (DebugView == 5u) { OutputTexture[imagePixel] = float4(reflected, 1.0f); return; }

    // Fresnel from a normal half-way between the surface and the ripples: a ripple facet must
    // not flip a grazing pixel to total reflection, but the ripples should still modulate it.
    const float3 fresnelNormal = normalize(macroNormal + normal);
    const float cosine = saturate(dot(fresnelNormal, toCamera));
    const float fresnel = 0.02f + 0.98f * pow(1.0f - cosine, max(FresnelPower, 1.0f));
    float3 color = lerp(refracted, reflected, saturate(fresnel));

    // Sun glint: normalised Blinn-Phong, so a sharper lobe is brighter, not dimmer.
    const float3 sun = normalize(SunDirection.xyz);
    const float3 halfVector = normalize(sun + toCamera);
    const float exponent = max(ShadeParams.w, 8.0f);
    const float glint = pow(saturate(dot(normal, halfVector)), exponent) * (exponent + 8.0f) / 25.1327f;
    color += SunColor.rgb * glint * Specular * fresnel * saturate(dot(normal, sun) * 4.0f);

    // Spray: a droplet is a pixel whose surroundings 2.5 particle radii out are empty or far
    // behind it. Splashing water reads white (aerated, scattering), so isolated droplets fade
    // toward a lit whitewater colour instead of drawing as clear refractive beads.
    const float particlePx = particleRadius * Focal() / z;
    const float ringPx = clamp(2.5f * particlePx, 2.0f, 48.0f);
    const float layerGap = 4.0f * particleRadius;
    float emptyCount = 0.0f;
    [unroll]
    for (int k = 0; k < 8; ++k) {
        const float angle = float(k) * 0.7853982f;
        const int2 probe = base + int2(round(float2(cos(angle), sin(angle)) * ringPx));
        const float probeDepth = LoadDepth(probe);
        emptyCount += (probeDepth <= 0.0f || probeDepth > z + layerGap) ? 1.0f : 0.0f;
    }
    const float spray = saturate((emptyCount - 5.0f) * 0.5f) * saturate(Foam);
    const float3 whitewater = (SkyGradient(float3(0.0f, 0.5f, 0.0f)) + SunColor.rgb * 0.15f) * 0.85f;
    color = lerp(color, whitewater, spray);

    // The colour already contains the scene behind the water, so the composite must not
    // blend it in again: alpha is the anti-aliased silhouette, lowered only for spray.
    OutputTexture[imagePixel] = float4(color, saturate(thicknessData.a) * (1.0f - 0.35f * spray));
}
