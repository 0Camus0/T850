#ifndef T850_RENDER_VIEWPORT_H
#define T850_RENDER_VIEWPORT_H

#include <video/BaseDriver.h>
#include <vector>

namespace t850 {

  struct RenderViewportDesc {
    int colorCount = 1;
    int colorFormat = BaseRT::RGBA8;
    std::vector<int> colorFormats;
    int depthFormat = BaseRT::F32;
    bool generateMips = false;
    int minWidth = 1;
    int minHeight = 1;
    int stableFrameThreshold = 8;
  };

  class RenderViewport {
  public:
    bool Ensure(BaseDriver* driver, int width, int height, const RenderViewportDesc& desc = RenderViewportDesc{});
    bool ShouldResize(int desiredWidth, int desiredHeight, bool resizeHeld, const RenderViewportDesc& desc = RenderViewportDesc{});
    void Destroy(BaseDriver* driver);

    int Handle() const { return m_handle; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }
    bool IsValid() const { return m_handle >= 0 && m_width > 0 && m_height > 0; }

  private:
    int ClampWidth(int width, const RenderViewportDesc& desc) const;
    int ClampHeight(int height, const RenderViewportDesc& desc) const;

    int m_handle = -1;
    int m_width = 0;
    int m_height = 0;
    int m_pendingWidth = 0;
    int m_pendingHeight = 0;
    int m_stableFrames = 0;
  };

} // namespace t850

#endif // T850_RENDER_VIEWPORT_H
