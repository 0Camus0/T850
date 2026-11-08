#version 430
// GENERATED from CS_PbfSurface.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=1df1cdbd532e23b17238b721d676dd8fbe00332661c2c5dde51bbe0336072645; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

layout(binding = 0, std140) uniform PbfConstants
{
    uint ParticleCount;
    uint GridX;
    uint GridY;
    uint GridZ;
    float DeltaTime;
    float SmoothingRadius;
    float ParticleRadius;
    float RestDensity;
    float StiffnessEps;
    float ArtPressureK;
    float ArtPressureN;
    float ViscosityC;
    float Gravity;
    float MaxVelocity;
    float GridOriginX;
    float GridOriginY;
    float GridOriginZ;
    float BasinMinX;
    float BasinMinY;
    float BasinMinZ;
    float BasinMaxX;
    float BasinMaxY;
    float BasinMaxZ;
    uint BoxCount;
    uint Iteration;
    uint SurfaceWidth;
    uint SurfaceDepth;
    float WaterPlaneY;
    float RestScale;
    float RestDenomReference;
    float MinNeighbours;
    float Pad6;
    float WindX;
    float WindZ;
    float WindStrength;
    float Pad7;
} _18;

layout(binding = 1, std430) readonly buffer Predicted
{
    vec4 _data[];
} Predicted_1;

layout(binding = 3, std430) buffer ColumnHeights
{
    int _data[];
} ColumnHeights_1;

layout(binding = 4, rgba16f) uniform writeonly image2D SurfaceOut;

