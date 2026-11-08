#version 430
// GENERATED from CS_FluidDiagnostics3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=eb34d6f87f1dd7f30e366a5cef217a9b529f61bcb3242f5084929101f0edf6dc; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

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
} _20;

layout(binding = 2, std430) buffer Result
{
    uint _data[];
} Result_1;

layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _20.Width) || (gl_GlobalInvocationID.y >= _20.Height)) || (gl_GlobalInvocationID.z >= _20.Depth))
        {
            break;
        }
        if (_20.Iteration == 4294967295u)
        {
            if (((gl_GlobalInvocationID.x == 0u) && (gl_GlobalInvocationID.y == 0u)) && (gl_GlobalInvocationID.z == 0u))
            {
                Result_1._data[0] = 0u;
                Result_1._data[1] = 0u;
                Result_1._data[2] = 0u;
                Result_1._data[3] = 0u;
                Result_1._data[4] = 0u;
                Result_1._data[5] = 0u;
                Result_1._data[6] = 0u;
                Result_1._data[7] = 0u;
            }
            break;
        }
        vec4 _252 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(ivec3(gl_GlobalInvocationID), 0).xyz, 0);
        vec3 _253 = _252.xyz;
        uvec3 _255 = floatBitsToUint(_253);
        bool _269 = (((_255.x & 2139095040u) != 2139095040u) && ((_255.y & 2139095040u) != 2139095040u)) && ((_255.z & 2139095040u) != 2139095040u);
        uint _277 = uint(min((_269 ? length(_253) : 0.0) * 1000.0, 65535.0));
        uint _285 = atomicAdd(Result_1._data[0], 1u);
        uint _288 = atomicAdd(Result_1._data[1], _277);
        uint _291 = atomicMax(Result_1._data[2], _277);
        if (!_269)
        {
            uint _296 = atomicAdd(Result_1._data[3], 1u);
        }
        uint _300 = atomicAdd(Result_1._data[4], uint(clamp((_252.x + 8.0) * 1000.0, 0.0, 16000.0)));
        break;
    } while(false);
}

