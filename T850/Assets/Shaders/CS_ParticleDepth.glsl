#version 430
// GENERATED from CS_ParticleDepth.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=cc93486667c2e372021cf0646deada976d412e9614f9b9f36bc918af465b05ae; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, std140) uniform ParticleConstants
{
    uint ParticleCount;
    uint OutputWidth;
    uint OutputHeight;
    uint Iteration;
    float Radius;
    float Opacity;
    float Intensity;
    float Softness;
    float DepthBias;
    float Lifetime;
    float Spread;
    float Rise;
    float TimeSeconds;
    float SizeScaleMin;
    float SizeScaleMax;
    uint Shape;
    uint Blend;
    uint Source;
    float ScreenSpace;
    float TileCountX;
    float TileCountY;
    uint FrameIndex;
    float ProjScaleY;
    float Style;
    vec3 Color0;
    float Color0Pad;
    vec3 Color1;
    float Color1Pad;
    vec3 Color2;
    float Color2Pad;
    vec3 Color3;
    float Color3Pad;
    vec3 EmitterPosition;
    float EmitterPad;
    vec4 ViewProjection[4];
} _24;

layout(binding = 2, std430) readonly buffer TileMasks
{
    uint _data[];
} TileMasks_1;

layout(binding = 1, std430) readonly buffer Projected
{
    vec4 _data[];
} Projected_1;

layout(binding = 4, rgba16f) uniform writeonly image2D SurfaceDepth;
layout(binding = 3) uniform sampler2D SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if ((gl_GlobalInvocationID.x >= _24.OutputWidth) || (gl_GlobalInvocationID.y >= _24.OutputHeight))
        {
            break;
        }
        uvec2 _543 = uvec2(gl_GlobalInvocationID.x, (_24.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        uint _545 = gl_GlobalInvocationID.x / 8u;
        uint _547 = gl_GlobalInvocationID.y / 8u;
        if (_24.ScreenSpace < 0.5)
        {
            imageStore(SurfaceDepth, ivec2(_543), vec4(0.0));
            break;
        }
        if (TileMasks_1._data[_545 + (uint(_24.TileCountX) * _547)] != _24.FrameIndex)
        {
            imageStore(SurfaceDepth, ivec2(_543), vec4(0.0));
            break;
        }
        vec4 _599 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(int(gl_GlobalInvocationID.x), int((_24.OutputHeight - 1u) - gl_GlobalInvocationID.y), 0).xy, 0);
        float _600 = _599.x;
        float _611 = (abs(_600 - _24.ViewProjection[2].z) > 9.999999717180685365747194737196e-10) ? (_24.ViewProjection[3].z / (_600 - _24.ViewProjection[2].z)) : 1000000000.0;
        float _614 = float(_545 * 8u);
        float _617 = float(_547 * 8u);
        vec2 _620 = vec2(_614, _617) + vec2(8.0);
        vec2 _627 = vec2(float(gl_GlobalInvocationID.x) + 0.5, float(gl_GlobalInvocationID.y) + 0.5);
        float _632 = _24.Radius * _24.Radius;
        float _867;
        float _868;
        float _869;
        _869 = 0.0;
        _868 = 0.0;
        _867 = 1000000000.0;
        float _899;
        float _900;
        float _901;
        SPIRV_CROSS_LOOP
        for (uint _866 = 0u; _866 < _24.ParticleCount; _869 = _901, _868 = _900, _867 = _899, _866++)
        {
            if (Projected_1._data[_866].z <= 0.0)
            {
                _901 = _869;
                _900 = _868;
                _899 = _867;
                continue;
            }
            vec2 _657 = Projected_1._data[_866].xy * vec2(float(_24.OutputWidth), float(_24.OutputHeight));
            if (((((_657.x + Projected_1._data[_866].z) < _614) || ((_657.x - Projected_1._data[_866].z) > _620.x)) || ((_657.y + Projected_1._data[_866].z) < _617)) || ((_657.y - Projected_1._data[_866].z) > _620.y))
            {
                _901 = _869;
                _900 = _868;
                _899 = _867;
                continue;
            }
            vec2 _695 = _627 - _657;
            float _701 = Projected_1._data[_866].z * (1.0 - clamp(_24.Softness, 0.0, 1.0));
            float _870;
            if (_24.Shape == 1u)
            {
                _870 = max(abs(_695.x), abs(_695.y));
            }
            else
            {
                float _871;
                if (_24.Shape == 2u)
                {
                    _871 = length(_695 * vec2(1.0, 0.25));
                }
                else
                {
                    _871 = length(_695);
                }
                _870 = _871;
            }
            float _872;
            if (_24.Style >= 1.5)
            {
                _872 = float(_870 <= Projected_1._data[_866].z);
            }
            else
            {
                float _873;
                if (_24.Style >= 0.5)
                {
                    _873 = 1.0 - smoothstep(0.0, max(1.0, Projected_1._data[_866].z * 0.100000001490116119384765625), abs(_870 - (Projected_1._data[_866].z * 0.87999999523162841796875)));
                }
                else
                {
                    _873 = 1.0 - smoothstep(_701, max(Projected_1._data[_866].z, _701 + 9.9999997473787516355514526367188e-05), _870);
                }
                _872 = _873;
            }
            if (_872 <= 0.0)
            {
                _901 = _869;
                _900 = _868;
                _899 = _867;
                continue;
            }
            float _776 = min(_870 * (_24.Radius / max(Projected_1._data[_866].z, 0.001000000047497451305389404296875)), _24.Radius);
            float _783 = sqrt(max(_632 - (_776 * _776), 0.0));
            float _787 = Projected_1._data[_866].w - _783;
            float _877;
            if (_24.DepthBias > 0.0)
            {
                float _798 = clamp((_611 - _787) / _24.DepthBias, 0.0, 1.0);
                if (_798 <= 0.0)
                {
                    _901 = _869;
                    _900 = _868;
                    _899 = _867;
                    continue;
                }
                _877 = _798;
            }
            else
            {
                _877 = 1.0;
            }
            float _806 = _872 * _877;
            _901 = _869 + ((_806 * 2.0) * _783);
            _900 = _868 + _806;
            _899 = min(_867, _787);
        }
        if ((_867 >= 100000000.0) || (_868 <= 9.9999997473787516355514526367188e-06))
        {
            imageStore(SurfaceDepth, ivec2(_543), vec4(0.0));
            break;
        }
        imageStore(SurfaceDepth, ivec2(_543), vec4(_867, _869, _868, clamp(1.0 - exp(_869 * (-2.0)), 0.0, 1.0)));
        break;
    } while(false);
}

