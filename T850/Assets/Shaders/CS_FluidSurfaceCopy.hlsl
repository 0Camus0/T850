// T850 pool fluid: copies the solver's ping-pong surface texture into the
// render-graph surface target so mesh passes can sample it.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
Texture2D<float4> SurfaceIn : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> SurfaceOut : register(u0);

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    uint width = 0u, height = 0u;
    SurfaceOut.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
        return;
    SurfaceOut[id.xy] = SurfaceIn.Load(int3(id.xy, 0));
}
