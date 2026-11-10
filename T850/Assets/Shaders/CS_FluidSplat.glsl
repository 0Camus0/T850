#version 430
// GENERATED from /home/camus000/testBitso/T850/Assets/Shaders/CS_FluidSplat.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=99b7ae2ae36c3a6a570ec9dd9e010af8847ec96d892c53d22bd58a77e11377d5; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

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
} _56;

layout(binding = 1, std430) readonly buffer Projected
{
    vec4 _data[];
} Projected_1;

layout(binding = 3, std430) buffer DepthBits
{
    uint _data[];
} DepthBits_1;

layout(binding = 4, std430) buffer ThicknessBits
{
    uint _data[];
} ThicknessBits_1;

layout(binding = 2) uniform sampler2D SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _56.ParticleCount)
        {
            break;
        }
        vec4 _444 = Projected_1._data[gl_GlobalInvocationID.x];
        if (_444.z <= 0.0)
        {
            break;
        }
        bool _452 = _56.ScreenSpace > 0.5;
        vec2 _462 = _444.xy * vec2(float(_56.OutputWidth), float(_56.OutputHeight));
        float _465 = min(_444.z, 64.0);
        float _469 = _452 ? _56.DepthRadiusScale : 1.0;
        float _472 = _465 * _469;
        float _476 = max(_472, _465);
        if (_476 < 0.5)
        {
            break;
        }
        ivec2 _487 = max(ivec2(floor(_462 - vec2(_476))), ivec2(0));
        ivec2 _503 = min(ivec2(ceil(_462 + vec2(_476))), ivec2(int(_56.OutputWidth) - 1, int(_56.OutputHeight) - 1));
        if ((_487.x > _503.x) || (_487.y > _503.y))
        {
            break;
        }
        float _521 = max(_56.Radius * _469, 9.9999997473787516355514526367188e-05);
        float _524 = max(_56.Radius, 9.9999997473787516355514526367188e-05);
        float _527 = _472 * _472;
        float _531 = max(_465 * _465, 9.9999999747524270787835121154785e-07);
        int _533 = _487.y;
        for (int _720 = _533; _720 <= _503.y; _720++)
        {
            int _542 = _487.x;
            for (int _721 = _542; _721 <= _503.x; _721++)
            {
                vec2 _558 = vec2(float(_721) + 0.5, float(_720) + 0.5) - _462;
                float _561 = dot(_558, _558);
                bool _564 = _561 <= _527;
                bool _567 = _561 <= _531;
                if ((!_564) && (!_567))
                {
                    continue;
                }
                float _589 = _444.w - (_564 ? (sqrt(max(1.0 - (_561 / _527), 0.0)) * _521) : _521);
                vec4 _611 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_721, int((_56.OutputHeight - 1u) - uint(_720)), 0).xy, 0);
                float _612 = _611.x;
                if (_589 >= (((_612 > 0.0) && (abs(_612 - _56.ProjA) > 9.999999717180685365747194737196e-10)) ? (_56.ProjB / (_612 - _56.ProjA)) : 1000000015047466219876688855040.0))
                {
                    continue;
                }
                uint _642 = uint(_721) + (uint(_720) * _56.OutputWidth);
                if (_564)
                {
                    if (_452)
                    {
                        uint _651 = atomicMin(DepthBits_1._data[_642], floatBitsToUint(_589));
                    }
                    else
                    {
                        uint _658 = atomicMin(DepthBits_1._data[_642], (uint(clamp((log2(max(_589, 0.0500000007450580596923828125)) + 4.3219280242919921875) * 0.069990210235118865966796875, 0.0, 1.0) * 32766.0) << 17u) | (gl_GlobalInvocationID.x & 131071u));
                    }
                }
                if (_567)
                {
                    uint _680 = atomicAdd(ThicknessBits_1._data[_642], uint((((2.0 * sqrt(max(1.0 - (_561 / _531), 0.0))) * _524) * _56.InvPackingFactor) * 1024.0));
                }
            }
        }
        break;
    } while(false);
}