void main()
{
    do
    {
        if (_18.Iteration == 0u)
        {
            if (gl_GlobalInvocationID.x >= _18.ParticleCount)
            {
                break;
            }
            vec4 _676 = Predicted_1._data[gl_GlobalInvocationID.x];
            float _704 = (_676.x - _18.BasinMinX) / ((_18.BasinMaxX - _18.BasinMinX) / max(float(_18.SurfaceWidth) - 1.0, 1.0));
            float _711 = (_676.z - _18.BasinMinZ) / ((_18.BasinMaxZ - _18.BasinMinZ) / max(float(_18.SurfaceDepth) - 1.0, 1.0));
            int _714 = int(floor(_704));
            int _717 = int(floor(_711));
            SPIRV_CROSS_UNROLL
            for (int _1130 = -2; _1130 <= 2; _1130++)
            {
                SPIRV_CROSS_UNROLL
                for (int _1131 = -2; _1131 <= 2; _1131++)
                {
                    int _730 = _714 + _1131;
                    int _733 = _717 + _1130;
                    if ((((_730 < 0) || (_733 < 0)) || (_730 >= int(_18.SurfaceWidth))) || (_733 >= int(_18.SurfaceDepth)))
                    {
                        continue;
                    }
                    float _754 = _704 - float(_730);
                    float _758 = _711 - float(_733);
                    float _765 = (_754 * _754) + (_758 * _758);
                    if (_765 > 4.0)
                    {
                        continue;
                    }
                    float _772 = 1.0 - (_765 * 0.25);
                    if (_772 <= 0.0)
                    {
                        continue;
                    }
                    uint _783 = uint(_730) + (_18.SurfaceWidth * uint(_733));
                    int _796 = atomicMax(ColumnHeights_1._data[_783 * 2u], floatBitsToInt(max(_676.y - _18.BasinMinY, 0.0)));
                    int _805 = atomicAdd(ColumnHeights_1._data[(_783 * 2u) + 1u], int(max(_772 * 256.0, 1.0)));
                }
            }
            break;
        }
        if (gl_GlobalInvocationID.x >= (_18.SurfaceWidth * _18.SurfaceDepth))
        {
            break;
        }
        uint _827 = gl_GlobalInvocationID.x % _18.SurfaceWidth;
        uint _830 = gl_GlobalInvocationID.x / _18.SurfaceWidth;
        float _1123;
        float _1124;
        _1124 = 0.0;
        _1123 = 0.0;
        float _1127;
        float _1128;
        SPIRV_CROSS_UNROLL
        for (int _1122 = -1; _1122 <= 1; _1124 = _1127, _1123 = _1128, _1122++)
        {
            _1128 = _1123;
            _1127 = _1124;
            float _1135;
            float _1136;
            SPIRV_CROSS_UNROLL
            for (int _1125 = -1; _1125 <= 1; _1128 = _1136, _1127 = _1135, _1125++)
            {
                int _879 = int(_827) + _1125;
                int _883 = int(_830) + _1122;
                if ((((_879 < 0) || (_883 < 0)) || (_879 >= int(_18.SurfaceWidth))) || (_883 >= int(_18.SurfaceDepth)))
                {
                    _1136 = _1128;
                    _1135 = _1127;
                    continue;
                }
                uint _907 = uint(_879) + (_18.SurfaceWidth * uint(_883));
                if (float(ColumnHeights_1._data[(_907 * 2u) + 1u]) <= 0.5)
                {
                    _1136 = _1128;
                    _1135 = _1127;
                    continue;
                }
                float _936 = ((_1125 == 0) && (_1122 == 0)) ? 4.0 : (((_1125 == 0) || (_1122 == 0)) ? 2.0 : 1.0);
                _1136 = _1128 + _936;
                _1135 = _1127 + (intBitsToFloat(ColumnHeights_1._data[_907 * 2u]) * _936);
            }
        }
        float _964 = (_1123 > 0.0) ? (((_1124 / _1123) + _18.BasinMinY) - _18.WaterPlaneY) : 0.0;
        uint _970 = (_827 > 0u) ? (gl_GlobalInvocationID.x - 1u) : gl_GlobalInvocationID.x;
        uint _978 = ((_827 + 1u) < _18.SurfaceWidth) ? (gl_GlobalInvocationID.x + 1u) : gl_GlobalInvocationID.x;
        uint _985 = (_830 > 0u) ? (gl_GlobalInvocationID.x - _18.SurfaceWidth) : gl_GlobalInvocationID.x;
        uint _994 = ((_830 + 1u) < _18.SurfaceDepth) ? (gl_GlobalInvocationID.x + _18.SurfaceWidth) : gl_GlobalInvocationID.x;
        bool _1098 = (float(ColumnHeights_1._data[(gl_GlobalInvocationID.x * 2u) + 1u]) > 0.5) || (_1123 > 0.0);
        imageStore(SurfaceOut, ivec2(uvec2(_827, _830)), vec4(_1098 ? _964 : 0.0, 0.0, _1098 ? ((((float(ColumnHeights_1._data[(_978 * 2u) + 1u]) > 0.5) ? ((intBitsToFloat(ColumnHeights_1._data[_978 * 2u]) + _18.BasinMinY) - _18.WaterPlaneY) : _964) - ((float(ColumnHeights_1._data[(_970 * 2u) + 1u]) > 0.5) ? ((intBitsToFloat(ColumnHeights_1._data[_970 * 2u]) + _18.BasinMinY) - _18.WaterPlaneY) : _964)) / (2.0 * ((_18.BasinMaxX - _18.BasinMinX) / max(float(_18.SurfaceWidth) - 1.0, 1.0)))) : 0.0, _1098 ? ((((float(ColumnHeights_1._data[(_994 * 2u) + 1u]) > 0.5) ? ((intBitsToFloat(ColumnHeights_1._data[_994 * 2u]) + _18.BasinMinY) - _18.WaterPlaneY) : _964) - ((float(ColumnHeights_1._data[(_985 * 2u) + 1u]) > 0.5) ? ((intBitsToFloat(ColumnHeights_1._data[_985 * 2u]) + _18.BasinMinY) - _18.WaterPlaneY) : _964)) / (2.0 * ((_18.BasinMaxZ - _18.BasinMinZ) / max(float(_18.SurfaceDepth) - 1.0, 1.0)))) : 0.0));
        break;
    } while(false);
}

