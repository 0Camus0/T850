#version 430
// GENERATED from CS_FluidSeedAlpha3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=19fb149fb52e382690512d2ad3ab33f178538fbb6753bf01784d34c72963457b; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
} _33;

layout(binding = 1, std430) readonly buffer WallMask
{
    uint _data[];
} WallMask_1;

layout(binding = 2, r32f) uniform writeonly image3D AlphaOut;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _33.Width) || (gl_GlobalInvocationID.y >= _33.Height)) || (gl_GlobalInvocationID.z >= _33.Depth))
        {
            break;
        }
        ivec3 _226 = ivec3(gl_GlobalInvocationID);
        bool _343;
        do
        {
            if ((((((_226.x < 0) || (_226.y < 0)) || (_226.z < 0)) || (_226.x >= int(_33.Width))) || (_226.y >= int(_33.Height))) || (_226.z >= int(_33.Depth)))
            {
                _343 = true;
                break;
            }
            _343 = WallMask_1._data[uint(_226.x) + (_33.Width * (uint(_226.y) + (_33.Height * uint(_226.z))))] != 0u;
            break;
        } while(false);
        if (_343)
        {
            imageStore(AlphaOut, ivec3(gl_GlobalInvocationID), vec4(0.0));
            break;
        }
        float _239 = _33.SurfaceLevel * float(_33.Height);
        float _242 = float(gl_GlobalInvocationID.y);
        float _344;
        if (_239 >= (_242 + 1.0))
        {
            _344 = 1.0;
        }
        else
        {
            float _345;
            if (_239 <= _242)
            {
                _345 = 0.0;
            }
            else
            {
                _345 = _239 - _242;
            }
            _344 = _345;
        }
        imageStore(AlphaOut, ivec3(gl_GlobalInvocationID), vec4(_344));
        break;
    } while(false);
}

