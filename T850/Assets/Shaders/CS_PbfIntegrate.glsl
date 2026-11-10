#version 430
// GENERATED from CS_PbfIntegrate.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=46b0069b3284256d2a9909b9e2eaec817531040c0bd991ffee9c0da295aea300; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
        vec3 _673 = Velocities_1._data[gl_GlobalInvocationID.x].xyz;
        _673.y = Velocities_1._data[gl_GlobalInvocationID.x].y - (_59.Gravity * _59.DeltaTime);
        float _692 = length(vec2(_59.WindX, _59.WindZ));
        vec3 _1249;
        if (((_692 > 0.0) && ((Lambda_1._data[gl_GlobalInvocationID.x].w > 0.5) && (Lambda_1._data[gl_GlobalInvocationID.x].w < 27.0))) && (Positions_1._data[gl_GlobalInvocationID.x].y > (_59.WaterPlaneY - _59.ParticleRadius)))
        {
            vec3 _728 = (vec3(_59.WindX / _692, 0.0, _59.WindZ / _692) * _692) - _673;
            _1249 = _673 + (vec3(_728.x, 0.0, _728.z) * min((((_59.WindStrength * 1.5599999869664316065609455108643e-06) * length(_728.xz)) / max(_59.ParticleRadius, 9.9999997473787516355514526367188e-05)) * _59.DeltaTime, 0.5));
        }
        else
        {
            _1249 = _673;
        }
        float _755 = length(_1249);
        vec3 _1250;
        if (_755 > _59.MaxVelocity)
        {
            _1250 = _1249 * (_59.MaxVelocity / _755);
        }
        else
        {
            _1250 = _1249;
        }
        Velocities_1._data[gl_GlobalInvocationID.x] = vec4(_1250, 0.0);
        vec3 _781 = Positions_1._data[gl_GlobalInvocationID.x].xyz + (_1250 * _59.DeltaTime);
        float _836 = _59.ParticleRadius * 0.5;
        vec3 _1253;
        if ((((_781.x > (_59.BasinMinX - _836)) && (_781.x < (_59.BasinMaxX + _836))) && (_781.z > (_59.BasinMinZ - _836))) && (_781.z < (_59.BasinMaxZ + _836)))
        {
            float _876 = max(_781.y, _59.BasinMinY + _836);
            vec3 _1181 = _781;
            _1181.y = _876;
            vec3 _1254;
            if (_876 < _59.WallTopY)
            {
                _1254 = vec3(clamp(_781.x, _59.BasinMinX + _836, _59.BasinMaxX - _836), _876, clamp(_781.z, _59.BasinMinZ + _836, _59.BasinMaxZ - _836));
            }
            else
            {
                _1254 = _1181;
            }
            _1253 = _1254;
        }
        else
        {
            vec3 _1191 = _781;
            _1191.y = max(_781.y, (_59.WaterPlaneY + 0.1599999964237213134765625) + _836);
            _1253 = _1191;
        }
        vec3 _1252;
        _1252 = _1253;
        vec3 _1318;
        SPIRV_CROSS_LOOP
        for (uint _1251 = 0u; _1251 < _59.BoxCount; _1252 = _1318, _1251++)
        {
            if ((((((_1252.x <= SolidBoxes_1._data[_1251].lo.x) || (_1252.x >= SolidBoxes_1._data[_1251].hi.x)) || (_1252.y <= SolidBoxes_1._data[_1251].lo.y)) || (_1252.y >= SolidBoxes_1._data[_1251].hi.y)) || (_1252.z <= SolidBoxes_1._data[_1251].lo.z)) || (_1252.z >= SolidBoxes_1._data[_1251].hi.z))
            {
                _1318 = _1252;
                continue;
            }
            int _1255;
            float _1273;
            float _1286;
            if (Positions_1._data[gl_GlobalInvocationID.x].x <= SolidBoxes_1._data[_1251].lo.x)
            {
                _1286 = -1.0;
                _1273 = _1252.x - SolidBoxes_1._data[_1251].lo.x;
                _1255 = 0;
            }
            else
            {
                int _1256;
                float _1274;
                float _1287;
                if (Positions_1._data[gl_GlobalInvocationID.x].x >= SolidBoxes_1._data[_1251].hi.x)
                {
                    _1287 = 1.0;
                    _1274 = SolidBoxes_1._data[_1251].hi.x - _1252.x;
                    _1256 = 0;
                }
                else
                {
                    int _1257;
                    float _1275;
                    float _1288;
                    if (Positions_1._data[gl_GlobalInvocationID.x].y <= SolidBoxes_1._data[_1251].lo.y)
                    {
                        _1288 = -1.0;
                        _1275 = _1252.y - SolidBoxes_1._data[_1251].lo.y;
                        _1257 = 1;
                    }
                    else
                    {
                        int _1258;
                        float _1276;
                        float _1289;
                        if (Positions_1._data[gl_GlobalInvocationID.x].y >= SolidBoxes_1._data[_1251].hi.y)
                        {
                            _1289 = 1.0;
                            _1276 = SolidBoxes_1._data[_1251].hi.y - _1252.y;
                            _1258 = 1;
                        }
                        else
                        {
                            int _1259;
                            float _1277;
                            float _1290;
                            if (Positions_1._data[gl_GlobalInvocationID.x].z <= SolidBoxes_1._data[_1251].lo.z)
                            {
                                _1290 = -1.0;
                                _1277 = _1252.z - SolidBoxes_1._data[_1251].lo.z;
                                _1259 = 2;
                            }
                            else
                            {
                                bool _1037 = Positions_1._data[gl_GlobalInvocationID.x].z >= SolidBoxes_1._data[_1251].hi.z;
                                float _1278;
                                if (_1037)
                                {
                                    _1278 = SolidBoxes_1._data[_1251].hi.z - _1252.z;
                                }
                                else
                                {
                                    _1278 = 0.0;
                                }
                                _1290 = float(_1037);
                                _1277 = _1278;
                                _1259 = _1037 ? 2 : (-1);
                            }
                            _1289 = _1290;
                            _1276 = _1277;
                            _1258 = _1259;
                        }
                        _1288 = _1289;
                        _1275 = _1276;
                        _1257 = _1258;
                    }
                    _1287 = _1288;
                    _1274 = _1275;
                    _1256 = _1257;
                }
                _1286 = _1287;
                _1273 = _1274;
                _1255 = _1256;
            }
            float _1272;
            int _1279;
            float _1285;
            if (_1255 < 0)
            {
                float _1057 = _1252.x - SolidBoxes_1._data[_1251].lo.x;
                float _1062 = SolidBoxes_1._data[_1251].hi.x - _1252.x;
                float _1067 = _1252.y - SolidBoxes_1._data[_1251].lo.y;
                float _1072 = SolidBoxes_1._data[_1251].hi.y - _1252.y;
                float _1077 = _1252.z - SolidBoxes_1._data[_1251].lo.z;
                float _1082 = SolidBoxes_1._data[_1251].hi.z - _1252.z;
                bool _1086 = _1062 < _1057;
                float _1324 = _1086 ? _1062 : _1057;
                bool _1092 = _1067 < _1324;
                float _1327 = _1092 ? _1067 : _1324;
                bool _1098 = _1072 < _1327;
                float _1330 = _1098 ? _1072 : _1327;
                bool _1104 = _1077 < _1330;
                float _1333 = _1104 ? _1077 : _1330;
                bool _1110 = _1082 < _1333;
                _1285 = _1110 ? 1.0 : (_1104 ? (-1.0) : (_1098 ? 1.0 : (_1092 ? (-1.0) : (_1086 ? 1.0 : (-1.0)))));
                _1279 = _1110 ? 2 : (_1104 ? 2 : (_1098 ? 1 : int(_1092)));
                _1272 = _1110 ? _1082 : _1333;
            }
            else
            {
                _1285 = _1286;
                _1279 = _1255;
                _1272 = _1273;
            }
            float _1122 = min(_1272 + _836, _59.ParticleRadius * 0.0500000007450580596923828125);
            vec3 _1319;
            if (_1279 == 0)
            {
                vec3 _1242 = _1252;
                _1242.x = _1252.x + (_1285 * _1122);
                _1319 = _1242;
            }
            else
            {
                vec3 _1320;
                if (_1279 == 1)
                {
                    vec3 _1245 = _1252;
                    _1245.y = _1252.y + (_1285 * _1122);
                    _1320 = _1245;
                }
                else
                {
                    vec3 _1248 = _1252;
                    _1248.z = _1252.z + (_1285 * _1122);
                    _1320 = _1248;
                }
                _1319 = _1320;
            }
            _1318 = _1319;
        }
        Predicted_1._data[gl_GlobalInvocationID.x] = vec4(_1252, 1.0);
        break;
    } while(false);
}

