#version 430
// GENERATED from CS_FluidResolve.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=35eeb07af52ede84a0f800870a58bb03ff71a742e25caaf5ea340daab9030104; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
    uint ThicknessShift;
    float ThicknessPad0;
    float ThicknessPad1;
    float ThicknessPad2;
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
        uint _568 = gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _24.OutputWidth);
        uint _571 = DepthBits_1._data[_568];
        float _576 = (_571 < 2139095040u) ? uintBitsToFloat(_571) : 0.0;
        float _964;
        float _966;
        if (min(_24.ThicknessShift, 1u) == 0u)
        {
            float _965;
            float _967;
            _967 = 0.0;
            _965 = 0.0;
            float _982;
            float _983;
            SPIRV_CROSS_UNROLL
            for (int _963 = -2; _963 <= 2; _967 = _983, _965 = _982, _963++)
            {
                int _592 = int(gl_GlobalInvocationID.y) + _963;
                float _599 = (_963 == 0) ? 6.0 : ((abs(_963) == 1) ? 4.0 : 1.0);
                _983 = _967;
                _982 = _965;
                float _993;
                float _994;
                SPIRV_CROSS_UNROLL
                for (int _980 = -2; _980 <= 2; _983 = _994, _982 = _993, _980++)
                {
                    int _609 = int(gl_GlobalInvocationID.x) + _980;
                    if ((((_609 < 0) || (_592 < 0)) || (_609 >= int(_24.OutputWidth))) || (_592 >= int(_24.OutputHeight)))
                    {
                        _994 = _983;
                        _993 = _982;
                        continue;
                    }
                    float _637 = _599 * ((_980 == 0) ? 6.0 : ((abs(_980) == 1) ? 4.0 : 1.0));
                    _994 = _983 + _637;
                    _993 = _982 + (float(ThicknessBits_1._data[uint(_609) + (uint(_592) * _24.OutputWidth)]) * _637);
                }
            }
            _966 = _967;
            _964 = _965;
        }
        else
        {
            uint _668 = (_24.OutputWidth + 1u) >> uint(1);
            uint _672 = (_24.OutputHeight + 1u) >> uint(1);
            vec2 _680 = ((vec2(gl_GlobalInvocationID.xy) + vec2(0.5)) * 0.5) - vec2(0.5);
            ivec2 _683 = ivec2(floor(_680));
            vec2 _687 = _680 - vec2(_683);
            vec4 _529 = vec4((1.0 - _687.x) * 0.25, ((1.0 - _687.x) * 0.5) + (_687.x * 0.25), ((1.0 - _687.x) * 0.25) + (_687.x * 0.5), _687.x * 0.25);
            vec4 _530 = vec4((1.0 - _687.y) * 0.25, ((1.0 - _687.y) * 0.5) + (_687.y * 0.25), ((1.0 - _687.y) * 0.25) + (_687.y * 0.5), _687.y * 0.25);
            float _958;
            float _960;
            _960 = 0.0;
            _958 = 0.0;
            float _986;
            float _988;
            SPIRV_CROSS_UNROLL
            for (int _954 = -1; _954 <= 2; _960 = _988, _958 = _986, _954++)
            {
                int _744 = _683.y + _954;
                if ((_744 < 0) || (_744 >= int(_672)))
                {
                    _988 = _960;
                    _986 = _958;
                    continue;
                }
                float _957;
                float _959;
                _959 = _960;
                _957 = _958;
                float _987;
                float _989;
                SPIRV_CROSS_UNROLL
                for (int _955 = -1; _955 <= 2; _959 = _989, _957 = _987, _955++)
                {
                    int _762 = _683.x + _955;
                    if ((_762 < 0) || (_762 >= int(_668)))
                    {
                        _989 = _959;
                        _987 = _957;
                        continue;
                    }
                    float _780 = _530[_954 + 1] * _529[_955 + 1];
                    _989 = _959 + _780;
                    _987 = _957 + (float(ThicknessBits_1._data[uint(_762) + (uint(_744) * _668)]) * _780);
                }
                _988 = _959;
                _986 = _957;
            }
            _966 = _960;
            _964 = _958;
        }
        float _811 = (_964 / max(_966, 1.0)) * 0.0009765625;
        float _974;
        vec4 _977;
        if (_24.Iteration == 2u)
        {
            uint _830 = _568 * 5u;
            float _975;
            vec4 _978;
            if ((_576 > 0.0) && (SurfelBits_1._data[_830] > 4u))
            {
                vec3 _872 = vec3(float(int(SurfelBits_1._data[_830 + 2u])), float(int(SurfelBits_1._data[_830 + 3u])), float(int(SurfelBits_1._data[_830 + 4u])));
                float _874 = length(_872);
                vec4 _979;
                if (_874 > 1.0)
                {
                    _979 = vec4(_872 / vec3(_874), clamp(float(SurfelBits_1._data[_830]) * 0.000244140625, 0.0, 1.0));
                }
                else
                {
                    _979 = vec4(0.0);
                }
                _978 = _979;
                _975 = _576 + ((_24.SurfelBand * float(SurfelBits_1._data[_830 + 1u])) / float(SurfelBits_1._data[_830]));
            }
            else
            {
                _978 = vec4(0.0);
                _975 = _576;
            }
            SurfelBits_1._data[_830] = 0u;
            SurfelBits_1._data[_830 + 1u] = 0u;
            SurfelBits_1._data[_830 + 2u] = 0u;
            SurfelBits_1._data[_830 + 3u] = 0u;
            SurfelBits_1._data[_830 + 4u] = 0u;
            _977 = _978;
            _974 = _975;
        }
        else
        {
            _977 = vec4(0.0);
            _974 = _576;
        }
        uvec2 _916 = uvec2(gl_GlobalInvocationID.x, (_24.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        imageStore(DepthOut, ivec2(_916), vec4(_974));
        imageStore(ThicknessOut, ivec2(_916), vec4(_811, 0.0, 0.0, (_576 > 0.0) ? smoothstep(0.0, max((0.699999988079071044921875 * _24.Radius) * _24.InvPackingFactor, 9.9999997473787516355514526367188e-05), _811) : 0.0));
        imageStore(NormalOut, ivec2(_916), _977);
        break;
    } while(false);
}

