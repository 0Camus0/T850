#pragma once

#include <utils/xMaths.h>

#include <cstdint>

namespace t850::game {

struct InputFrame {
  XVECTOR3 moveAxis = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  XVECTOR3 lookDelta = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  uint32_t buttonsDown = 0;
  uint32_t buttonsPressed = 0;
  XVECTOR3 pointerWorldRay = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  float dtSeconds = 0.0f;
};

} // namespace t850::game