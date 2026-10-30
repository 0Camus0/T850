// T850 pool fluid: stamps the moving bodies into the wall mask on the GPU.
//
// The mask used to be rebuilt on the CPU (a copy of every voxel) and re-created as a
// new GPU buffer whenever any body's cell footprint changed, which is a blocking
// upload that stalled the frame for ~10 ms while a ball moved. This kernel rewrites
// the persistent mask from the static base mask instead, with the same rules as the
// CPU stamp it replaces: authored solids stay 1, a fluid cell whose centre lies inside
// body k becomes 2 + k, and overlapping bodies resolve to the later index.

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
    float OpenCeiling;
    float OpenCeilingPad0;
    float OpenCeilingPad1;
    float OpenCeilingPad2;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
cbuffer StampConstants : register(b1)
{
    float BasinMinX;
    float BasinMinY;
    float BasinMinZ;
    uint BodyCount;
    float StepX;
    float StepY;
    float StepZ;
    float StampPad;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
StructuredBuffer<uint> BaseMask : register(t0);

// (x, y, z, radius) per body in world units; radius <= 0 marks a disabled body.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
StructuredBuffer<float4> Bodies : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
RWStructuredBuffer<uint> Mask : register(u0);

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    const uint index = id.x + Width * (id.y + Height * id.z);
    uint value = BaseMask[index];
    if (value == 0u) {
        const float px = BasinMinX + ((float)id.x + 0.5f) * StepX;
        const float py = BasinMinY + ((float)id.y + 0.5f) * StepY;
        const float pz = BasinMinZ + ((float)id.z + 0.5f) * StepZ;
        for (uint body = 0u; body < BodyCount; ++body) {
            const float4 sphere = Bodies[body];
            if (sphere.w <= 0.0f)
                continue;
            const float dx = px - sphere.x;
            const float dy = py - sphere.y;
            const float dz = pz - sphere.z;
            if (dx * dx + dy * dy + dz * dz <= sphere.w * sphere.w)
                value = 2u + body;
        }
    }
    Mask[index] = value;
}
