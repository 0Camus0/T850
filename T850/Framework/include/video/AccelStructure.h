/*********************************************************
 * T850 Engine — Ray Tracing
 *
 * AccelStructure.h: Backend-agnostic acceleration structure
 *   abstractions (BLAS + TLAS) for DXR and Vulkan RT.
 *********************************************************/

#ifndef T850_ACCELSTRUCTURE_H
#define T850_ACCELSTRUCTURE_H

#include <cstdint>
#include <utils/xMaths.h>

namespace t850 {

  // ──────────────────────────────────────────────────────
  //  Instance descriptor: one entry in the TLAS instance
  //  buffer.  Layout is compatible with both
  //  D3D12_RAYTRACING_INSTANCE_DESC and
  //  VkAccelerationStructureInstanceKHR when the first 12
  //  floats are a row-major 3×4 affine transform.
  // ──────────────────────────────────────────────────────
  struct RTInstanceDesc {
    float     transform[3][4];   // row-major affine world transform
    uint32_t  instanceID        : 24; // SV_InstanceID / gl_InstanceCustomIndexEXT
    uint32_t  instanceMask      :  8; // visibility mask (0xFF = always visible)
    uint32_t  instanceContribToHitGroupIndex : 24; // hit group start offset in SBT
    uint32_t  flags             :  8; // D3D12_RAYTRACING_INSTANCE_FLAGS / VkGeometryInstanceFlagsKHR
    uint64_t  blasGPUAddress;         // GPU VA of the bottom-level AS
  };

  // ──────────────────────────────────────────────────────
  //  Bottom-Level Acceleration Structure
  //  One BLAS per mesh-subset (static or dynamic).
  // ──────────────────────────────────────────────────────
  class BLAS {
  public:
    virtual ~BLAS() = default;

    // Build or rebuild the BLAS from the vertex/index buffer that was
    // supplied at creation time.  Must be called inside an active command
    // list / command buffer (i.e. between BeginFrame and EndFrame).
    // allowUpdate = true → use ALLOW_UPDATE | PREFER_FAST_BUILD (skinned).
    virtual void Build(bool allowUpdate = false) = 0;

    // Refit (in-place update) of an existing BLAS built with allowUpdate=true.
    // Cheaper than a full rebuild; used for animated geometry each frame.
    virtual void Refit() = 0;

    // Free all GPU resources.
    virtual void Destroy() = 0;

    // Returns the GPU virtual address / device address of the AS result
    // buffer (used to fill RTInstanceDesc::blasGPUAddress).
    virtual uint64_t GetGPUAddress() const = 0;

    bool isBuilt = false;
  };

  // ──────────────────────────────────────────────────────
  //  Top-Level Acceleration Structure
  //  One TLAS per scene; rebuilt / refitted every frame.
  // ──────────────────────────────────────────────────────
  class TLAS {
  public:
    virtual ~TLAS() = default;

    // (Re)build the TLAS from an array of RTInstanceDesc entries.
    // instances    – pointer to RTInstanceDesc array
    // instanceCount – number of valid entries (≤ maxInstances supplied at
    //                CreateTLAS time)
    virtual void Build(const RTInstanceDesc* instances, uint32_t instanceCount) = 0;

    // Free all GPU resources.
    virtual void Destroy() = 0;

    // Returns a GPU virtual address / device address suitable for use as a
    // shader-visible SRV (D3D12) or descriptor (Vulkan).
    virtual uint64_t GetGPUAddress() const = 0;

    // D3D12 only: returns the CPU-side descriptor handle for the TLAS SRV
    // that was placed into the CBV/SRV/UAV heap.  Returns 0 on other backends.
    virtual uint64_t GetSRVDescriptorIndex() const { return 0; }
  };

} // namespace t850

#endif // T850_ACCELSTRUCTURE_H
