#include <game/GameSelfTest.h>
#include <core/Core.h>
#include <emscripten/stack.h>
#include <cstdio>
#include <thread>
#include <utils/cil.h>
#include <utils/TextureMipmaps.h>

bool CheckBCFallback() {
  const unsigned char redBlock[8] = {0, 248, 0, 0, 0, 0, 0, 0};
  std::vector<unsigned char> encoded;
  for (unsigned block = 0; block < 18; ++block) encoded.insert(encoded.end(), std::begin(redBlock), std::end(redBlock));
  std::vector<unsigned char> decoded;
  if (!t850::DecompressDXTToRGBA(encoded.data(), encoded.size(), 4, 4, 3, 6, CIL_DXT1, decoded) || decoded.size() != 6 * (16 + 4 + 1) * 4) return false;
  for (size_t pixel = 0; pixel < decoded.size(); pixel += 4)
    if (decoded[pixel] != 255 || decoded[pixel + 1] || decoded[pixel + 2] || decoded[pixel + 3] != 255) return false;
  if (t850::DecompressDXTToRGBA(encoded.data(), encoded.size() - 1, 4, 4, 3, 6, CIL_DXT1, decoded)) return false;
  const unsigned char transparent[8] = {0, 0, 255, 255, 255, 255, 255, 255};
  if (!t850::DecompressDXTToRGBA(transparent, 8, 1, 1, 1, 1, CIL_DXT1, decoded) || decoded[3] != 0) return false;
  unsigned char alphaBlock[16] = {255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 255, 255, 255, 255, 255, 255};
  if (!t850::DecompressDXTToRGBA(alphaBlock, 16, 1, 1, 1, 1, CIL_DXT3, decoded) || decoded[0] != 170 || decoded[3] != 255) return false;
  alphaBlock[0] = 0; alphaBlock[1] = 255; alphaBlock[2] = 7;
  if (!t850::DecompressDXTToRGBA(alphaBlock, 16, 1, 1, 1, 1, CIL_DXT5, decoded) || decoded[0] != 170 || decoded[3] != 255) return false;
  return true;
}

t850::AppBase* pApp = nullptr;

[[gnu::noinline]] bool ExerciseStackFrame() {
  volatile unsigned char scratch[640 * 1024];
  for (size_t offset = 0; offset < sizeof(scratch); offset += 4096)
    scratch[offset] = static_cast<unsigned char>(offset / 4096);
  for (size_t offset = 0; offset < sizeof(scratch); offset += 4096)
    if (scratch[offset] != static_cast<unsigned char>(offset / 4096)) return false;
  return true;
}

bool CheckStackBudget() {
  if (emscripten_stack_get_free() < 1024 * 1024) return false;
  return ExerciseStackFrame();
}

int main() {
  for (float value : {0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 65504.0f, -65504.0f, 0.00006103515625f}) {
    if (t850::HalfToFloat(t850::FloatToHalf(value)) != value) return 1;
  }
  if (!CheckBCFallback()) { std::fputs("BC texture fallback FAIL\n", stderr); return 1; }
  std::puts("BC texture fallback PASS (mips, cubemap, transparency, BC2/BC3 alpha, truncated payload)");
  bool mainStackReady = CheckStackBudget();
  bool workerStackReady = false;
  std::thread worker([&workerStackReady]() { workerStackReady = CheckStackBudget(); });
  worker.join();
  if (!mainStackReady || !workerStackReady) {
    std::fprintf(stderr, "Wasm stack budget FAIL: main=%d worker=%d\n", mainStackReady, workerStackReady);
    return 1;
  }
  std::puts("Wasm main/pthread stack budget PASS (640 KiB stack frame)");
  return t850::game::RunGameSelfTests();
}