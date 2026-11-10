#version 430
// GENERATED from CS_ParticleSmooth.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=b39927e98f05d9c174f9768c856b5aba4960f71de3c030a2cf9ab3fbe47d1507; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
        uvec2 _570 = uvec2(gl_GlobalInvocationID.x, (_37.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        ivec2 _573 = ivec2(gl_GlobalInvocationID.xy);
        float _1334;
        do
        {
            if ((((_573.x < 0) || (_573.y < 0)) || (_573.x >= int(_37.OutputWidth))) || (_573.y >= int(_37.OutputHeight)))
            {
                _1334 = 0.0;
                break;
            }
            _1334 = texelFetch(SPIRV_Cross_CombinedSourceSPIRV_Cross_DummySampler, ivec3(_573.x, int((_37.OutputHeight - 1u) - uint(_573.y)), 0).xy, 0).x;
            break;
        } while(false);
        if (((_1334 <= 0.0) || (_37.Strength <= 0.0)) || (_37.Spacing <= 0.0))
        {
            imageStore(Destination, ivec2(_570), vec4(_1334));
            break;
        }
        if ((_37.Spacing > (0.5 * clamp((_37.WorldRadius * ((0.5 * float(_37.OutputHeight)) * max(_37.ProjScaleY, 9.9999997473787516355514526367188e-05))) / _1334, 1.0, max(_37.RadiusPx, 1.0)))) && (_37.Spacing > 1.0))
        {
            imageStore(Destination, ivec2(_570), vec4(_1334));
            break;
        }
        int _626 = int(_37.Spacing + 0.5);
        float _629 = max(_37.Tolerance, 9.9999997473787516355514526367188e-05);
        float _633 = (-1.0) / (_629 * _629);
        ivec2 _637 = _573 - ivec2(_626, 0);
        float _1335;
        do
        {
            if ((((_637.x < 0) || (_637.y < 0)) || (_637.x >= int(_37.OutputWidth))) || (_637.y >= int(_37.OutputHeight)))
            {
                _1335 = 0.0;
                break;
            }
            _1335 = texelFetch(SPIRV_Cross_CombinedSourceSPIRV_Cross_DummySampler, ivec3(_637.x, int((_37.OutputHeight - 1u) - uint(_637.y)), 0).xy, 0).x;
            break;
        } while(false);
        ivec2 _642 = _573 + ivec2(_626, 0);
        float _1336;
        do
        {
            if ((((_642.x < 0) || (_642.y < 0)) || (_642.x >= int(_37.OutputWidth))) || (_642.y >= int(_37.OutputHeight)))
            {
                _1336 = 0.0;
                break;
            }
            _1336 = texelFetch(SPIRV_Cross_CombinedSourceSPIRV_Cross_DummySampler, ivec3(_642.x, int((_37.OutputHeight - 1u) - uint(_642.y)), 0).xy, 0).x;
            break;
        } while(false);
        ivec2 _647 = _573 - ivec2(0, _626);
        float _1337;
        do
        {
            if ((((_647.x < 0) || (_647.y < 0)) || (_647.x >= int(_37.OutputWidth))) || (_647.y >= int(_37.OutputHeight)))
            {
                _1337 = 0.0;
                break;
            }
            _1337 = texelFetch(SPIRV_Cross_CombinedSourceSPIRV_Cross_DummySampler, ivec3(_647.x, int((_37.OutputHeight - 1u) - uint(_647.y)), 0).xy, 0).x;
            break;
        } while(false);
        ivec2 _652 = _573 + ivec2(0, _626);
        float _1338;
        do
        {
            if ((((_652.x < 0) || (_652.y < 0)) || (_652.x >= int(_37.OutputWidth))) || (_652.y >= int(_37.OutputHeight)))
            {
                _1338 = 0.0;
                break;
            }
            _1338 = texelFetch(SPIRV_Cross_CombinedSourceSPIRV_Cross_DummySampler, ivec3(_652.x, int((_37.OutputHeight - 1u) - uint(_652.y)), 0).xy, 0).x;
            break;
        } while(false);
        float _655 = 2.0 * _629;
        bool _664 = (_1335 > 0.0) && (abs(_1335 - _1334) < _655);
        bool _673 = (_1336 > 0.0) && (abs(_1336 - _1334) < _655);
        bool _682 = (_1337 > 0.0) && (abs(_1337 - _1334) < _655);
        bool _691 = (_1338 > 0.0) && (abs(_1338 - _1334) < _655);
        float _693 = float(_626);
        float _717 = (_664 && _673) ? ((_1336 - _1335) / (2.0 * _693)) : (_673 ? ((_1336 - _1334) / _693) : (_664 ? ((_1334 - _1335) / _693) : 0.0));
        float _741 = (_682 && _691) ? ((_1338 - _1337) / (2.0 * _693)) : (_691 ? ((_1338 - _1334) / _693) : (_682 ? ((_1334 - _1337) / _693) : 0.0));
        float _1340;
        float _1341;
        _1341 = 0.140625;
        _1340 = _1334 * 0.140625;
        float _1351;
        float _1352;
        SPIRV_CROSS_UNROLL
        for (int _1339 = 0; _1339 < 12; _1341 = _1352, _1340 = _1351, _1339++)
        {
            int _757 = (_1339 < 2) ? (_1339 + 1) : (((_1339 - 2) % 5) - 2);
            int _763 = (_1339 < 2) ? 0 : ((_1339 < 7) ? 1 : 2);
            ivec2 _783 = ivec2(_757, _763) * ivec2(_626);
            ivec2 _786 = _573 + _783;
            float _1342;
            do
            {
                if ((((_786.x < 0) || (_786.y < 0)) || (_786.x >= int(_37.OutputWidth))) || (_786.y >= int(_37.OutputHeight)))
                {
                    _1342 = 0.0;
                    break;
                }
                _1342 = texelFetch(SPIRV_Cross_CombinedSourceSPIRV_Cross_DummySampler, ivec3(_786.x, int((_37.OutputHeight - 1u) - uint(_786.y)), 0).xy, 0).x;
                break;
            } while(false);
            ivec2 _790 = _573 - _783;
            float _1343;
            do
            {
                if ((((_790.x < 0) || (_790.y < 0)) || (_790.x >= int(_37.OutputWidth))) || (_790.y >= int(_37.OutputHeight)))
                {
                    _1343 = 0.0;
                    break;
                }
                _1343 = texelFetch(SPIRV_Cross_CombinedSourceSPIRV_Cross_DummySampler, ivec3(_790.x, int((_37.OutputHeight - 1u) - uint(_790.y)), 0).xy, 0).x;
                break;
            } while(false);
            if ((_1342 <= 0.0) || (_1343 <= 0.0))
            {
                _1352 = _1341;
                _1351 = _1340;
                continue;
            }
            float _809 = (_717 * float(_783.x)) + (_741 * float(_783.y));
            float _814 = _1342 - (_1334 + _809);
            float _819 = _1343 - (_1334 - _809);
            if ((abs(_814) > _629) || (abs(_819) > _629))
            {
                _1352 = _1341;
                _1351 = _1340;
                continue;
            }
            float _844 = (((_757 == 0) ? 0.375 : ((abs(_757) == 1) ? 0.25 : 0.0625)) * ((_763 == 0) ? 0.375 : ((abs(_763) == 1) ? 0.25 : 0.0625))) * exp(((_814 * _814) + (_819 * _819)) * _633);
            _1352 = _1341 + (2.0 * _844);
            _1351 = _1340 + ((_1342 + _1343) * _844);
        }
        imageStore(Destination, ivec2(_570), vec4(mix(_1334, _1340 / max(_1341, 9.9999999747524270787835121154785e-07), clamp(_37.Strength, 0.0, 1.0))));
        break;
    } while(false);
}

