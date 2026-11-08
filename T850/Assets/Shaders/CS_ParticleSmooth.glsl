#version 430
// GENERATED from CS_ParticleSmooth.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=ef67454e044c608a1805f46affaaa7c802c7588549163252d98c1669c7971480; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
#if defined(GL_EXT_control_flow_attributes)
#extension GL_EXT_control_flow_attributes : require
#define SPIRV_CROSS_FLATTEN [[flatten]]
#define SPIRV_CROSS_BRANCH [[dont_flatten]]
#define SPIRV_CROSS_UNROLL [[unroll]]
#define SPIRV_CROSS_LOOP [[dont_unroll]]
#else
#define SPIRV_CROSS_FLATTEN
#define SPIRV_CROSS_BRANCH
#define SPIRV_CROSS_UNROLL
#define SPIRV_CROSS_LOOP
#endif
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, std140) uniform ParticleSmoothConstants
{
    uint OutputWidth;
    uint OutputHeight;
    uint Stage;
    uint Pad0;
    float Strength;
    float Tolerance;
    float RadiusPx;
    float Pad1;
} _28;

layout(binding = 2, rgba16f) uniform writeonly image2D Destination;
layout(binding = 1) uniform sampler2D SPIRV_Cross_CombinedSourceSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if ((gl_GlobalInvocationID.x >= _28.OutputWidth) || (gl_GlobalInvocationID.y >= _28.OutputHeight))
        {
            break;
        }
        uvec2 _321 = uvec2(gl_GlobalInvocationID.x, (_28.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        ivec2 _455 = clamp(ivec2(gl_GlobalInvocationID.xy), ivec2(0), ivec2(int(_28.OutputWidth) - 1, int(_28.OutputHeight) - 1));
        vec4 _481 = texelFetch(SPIRV_Cross_CombinedSourceSPIRV_Cross_DummySampler, ivec3(_455.x, int((_28.OutputHeight - 1u) - uint(_455.y)), 0).xy, 0);
        if ((_28.Strength <= 0.0) || (_481.x <= 0.0))
        {
            imageStore(Destination, ivec2(_321), _481);
            break;
        }
        ivec2 _340 = ivec2(gl_GlobalInvocationID.xy);
        float _342 = _481.x;
        int _347 = clamp(int(_28.RadiusPx + 0.5), 1, 16);
        int _349 = -_347;
        float _545;
        float _546;
        _546 = 0.0;
        _545 = 0.0;
        float _550;
        float _551;
        SPIRV_CROSS_LOOP
        for (int _544 = _349; _544 <= _347; _546 = _550, _545 = _551, _544++)
        {
            int _357 = -_347;
            _551 = _545;
            _550 = _546;
            float _554;
            float _555;
            SPIRV_CROSS_LOOP
            for (int _548 = _357; _548 <= _347; _551 = _555, _550 = _554, _548++)
            {
                ivec2 _495 = clamp(_340 + ivec2(_548, _544), ivec2(0), ivec2(int(_28.OutputWidth) - 1, int(_28.OutputHeight) - 1));
                vec4 _521 = texelFetch(SPIRV_Cross_CombinedSourceSPIRV_Cross_DummySampler, ivec3(_495.x, int((_28.OutputHeight - 1u) - uint(_495.y)), 0).xy, 0);
                if ((_521.x <= 0.0) || (abs(_521.x - _342) > _28.Tolerance))
                {
                    _555 = _551;
                    _554 = _550;
                    continue;
                }
                float _399 = max(0.0, 1.0 - (sqrt(float((_548 * _548) + (_544 * _544))) / float(_347 + 1)));
                _555 = _551 + _399;
                _554 = _550 + (_521.x * _399);
            }
        }
        float _547;
        if (_545 > 9.9999997473787516355514526367188e-06)
        {
            _547 = mix(_342, _546 / _545, clamp(_28.Strength, 0.0, 1.0));
        }
        else
        {
            _547 = _342;
        }
        imageStore(Destination, ivec2(_321), vec4(_547, _481.yzw));
        break;
    } while(false);
}

