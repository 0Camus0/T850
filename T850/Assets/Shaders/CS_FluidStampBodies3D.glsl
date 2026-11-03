#version 430
// GENERATED from CS_FluidStampBodies3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=4d49d3eabd5aa3b1a5f02dcecca58d82d315f2ad2fd23da08453bc74fafb9808; defines=T850_SPIRV=1,T850_VULKAN=1
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

layout(binding = 2, std430) readonly buffer BaseMask
{
    uint _data[];
} BaseMask_1;

layout(binding = 1, std140) uniform StampConstants
{
    float BasinMinX;
    float BasinMinY;
    float BasinMinZ;
    uint BodyCount;
    float StepX;
    float StepY;
    float StepZ;
    float StampPad;
} _78;

layout(binding = 3, std430) readonly buffer Bodies
{
    vec4 _data[];
} Bodies_1;

layout(binding = 4, std430) buffer Mask
{
    uint _data[];
} Mask_1;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _20.Width) || (gl_GlobalInvocationID.y >= _20.Height)) || (gl_GlobalInvocationID.z >= _20.Depth))
        {
            break;
        }
        uint _249 = gl_GlobalInvocationID.x + (_20.Width * (gl_GlobalInvocationID.y + (_20.Height * gl_GlobalInvocationID.z)));
        uint _359;
        if (BaseMask_1._data[_249] == 0u)
        {
            float _265 = _78.BasinMinX + ((float(gl_GlobalInvocationID.x) + 0.5) * _78.StepX);
            float _275 = _78.BasinMinY + ((float(gl_GlobalInvocationID.y) + 0.5) * _78.StepY);
            float _285 = _78.BasinMinZ + ((float(gl_GlobalInvocationID.z) + 0.5) * _78.StepZ);
            uint _360;
            _360 = BaseMask_1._data[_249];
            uint _363;
            for (uint _358 = 0u; _358 < _78.BodyCount; _360 = _363, _358++)
            {
                if (Bodies_1._data[_358].w <= 0.0)
                {
                    _363 = _360;
                    continue;
                }
                float _304 = _265 - Bodies_1._data[_358].x;
                float _308 = _275 - Bodies_1._data[_358].y;
                float _312 = _285 - Bodies_1._data[_358].z;
                uint _364;
                if ((((_304 * _304) + (_308 * _308)) + (_312 * _312)) <= (Bodies_1._data[_358].w * Bodies_1._data[_358].w))
                {
                    _364 = 2u + _358;
                }
                else
                {
                    _364 = _360;
                }
                _363 = _364;
            }
            _359 = _360;
        }
        else
        {
            _359 = BaseMask_1._data[_249];
        }
        Mask_1._data[_249] = _359;
        break;
    } while(false);
}

