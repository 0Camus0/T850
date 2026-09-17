#version 430 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(std140, binding = 0) uniform GodRaysConstants {
    mat4 WVPInverse;
    mat4 WVPLight;
    vec4 CameraPosition;
    vec4 SunDirectionAndBias;
    vec4 VolumeCenterAndEnabled;
    vec4 VolumeHalfExtentsAndFactor;
    vec4 OutputSizeStepsAndEnabled;
};

layout(binding = 1) uniform sampler2D SceneDepth;
layout(binding = 2) uniform sampler2DShadow ShadowDepth;
layout(rgba8, binding = 5) writeonly uniform image2D Output;

const float DepthClearEpsilon = 0.0001;
const float GScattering = -0.2;
const float Pi = 3.14159265359;

float ComputeScattering(float lightDotView)
{
    float result = 1.0 - GScattering * GScattering;
    return result / (4.0 * Pi * pow(
        1.0 + GScattering * GScattering - (2.0 * GScattering) * lightDotView, 1.5));
}

bool IntersectGodRaysBox(vec3 origin, vec3 direction, vec3 boxMin, vec3 boxMax,
                         out float tNear, out float tFar)
{
    vec3 safeDirection = direction;
    safeDirection.x = abs(safeDirection.x) < 1e-6 ? (safeDirection.x < 0.0 ? -1e-6 : 1e-6) : safeDirection.x;
    safeDirection.y = abs(safeDirection.y) < 1e-6 ? (safeDirection.y < 0.0 ? -1e-6 : 1e-6) : safeDirection.y;
    safeDirection.z = abs(safeDirection.z) < 1e-6 ? (safeDirection.z < 0.0 ? -1e-6 : 1e-6) : safeDirection.z;
    vec3 t0 = (boxMin - origin) / safeDirection;
    vec3 t1 = (boxMax - origin) / safeDirection;
    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);
    tNear = max(max(tMin.x, tMin.y), tMin.z);
    tFar = min(min(tMax.x, tMax.y), tMax.z);
    return tFar >= max(tNear, 0.0);
}

void main()
{
    uvec2 dispatchId = gl_GlobalInvocationID.xy;
    uint width = uint(OutputSizeStepsAndEnabled.x);
    uint height = uint(OutputSizeStepsAndEnabled.y);
    if (dispatchId.x >= width || dispatchId.y >= height)
        return;
    if (OutputSizeStepsAndEnabled.w < 0.5) {
        imageStore(Output, ivec2(dispatchId), vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }

    vec2 uv = (vec2(dispatchId) + vec2(0.5)) / vec2(width, height);
    float depth = textureLod(SceneDepth, uv, 0.0).r;
    if (depth <= DepthClearEpsilon) {
        imageStore(Output, ivec2(dispatchId), vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }

    vec2 clipPos = uv * 2.0 - 1.0;
    vec4 position = WVPInverse * vec4(clipPos, depth, 1.0);
    position.xyz /= position.w;
    position.w = 1.0;
    int steps = max(int(OutputSizeStepsAndEnabled.z), 2);
    vec4 ray = position - CameraPosition;
    vec4 rayDir = normalize(ray);
    float rayLength = length(ray.xyz);

    vec4 intersectionNear = CameraPosition;
    vec4 intersectionFar = position;
    if (VolumeCenterAndEnabled.w > 0.5) {
        vec3 halfExtents = max(abs(VolumeHalfExtentsAndFactor.xyz), vec3(0.001));
        float tNear;
        float tFar;
        if (!IntersectGodRaysBox(CameraPosition.xyz, rayDir.xyz,
                VolumeCenterAndEnabled.xyz - halfExtents,
                VolumeCenterAndEnabled.xyz + halfExtents, tNear, tFar)) {
            imageStore(Output, ivec2(dispatchId), vec4(0.0, 0.0, 0.0, 1.0));
            return;
        }
        tNear = clamp(tNear, 0.0, rayLength);
        tFar = clamp(tFar, 0.0, rayLength);
        if (tFar <= tNear) {
            imageStore(Output, ivec2(dispatchId), vec4(0.0, 0.0, 0.0, 1.0));
            return;
        }
        intersectionNear = CameraPosition + rayDir * tNear;
        intersectionFar = CameraPosition + rayDir * tFar;
    }

    vec3 accumulatedFog = vec3(0.0);
    vec3 lightColor = vec3(0.9803, 0.8392, 0.6470);
    vec3 sunDirection = normalize(SunDirectionAndBias.xyz);
    float shadowBias = max(SunDirectionAndBias.w, 0.0);
    for (int index = 0; index < steps; ++index) {
        float rayT = float(index) / float(steps - 1);
        vec4 worldPosition = mix(intersectionFar, intersectionNear, rayT);
        vec4 lightPosition = WVPLight * worldPosition;
        lightPosition.xyz /= lightPosition.w;
        vec2 shadowUV = lightPosition.xy * 0.5 + 0.5;
        if (shadowUV.x < 1.0 && shadowUV.y < 1.0 && shadowUV.x > 0.0 && shadowUV.y > 0.0 &&
            lightPosition.z > 0.0 && lightPosition.z < 1.0) {
            float visible = texture(ShadowDepth, vec3(shadowUV, lightPosition.z + shadowBias));
            if (visible > 0.0)
                accumulatedFog += lightColor * ComputeScattering(dot(rayDir.rgb, sunDirection));
        }
    }
    accumulatedFog = pow(accumulatedFog / float(steps), vec3(0.4545));
    accumulatedFog *= VolumeHalfExtentsAndFactor.w;
    imageStore(Output, ivec2(dispatchId), vec4(accumulatedFog, 1.0));
}