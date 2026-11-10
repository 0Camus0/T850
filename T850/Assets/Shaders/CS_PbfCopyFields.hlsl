// T850 Position Based Fluids: copy an updated field back into the live buffer.
//
// Several PBF stages are Jacobi-style: they read a buffer and write an updated version of it
// (XSPH viscosity, the velocity update). The obvious implementation is to ping-pong two
// buffers and swap the handles, but that silently invalidates every pointer anybody else
// holds -- the particle renderer is handed the position/velocity buffers by name, and a
// descriptor set recorded earlier in the frame still refers to the old allocation. That is
// what crashed the renderer.
//
// So the handles never change: the stage writes into a scratch buffer and this kernel copies
// the result back in place. One extra dispatch is far cheaper than an ownership problem.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer PbfConstants : register(b0)
{
    uint  ParticleCount;
    uint  GridX;
    uint  GridY;
    uint  GridZ;
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
    uint  BoxCount;
    uint  Iteration;
    uint  SurfaceWidth;
    uint  SurfaceDepth;
    float WaterPlaneY;
    float RestScale;
    float RestDenomReference;
    float MinNeighbours;
    float WallTopY;   // solid walls end here (the pool rim); open above
    float WindX;
    float WindZ;
    float WindStrength;
    float Pad7;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
StructuredBuffer<float4> Source : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
RWStructuredBuffer<float4> Destination : register(u0);

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= ParticleCount)
        return;
    // One float4 per particle (xyz + alive flag), matching the host's 16-byte stride.
    Destination[i] = Source[i];
}
