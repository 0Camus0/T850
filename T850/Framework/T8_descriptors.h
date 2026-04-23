#ifndef T800_DESCRIPTORS_H
#define T800_DESCRIPTORS_H
#include <video/ShaderBase.h>
#include <cstdint>

namespace t800 {

// ── Pass type (mutually exclusive, 6 bits → 64 max) ──
namespace PassType {
  enum E : uint8_t {
    NONE = 0,
    FORWARD,
    GBUFFER,
    SHADOW_MAP,
    FSQUAD_1_TEX,
    FSQUAD_2_TEX,
    FSQUAD_3_TEX,
    FSQUAD_TESTING,
    DEFERRED,
    SHADOW_COMP,
    VERTICAL_BLUR,
    HORIZONTAL_BLUR,
    ONE_PASS_BLUR,
    BRIGHT,
    HDR_COMP,
    LUMINANCE_MAP,
    ADAPT_LUMINANCE,
    COC,
    COMBINE_COC,
    DOF,
    DOF_2,
    BACKBUFFER,
    GOD_RAY_CALCULATION,
    GOD_RAY_BLEND,
    SSAO,
    RAY_MARCH,
    RADIAL_DEPTH,
    LIGHT_RAY_MARCHING,
    LIGHT_ADD,
    FADE,
    DEFERRED_LDR,
    COUNT
  };
}

// ── Structured shader key: 32 bits ──
//  Bits  0-4:  vertex attributes (5)
//  Bits  5-11: texture maps (7)
//  Bits 12-14: special modes (3)
//  Bits 15-19: effect toggles (5)
//  Bits 20-25: pass type (6-bit enum)
//  Bits 26-31: reserved (6)
struct ShaderKey {
  uint32_t bits;

  constexpr ShaderKey() : bits(0xFFFFFFFF) {}
  constexpr explicit ShaderKey(uint32_t b) : bits(b) {}

  // ── Vertex attributes ──
  static constexpr uint32_t HAS_NORMALS    = 1u << 0;
  static constexpr uint32_t HAS_TANGENTS   = 1u << 1;
  static constexpr uint32_t HAS_BINORMALS  = 1u << 2;
  static constexpr uint32_t HAS_TEXCOORD0  = 1u << 3;
  static constexpr uint32_t HAS_TEXCOORD1  = 1u << 4;

  // ── Texture maps ──
  static constexpr uint32_t DIFFUSE_MAP    = 1u << 5;
  static constexpr uint32_t SPECULAR_MAP   = 1u << 6;
  static constexpr uint32_t GLOSS_MAP      = 1u << 7;
  static constexpr uint32_t NORMAL_MAP     = 1u << 8;
  static constexpr uint32_t REFLECT_MAP    = 1u << 9;
  static constexpr uint32_t HEIGHT_MAP     = 1u << 10;
  static constexpr uint32_t METALLIC_MAP   = 1u << 11;

  // ── Special modes ──
  static constexpr uint32_t NO_LIGHT       = 1u << 12;
  static constexpr uint32_t FRESNEL        = 1u << 13;
  static constexpr uint32_t OMNI_SHADOWS   = 1u << 14;

  // ── Effect toggles (converted from runtime branches) ──
  static constexpr uint32_t PARALLAX       = 1u << 15;
  static constexpr uint32_t SHADOWS        = 1u << 16;
  static constexpr uint32_t SSAO           = 1u << 17;
  static constexpr uint32_t AUTO_FOCUS     = 1u << 18;
  static constexpr uint32_t GOD_RAYS       = 1u << 19;

  // ── Extended toggles (bits 26+) ──
  static constexpr uint32_t PARALLAX_SHADOW = 1u << 26;
  static constexpr uint32_t GLTF_TANGENT_SPACE = 1u << 27;
  static constexpr uint32_t HAS_SKINNING = 1u << 28;

  // ── Pass type (6 bits) ──
  static constexpr uint32_t PASS_SHIFT = 20;
  static constexpr uint32_t PASS_MASK  = 0x3Fu << PASS_SHIFT;

  void    setPass(uint8_t p) { bits = (bits & ~PASS_MASK) | (uint32_t(p) << PASS_SHIFT); }
  uint8_t getPass() const    { return (bits >> PASS_SHIFT) & 0x3F; }

  bool has(uint32_t flag) const { return (bits & flag) != 0; }
  bool isValid() const { return bits != 0xFFFFFFFF; }

