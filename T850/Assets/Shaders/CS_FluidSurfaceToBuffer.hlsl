// T850 pool fluid: copy the 2D surface texture to a buffer for CPU readback.
//
// The surface texture is what the vertex shader displaces the water mesh with, so it is
// the only way to test the render-facing path. Reading the volume fraction instead
// cannot catch a bug in the surface kernel itself: CS_FluidSurface3D once derived its
// grid row from id.z (always 0 in a (width, depth, 1) dispatch), so every one of the
// 50 output rows was filled from grid column z = 0 - the texture varied along X and was
// exactly constant along Y, while the alpha volume was perfectly correct. A gate that
// reads alpha passes that bug; a gate that reads this texture does not.
//
// One float4 per texel: R = elevation delta from the water plane (m), G = foam,
// B/A = tent-filtered per-texel height differences.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer FluidConstants : register(b0)
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
    // 1 when the rows above the domain are open air, 0 for a closed ceiling.
    float OpenCeiling;
    float OpenCeilingPad0;
    float OpenCeilingPad1;
    float OpenCeilingPad2;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture2D<float4> SurfaceIn : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
RWStructuredBuffer<float4> SurfaceOut : register(u0);

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    // The surface is width x depth: dispatch as (Width, Depth, 1) and index with id.xy.
    if (id.x >= Width || id.y >= Depth)
        return;
    SurfaceOut[id.x + Width * id.y] = SurfaceIn.Load(int3(int(id.x), int(id.y), 0));
}
