#version 430
// GENERATED from CS_PbfDensity.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=17148e4a507fbffc0750c0e19e52d33bf45cce841d6e7bee484318e4125c47f8; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
} _30;

layout(binding = 1, std430) readonly buffer Predicted
{
    vec4 _data[];
} Predicted_1;

layout(binding = 3, std430) buffer Lambda
{
    vec4 _data[];
} Lambda_1;

layout(binding = 2, std430) readonly buffer GridBuffer
{
    uint _data[];
} GridBuffer_1;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _30.ParticleCount)
        {
            break;
        }
        uint _515 = (_30.GridX * _30.GridY) * _30.GridZ;
        float _518 = 1.0 / _30.SmoothingRadius;
        if (Predicted_1._data[gl_GlobalInvocationID.x].w < 0.5)
        {
            Lambda_1._data[gl_GlobalInvocationID.x] = vec4(0.0);
            break;
        }
        int _544 = clamp(int(floor((Predicted_1._data[gl_GlobalInvocationID.x].x - _30.GridOriginX) * _518)), 0, int(_30.GridX) - 1);
        int _558 = clamp(int(floor((Predicted_1._data[gl_GlobalInvocationID.x].y - _30.GridOriginY) * _518)), 0, int(_30.GridY) - 1);
        int _572 = clamp(int(floor((Predicted_1._data[gl_GlobalInvocationID.x].z - _30.GridOriginZ) * _518)), 0, int(_30.GridZ) - 1);
        float _577 = _30.SmoothingRadius * _30.SmoothingRadius;
        vec3 _846;
        float _847;
        float _848;
        uint _849;
        _849 = 0u;
        _848 = 0.0;
        _847 = 0.0;
        _846 = vec3(0.0);
        float _860;
        vec3 _866;
        float _871;
        uint _876;
        SPIRV_CROSS_LOOP
        for (int _845 = -1; _845 <= 1; _849 = _876, _848 = _860, _847 = _871, _846 = _866, _845++)
        {
            _876 = _849;
            _871 = _847;
            _866 = _846;
            _860 = _848;
            float _859;
            vec3 _865;
            float _870;
            uint _875;
            SPIRV_CROSS_LOOP
            for (int _850 = -1; _850 <= 1; _850++, _876 = _875, _871 = _870, _866 = _865, _860 = _859)
            {
                _875 = _876;
                _870 = _871;
                _865 = _866;
                _859 = _860;
                float _895;
                vec3 _898;
                float _900;
                uint _902;
                SPIRV_CROSS_LOOP
                for (int _852 = -1; _852 <= 1; _875 = _902, _870 = _900, _865 = _898, _859 = _895, _852++)
                {
                    int _595 = _544 + _852;
                    int _598 = _558 + _850;
                    int _601 = _572 + _845;
                    if ((((((_595 < 0) || (_598 < 0)) || (_601 < 0)) || (_595 >= int(_30.GridX))) || (_598 >= int(_30.GridY))) || (_601 >= int(_30.GridZ)))
                    {
                        _902 = _875;
                        _900 = _870;
                        _898 = _865;
                        _895 = _859;
                        continue;
                    }
                    uint _643 = uint(_595) + (_30.GridX * (uint(_598) + (_30.GridY * uint(_601))));
                    uint _647 = min(GridBuffer_1._data[_643], 64u);
                    float _858;
                    vec3 _864;
                    float _869;
                    uint _874;
                    _874 = _875;
                    _869 = _870;
                    _864 = _865;
                    _858 = _859;
                    float _896;
                    vec3 _899;
                    float _901;
                    uint _903;
                    SPIRV_CROSS_LOOP
                    for (uint _855 = 0u; _855 < _647; _874 = _903, _869 = _901, _864 = _899, _858 = _896, _855++)
                    {
                        uint _660 = (64u * (_515 + _643)) + _855;
                        if (GridBuffer_1._data[_660] >= _30.ParticleCount)
                        {
                            _903 = _874;
                            _901 = _869;
                            _899 = _864;
                            _896 = _858;
                            continue;
                        }
                        vec3 _675 = Predicted_1._data[gl_GlobalInvocationID.x].xyz - Predicted_1._data[GridBuffer_1._data[_660]].xyz;
                        float _678 = dot(_675, _675);
                        if (_678 >= _577)
                        {
                            _903 = _874;
                            _901 = _869;
                            _899 = _864;
                            _896 = _858;
                            continue;
                        }
                        float _856;
                        do
                        {
                            if (_678 >= (_30.SmoothingRadius * _30.SmoothingRadius))
                            {
                                _856 = 0.0;
                                break;
                            }
                            float _772 = (_30.SmoothingRadius * _30.SmoothingRadius) - _678;
                            _856 = ((((1.566681385040283203125 / pow(_30.SmoothingRadius, 9.0)) * _772) * _772) * _772) / max(_30.RestScale, 9.9999999747524270787835121154785e-07);
                            break;
                        } while(false);
                        vec3 _861;
                        do
                        {
                            float _801 = length(_675);
                            if ((_801 <= 9.9999999747524270787835121154785e-07) || (_801 >= _30.SmoothingRadius))
                            {
                                _861 = vec3(0.0);
                                break;
                            }
                            _861 = _675 * ((((((-14.323944091796875) / pow(_30.SmoothingRadius, 6.0)) * (_30.SmoothingRadius - _801)) * (_30.SmoothingRadius - _801)) / _801) / max(_30.RestScale, 9.9999999747524270787835121154785e-07));
                            break;
                        } while(false);
                        _903 = _874 + uint(1);
                        _901 = _869 + dot(_861, _861);
                        _899 = _864 + _861;
                        _896 = _858 + _856;
                    }
                    _902 = _874;
                    _900 = _869;
                    _898 = _864;
                    _895 = _858;
                }
            }
        }
        float _727 = (dot(_846, _846) + _847) + (_30.StiffnessEps * _30.RestDenomReference);
        Lambda_1._data[gl_GlobalInvocationID.x] = vec4((_727 > 0.0) ? ((-max((_848 / _30.RestDensity) - 1.0, 0.0)) / _727) : 0.0, _848, dot(_846, _846), float(_849));
        break;
    } while(false);
}

