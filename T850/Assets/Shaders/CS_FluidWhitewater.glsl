#version 430
// GENERATED from CS_FluidWhitewater.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=41ea548ca30fe9bceab7a8d01c0199e36b8db7d1624bc04830330239270c4eb4; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(binding = 0, std140) uniform WhitewaterSplatConstants
{
    uint OutputWidth;
    uint OutputHeight;
    uint DiffuseCapacity;
    uint Enabled;
    float ProjScaleX;
    float ProjScaleY;
    float ProjA;
    float ProjB;
    float SprayRadius;
    float FoamRadius;
    float BubbleRadius;
    float WaterAbsorption;
    vec4 ViewProjection[4];
} _47;

layout(binding = 1, std430) readonly buffer Diffuse
{
    vec4 _data[];
} Diffuse_1;

layout(binding = 4, std430) buffer WhitewaterBits
{
    uint _data[];
} WhitewaterBits_1;

layout(binding = 3) uniform sampler2D SPIRV_Cross_CombinedFluidDepthSPIRV_Cross_DummySampler;
layout(binding = 2) uniform sampler2D SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if ((_47.Enabled == 0u) || (gl_GlobalInvocationID.x >= _47.DiffuseCapacity))
        {
            break;
        }
        uint _640 = gl_GlobalInvocationID.x * 2u;
        vec4 _643 = Diffuse_1._data[_640];
        if (_643.w <= 0.0)
        {
            break;
        }
        uint _651 = (gl_GlobalInvocationID.x * 2u) + 1u;
        float _653 = Diffuse_1._data[_651].w;
        vec4 _959 = (((_47.ViewProjection[0] * _643.x) + (_47.ViewProjection[1] * _643.y)) + (_47.ViewProjection[2] * _643.z)) + _47.ViewProjection[3];
        float _973 = _959.w;
        if (_973 < 0.100000001490116119384765625)
        {
            break;
        }
        float _677 = (0.5 * float(_47.OutputHeight)) * max(_47.ProjScaleY, 9.9999997473787516355514526367188e-05);
        vec2 _702 = vec2(((_959.x / max(_47.ProjScaleX, 9.9999999747524270787835121154785e-07)) * ((0.5 * float(_47.OutputWidth)) * max(_47.ProjScaleX, 9.9999997473787516355514526367188e-05))) / _973, ((-(_959.y / max(_47.ProjScaleY, 9.9999999747524270787835121154785e-07))) * _677) / _973) + (vec2(float(_47.OutputWidth), float(_47.OutputHeight)) * 0.5);
        float _720 = (((_653 < 0.5) ? _47.SprayRadius : ((_653 < 1.5) ? _47.FoamRadius : _47.BubbleRadius)) * _677) / _973;
        float _726 = clamp(((_653 < 0.5) ? 2.5 : 3.0) * _720, 1.5, 12.0);
        float _739 = clamp((_720 * _720) / (_726 * _726), 0.0, 1.0) * clamp(_643.w * 2.5, 0.0, 1.0);
        if (_739 <= 0.001000000047497451305389404296875)
        {
            break;
        }
        uvec2 _745 = uvec2(textureSize(SPIRV_Cross_CombinedFluidDepthSPIRV_Cross_DummySampler, 0));
        uint _750 = _745.y;
        ivec2 _758 = max(ivec2(floor(_702 - vec2(_726))), ivec2(0));
        ivec2 _774 = min(ivec2(ceil(_702 + vec2(_726))), ivec2(int(_47.OutputWidth) - 1, int(_47.OutputHeight) - 1));
        float _777 = _726 * _726;
        int _779 = _758.y;
        for (int _1108 = _779; _1108 <= _774.y; _1108++)
        {
            int _788 = _758.x;
            for (int _1109 = _788; _1109 <= _774.x; _1109++)
            {
                vec2 _804 = vec2(float(_1109) + 0.5, float(_1108) + 0.5) - _702;
                float _807 = dot(_804, _804);
                if (_807 > _777)
                {
                    continue;
                }
                vec4 _999 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_1109, int((_47.OutputHeight - 1u) - uint(_1108)), 0).xy, 0);
                float _1000 = _999.x;
                if (_973 >= ((((_1000 > 0.0) && (abs(_1000 - _47.ProjA) > 9.999999717180685365747194737196e-10)) ? (_47.ProjB / (_1000 - _47.ProjA)) : 1000000015047466219876688855040.0) - 0.00999999977648258209228515625))
                {
                    continue;
                }
                float _833 = ((1.0 - (_807 / _777)) * (1.0 - (_807 / _777))) * _739;
                ivec2 _1042 = min(ivec2((vec2(float(_1109), float(_1108)) * vec2(_745)) / vec2(float(_47.OutputWidth), float(_47.OutputHeight))), (ivec2(_745) - ivec2(1)));
                vec4 _1068 = texelFetch(SPIRV_Cross_CombinedFluidDepthSPIRV_Cross_DummySampler, ivec3(_1042.x, int((_750 - 1u) - uint(_1042.y)), 0).xy, 0);
                float _1069 = _1068.x;
                uint _846 = (uint(_1109) + (uint(_1108) * _47.OutputWidth)) * 4u;
                if (_653 < 0.5)
                {
                    if ((_1069 <= 0.0) || (_973 < (_1069 + 0.0199999995529651641845703125)))
                    {
                        uint _865 = atomicAdd(WhitewaterBits_1._data[_846], uint(_833 * 4096.0));
                    }
                }
                else
                {
                    if (_653 < 1.5)
                    {
                        if ((_1069 <= 0.0) || (abs(_973 - _1069) < 0.4000000059604644775390625))
                        {
                            uint _887 = atomicAdd(WhitewaterBits_1._data[_846 + 1u], uint(_833 * 4096.0));
                        }
                    }
                    else
                    {
                        if ((_1069 > 0.0) && (_973 > _1069))
                        {
                            float _909 = _833 * exp((-(_973 - _1069)) * (_47.WaterAbsorption + 1.5));
                            uint _916 = atomicAdd(WhitewaterBits_1._data[_846 + 2u], uint(_909 * 4096.0));
                            uint _924 = atomicMax(WhitewaterBits_1._data[_846 + 3u], uint(clamp(_909, 0.0, 1.0) * 65535.0));
                        }
                    }
                }
            }
        }
        break;
    } while(false);
}

