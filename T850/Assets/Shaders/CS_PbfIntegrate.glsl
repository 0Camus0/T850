#version 430
// GENERATED from CS_PbfIntegrate.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=32510e019c6f4af5fa83fb5ef6a95332adb6b2da4056fcd23219328bd3ff34b0; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
    float Pad6;
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
        vec3 _670 = Velocities_1._data[gl_GlobalInvocationID.x].xyz;
        _670.y = Velocities_1._data[gl_GlobalInvocationID.x].y - (_59.Gravity * _59.DeltaTime);
        float _689 = length(vec2(_59.WindX, _59.WindZ));
        vec3 _1243;
        if (((_689 > 0.0) && ((Lambda_1._data[gl_GlobalInvocationID.x].y > 9.9999997473787516355514526367188e-05) && (Lambda_1._data[gl_GlobalInvocationID.x].y < (0.89999997615814208984375 * _59.RestDensity)))) && (Positions_1._data[gl_GlobalInvocationID.x].y > (_59.WaterPlaneY - _59.ParticleRadius)))
        {
            vec3 _733 = (vec3(_59.WindX / _689, 0.0, _59.WindZ / _689) * _689) - _670;
            _1243 = _670 + (vec3(_733.x, 0.0, _733.z) * min(((_59.WindStrength * _689) * 0.001000000047497451305389404296875) * _59.DeltaTime, 0.5));
        }
        else
        {
            _1243 = _670;
        }
        float _749 = length(_1243);
        vec3 _1244;
        if (_749 > _59.MaxVelocity)
        {
            _1244 = _1243 * (_59.MaxVelocity / _749);
        }
        else
        {
            _1244 = _1243;
        }
        Velocities_1._data[gl_GlobalInvocationID.x] = vec4(_1244, 0.0);
        vec3 _775 = Positions_1._data[gl_GlobalInvocationID.x].xyz + (_1244 * _59.DeltaTime);
        float _830 = _59.ParticleRadius * 0.5;
        vec3 _1247;
        if ((((_775.x > (_59.BasinMinX - _830)) && (_775.x < (_59.BasinMaxX + _830))) && (_775.z > (_59.BasinMinZ - _830))) && (_775.z < (_59.BasinMaxZ + _830)))
        {
            float _870 = max(_775.y, _59.BasinMinY + _830);
            vec3 _1175 = _775;
            _1175.y = _870;
            vec3 _1248;
            if (_870 < _59.WaterPlaneY)
            {
                _1248 = vec3(clamp(_775.x, _59.BasinMinX + _830, _59.BasinMaxX - _830), _870, clamp(_775.z, _59.BasinMinZ + _830, _59.BasinMaxZ - _830));
            }
            else
            {
                _1248 = _1175;
            }
            _1247 = _1248;
        }
        else
        {
            vec3 _1185 = _775;
            _1185.y = max(_775.y, (_59.WaterPlaneY + 0.1599999964237213134765625) + _830);
            _1247 = _1185;
        }
        vec3 _1246;
        _1246 = _1247;
        vec3 _1312;
        SPIRV_CROSS_LOOP
        for (uint _1245 = 0u; _1245 < _59.BoxCount; _1246 = _1312, _1245++)
        {
            if ((((((_1246.x <= SolidBoxes_1._data[_1245].lo.x) || (_1246.x >= SolidBoxes_1._data[_1245].hi.x)) || (_1246.y <= SolidBoxes_1._data[_1245].lo.y)) || (_1246.y >= SolidBoxes_1._data[_1245].hi.y)) || (_1246.z <= SolidBoxes_1._data[_1245].lo.z)) || (_1246.z >= SolidBoxes_1._data[_1245].hi.z))
            {
                _1312 = _1246;
                continue;
            }
            int _1249;
            float _1267;
            float _1280;
            if (Positions_1._data[gl_GlobalInvocationID.x].x <= SolidBoxes_1._data[_1245].lo.x)
            {
                _1280 = -1.0;
                _1267 = _1246.x - SolidBoxes_1._data[_1245].lo.x;
                _1249 = 0;
            }
            else
            {
                int _1250;
                float _1268;
                float _1281;
                if (Positions_1._data[gl_GlobalInvocationID.x].x >= SolidBoxes_1._data[_1245].hi.x)
                {
                    _1281 = 1.0;
                    _1268 = SolidBoxes_1._data[_1245].hi.x - _1246.x;
                    _1250 = 0;
                }
                else
                {
                    int _1251;
                    float _1269;
                    float _1282;
                    if (Positions_1._data[gl_GlobalInvocationID.x].y <= SolidBoxes_1._data[_1245].lo.y)
                    {
                        _1282 = -1.0;
                        _1269 = _1246.y - SolidBoxes_1._data[_1245].lo.y;
                        _1251 = 1;
                    }
                    else
                    {
                        int _1252;
                        float _1270;
                        float _1283;
                        if (Positions_1._data[gl_GlobalInvocationID.x].y >= SolidBoxes_1._data[_1245].hi.y)
                        {
                            _1283 = 1.0;
                            _1270 = SolidBoxes_1._data[_1245].hi.y - _1246.y;
                            _1252 = 1;
                        }
                        else
                        {
                            int _1253;
                            float _1271;
                            float _1284;
                            if (Positions_1._data[gl_GlobalInvocationID.x].z <= SolidBoxes_1._data[_1245].lo.z)
                            {
                                _1284 = -1.0;
                                _1271 = _1246.z - SolidBoxes_1._data[_1245].lo.z;
                                _1253 = 2;
                            }
                            else
                            {
                                bool _1031 = Positions_1._data[gl_GlobalInvocationID.x].z >= SolidBoxes_1._data[_1245].hi.z;
                                float _1272;
                                if (_1031)
                                {
                                    _1272 = SolidBoxes_1._data[_1245].hi.z - _1246.z;
                                }
                                else
                                {
                                    _1272 = 0.0;
                                }
                                _1284 = float(_1031);
                                _1271 = _1272;
                                _1253 = _1031 ? 2 : (-1);
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
                _1280 = _1281;
                _1267 = _1268;
                _1249 = _1250;
            }
            float _1266;
            int _1273;
            float _1279;
            if (_1249 < 0)
            {
                float _1051 = _1246.x - SolidBoxes_1._data[_1245].lo.x;
                float _1056 = SolidBoxes_1._data[_1245].hi.x - _1246.x;
                float _1061 = _1246.y - SolidBoxes_1._data[_1245].lo.y;
                float _1066 = SolidBoxes_1._data[_1245].hi.y - _1246.y;
                float _1071 = _1246.z - SolidBoxes_1._data[_1245].lo.z;
                float _1076 = SolidBoxes_1._data[_1245].hi.z - _1246.z;
                bool _1080 = _1056 < _1051;
                float _1318 = _1080 ? _1056 : _1051;
                bool _1086 = _1061 < _1318;
                float _1321 = _1086 ? _1061 : _1318;
                bool _1092 = _1066 < _1321;
                float _1324 = _1092 ? _1066 : _1321;
                bool _1098 = _1071 < _1324;
                float _1327 = _1098 ? _1071 : _1324;
                bool _1104 = _1076 < _1327;
                _1279 = _1104 ? 1.0 : (_1098 ? (-1.0) : (_1092 ? 1.0 : (_1086 ? (-1.0) : (_1080 ? 1.0 : (-1.0)))));
                _1273 = _1104 ? 2 : (_1098 ? 2 : (_1092 ? 1 : int(_1086)));
                _1266 = _1104 ? _1076 : _1327;
            }
            else
            {
                _1279 = _1280;
                _1273 = _1249;
                _1266 = _1267;
            }
            float _1116 = min(_1266 + _830, _59.ParticleRadius * 0.0500000007450580596923828125);
            vec3 _1313;
            if (_1273 == 0)
            {
                vec3 _1236 = _1246;
                _1236.x = _1246.x + (_1279 * _1116);
                _1313 = _1236;
            }
            else
            {
                vec3 _1314;
                if (_1273 == 1)
                {
                    vec3 _1239 = _1246;
                    _1239.y = _1246.y + (_1279 * _1116);
                    _1314 = _1239;
                }
                else
                {
                    vec3 _1242 = _1246;
                    _1242.z = _1246.z + (_1279 * _1116);
                    _1314 = _1242;
                }
                _1313 = _1314;
            }
            _1312 = _1313;
        }
        Predicted_1._data[gl_GlobalInvocationID.x] = vec4(_1246, 1.0);
        break;
    } while(false);
}

