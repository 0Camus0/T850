#version 430
// GENERATED from CS_FluidBodyLevel.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=69b02733c3f5a1c82f8041d3168a16135e143d262a8cba1f61fadd1f9a044636; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(binding = 1, std140) uniform BodyLevelConstants
{
    float BasinMinX;
    float BasinMinY;
    float BasinMinZ;
    float WaterPlaneY;
    float StepX;
    float StepY;
    float StepZ;
    uint BodyCount;
} _20;

layout(binding = 5, std430) readonly buffer Bodies
{
    vec4 _data[];
} Bodies_1;

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
} _99;

layout(binding = 4, std430) readonly buffer WallMask
{
    uint _data[];
} WallMask_1;

layout(binding = 6, std430) buffer Result
{
    vec4 _data[];
} Result_1;

layout(binding = 2) uniform sampler2D SPIRV_Cross_CombinedSurfaceInSPIRV_Cross_DummySampler;
layout(binding = 3) uniform sampler3D SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _20.BodyCount)
        {
            break;
        }
        vec4 _663;
        if (Bodies_1._data[gl_GlobalInvocationID.x].w > 0.0)
        {
            float _403 = Bodies_1._data[gl_GlobalInvocationID.x].w * 1.0499999523162841796875;
            float _409 = (Bodies_1._data[gl_GlobalInvocationID.x].w * 2.0) + _20.StepX;
            int _422 = max(int(floor(((Bodies_1._data[gl_GlobalInvocationID.x].x - _409) - _20.BasinMinX) / _20.StepX)), 0);
            int _439 = min(int(ceil(((Bodies_1._data[gl_GlobalInvocationID.x].x + _409) - _20.BasinMinX) / _20.StepX)), (int(_99.Width) - 1));
            int _452 = max(int(floor(((Bodies_1._data[gl_GlobalInvocationID.x].z - _409) - _20.BasinMinZ) / _20.StepZ)), 0);
            int _469 = min(int(ceil(((Bodies_1._data[gl_GlobalInvocationID.x].z + _409) - _20.BasinMinZ) / _20.StepZ)), (int(_99.Depth) - 1));
            int _483 = clamp(int((Bodies_1._data[gl_GlobalInvocationID.x].y - _20.BasinMinY) / _20.StepY), 0, int(_99.Height) - 1);
            float _659;
            float _660;
            float _661;
            float _662;
            _662 = 0.0;
            _661 = 0.0;
            _660 = 0.0;
            _659 = 0.0;
            float _668;
            float _669;
            float _670;
            float _671;
            SPIRV_CROSS_LOOP
            for (int _658 = _452; _658 <= _469; _662 = _670, _661 = _669, _660 = _668, _659 = _671, _658++)
            {
                _671 = _659;
                _670 = _662;
                _669 = _661;
                _668 = _660;
                float _676;
                float _677;
                float _678;
                float _679;
                SPIRV_CROSS_LOOP
                for (int _666 = _422; _666 <= _439; _671 = _679, _670 = _678, _669 = _677, _668 = _676, _666++)
                {
                    float _509 = (_20.BasinMinX + ((float(_666) + 0.5) * _20.StepX)) - Bodies_1._data[gl_GlobalInvocationID.x].x;
                    float _521 = (_20.BasinMinZ + ((float(_658) + 0.5) * _20.StepZ)) - Bodies_1._data[gl_GlobalInvocationID.x].z;
                    float _529 = sqrt((_509 * _509) + (_521 * _521));
                    if ((_529 < _403) || (_529 > _409))
                    {
                        _679 = _671;
                        _678 = _670;
                        _677 = _669;
                        _676 = _668;
                        continue;
                    }
                    vec4 _548 = texelFetch(SPIRV_Cross_CombinedSurfaceInSPIRV_Cross_DummySampler, ivec3(_666, _658, 0).xy, 0);
                    float _549 = _548.x;
                    int _565 = clamp(int(((_20.WaterPlaneY + _549) - _20.BasinMinY) / _20.StepY), 0, int(_99.Height) - 1);
                    if (WallMask_1._data[uint(_666) + (_99.Width * (uint(_565) + (_99.Height * uint(_658))))] != 0u)
                    {
                        _679 = _671;
                        _678 = _670;
                        _677 = _669;
                        _676 = _668;
                        continue;
                    }
                    vec4 _600 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_666, min(_565, _483), _658, 0).xyz, 0);
                    _679 = _671 + 1.0;
                    _678 = _670 + _600.z;
                    _677 = _669 + _600.x;
                    _676 = _668 + _549;
                }
            }
            vec4 _664;
            if (_659 > 0.0)
            {
                _664 = vec4(_660 / _659, _661 / _659, _662 / _659, _659);
            }
            else
            {
                _664 = vec4(0.0);
            }
            _663 = _664;
        }
        else
        {
            _663 = vec4(0.0);
        }
        Result_1._data[gl_GlobalInvocationID.x] = _663;
        break;
    } while(false);
}

