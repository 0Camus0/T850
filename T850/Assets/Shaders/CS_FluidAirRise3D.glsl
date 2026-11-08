#version 430
// GENERATED from CS_FluidAirRise3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=318a92e7cd33f72fd9582c951e3ba439875a7721c3062d81b869b1d8fc51c01b; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
} _37;

layout(binding = 3, std430) readonly buffer WallMask
{
    uint _data[];
} WallMask_1;

layout(binding = 2, r32f) uniform writeonly image3D AlphaOut;
layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _37.Width) || (gl_GlobalInvocationID.y >= _37.Height)) || (gl_GlobalInvocationID.z >= _37.Depth))
        {
            break;
        }
        ivec3 _308 = ivec3(gl_GlobalInvocationID);
        bool _862;
        do
        {
            if ((((((_308.x < 0) || (_308.y < 0)) || (_308.z < 0)) || (_308.x >= int(_37.Width))) || (_308.y >= int(_37.Height))) || (_308.z >= int(_37.Depth)))
            {
                _862 = true;
                break;
            }
            _862 = WallMask_1._data[uint(_308.x) + (_37.Width * (uint(_308.y) + (_37.Height * uint(_308.z))))] != 0u;
            break;
        } while(false);
        if (_862)
        {
            imageStore(AlphaOut, ivec3(gl_GlobalInvocationID), vec4(0.0));
            break;
        }
        float _865;
        do
        {
            ivec3 _423 = _308 + ivec3(0, 1, 0);
            bool _863;
            do
            {
                if ((((((_308.x < 0) || (_308.y < 0)) || (_308.z < 0)) || (_308.x >= int(_37.Width))) || (_308.y >= int(_37.Height))) || (_308.z >= int(_37.Depth)))
                {
                    _863 = true;
                    break;
                }
                _863 = WallMask_1._data[uint(_308.x) + (_37.Width * (uint(_308.y) + (_37.Height * uint(_308.z))))] != 0u;
                break;
            } while(false);
            bool _864;
            do
            {
                if ((((((_423.x < 0) || (_423.y < 0)) || (_423.z < 0)) || (_423.x >= int(_37.Width))) || (_423.y >= int(_37.Height))) || (_423.z >= int(_37.Depth)))
                {
                    _864 = true;
                    break;
                }
                _864 = WallMask_1._data[uint(_423.x) + (_37.Width * (uint(_423.y) + (_37.Height * uint(_423.z))))] != 0u;
                break;
            } while(false);
            if (_863 || _864)
            {
                _865 = 0.0;
                break;
            }
            _865 = (clamp((_37.Padding0 * _37.DeltaTime) / _37.CellSize, 0.0, 1.0) * (1.0 - clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_308, 0).xyz, 0).x, 0.0, 1.0))) * clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_423, 0).xyz, 0).x, 0.0, 1.0);
            break;
        } while(false);
        ivec3 _337 = _308 - ivec3(0, 1, 0);
        float _868;
        do
        {
            bool _866;
            do
            {
                if ((((((_337.x < 0) || (_337.y < 0)) || (_337.z < 0)) || (_337.x >= int(_37.Width))) || (_337.y >= int(_37.Height))) || (_337.z >= int(_37.Depth)))
                {
                    _866 = true;
                    break;
                }
                _866 = WallMask_1._data[uint(_337.x) + (_37.Width * (uint(_337.y) + (_37.Height * uint(_337.z))))] != 0u;
                break;
            } while(false);
            bool _867;
            do
            {
                if ((((((_308.x < 0) || (_308.y < 0)) || (_308.z < 0)) || (_308.x >= int(_37.Width))) || (_308.y >= int(_37.Height))) || (_308.z >= int(_37.Depth)))
                {
                    _867 = true;
                    break;
                }
                _867 = WallMask_1._data[uint(_308.x) + (_37.Width * (uint(_308.y) + (_37.Height * uint(_308.z))))] != 0u;
                break;
            } while(false);
            if (_866 || _867)
            {
                _868 = 0.0;
                break;
            }
            _868 = (clamp((_37.Padding0 * _37.DeltaTime) / _37.CellSize, 0.0, 1.0) * (1.0 - clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_337, 0).xyz, 0).x, 0.0, 1.0))) * clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_308, 0).xyz, 0).x, 0.0, 1.0);
            break;
        } while(false);
        imageStore(AlphaOut, ivec3(gl_GlobalInvocationID), vec4(clamp((clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_308, 0).xyz, 0).x, 0.0, 1.0) + _865) - _868, 0.0, 1.0)));
        break;
    } while(false);
}

