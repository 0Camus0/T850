#version 430
// GENERATED from CS_FluidVolumeCorrect3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=35714308f586974b61fa827d9a03a00793908ea6a13e3a21dd2dce7f3413f99e; defines=T850_SPIRV=1,T850_VULKAN=1
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(binding = 2, std140) uniform CorrectionConstants
{
    float ReferenceVolume;
    uint SumSlot;
    float CorrectionPad0;
    float CorrectionPad1;
} _32;

layout(binding = 4, std430) buffer Sums
{
    uvec4 _data[];
} Sums_1;

layout(binding = 0, std140) uniform FluidConstants
{
    uint Width;
    uint Height;
    uint Depth;
    uint Iteration;
    float DeltaTime;
    float CellSize;
    float Viscosity;
    float Gravity;
    float WindX;
    float WindY;
    float WindZ;
    float Damping;
    float SurfaceLevel;
    float TimeValue;
    float PressureScale;
    float Padding0;
    float OpenCeiling;
    float OpenCeilingPad0;
    float OpenCeilingPad1;
    float OpenCeilingPad2;
} _84;

layout(binding = 3, r32f) uniform writeonly image3D AlphaOut;
layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        uint _272 = (_32.SumSlot & 1u) * 2u;
        uvec4 _275 = Sums_1._data[_272];
        uint _277 = _272 + 1u;
        uvec4 _279 = Sums_1._data[_277];
        if (all(equal(gl_GlobalInvocationID, uvec3(0u))))
        {
            uint _288 = ((_32.SumSlot + 1u) & 1u) * 2u;
            Sums_1._data[_288] = uvec4(0u);
            Sums_1._data[_288 + 1u] = uvec4(0u);
        }
        if (((gl_GlobalInvocationID.x >= _84.Width) || (gl_GlobalInvocationID.y >= _84.Height)) || (gl_GlobalInvocationID.z >= _84.Depth))
        {
            break;
        }
        float _331 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(ivec3(gl_GlobalInvocationID), 0).xyz, 0).x, 0.0, 1.0);
        float _431 = float(_275.z) + (float(_275.w) * 1.52587890625e-05);
        float _439 = float(_279.x) + (float(_279.y) * 1.52587890625e-05);
        float _350 = _32.ReferenceVolume - (float(_275.x) + (float(_275.y) * 1.52587890625e-05));
        float _452 = (((abs(_350) <= 0.5) || ((_350 > 0.0) && (_431 <= 1.0))) || ((_350 < 0.0) && (_439 <= 1.0))) ? 0.0 : _350;
        if ((_331 < 0.5) || (_331 >= 0.999000012874603271484375))
        {
            imageStore(AlphaOut, ivec3(gl_GlobalInvocationID), vec4(_331));
            break;
        }
        float _450;
        if ((_452 > 0.0) && (_431 > 9.9999997473787516355514526367188e-05))
        {
            _450 = clamp(_331 + ((_452 * (1.0 - _331)) / _431), 0.0, 1.0);
        }
        else
        {
            float _451;
            if ((_452 < 0.0) && (_439 > 9.9999997473787516355514526367188e-05))
            {
                _451 = clamp(_331 + ((_452 * _331) / _439), 0.0, 1.0);
            }
            else
            {
                _451 = _331;
            }
            _450 = _451;
        }
        imageStore(AlphaOut, ivec3(gl_GlobalInvocationID), vec4(_450));
        break;
    } while(false);
}

