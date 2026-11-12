#version 430
// GENERATED from CS_PbfParticles.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=a3fc51ee95443964253918b792d8abee8426e55a7ac6095ec613d7b8793dc97f; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

float _1014;

layout(binding = 5, std430) readonly buffer ViewProjection
{
    float _data[];
} ViewProjection_1;

layout(binding = 0, std140) uniform ParticleConstants
{
    uint ParticleCount;
    uint OutputWidth;
    uint OutputHeight;
    uint Iteration;
    float ParticleRadius;
    float DepthBias;
    float Opacity;
    float Pad0;
    vec3 CameraPosition;
    float Pad1;
    vec3 FluidColor;
    float TileCountX;
    float TileCountY;
    float Pad2;
    float Pad3;
} _136;

layout(binding = 1, std430) readonly buffer Positions
{
    vec4 _data[];
} Positions_1;

layout(binding = 3, std430) buffer Projected
{
    vec4 _data[];
} Projected_1;

layout(binding = 4, std430) buffer TileMasks
{
    uint _data[];
} TileMasks_1;

layout(binding = 2, std430) readonly buffer Velocities
{
    vec4 _data[];
} Velocities_1;

layout(binding = 6, rgba32f) uniform writeonly image2D OutputTexture;

void main()
{
    do
    {
        if (_136.Iteration == 0u)
        {
            if (gl_GlobalInvocationID.x >= _136.ParticleCount)
            {
                break;
            }
            vec4 _593 = Positions_1._data[gl_GlobalInvocationID.x];
            float _893 = ViewProjection_1._data[0];
            float _898 = ViewProjection_1._data[4];
            float _904 = ViewProjection_1._data[8];
            float _908 = ViewProjection_1._data[12];
            float _913 = ViewProjection_1._data[1];
            float _918 = ViewProjection_1._data[5];
            float _924 = ViewProjection_1._data[9];
            float _928 = ViewProjection_1._data[13];
            float _953 = ViewProjection_1._data[3];
            float _958 = ViewProjection_1._data[7];
            float _964 = ViewProjection_1._data[11];
            float _968 = ViewProjection_1._data[15];
            float _969 = (((_593.x * _953) + (_593.y * _958)) + (_593.z * _964)) + _968;
            if (_969 <= 9.9999997473787516355514526367188e-05)
            {
                Projected_1._data[gl_GlobalInvocationID.x] = vec4(0.0, 0.0, -1.0, 0.0);
                break;
            }
            vec2 _609 = vec4((((_593.x * _893) + (_593.y * _898)) + (_593.z * _904)) + _908, (((_593.x * _913) + (_593.y * _918)) + (_593.z * _924)) + _928, _1014, _969).xy / vec2(_969);
            float _613 = (_609.x * 0.5) + 0.5;
            float _617 = 0.5 - (_609.y * 0.5);
            Projected_1._data[gl_GlobalInvocationID.x] = vec4(_613, _617, (_136.ParticleRadius * float(_136.OutputHeight)) / max(_969, 0.001000000047497451305389404296875), _969);
            if ((((_613 < 0.0) || (_613 > 1.0)) || (_617 < 0.0)) || (_617 > 1.0))
            {
                break;
            }
            uint _692 = atomicOr(TileMasks_1._data[min(uint(_613 * float(_136.OutputWidth / 8u)), uint(max(_136.TileCountX - 1.0, 0.0))) + (uint(_136.TileCountX) * min(uint(_617 * float(_136.OutputHeight / 8u)), uint(max(_136.TileCountY - 1.0, 0.0))))], 1u);
            break;
        }
        uint _698 = gl_GlobalInvocationID.x % _136.OutputWidth;
        uint _703 = gl_GlobalInvocationID.x / _136.OutputWidth;
        uvec2 _704 = uvec2(_698, _703);
        if (gl_GlobalInvocationID.x >= (_136.OutputWidth * _136.OutputHeight))
        {
            break;
        }
        uint _717 = _698 / 8u;
        uint _720 = _703 / 8u;
        if (TileMasks_1._data[_717 + (uint(_136.TileCountX) * _720)] == 0u)
        {
            imageStore(OutputTexture, ivec2(_704), vec4(0.0));
            break;
        }
        vec2 _740 = vec2(_704) + vec2(0.5);
        float _1016;
        vec3 _1017;
        _1017 = vec3(0.0);
        _1016 = 0.0;
        float _1019;
        vec3 _1020;
        SPIRV_CROSS_LOOP
        for (uint _1015 = 0u; _1015 < _136.ParticleCount; _1017 = _1020, _1016 = _1019, _1015++)
        {
            if (Projected_1._data[_1015].z <= 0.0)
            {
                _1020 = _1017;
                _1019 = _1016;
                continue;
            }
            vec2 _765 = Projected_1._data[_1015].xy * vec2(float(_136.OutputWidth), float(_136.OutputHeight));
            float _768 = float(_717 * 8u);
            float _771 = float(_720 * 8u);
            vec2 _774 = vec2(_768, _771) + vec2(8.0);
            if (((((_765.x + Projected_1._data[_1015].z) < _768) || ((_765.x - Projected_1._data[_1015].z) > _774.x)) || ((_765.y + Projected_1._data[_1015].z) < _771)) || ((_765.y - Projected_1._data[_1015].z) > _774.y))
            {
                _1020 = _1017;
                _1019 = _1016;
                continue;
            }
            float _814 = length(_740 - _765);
            if (_814 > Projected_1._data[_1015].z)
            {
                _1020 = _1017;
                _1019 = _1016;
                continue;
            }
            float _849 = (smoothstep(0.0, 0.3499999940395355224609375, clamp(1.0 - (_814 / max(Projected_1._data[_1015].z, 0.001000000047497451305389404296875)), 0.0, 1.0)) * _136.Opacity) * clamp(1.0 - _1016, 0.0, 1.0);
            _1020 = _1017 + (mix(_136.FluidColor, vec3(1.0), vec3(clamp(length(Velocities_1._data[_1015].xyz) * 0.125, 0.0, 1.0) * 0.3499999940395355224609375)) * _849);
            _1019 = _1016 + _849;
        }
        if (_1016 <= 0.001000000047497451305389404296875)
        {
            imageStore(OutputTexture, ivec2(_704), vec4(0.0));
            break;
        }
        imageStore(OutputTexture, ivec2(_704), vec4(_1017 / vec3(max(_1016, 9.9999997473787516355514526367188e-05)), clamp(_1016, 0.0, 1.0)));
        break;
    } while(false);
}

