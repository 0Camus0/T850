#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace t850 {
inline uint16_t FloatToHalf(float value) {
  if (std::isnan(value)) return 0x7e00u;
  value = std::clamp(value, -65504.0f, 65504.0f);
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  const uint32_t sign = (bits >> 16) & 0x8000u;
  int exponent = static_cast<int>((bits >> 23) & 0xffu) - 127 + 15;
  uint32_t mantissa = bits & 0x7fffffu;
  if (exponent <= 0) {
    if (exponent < -10) return static_cast<uint16_t>(sign);
    mantissa = (mantissa | 0x800000u) >> (1 - exponent);
    return static_cast<uint16_t>(sign | ((mantissa + 0x1000u) >> 13));
  }
  mantissa += 0x1000u;
  if (mantissa & 0x800000u) { mantissa = 0; ++exponent; }
  if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7bffu);
  return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13));
}

inline float HalfToFloat(uint16_t value) {
  const float sign = (value & 0x8000u) ? -1.0f : 1.0f;
  const int exponent = int((value >> 10u) & 0x1Fu);
  const int mantissa = int(value & 0x03FFu);
  if (exponent == 0)
    return sign * std::ldexp(float(mantissa), -24);
  if (exponent == 31)
    return mantissa ? std::numeric_limits<float>::quiet_NaN() : sign * std::numeric_limits<float>::infinity();
  return sign * std::ldexp(float(1024 + mantissa), exponent - 25);
}

inline unsigned CalculateFullMipCount(unsigned width, unsigned height) {
  unsigned levels = 1;
  while (width > 1 || height > 1) {
    width = std::max(1u, width >> 1);
    height = std::max(1u, height >> 1);
    ++levels;
  }
  return levels;
}

inline void GenerateMipChain8(const unsigned char* source, unsigned width, unsigned height, unsigned faceCount,
                             unsigned bytesPerPixel, std::vector<unsigned char>& output) {
  const unsigned mipCount = CalculateFullMipCount(width, height);
  size_t faceBytes = 0;
  for (unsigned mip = 0; mip < mipCount; ++mip)
    faceBytes += static_cast<size_t>(std::max(1u, width >> mip)) * std::max(1u, height >> mip) * bytesPerPixel;
  output.resize(faceBytes * faceCount);
  size_t destinationOffset = 0;
  const size_t baseFaceBytes = static_cast<size_t>(width) * height * bytesPerPixel;
  for (unsigned face = 0; face < faceCount; ++face) {
    std::memcpy(output.data() + destinationOffset, source + face * baseFaceBytes, baseFaceBytes);
    size_t previousOffset = destinationOffset;
    destinationOffset += baseFaceBytes;
    unsigned previousWidth = width;
    unsigned previousHeight = height;
    for (unsigned mip = 1; mip < mipCount; ++mip) {
      const unsigned mipWidth = std::max(1u, previousWidth >> 1);
      const unsigned mipHeight = std::max(1u, previousHeight >> 1);
      const auto* previous = output.data() + previousOffset;
      auto* destination = output.data() + destinationOffset;
      for (unsigned row = 0; row < mipHeight; ++row) {
        for (unsigned column = 0; column < mipWidth; ++column) {
          const unsigned firstColumn = column * 2;
          const unsigned firstRow = row * 2;
          const unsigned secondColumn = std::min(firstColumn + 1, previousWidth - 1);
          const unsigned secondRow = std::min(firstRow + 1, previousHeight - 1);
          const std::array<size_t, 4> samples{
            (static_cast<size_t>(firstRow) * previousWidth + firstColumn) * bytesPerPixel,
            (static_cast<size_t>(firstRow) * previousWidth + secondColumn) * bytesPerPixel,
            (static_cast<size_t>(secondRow) * previousWidth + firstColumn) * bytesPerPixel,
            (static_cast<size_t>(secondRow) * previousWidth + secondColumn) * bytesPerPixel};
          const size_t destinationIndex = (static_cast<size_t>(row) * mipWidth + column) * bytesPerPixel;
          unsigned alphaSum = 0;
          if (bytesPerPixel == 4) {
            for (const auto sample : samples) alphaSum += previous[sample + 3];
            destination[destinationIndex + 3] = static_cast<unsigned char>((alphaSum + 2) / 4);
          }
          for (unsigned channel = 0; channel < (bytesPerPixel == 4 ? 3 : bytesPerPixel); ++channel) {
            unsigned sum = 0;
            for (const auto sample : samples)
              sum += previous[sample + channel] * (bytesPerPixel == 4 ? previous[sample + 3] : 1);
            destination[destinationIndex + channel] = static_cast<unsigned char>(bytesPerPixel == 4
              ? (alphaSum ? (sum + alphaSum / 2) / alphaSum : 0) : (sum + 2) / 4);
          }
        }
      }
      previousOffset = destinationOffset;
      previousWidth = mipWidth;
      previousHeight = mipHeight;
      destinationOffset += static_cast<size_t>(mipWidth) * mipHeight * bytesPerPixel;
    }
  }
}
}