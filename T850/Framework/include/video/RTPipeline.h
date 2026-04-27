/*********************************************************
 * T850 Engine — Ray Tracing
 *
 * RTPipeline.h: Backend-agnostic ray tracing pipeline +
 *   shader binding table abstraction.
 *********************************************************/

#ifndef T800_RTPIPELINE_H
#define T800_RTPIPELINE_H

#include <Descriptors.h>
#include <cstdint>
#include <string>

namespace t850 {

  // ──────────────────────────────────────────────────────
  //  RTPipeline — wraps a backend-specific ray tracing
  //  state object (D3D12_STATE_OBJECT / VkPipeline) and
  //  its associated shader binding table.
  //
  //  Lifecycle:
  //    1. Device::CreateRTPipeline(raygenSrc, missSrc, closestHitSrc, key)
  //    2. BaseDriver::DispatchRays(w, h, pipeline)  — one or more times per frame
  //    3. pipeline->Destroy()  — when no longer needed
  // ──────────────────────────────────────────────────────
  class RTPipeline {
  public:
    virtual ~RTPipeline() = default;

    // Free all GPU resources (state object, SBT, root signature / pipeline layout).
    virtual void Destroy() = 0;

    // Shader key used to identify this pipeline in caches / debug output.
    ShaderKey key;

    // Human-readable label (set to the raygen file name by default).
    std::string label;
  };

} // namespace t850

#endif // T800_RTPIPELINE_H
