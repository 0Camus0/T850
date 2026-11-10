#version 430
// GENERATED from CS_ParticleSmooth.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=0a1a48091a592d951fe3e9abd3db14de76b52a3702fb07406751972253aefb85; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
    float Spacing;
    float WorldRadius;
    float Strength;
    float Tolerance;
    float RadiusPx;
    float ProjScaleY;
} _37;

layout(binding = 2, r32f) uniform writeonly image2D Destination;
layout(binding = 1) uniform sampler2D SPIRV_Cross_CombinedSourceSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if ((gl_GlobalInvocationID.x >= _37.OutputWidth) || (gl_GlobalInvocationID.y >= _37.OutputHeight))
        {
            break;
        }
        uvec2 _368 = uvec2(gl_GlobalInvocationID.x, (_37.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        ivec2 _371 = ivec2(gl_GlobalInvocationID.xy);
        float _650;
        do
        {
            if ((((_371.x < 0) || (_371.y < 0)) || (_371.x >= int(_37.OutputWidth))) || (_371.y >= int(_37.OutputHeight)))
            {
                _650 = 0.0;
                break;
            }
            _650 = texelFetch(SPIRV_Cross_CombinedSourceSPIRV_Cross_DummySampler, ivec3(_371.x, int((_37.OutputHeight - 1u) - uint(_371.y)), 0).xy, 0).x;
            break;
        } while(false);
        if (((_650 <= 0.0) || (_37.Strength <= 0.0)) || (_37.Spacing <= 0.0))
        {
            imageStore(Destination, ivec2(_368), vec4(_650));
            break;
        }
        if ((_37.Spacing > (0.5 * clamp((_37.WorldRadius * ((0.5 * float(_37.OutputHeight)) * max(_37.ProjScaleY, 9.9999997473787516355514526367188e-05))) / _650, 1.0, max(_37.RadiusPx, 1.0)))) && (_37.Spacing > 1.0))
        {
            imageStore(Destination, ivec2(_368), vec4(_650));
            break;
        }
        int _424 = int(_37.Spacing + 0.5);
        float _427 = max(_37.Tolerance, 9.9999997473787516355514526367188e-05);
        float _431 = (-2.0) / (_427 * _427);
        float _652;
        float _653;
        _653 = 0.0;
        _652 = 0.0;
        float _658;
        float _660;
        SPIRV_CROSS_UNROLL
        for (int _651 = -2; _651 <= 2; _653 = _660, _652 = _658, _651++)
        {
            float _443 = (_651 == 0) ? 0.375 : ((abs(_651) == 1) ? 0.25 : 0.0625);
            _660 = _653;
            _658 = _652;
            float _665;
            float _666;
            SPIRV_CROSS_UNROLL
            for (int _654 = -2; _654 <= 2; _660 = _666, _658 = _665, _654++)
            {
                ivec2 _463 = _371 + (ivec2(_654, _651) * ivec2(_424));
                float _656;
                do
                {
                    if ((((_463.x < 0) || (_463.y < 0)) || (_463.x >= int(_37.OutputWidth))) || (_463.y >= int(_37.OutputHeight)))
                    {
                        _656 = 0.0;
                        break;
                    }
                    _656 = texelFetch(SPIRV_Cross_CombinedSourceSPIRV_Cross_DummySampler, ivec3(_463.x, int((_37.OutputHeight - 1u) - uint(_463.y)), 0).xy, 0).x;
                    break;
                } while(false);
                float _467 = _656 - _650;
                if ((_656 <= 0.0) || (abs(_467) > _427))
                {
                    _666 = _660;
                    _665 = _658;
                    continue;
                }
                float _486 = (((_654 == 0) ? 0.375 : ((abs(_654) == 1) ? 0.25 : 0.0625)) * _443) * exp((_467 * _467) * _431);
                _666 = _660 + _486;
                _665 = _658 + (_656 * _486);
            }
        }
        imageStore(Destination, ivec2(_368), vec4(mix(_650, _652 / max(_653, 9.9999999747524270787835121154785e-07), clamp(_37.Strength, 0.0, 1.0))));
        break;
    } while(false);
}

