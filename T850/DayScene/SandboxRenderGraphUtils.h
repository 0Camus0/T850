#pragma once

#include <scene/RenderGraph.h>

namespace t850::sandbox {

  inline void RefreshDeferredPassHandles(
      RenderGraph& graph,
      int& gBuffer,
      int& deferred,
      int& extra16F,
      int& depth,
      int& shadowAccum,
      int& extraHelper,
      int& bloomAccum,
      int& adaptedLumCurrent,
      int& adaptedLumPrev) {
    gBuffer           = graph.GetRTHandle("GBuffer");
    deferred          = graph.GetRTHandle("Deferred");
    extra16F          = graph.GetRTHandle("Extra16F");
    depth             = graph.GetRTHandle("DepthPass");
    shadowAccum       = graph.GetRTHandle("ShadowAccum");
    extraHelper       = graph.GetRTHandle("ExtraHelper");
    bloomAccum        = graph.GetRTHandle("BloomAccum");
    adaptedLumCurrent = graph.GetRTHandle("AdaptedLumCurrent");
    adaptedLumPrev    = graph.GetRTHandle("AdaptedLumPrev");
  }

} // namespace t850::sandbox
