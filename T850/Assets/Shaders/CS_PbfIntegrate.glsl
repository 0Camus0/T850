#version 430
// GENERATED from CS_PbfIntegrate.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=81afc5eaec5788606bdd8585fab9e560a02704385ff04d5355b56f71265fc704; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

struct SolidBox
{
    vec4 lo;
    vec4 hi;
};

layout(binding = 1, std430) buffer Positions
{
    vec4 _data[];
} Positions_1;

layout(binding = 2, std430) buffer Predicted
{
    vec4 _data[];
} Predicted_1;

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
    float WallTopY;
    float WindX;
    float WindZ;
    float WindStrength;
    float Pad7;
} _59;

layout(binding = 4, std430) readonly buffer SolidBoxes
{
    SolidBox _data[];
} SolidBoxes_1;

layout(binding = 3, std430) buffer Velocities
{
    vec4 _data[];
} Velocities_1;

layout(binding = 5, std430) readonly buffer Lambda
{
    vec4 _data[];
} Lambda_1;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _59.ParticleCount)
        {
            break;
        }
        if (Positions_1._data[gl_GlobalInvocationID.x].w < 0.5)
        {
            Velocities_1._data[gl_GlobalInvocationID.x] = vec4(0.0);
            Predicted_1._data[gl_GlobalInvocationID.x] = vec4(Positions_1._data[gl_GlobalInvocationID.x].xyz, 0.0);
            break;
        }
        vec3 _671 = Velocities_1._data[gl_GlobalInvocationID.x].xyz;
        _671.y = Velocities_1._data[gl_GlobalInvocationID.x].y - (_59.Gravity * _59.DeltaTime);
        float _690 = length(vec2(_59.WindX, _59.WindZ));
        vec3 _1244;
        if (((_690 > 0.0) && ((Lambda_1._data[gl_GlobalInvocationID.x].y > 9.9999997473787516355514526367188e-05) && (Lambda_1._data[gl_GlobalInvocationID.x].y < (0.89999997615814208984375 * _59.RestDensity)))) && (Positions_1._data[gl_GlobalInvocationID.x].y > (_59.WaterPlaneY - _59.ParticleRadius)))
        {
            vec3 _734 = (vec3(_59.WindX / _690, 0.0, _59.WindZ / _690) * _690) - _671;
            _1244 = _671 + (vec3(_734.x, 0.0, _734.z) * min(((_59.WindStrength * _690) * 0.001000000047497451305389404296875) * _59.DeltaTime, 0.5));
        }
        else
        {
            _1244 = _671;
        }
        float _750 = length(_1244);
        vec3 _1245;
        if (_750 > _59.MaxVelocity)
        {
            _1245 = _1244 * (_59.MaxVelocity / _750);
        }
        else
        {
            _1245 = _1244;
        }
        Velocities_1._data[gl_GlobalInvocationID.x] = vec4(_1245, 0.0);
        vec3 _776 = Positions_1._data[gl_GlobalInvocationID.x].xyz + (_1245 * _59.DeltaTime);
        float _831 = _59.ParticleRadius * 0.5;
        vec3 _1248;
        if ((((_776.x > (_59.BasinMinX - _831)) && (_776.x < (_59.BasinMaxX + _831))) && (_776.z > (_59.BasinMinZ - _831))) && (_776.z < (_59.BasinMaxZ + _831)))
        {
            float _871 = max(_776.y, _59.BasinMinY + _831);
            vec3 _1176 = _776;
            _1176.y = _871;
            vec3 _1249;
            if (_871 < _59.WallTopY)
            {
                _1249 = vec3(clamp(_776.x, _59.BasinMinX + _831, _59.BasinMaxX - _831), _871, clamp(_776.z, _59.BasinMinZ + _831, _59.BasinMaxZ - _831));
            }
            else
            {
                _1249 = _1176;
            }
            _1248 = _1249;
        }
        else
        {
            vec3 _1186 = _776;
            _1186.y = max(_776.y, (_59.WaterPlaneY + 0.1599999964237213134765625) + _831);
            _1248 = _1186;
        }
        vec3 _1247;
        _1247 = _1248;
        vec3 _1313;
        SPIRV_CROSS_LOOP
        for (uint _1246 = 0u; _1246 < _59.BoxCount; _1247 = _1313, _1246++)
        {
            if ((((((_1247.x <= SolidBoxes_1._data[_1246].lo.x) || (_1247.x >= SolidBoxes_1._data[_1246].hi.x)) || (_1247.y <= SolidBoxes_1._data[_1246].lo.y)) || (_1247.y >= SolidBoxes_1._data[_1246].hi.y)) || (_1247.z <= SolidBoxes_1._data[_1246].lo.z)) || (_1247.z >= SolidBoxes_1._data[_1246].hi.z))
            {
                _1313 = _1247;
                continue;
            }
            int _1250;
            float _1268;
            float _1281;
            if (Positions_1._data[gl_GlobalInvocationID.x].x <= SolidBoxes_1._data[_1246].lo.x)
            {
                _1281 = -1.0;
                _1268 = _1247.x - SolidBoxes_1._data[_1246].lo.x;
                _1250 = 0;
            }
            else
            {
                int _1251;
                float _1269;
                float _1282;
                if (Positions_1._data[gl_GlobalInvocationID.x].x >= SolidBoxes_1._data[_1246].hi.x)
                {
                    _1282 = 1.0;
                    _1269 = SolidBoxes_1._data[_1246].hi.x - _1247.x;
                    _1251 = 0;
                }
                else
                {
                    int _1252;
                    float _1270;
                    float _1283;
                    if (Positions_1._data[gl_GlobalInvocationID.x].y <= SolidBoxes_1._data[_1246].lo.y)
                    {
                        _1283 = -1.0;
                        _1270 = _1247.y - SolidBoxes_1._data[_1246].lo.y;
                        _1252 = 1;
                    }
                    else
                    {
                        int _1253;
                        float _1271;
                        float _1284;
                        if (Positions_1._data[gl_GlobalInvocationID.x].y >= SolidBoxes_1._data[_1246].hi.y)
                        {
                            _1284 = 1.0;
                            _1271 = SolidBoxes_1._data[_1246].hi.y - _1247.y;
                            _1253 = 1;
                        }
                        else
                        {
                            int _1254;
                            float _1272;
                            float _1285;
                            if (Positions_1._data[gl_GlobalInvocationID.x].z <= SolidBoxes_1._data[_1246].lo.z)
                            {
                                _1285 = -1.0;
                                _1272 = _1247.z - SolidBoxes_1._data[_1246].lo.z;
                                _1254 = 2;
                            }
                            else
                            {
                                bool _1032 = Positions_1._data[gl_GlobalInvocationID.x].z >= SolidBoxes_1._data[_1246].hi.z;
                                float _1273;
                                if (_1032)
                                {
                                    _1273 = SolidBoxes_1._data[_1246].hi.z - _1247.z;
                                }
                                else
                                {
                                    _1273 = 0.0;
                                }
                                _1285 = float(_1032);
                                _1272 = _1273;
                                _1254 = _1032 ? 2 : (-1);
                            }
                            _1284 = _1285;
                            _1271 = _1272;
                            _1253 = _1254;
                        }
                        _1283 = _1284;
                        _1270 = _1271;
                        _1252 = _1253;
                    }
                    _1282 = _1283;
                    _1269 = _1270;
                    _1251 = _1252;
                }
                _1281 = _1282;
                _1268 = _1269;
                _1250 = _1251;
            }
            float _1267;
            int _1274;
            float _1280;
            if (_1250 < 0)
            {
                float _1052 = _1247.x - SolidBoxes_1._data[_1246].lo.x;
                float _1057 = SolidBoxes_1._data[_1246].hi.x - _1247.x;
                float _1062 = _1247.y - SolidBoxes_1._data[_1246].lo.y;
                float _1067 = SolidBoxes_1._data[_1246].hi.y - _1247.y;
                float _1072 = _1247.z - SolidBoxes_1._data[_1246].lo.z;
                float _1077 = SolidBoxes_1._data[_1246].hi.z - _1247.z;
                bool _1081 = _1057 < _1052;
                float _1319 = _1081 ? _1057 : _1052;
                bool _1087 = _1062 < _1319;
                float _1322 = _1087 ? _1062 : _1319;
                bool _1093 = _1067 < _1322;
                float _1325 = _1093 ? _1067 : _1322;
                bool _1099 = _1072 < _1325;
                float _1328 = _1099 ? _1072 : _1325;
                bool _1105 = _1077 < _1328;
                _1280 = _1105 ? 1.0 : (_1099 ? (-1.0) : (_1093 ? 1.0 : (_1087 ? (-1.0) : (_1081 ? 1.0 : (-1.0)))));
                _1274 = _1105 ? 2 : (_1099 ? 2 : (_1093 ? 1 : int(_1087)));
                _1267 = _1105 ? _1077 : _1328;
            }
            else
            {
                _1280 = _1281;
                _1274 = _1250;
                _1267 = _1268;
            }
            float _1117 = min(_1267 + _831, _59.ParticleRadius * 0.0500000007450580596923828125);
            vec3 _1314;
            if (_1274 == 0)
            {
                vec3 _1237 = _1247;
                _1237.x = _1247.x + (_1280 * _1117);
                _1314 = _1237;
            }
            else
            {
                vec3 _1315;
                if (_1274 == 1)
                {
                    vec3 _1240 = _1247;
                    _1240.y = _1247.y + (_1280 * _1117);
                    _1315 = _1240;
                }
                else
                {
                    vec3 _1243 = _1247;
                    _1243.z = _1247.z + (_1280 * _1117);
                    _1315 = _1243;
                }
                _1314 = _1315;
            }
            _1313 = _1314;
        }
        Predicted_1._data[gl_GlobalInvocationID.x] = vec4(_1247, 1.0);
        break;
    } while(false);
}

