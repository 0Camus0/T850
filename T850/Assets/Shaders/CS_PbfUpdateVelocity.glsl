#version 430
// GENERATED from CS_PbfUpdateVelocity.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=46bb225b44e4180b8c7e91852b7e4eebacb7437fed662333b10353f9f794c6c7; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
} _22;

layout(binding = 2, std430) readonly buffer Predicted
{
    vec4 _data[];
} Predicted_1;

layout(binding = 4, std430) buffer VelocitiesNext
{
    vec4 _data[];
} VelocitiesNext_1;

layout(binding = 1, std430) buffer Positions
{
    vec4 _data[];
} Positions_1;

layout(binding = 5, std430) readonly buffer GridBuffer
{
    uint _data[];
} GridBuffer_1;

layout(binding = 3, std430) readonly buffer Velocities
{
    vec4 _data[];
} Velocities_1;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _22.ParticleCount)
        {
            break;
        }
        uint _584 = (_22.GridX * _22.GridY) * _22.GridZ;
        float _587 = 1.0 / _22.SmoothingRadius;
        if (Predicted_1._data[gl_GlobalInvocationID.x].w < 0.5)
        {
            VelocitiesNext_1._data[gl_GlobalInvocationID.x] = vec4(0.0);
            Positions_1._data[gl_GlobalInvocationID.x] = vec4(Predicted_1._data[gl_GlobalInvocationID.x].xyz, 0.0);
            break;
        }
        vec4 _616 = Predicted_1._data[gl_GlobalInvocationID.x];
        vec3 _622 = (_616.xyz - Positions_1._data[gl_GlobalInvocationID.x].xyz) * (1.0 / max(_22.DeltaTime, 9.9999999747524270787835121154785e-07));
        float _624 = length(_622);
        vec3 _634 = _622 * (_22.MaxVelocity / _624);
        bvec3 _636 = bvec3(_624 > _22.MaxVelocity);
        vec3 _637 = vec3(_636.x ? _634.x : _622.x, _636.y ? _634.y : _622.y, _636.z ? _634.z : _622.z);
        int _651 = clamp(int(floor((_616.x - _22.GridOriginX) * _587)), 0, int(_22.GridX) - 1);
        int _665 = clamp(int(floor((_616.y - _22.GridOriginY) * _587)), 0, int(_22.GridY) - 1);
        int _679 = clamp(int(floor((_616.z - _22.GridOriginZ) * _587)), 0, int(_22.GridZ) - 1);
        float _684 = _22.SmoothingRadius * _22.SmoothingRadius;
        float _992;
        vec3 _993;
        _993 = vec3(0.0);
        _992 = 0.0;
        vec3 _1017;
        float _1021;
        SPIRV_CROSS_LOOP
        for (int _991 = -1; _991 <= 1; _993 = _1017, _992 = _1021, _991++)
        {
            _1021 = _992;
            _1017 = _993;
            vec3 _1016;
            float _1020;
            SPIRV_CROSS_LOOP
            for (int _1003 = -1; _1003 <= 1; _1003++, _1021 = _1020, _1017 = _1016)
            {
                _1020 = _1021;
                _1016 = _1017;
                vec3 _1038;
                float _1040;
                SPIRV_CROSS_LOOP
                for (int _1005 = -1; _1005 <= 1; _1020 = _1040, _1016 = _1038, _1005++)
                {
                    int _702 = _651 + _1005;
                    int _705 = _665 + _1003;
                    int _708 = _679 + _991;
                    if ((((((_702 < 0) || (_705 < 0)) || (_708 < 0)) || (_702 >= int(_22.GridX))) || (_705 >= int(_22.GridY))) || (_708 >= int(_22.GridZ)))
                    {
                        _1040 = _1020;
                        _1038 = _1016;
                        continue;
                    }
                    uint _750 = uint(_702) + (_22.GridX * (uint(_705) + (_22.GridY * uint(_708))));
                    uint _754 = min(GridBuffer_1._data[_750], 64u);
                    vec3 _1015;
                    float _1019;
                    _1019 = _1020;
                    _1015 = _1016;
                    vec3 _1039;
                    float _1041;
                    SPIRV_CROSS_LOOP
                    for (uint _1008 = 0u; _1008 < _754; _1019 = _1041, _1015 = _1039, _1008++)
                    {
                        uint _767 = (64u * (_584 + _750)) + _1008;
                        if ((GridBuffer_1._data[_767] >= _22.ParticleCount) || (GridBuffer_1._data[_767] == gl_GlobalInvocationID.x))
                        {
                            _1041 = _1019;
                            _1039 = _1015;
                            continue;
                        }
                        vec3 _786 = _616.xyz - Predicted_1._data[GridBuffer_1._data[_767]].xyz;
                        float _789 = dot(_786, _786);
                        if (_789 >= _684)
                        {
                            _1041 = _1019;
                            _1039 = _1015;
                            continue;
                        }
                        float _1009;
                        do
                        {
                            if (_789 >= (_22.SmoothingRadius * _22.SmoothingRadius))
                            {
                                _1009 = 0.0;
                                break;
                            }
                            float _956 = (_22.SmoothingRadius * _22.SmoothingRadius) - _789;
                            _1009 = (((1.566681385040283203125 / pow(_22.SmoothingRadius, 9.0)) * _956) * _956) * _956;
                            break;
                        } while(false);
                        _1041 = _1019 + _1009;
                        _1039 = _1015 + ((Velocities_1._data[GridBuffer_1._data[_767]].xyz - _637) * _1009);
                    }
                    _1040 = _1019;
                    _1038 = _1015;
                }
            }
        }
        vec3 _995;
        if (_992 > 9.9999999600419720025001879548654e-13)
        {
            _995 = _637 + ((_993 / vec3(_992)) * _22.ViscosityC);
        }
        else
        {
            _995 = _637;
        }
        float _842 = _22.ParticleRadius * 0.5;
        vec3 _996;
        if (_616.y <= ((_22.BasinMinY + _842) + 9.9999997473787516355514526367188e-05))
        {
            vec3 _975 = _995;
            _975.y = 0.0;
            _996 = _975;
        }
        else
        {
            _996 = _995;
        }
        vec3 _999;
        if (_616.y < _22.WaterPlaneY)
        {
            vec3 _997;
            if (_616.x <= ((_22.BasinMinX + _842) + 9.9999997473787516355514526367188e-05))
            {
                vec3 _979 = _996;
                _979.x = 0.0;
                _997 = _979;
            }
            else
            {
                vec3 _998;
                if (_616.x >= ((_22.BasinMaxX - _842) - 9.9999997473787516355514526367188e-05))
                {
                    vec3 _982 = _996;
                    _982.x = 0.0;
                    _998 = _982;
                }
                else
                {
                    _998 = _996;
                }
                _997 = _998;
            }
            vec3 _1000;
            if (_616.z <= ((_22.BasinMinZ + _842) + 9.9999997473787516355514526367188e-05))
            {
                vec3 _985 = _997;
                _985.z = 0.0;
                _1000 = _985;
            }
            else
            {
                vec3 _1001;
                if (_616.z >= ((_22.BasinMaxZ - _842) - 9.9999997473787516355514526367188e-05))
                {
                    vec3 _988 = _997;
                    _988.z = 0.0;
                    _1001 = _988;
                }
                else
                {
                    _1001 = _997;
                }
                _1000 = _1001;
            }
            _999 = _1000;
        }
        else
        {
            _999 = _996;
        }
        float _908 = length(_999);
        vec3 _1002;
        if (_908 > _22.MaxVelocity)
        {
            _1002 = _999 * (_22.MaxVelocity / _908);
        }
        else
        {
            _1002 = _999;
        }
        VelocitiesNext_1._data[gl_GlobalInvocationID.x] = vec4(_1002, 0.0);
        Positions_1._data[gl_GlobalInvocationID.x] = vec4(_616.xyz, 1.0);
        break;
    } while(false);
}

