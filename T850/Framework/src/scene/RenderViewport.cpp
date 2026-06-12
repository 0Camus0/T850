#include <pch.h>
#include <scene/RenderViewport.h>
#include <utils/Log.h>

#include <algorithm>

namespace t850 {

  int RenderViewport::ClampWidth(int width, const RenderViewportDesc& desc) const {
    return (std::max)((std::max)(1, desc.minWidth), width);
  }

  int RenderViewport::ClampHeight(int height, const RenderViewportDesc& desc) const {
    return (std::max)((std::max)(1, desc.minHeight), height);
  }

  bool RenderViewport::ShouldResize(int desiredWidth,
                                    int desiredHeight,
                                    bool resizeHeld,
                                    const RenderViewportDesc& desc) {
    desiredWidth = ClampWidth(desiredWidth, desc);
    desiredHeight = ClampHeight(desiredHeight, desc);

    if (m_handle < 0) {
      return true;
    }
    if (m_width == desiredWidth && m_height == desiredHeight) {
      m_pendingWidth = desiredWidth;
      m_pendingHeight = desiredHeight;
      m_stableFrames = 0;
      return false;
    }

    if (m_pendingWidth != desiredWidth || m_pendingHeight != desiredHeight) {
      m_pendingWidth = desiredWidth;
      m_pendingHeight = desiredHeight;
      m_stableFrames = 0;
      return false;
    }

    if (resizeHeld) {
      return false;
    }

    ++m_stableFrames;
    return m_stableFrames >= (std::max)(0, desc.stableFrameThreshold);
  }

  bool RenderViewport::Ensure(BaseDriver* driver,
                              int width,
                              int height,
                              const RenderViewportDesc& desc) {
    if (!driver) {
      return false;
    }

    width = ClampWidth(width, desc);
    height = ClampHeight(height, desc);
    if (m_handle >= 0 && m_width == width && m_height == height) {
      return true;
    }

    Destroy(driver);
    if (!desc.colorFormats.empty()) {
      m_handle = driver->CreateRT(
          static_cast<int>(desc.colorFormats.size()),
          desc.colorFormats,
          desc.depthFormat,
          width,
          height,
          desc.generateMips);
    } else {
      m_handle = driver->CreateRT(
          desc.colorCount,
          desc.colorFormat,
          desc.depthFormat,
          width,
          height,
          desc.generateMips);
    }

    if (m_handle < 0) {
      T8_LOG_ERROR("[RenderViewport] Failed to create RT %dx%d colors=%d",
                   width,
                   height,
                   desc.colorFormats.empty() ? desc.colorCount : static_cast<int>(desc.colorFormats.size()));
      return false;
    }

    m_width = width;
    m_height = height;
    m_pendingWidth = width;
    m_pendingHeight = height;
    m_stableFrames = 0;
    return true;
  }

  void RenderViewport::Destroy(BaseDriver* driver) {
    if (driver && m_handle >= 0) {
      driver->DestroyRT(m_handle);
    }
    m_handle = -1;
    m_width = 0;
    m_height = 0;
    m_pendingWidth = 0;
    m_pendingHeight = 0;
    m_stableFrames = 0;
  }

} // namespace t850
