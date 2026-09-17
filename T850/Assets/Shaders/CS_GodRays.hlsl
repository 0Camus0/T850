#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer GodRaysConstants : register(b0)
{
    float4x4 WVPInverse;
    float4x4 WVPLight;
    float4 CameraPosition;
    float4 SunDirectionAndBias;
    float4 VolumeCenterAndEnabled;
    float4 VolumeHalfExtentsAndFactor;
    float4 OutputSizeStepsAndEnabled;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture2D<float> SceneDepth : register(t0);
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
Texture2D<float> ShadowDepth : register(t1);
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
SamplerState SceneDepthSampler : register(s0);
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
SamplerState ShadowDepthSampler : register(s1);
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba8]]
#endif
RWTexture2D<float4> Output : register(u0);

static const float DepthClearEpsilon = 0.0001f;
static const float GScattering = -0.2f;
static const float Pi = 3.14159265359f;

float ComputeScattering(float lightDotView)
{
    float result = 1.0f - GScattering * GScattering;
    result /= 4.0f * Pi * pow(
        1.0f + GScattering * GScattering - (2.0f * GScattering) * lightDotView,
        1.5f);
    return result;
}

bool IntersectGodRaysBox(
    float3 origin,
    float3 direction,
    float3 boxMin,
    float3 boxMax,
    out float tNear,
    out float tFar)
{
    float3 safeDirection = direction;
    safeDirection.x = abs(safeDirection.x) < 1e-6f
        ? (safeDirection.x < 0.0f ? -1e-6f : 1e-6f)
        : safeDirection.x;
    safeDirection.y = abs(safeDirection.y) < 1e-6f
        ? (safeDirection.y < 0.0f ? -1e-6f : 1e-6f)
        : safeDirection.y;
    safeDirection.z = abs(safeDirection.z) < 1e-6f
        ? (safeDirection.z < 0.0f ? -1e-6f : 1e-6f)
        : safeDirection.z;

    float3 t0 = (boxMin - origin) / safeDirection;
    float3 t1 = (boxMax - origin) / safeDirection;
    float3 tMin = min(t0, t1);
    float3 tMax = max(t0, t1);
    tNear = max(max(tMin.x, tMin.y), tMin.z);
    tFar = min(min(tMax.x, tMax.y), tMax.z);
    return tFar >= max(tNear, 0.0f);
}

[numthreads(8, 8, 1)]
void CS(uint3 dispatchId : SV_DispatchThreadID)
{
    const uint width = (uint)OutputSizeStepsAndEnabled.x;
    const uint height = (uint)OutputSizeStepsAndEnabled.y;
    if (dispatchId.x >= width || dispatchId.y >= height)
        return;

    if (OutputSizeStepsAndEnabled.w < 0.5f) {
        Output[dispatchId.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    const float2 uv = (float2(dispatchId.xy) + 0.5f) / float2(width, height);
    const float depth = SceneDepth.SampleLevel(SceneDepthSampler, uv, 0.0f);
    if (depth <= DepthClearEpsilon) {
        Output[dispatchId.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    const float2 clipPos = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 position = mul(WVPInverse, float4(clipPos, depth, 1.0f));
    position.xyz /= position.w;
    position.w = 1.0f;

    const int steps = max((int)OutputSizeStepsAndEnabled.z, 2);
    const float4 ray = position - CameraPosition;
    const float4 rayDir = normalize(ray);
    const float rayLength = length(ray.xyz);

    float4 intersectionNear = CameraPosition;
    float4 intersectionFar = position;
    if (VolumeCenterAndEnabled.w > 0.5f) {
        const float3 volumeHalfExtents = max(
            abs(VolumeHalfExtentsAndFactor.xyz),
            float3(0.001f, 0.001f, 0.001f));
        float tNear;
        float tFar;
        if (!IntersectGodRaysBox(
                CameraPosition.xyz,
                rayDir.xyz,
                VolumeCenterAndEnabled.xyz - volumeHalfExtents,
                VolumeCenterAndEnabled.xyz + volumeHalfExtents,
                tNear,
                tFar)) {
            Output[dispatchId.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        }
        tNear = clamp(tNear, 0.0f, rayLength);
        tFar = clamp(tFar, 0.0f, rayLength);
        if (tFar <= tNear) {
            Output[dispatchId.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        }
        intersectionNear = CameraPosition + rayDir * tNear;
        intersectionFar = CameraPosition + rayDir * tFar;
    }

    float3 accumulatedFog = 0.0f.xxx;
    const float3 lightColor = float3(0.9803f, 0.8392f, 0.6470f);
    const float3 sunDirection = normalize(SunDirectionAndBias.xyz);
    const float shadowBias = max(SunDirectionAndBias.w, 0.0f);

    [loop]
    for (int i = 0; i < steps; ++i) {
        const float rayT = (float)i / (float)(steps - 1);
        const float4 worldPosition = lerp(intersectionFar, intersectionNear, rayT);
        float4 lightPosition = mul(WVPLight, worldPosition);
        lightPosition.xyz /= lightPosition.w;
        float2 shadowUV = lightPosition.xy * 0.5f + 0.5f;
        shadowUV.y = 1.0f - shadowUV.y;

        if (shadowUV.x < 1.0f && shadowUV.y < 1.0f &&
            shadowUV.x > 0.0f && shadowUV.y > 0.0f &&
            lightPosition.z > 0.0f && lightPosition.z < 1.0f) {
            float shadowDepth = ShadowDepth.SampleLevel(ShadowDepthSampler, shadowUV, 0.0f);
            shadowDepth -= shadowBias;
            if (lightPosition.z >= shadowDepth) {
                accumulatedFog += lightColor * ComputeScattering(dot(rayDir.rgb, sunDirection));
            }
        }
    }

    accumulatedFog /= (float)steps;
    accumulatedFog = pow(accumulatedFog, float3(0.4545f, 0.4545f, 0.4545f));
    accumulatedFog *= VolumeHalfExtentsAndFactor.w;
    Output[dispatchId.xy] = float4(accumulatedFog, 1.0f);
}