  bool operator==(const ShaderKey& o) const { return bits == o.bits; }
  bool operator!=(const ShaderKey& o) const { return bits != o.bits; }
};

struct ShaderKeyHash {
  size_t operator()(const ShaderKey& k) const { return std::hash<uint32_t>()(k.bits); }
};
namespace T8_BUFFER_USAGE{
  enum E {
    DEFAULT,
    DINAMIC,
    STATIC
  };
}

namespace T8_IB_FORMAR {
  enum E {
    R16,
    R32
  };
}

namespace T8_TOPOLOGY {
  enum E {
    POINT_LIST,
    LINE_LIST,
    LINE_STRIP,
    TRIANLE_LIST,
    TRIANGLE_STRIP
  };
}

namespace T8_SHADER_TYPE {
  enum E {
    VERTEX,
    FRAGMENT
  };
}

namespace T8_BUFFER_TYPE {
  enum E {
    VERTEX,
    INDEX,
    CONSTANT
  };
}

namespace T8_CBUFFER_TYPE {
  enum E {
    FLOAT,
    VECTOR2,
    VECTOR4,
    MATRIX,
    TEXTURE
  };
}

namespace T8_FORMAT {
  enum E {
    FORMAT_UNKNOWN = 0,
    FORMAT_R32G32B32A32_TYPELESS = 1,
    FORMAT_R32G32B32A32_FLOAT = 2,
    FORMAT_R32G32B32A32_UINT = 3,
    FORMAT_R32G32B32A32_SINT = 4,
    FORMAT_R32G32B32_TYPELESS = 5,
    FORMAT_R32G32B32_FLOAT = 6,
    FORMAT_R32G32B32_UINT = 7,
    FORMAT_R32G32B32_SINT = 8,
    FORMAT_R16G16B16A16_TYPELESS = 9,
    FORMAT_R16G16B16A16_FLOAT = 10,
    FORMAT_R16G16B16A16_UNORM = 11,
    FORMAT_R16G16B16A16_UINT = 12,
    FORMAT_R16G16B16A16_SNORM = 13,
    FORMAT_R16G16B16A16_SINT = 14,
    FORMAT_R32G32_TYPELESS = 15,
    FORMAT_R32G32_FLOAT = 16,
    FORMAT_R32G32_UINT = 17,
    FORMAT_R32G32_SINT = 18,
    FORMAT_R32G8X24_TYPELESS = 19,
    FORMAT_D32_FLOAT_S8X24_UINT = 20,
    FORMAT_R32_FLOAT_X8X24_TYPELESS = 21,
    FORMAT_X32_TYPELESS_G8X24_UINT = 22,
    FORMAT_R10G10B10A2_TYPELESS = 23,
    FORMAT_R10G10B10A2_UNORM = 24,
    FORMAT_R10G10B10A2_UINT = 25,
    FORMAT_R11G11B10_FLOAT = 26,
    FORMAT_R8G8B8A8_TYPELESS = 27,
    FORMAT_R8G8B8A8_UNORM = 28,
    FORMAT_R8G8B8A8_UNORM_SRGB = 29,
    FORMAT_R8G8B8A8_UINT = 30,
    FORMAT_R8G8B8A8_SNORM = 31,
    FORMAT_R8G8B8A8_SINT = 32,
    FORMAT_R16G16_TYPELESS = 33,
    FORMAT_R16G16_FLOAT = 34,
    FORMAT_R16G16_UNORM = 35,
    FORMAT_R16G16_UINT = 36,
    FORMAT_R16G16_SNORM = 37,
    FORMAT_R16G16_SINT = 38,
    FORMAT_R32_TYPELESS = 39,
    FORMAT_D32_FLOAT = 40,
    FORMAT_R32_FLOAT = 41,
    FORMAT_R32_UINT = 42,
    FORMAT_R32_SINT = 43,
    FORMAT_R24G8_TYPELESS = 44,
    FORMAT_D24_UNORM_S8_UINT = 45,
    FORMAT_R24_UNORM_X8_TYPELESS = 46,
    FORMAT_X24_TYPELESS_G8_UINT = 47,
    FORMAT_R8G8_TYPELESS = 48,
    FORMAT_R8G8_UNORM = 49,
    FORMAT_R8G8_UINT = 50,
    FORMAT_R8G8_SNORM = 51,
    FORMAT_R8G8_SINT = 52,
    FORMAT_R16_TYPELESS = 53,
    FORMAT_R16_FLOAT = 54,
    FORMAT_D16_UNORM = 55,
    FORMAT_R16_UNORM = 56,
    FORMAT_R16_UINT = 57,
    FORMAT_R16_SNORM = 58,
    FORMAT_R16_SINT = 59,
    FORMAT_R8_TYPELESS = 60,
    FORMAT_R8_UNORM = 61,
    FORMAT_R8_UINT = 62,
    FORMAT_R8_SNORM = 63,
    FORMAT_R8_SINT = 64,
    FORMAT_A8_UNORM = 65,
    FORMAT_R1_UNORM = 66,
    FORMAT_R9G9B9E5_SHAREDEXP = 67,
    FORMAT_R8G8_B8G8_UNORM = 68,
    FORMAT_G8R8_G8B8_UNORM = 69,
    FORMAT_BC1_TYPELESS = 70,
    FORMAT_BC1_UNORM = 71,
    FORMAT_BC1_UNORM_SRGB = 72,
    FORMAT_BC2_TYPELESS = 73,
    FORMAT_BC2_UNORM = 74,
    FORMAT_BC2_UNORM_SRGB = 75,
    FORMAT_BC3_TYPELESS = 76,
    FORMAT_BC3_UNORM = 77,
    FORMAT_BC3_UNORM_SRGB = 78,
    FORMAT_BC4_TYPELESS = 79,
    FORMAT_BC4_UNORM = 80,
    FORMAT_BC4_SNORM = 81,
    FORMAT_BC5_TYPELESS = 82,
    FORMAT_BC5_UNORM = 83,
    FORMAT_BC5_SNORM = 84,
    FORMAT_B5G6R5_UNORM = 85,
    FORMAT_B5G5R5A1_UNORM = 86,
    FORMAT_B8G8R8A8_UNORM = 87,
    FORMAT_B8G8R8X8_UNORM = 88,
    FORMAT_R10G10B10_XR_BIAS_A2_UNORM = 89,
    FORMAT_B8G8R8A8_TYPELESS = 90,
    FORMAT_B8G8R8A8_UNORM_SRGB = 91,
    FORMAT_B8G8R8X8_TYPELESS = 92,
    FORMAT_B8G8R8X8_UNORM_SRGB = 93,
    FORMAT_BC6H_TYPELESS = 94,
    FORMAT_BC6H_UF16 = 95,
    FORMAT_BC6H_SF16 = 96,
    FORMAT_BC7_TYPELESS = 97,
    FORMAT_BC7_UNORM = 98,
    FORMAT_BC7_UNORM_SRGB = 99,
    FORMAT_AYUV = 100,
    FORMAT_Y410 = 101,
    FORMAT_Y416 = 102,
    FORMAT_NV12 = 103,
    FORMAT_P010 = 104,
    FORMAT_P016 = 105,
    FORMAT_420_OPAQUE = 106,
    FORMAT_YUY2 = 107,
    FORMAT_Y210 = 108,
    FORMAT_Y216 = 109,
    FORMAT_NV11 = 110,
    FORMAT_AI44 = 111,
    FORMAT_IA44 = 112,
    FORMAT_P8 = 113,
    FORMAT_A8P8 = 114,
    FORMAT_B4G4R4A4_UNORM = 115
  };
}
namespace GRAPHICS_API {
  enum E {
    OPENGL,
    D3D11,
    D3D12,
    VULKAN
  };
}

namespace T8_VIDEO_MODE {
  enum E {
    FULLSCREEN,
    WINDOWED
  };
}

enum TEXT_BASIC_FORMAT {
  CH_ALPHA = 1,
  CH_RGB = 2,
  CH_RGBA = 4
};

enum TEXT_BASIC_PARAMS {
  TILED = 1,
  CLAMP_TO_EDGE = 2,
  MIPMAPS = 4,
  CLAMP_TO_BORDER = 8,
  NEAREST_FILTER = 16,
  LINEAR_FILTER = 32
};


  struct BufferDesc {
    int byteWidth;
    T8_BUFFER_USAGE::E usage;
  };

  struct ApplicationDesc {
    GRAPHICS_API::E api;
    T8_VIDEO_MODE::E videoMode;
    unsigned int width;
    unsigned int height;
    std::string title;
    ApplicationDesc() {
      api = GRAPHICS_API::D3D11;
      width = 1280;
      height = 720;
      title = "My New Project";
      videoMode = T8_VIDEO_MODE::WINDOWED;
    }
  };

  struct InputElement {
    std::string name;
    hyperspace::shader::datatype_ type;
    int loc;
    int num;
    int size;
    int bufferBytePosition;
    InputElement() {
      loc = -1;
      num = 1;
      bufferBytePosition = 0;
      size = 0;
    }
  };
}


#endif
