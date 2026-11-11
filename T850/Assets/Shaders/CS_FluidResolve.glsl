#version 430
// GENERATED from CS_FluidResolve.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=7e25155d04f098e200a8ca7d20e190a20568fd25f8bfc9554d26eb1e090f55db; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

layout(binding = 0, std140) uniform ParticleConstants
{
    uint ParticleCount;
    uint OutputWidth;
    uint OutputHeight;
    uint Iteration;
    float Radius;
    float Opacity;
    float Intensity;
    float Softness;
    float DepthBias;
    float Lifetime;
    float Spread;
    float Rise;
    float TimeSeconds;
    float SizeScaleMin;
    float SizeScaleMax;
    uint Shape;
    uint Blend;
    uint Source;
    float ScreenSpace;
    float TileCountX;
    float TileCountY;
    uint FrameIndex;
    float ProjScaleY;
    float Style;
    float DepthRadiusScale;
    float InvPackingFactor;
    float ProjA;
    float ProjB;
    vec3 Color0;
    float Color0Pad;
    vec3 Color1;
    float Color1Pad;
    vec3 Color2;
    float SurfelBand;
    vec3 Color3;
    float SurfelSharpness;
    vec3 EmitterPosition;
    float EmitterPad;
    vec4 ViewProjection[4];
} _24;

layout(binding = 1, std430) readonly buffer DepthBits
{
    uint _data[];
} DepthBits_1;

layout(binding = 2, std430) readonly buffer ThicknessBits
{
    uint _data[];
} ThicknessBits_1;

layout(binding = 5, std430) buffer SurfelBits
{
    uint _data[];
} SurfelBits_1;

layout(binding = 3, r32f) uniform writeonly image2D DepthOut;
layout(binding = 4, rgba16f) uniform writeonly image2D ThicknessOut;
layout(binding = 6, rgba16f) uniform writeonly image2D NormalOut;

void main()
{
    do
    {
        if ((gl_GlobalInvocationID.x >= _24.OutputWidth) || (gl_GlobalInvocationID.y >= _24.OutputHeight))
        {
            break;
        }
        uint _383 = gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _24.OutputWidth);
        uint _386 = DepthBits_1._data[_383];
        float _391 = (_386 < 2139095040u) ? uintBitsToFloat(_386) : 0.0;
        float _606;
        float _607;
        _607 = 0.0;
        _606 = 0.0;
        float _617;
        float _618;
        SPIRV_CROSS_UNROLL
        for (int _605 = -2; _605 <= 2; _607 = _618, _606 = _617, _605++)
        {
            int _401 = int(gl_GlobalInvocationID.y) + _605;
            float _408 = (_605 == 0) ? 6.0 : ((abs(_605) == 1) ? 4.0 : 1.0);
            _618 = _607;
            _617 = _606;
            float _623;
            float _624;
            SPIRV_CROSS_UNROLL
            for (int _615 = -2; _615 <= 2; _618 = _624, _617 = _623, _615++)
            {
                int _418 = int(gl_GlobalInvocationID.x) + _615;
                if ((((_418 < 0) || (_401 < 0)) || (_418 >= int(_24.OutputWidth))) || (_401 >= int(_24.OutputHeight)))
                {
                    _624 = _618;
                    _623 = _617;
                    continue;
                }
                float _446 = _408 * ((_615 == 0) ? 6.0 : ((abs(_615) == 1) ? 4.0 : 1.0));
                _624 = _618 + _446;
                _623 = _617 + (float(ThicknessBits_1._data[uint(_418) + (uint(_401) * _24.OutputWidth)]) * _446);
            }
        }
        float _477 = (_606 / max(_607, 1.0)) * 0.0009765625;
        float _609;
        vec4 _612;
        if (_24.Iteration == 2u)
        {
            uint _496 = _383 * 5u;
            float _610;
            vec4 _613;
            if ((_391 > 0.0) && (SurfelBits_1._data[_496] > 4u))
            {
                vec3 _538 = vec3(float(int(SurfelBits_1._data[_496 + 2u])), float(int(SurfelBits_1._data[_496 + 3u])), float(int(SurfelBits_1._data[_496 + 4u])));
                float _540 = length(_538);
                vec4 _614;
                if (_540 > 1.0)
                {
                    _614 = vec4(_538 / vec3(_540), clamp(float(SurfelBits_1._data[_496]) * 0.000244140625, 0.0, 1.0));
                }
                else
                {
                    _614 = vec4(0.0);
                }
                _613 = _614;
                _610 = _391 + ((_24.SurfelBand * float(SurfelBits_1._data[_496 + 1u])) / float(SurfelBits_1._data[_496]));
            }
            else
            {
                _613 = vec4(0.0);
                _610 = _391;
            }
            SurfelBits_1._data[_496] = 0u;
            SurfelBits_1._data[_496 + 1u] = 0u;
            SurfelBits_1._data[_496 + 2u] = 0u;
            SurfelBits_1._data[_496 + 3u] = 0u;
            SurfelBits_1._data[_496 + 4u] = 0u;
            _612 = _613;
            _609 = _610;
        }
        else
        {
            _612 = vec4(0.0);
            _609 = _391;
        }
        uvec2 _582 = uvec2(gl_GlobalInvocationID.x, (_24.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        imageStore(DepthOut, ivec2(_582), vec4(_609));
        imageStore(ThicknessOut, ivec2(_582), vec4(_477, 0.0, 0.0, (_391 > 0.0) ? smoothstep(0.0, max((0.699999988079071044921875 * _24.Radius) * _24.InvPackingFactor, 9.9999997473787516355514526367188e-05), _477) : 0.0));
        imageStore(NormalOut, ivec2(_582), _612);
        break;
    } while(false);
}

