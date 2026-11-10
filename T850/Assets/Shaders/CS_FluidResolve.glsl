#version 430
// GENERATED from CS_FluidResolve.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=6b6d87aee2029b82d00e01c3a59d4a7dc965bf190a5701cad62bf747b82fcbee; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
    float Color2Pad;
    vec3 Color3;
    float Color3Pad;
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

layout(binding = 3, r32f) uniform writeonly image2D DepthOut;
layout(binding = 4, rgba16f) uniform writeonly image2D ThicknessOut;

void main()
{
    do
    {
        if ((gl_GlobalInvocationID.x >= _24.OutputWidth) || (gl_GlobalInvocationID.y >= _24.OutputHeight))
        {
            break;
        }
        uint _274 = gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _24.OutputWidth);
        uint _277 = DepthBits_1._data[_274];
        float _282 = (_277 < 2139095040u) ? uintBitsToFloat(_277) : 0.0;
        float _410;
        float _411;
        _411 = 0.0;
        _410 = 0.0;
        float _414;
        float _415;
        SPIRV_CROSS_UNROLL
        for (int _409 = -2; _409 <= 2; _411 = _415, _410 = _414, _409++)
        {
            int _292 = int(gl_GlobalInvocationID.y) + _409;
            float _299 = (_409 == 0) ? 6.0 : ((abs(_409) == 1) ? 4.0 : 1.0);
            _415 = _411;
            _414 = _410;
            float _418;
            float _419;
            SPIRV_CROSS_UNROLL
            for (int _412 = -2; _412 <= 2; _415 = _419, _414 = _418, _412++)
            {
                int _309 = int(gl_GlobalInvocationID.x) + _412;
                if ((((_309 < 0) || (_292 < 0)) || (_309 >= int(_24.OutputWidth))) || (_292 >= int(_24.OutputHeight)))
                {
                    _419 = _415;
                    _418 = _414;
                    continue;
                }
                float _337 = _299 * ((_412 == 0) ? 6.0 : ((abs(_412) == 1) ? 4.0 : 1.0));
                _419 = _415 + _337;
                _418 = _414 + (float(ThicknessBits_1._data[uint(_309) + (uint(_292) * _24.OutputWidth)]) * _337);
            }
        }
        float _368 = (_410 / max(_411, 1.0)) * 0.0009765625;
        uvec2 _390 = uvec2(gl_GlobalInvocationID.x, (_24.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        imageStore(DepthOut, ivec2(_390), vec4(_282));
        imageStore(ThicknessOut, ivec2(_390), vec4(_368, 0.0, 0.0, (_282 > 0.0) ? smoothstep(0.0, max((0.699999988079071044921875 * _24.Radius) * _24.InvPackingFactor, 9.9999997473787516355514526367188e-05), _368) : 0.0));
        break;
    } while(false);
}

