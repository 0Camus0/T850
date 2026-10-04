#version 430 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(std140, binding = 0) uniform PostProcessConstants {
    vec4 OutputSizeAndParams0;
    vec4 Params1;
};

layout(binding = 1) uniform sampler2D HdrTexture;
layout(binding = 2) uniform sampler2D BloomTexture;
layout(binding = 3) uniform sampler2D AdaptedLuminanceTexture;
layout(rgba8, binding = 7) writeonly uniform image2D OutputTexture;

void main()
{
    uvec2 dispatchId = gl_GlobalInvocationID.xy;
    uvec2 outputSize = uvec2(OutputSizeAndParams0.xy);
    if (any(greaterThanEqual(dispatchId, outputSize)))
        return;

    vec2 uv = (vec2(dispatchId) + vec2(0.5)) / vec2(outputSize);
    vec3 hdrColor = textureLod(HdrTexture, uv, 0.0).rgb;
    float avgLuminance = exp(textureLod(AdaptedLuminanceTexture, vec2(0.5), 0.0).r);
    avgLuminance = max(avgLuminance, 0.001);
    float keyValue = 1.03 - (2.0 / (2.0 + log(avgLuminance + 1.0) / log(10.0)));
    float linearExposure = keyValue / avgLuminance;
    vec3 color = exp2(log2(max(linearExposure, 0.0001)) + OutputSizeAndParams0.w) * hdrColor;

    float pixelLuminance = max(dot(color, vec3(0.299, 0.587, 0.114)), 0.0001);
    float whiteLevel = max(Params1.x, 0.001);
    float toneMappedLuminance = pixelLuminance *
        (1.0 + pixelLuminance / (whiteLevel * whiteLevel)) / (1.0 + pixelLuminance);
    vec3 toneMapped = toneMappedLuminance * (color / pixelLuminance);
    vec3 bloom = textureLod(BloomTexture, uv, 0.0).rgb;
    imageStore(OutputTexture, ivec2(dispatchId), vec4(toneMapped + OutputSizeAndParams0.z * bloom, 1.0));
}