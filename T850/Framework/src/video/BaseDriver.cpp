#include <pch.h>
/*********************************************************
* Copyright (C) 2017 Daniel Enriquez (camus_mm@hotmail.com)
* All Rights Reserved
*
* You may use, distribute and modify this code under the
* following terms:
* ** Do not claim that you wrote this software
* ** A mention would be appreciated but not needed
* ** I do not and will not provide support, this software is "as is"
* ** Enjoy, learn and share.
*********************************************************/
#include <video/BaseDriver.h>
#include <utils/cil.h>
#include <utils/Log.h>
#include <utils/ShaderPermutationDump.h>
#include <utils/ResourceLocator.h>
#include <debug/RenderTrace.h>
#include <debug/LoadingProgress.h>
#include <core/Config.h>
#include <algorithm>
#include <ctime>
#include <iostream>
#include <string>
#include <fstream>
#include <string.h>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <atomic>
#include <cctype>
#include <cstdint>

#ifndef T850_DUMP_TEXTURE_UPLOADS
#define T850_DUMP_TEXTURE_UPLOADS 0
#endif

namespace t850 {
  BaseDriver*	g_pBaseDriver = 0;
  Device*           T8Device;	// Device for create resources
  DeviceContext*    T8DeviceContext; // Context to set and manipulate the resources

#include <utils/Checker.h>

#if T850_DUMP_TEXTURE_UPLOADS
  namespace {
    constexpr uint32_t DDS_MAGIC = 0x20534444u;
    constexpr uint32_t DDSD_CAPS = 0x00000001u;
    constexpr uint32_t DDSD_HEIGHT = 0x00000002u;
    constexpr uint32_t DDSD_WIDTH = 0x00000004u;
    constexpr uint32_t DDSD_PITCH = 0x00000008u;
    constexpr uint32_t DDSD_PIXELFORMAT = 0x00001000u;
    constexpr uint32_t DDSD_MIPMAPCOUNT = 0x00020000u;
    constexpr uint32_t DDSD_LINEARSIZE = 0x00080000u;
    constexpr uint32_t DDPF_ALPHAPIXELS = 0x00000001u;
    constexpr uint32_t DDPF_FOURCC = 0x00000004u;
    constexpr uint32_t DDPF_RGB = 0x00000040u;
    constexpr uint32_t DDPF_LUMINANCE = 0x00020000u;
    constexpr uint32_t DDSCAPS_COMPLEX = 0x00000008u;
    constexpr uint32_t DDSCAPS_TEXTURE = 0x00001000u;
    constexpr uint32_t DDSCAPS_MIPMAP = 0x00400000u;
    constexpr uint32_t DDSCAPS2_CUBEMAP = 0x00000200u;
    constexpr uint32_t DDSCAPS2_CUBEMAP_POSITIVEX = 0x00000400u;
    constexpr uint32_t DDSCAPS2_CUBEMAP_NEGATIVEX = 0x00000800u;
    constexpr uint32_t DDSCAPS2_CUBEMAP_POSITIVEY = 0x00001000u;
    constexpr uint32_t DDSCAPS2_CUBEMAP_NEGATIVEY = 0x00002000u;
    constexpr uint32_t DDSCAPS2_CUBEMAP_POSITIVEZ = 0x00004000u;
    constexpr uint32_t DDSCAPS2_CUBEMAP_NEGATIVEZ = 0x00008000u;
    constexpr uint32_t DDS_RESOURCE_DIMENSION_TEXTURE2D = 3u;
    constexpr uint32_t DXGI_FORMAT_R16G16B16A16_FLOAT = 10u;

    std::atomic_uint g_textureUploadDumpCounter{0};

    uint32_t FourCC(char a, char b, char c, char d) {
      return static_cast<uint32_t>(static_cast<unsigned char>(a)) |
             (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8) |
             (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16) |
             (static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24);
    }

    void WriteU32(std::ofstream& out, uint32_t value) {
      out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    std::string DumpSanitize(std::string value) {
      if (value.empty()) value = "texture";
      for (char& ch : value) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!std::isalnum(c) && ch != '-' && ch != '_' && ch != '.') ch = '_';
      }
      constexpr std::size_t maxLen = 96;
      if (value.size() > maxLen) value = value.substr(value.size() - maxLen);
      return value;
    }

    uint32_t DumpMipCount(const Texture& tex) {
      return tex.mipmaps > 0 ? tex.mipmaps : 1;
    }

    uint32_t DumpFaceCount(const Texture& tex) {
      return (tex.cil_props & CIL_CUBE_MAP) ? 6u : 1u;
    }

    uint32_t DumpBytesPerPixel(const Texture& tex) {
      if (tex.cil_props & CIL_HALF_FLOAT) return 8u;
      if (tex.props & TextBasicFormat::CH_ALPHA) return 1u;
      if (tex.props & TextBasicFormat::CH_RGB) return 3u;
      return 4u;
    }

    uint32_t DumpCompressedBlockSize(const Texture& tex) {
      return (tex.cil_props & CIL_DXT3) || (tex.cil_props & CIL_DXT5) ? 16u : 8u;
    }

    uint32_t DumpCompressedFourCC(const Texture& tex) {
      if (tex.cil_props & CIL_DXT3) return FourCC('D', 'X', 'T', '3');
      if (tex.cil_props & CIL_DXT5) return FourCC('D', 'X', 'T', '5');
      return FourCC('D', 'X', 'T', '1');
    }

    std::size_t DumpComputedPayloadSize(const Texture& tex, bool compressed) {
      const uint32_t faces = DumpFaceCount(tex);
      const uint32_t mips = DumpMipCount(tex);
      std::size_t bytes = 0;
      for (uint32_t face = 0; face < faces; ++face) {
        uint32_t width = std::max(1u, tex.x);
        uint32_t height = std::max(1u, tex.y);
        for (uint32_t mip = 0; mip < mips; ++mip) {
          if (compressed) {
            const uint32_t blockSize = DumpCompressedBlockSize(tex);
            const uint32_t blocksX = std::max(1u, (width + 3u) / 4u);
            const uint32_t blocksY = std::max(1u, (height + 3u) / 4u);
            bytes += static_cast<std::size_t>(blocksX) * blocksY * blockSize;
          } else {
            bytes += static_cast<std::size_t>(width) * height * DumpBytesPerPixel(tex);
          }
          width = std::max(1u, width >> 1);
          height = std::max(1u, height >> 1);
        }
      }
      return bytes;
    }

    bool DumpWriteDDS(const std::filesystem::path& path, const Texture& tex, const unsigned char* data, std::size_t dataSize, bool compressed) {
      std::ofstream out(path, std::ios::binary);
      if (!out.is_open()) return false;

      const uint32_t width = std::max(1u, tex.x);
      const uint32_t height = std::max(1u, tex.y);
      const uint32_t mips = DumpMipCount(tex);
      const uint32_t faces = DumpFaceCount(tex);
      const bool cube = faces == 6;
      const bool dx10 = !compressed && (tex.cil_props & CIL_HALF_FLOAT);
      const uint32_t bpp = DumpBytesPerPixel(tex);
      const uint32_t pitchOrLinear = compressed
        ? std::max(1u, (width + 3u) / 4u) * DumpCompressedBlockSize(tex)
        : width * bpp;

      WriteU32(out, DDS_MAGIC);
      WriteU32(out, 124u);
      WriteU32(out, DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT |
                    (mips > 1 ? DDSD_MIPMAPCOUNT : 0u) |
                    (compressed ? DDSD_LINEARSIZE : DDSD_PITCH));
      WriteU32(out, height);
      WriteU32(out, width);
      WriteU32(out, pitchOrLinear);
      WriteU32(out, 0u);
      WriteU32(out, mips);
      for (int i = 0; i < 11; ++i) WriteU32(out, 0u);

      WriteU32(out, 32u);
      if (compressed || dx10) {
        WriteU32(out, DDPF_FOURCC);
        WriteU32(out, dx10 ? FourCC('D', 'X', '1', '0') : DumpCompressedFourCC(tex));
        WriteU32(out, 0u); WriteU32(out, 0u); WriteU32(out, 0u); WriteU32(out, 0u); WriteU32(out, 0u);
      } else if (tex.props & TextBasicFormat::CH_ALPHA) {
        WriteU32(out, DDPF_LUMINANCE);
        WriteU32(out, 0u);
        WriteU32(out, 8u);
        WriteU32(out, 0x000000ffu);
        WriteU32(out, 0u);
        WriteU32(out, 0u);
        WriteU32(out, 0u);
      } else if (tex.props & TextBasicFormat::CH_RGB) {
        WriteU32(out, DDPF_RGB);
        WriteU32(out, 0u);
        WriteU32(out, 24u);
        WriteU32(out, 0x000000ffu);
        WriteU32(out, 0x0000ff00u);
        WriteU32(out, 0x00ff0000u);
        WriteU32(out, 0u);
      } else {
        WriteU32(out, DDPF_RGB | DDPF_ALPHAPIXELS);
        WriteU32(out, 0u);
        WriteU32(out, 32u);
        WriteU32(out, 0x000000ffu);
        WriteU32(out, 0x0000ff00u);
        WriteU32(out, 0x00ff0000u);
        WriteU32(out, 0xff000000u);
      }

      WriteU32(out, DDSCAPS_TEXTURE | (mips > 1 ? (DDSCAPS_COMPLEX | DDSCAPS_MIPMAP) : 0u) | (cube ? DDSCAPS_COMPLEX : 0u));
      WriteU32(out, cube ? (DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEX | DDSCAPS2_CUBEMAP_NEGATIVEX |
                            DDSCAPS2_CUBEMAP_POSITIVEY | DDSCAPS2_CUBEMAP_NEGATIVEY |
                            DDSCAPS2_CUBEMAP_POSITIVEZ | DDSCAPS2_CUBEMAP_NEGATIVEZ) : 0u);
      WriteU32(out, 0u);
      WriteU32(out, 0u);
      WriteU32(out, 0u);

      if (dx10) {
        WriteU32(out, DXGI_FORMAT_R16G16B16A16_FLOAT);
        WriteU32(out, DDS_RESOURCE_DIMENSION_TEXTURE2D);
        WriteU32(out, 0u);
        WriteU32(out, faces);
        WriteU32(out, 0u);
      }

      out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(dataSize));
      return out.good();
    }

    void DumpTextureUpload(const Texture& tex, const unsigned char* data, bool compressed, const char* source) {
      if (!data || tex.x == 0 || tex.y == 0) return;

      std::size_t payloadSize = tex.size > 0 ? tex.size : DumpComputedPayloadSize(tex, compressed);
      if (payloadSize == 0) return;

      std::filesystem::path dir = "texture_upload_dumps";
      std::error_code ec;
      std::filesystem::create_directories(dir, ec);
      if (ec) {
        T8_LOG_ERROR("[TextureDump] Failed to create '%s': %s", dir.string().c_str(), ec.message().c_str());
        return;
      }

      std::string label = !tex.filepath.empty() ? tex.filepath : tex.optname;
      if (label.empty()) label = source ? source : "texture";
      const unsigned index = g_textureUploadDumpCounter.fetch_add(1, std::memory_order_relaxed);
      std::ostringstream stem;
      stem << std::setw(5) << std::setfill('0') << index << "_"
           << DumpSanitize(label) << "_" << tex.x << "x" << tex.y
           << "_ch" << tex.m_channels << "_m" << DumpMipCount(tex)
           << ((tex.cil_props & CIL_CUBE_MAP) ? "_cube" : "")
           << (compressed ? "_compressed" : "");

      const std::filesystem::path ddsPath = dir / (stem.str() + ".dds");
      const std::filesystem::path metaPath = dir / (stem.str() + ".txt");
      if (DumpWriteDDS(ddsPath, tex, data, payloadSize, compressed)) {
        std::ofstream meta(metaPath);
        if (meta.is_open()) {
          meta << "source=" << (source ? source : "") << "\n";
          meta << "label=" << label << "\n";
          meta << "width=" << tex.x << "\n";
          meta << "height=" << tex.y << "\n";
          meta << "channels=" << tex.m_channels << "\n";
          meta << "mipmaps=" << DumpMipCount(tex) << "\n";
          meta << "payload_bytes=" << payloadSize << "\n";
          meta << "props=0x" << std::hex << tex.props << "\n";
          meta << "params=0x" << std::hex << tex.params << "\n";
          meta << "cil_props=0x" << std::hex << tex.cil_props << "\n";
        }
        T8_LOG_INFO("[TextureDump] Wrote %s", ddsPath.string().c_str());
      } else {
        T8_LOG_ERROR("[TextureDump] Failed to write %s", ddsPath.string().c_str());
      }
    }
  }
#endif

  bool		Texture::LoadTexture(const char *fn) {
    bool found = false;
    std::string path = "Textures/";
    filepath = path + std::string(fn);
    found = ResourceLocator::Instance().Exists(filepath);

    int x = 0, y = 0;
    unsigned char *buffer = 0;

    if (!found) {
      buffer = (unsigned char*)g_chkr.pixel_data;
      x = g_chkr.width;
      y = g_chkr.height;
      m_channels = g_chkr.bytes_per_pixel;
      T8_LOG_ERROR("Texture '%s' not found, loading checker", filepath.c_str());
    }
    else {
      //buffer = stbi_load(filepath.c_str(), &x, &y, &channels, 0);
      cil_props = 0;
      buffer = cil_load((filepath.c_str()), &x, &y, &mipmaps, &cil_props, &size);
    }

    if (!buffer) {
      T8_LOG_ERROR("Texture '%s' failed to load (unsupported or corrupt image)", filepath.c_str());
      return false;
    }

    bounded = 1;
    this->x = x;
    this->y = y;
    props = 0;

    if (cil_props&CIL_RGBA) {
      props |= TextBasicFormat::CH_RGBA;
      m_channels = 4;
    }
    else {
      props |= TextBasicFormat::CH_RGB;
      m_channels = 3;
    }

    memcpy(&optname[0], fn, strlen(fn));
    optname[strlen(fn)] = '\0';

  #if T850_DUMP_TEXTURE_UPLOADS
    DumpTextureUpload(*this, buffer, (cil_props & CIL_COMPRESSED) != 0, "LoadTexture");
  #endif

    if (cil_props & CIL_COMPRESSED) {
      LoadAPITextureCompressed(buffer);
    } else {
      LoadAPITexture(T8DeviceContext, buffer);
    }
    if (found) {
      cil_free_buffer(buffer, cil_props);
    }

    return true;
  }

  bool Texture::LoadFromMemory(const unsigned char * buff, int w, int h, int channels, const char* debugName)
  {
    m_channels = channels;
    cil_props = 0;

    if (!buff)
      return false;

    bounded = 1;
    this->x = w;
    this->y = h;
    props = 0;

    if (channels == 4) {
      props |= TextBasicFormat::CH_RGBA;
    }
    else if (channels == 3){
      props |= TextBasicFormat::CH_RGB;
    }
    else if (channels == 1) {
      props |= TextBasicFormat::CH_ALPHA;
    }

    if (debugName && debugName[0]) {
      std::strncpy(optname, debugName, sizeof(optname) - 1);
      optname[sizeof(optname) - 1] = '\0';
    }

#if T850_DUMP_TEXTURE_UPLOADS
    DumpTextureUpload(*this, buff, false, "LoadFromMemory");
#endif

    LoadAPITexture(T8DeviceContext, const_cast<unsigned char*>(buff));

    return true;
  }

  bool Texture::CreateCubeMap(const unsigned char * buff, int w, int h)
  {
    m_channels = 4;
    cil_props = CIL_CUBE_MAP;
    bounded = 1;
    this->x = w;
    this->y = h;
    props = 0;

    if (m_channels == 4) {
      props |= TextBasicFormat::CH_RGBA;
    }
    else if (m_channels == 3) {
      props |= TextBasicFormat::CH_RGB;
    }
    else if (m_channels == 1) {
      props |= TextBasicFormat::CH_ALPHA;
    }
#if T850_DUMP_TEXTURE_UPLOADS
    DumpTextureUpload(*this, buff, false, "CreateCubeMap");
#endif
    LoadAPITexture(T8DeviceContext, const_cast<unsigned char*>(buff));
    return true;
  }

  void Texture::release() {
    DestroyAPITexture();
    delete this;
  }

  bool BaseRT::LoadRT(int nrt, int cf, int df, int w, int h, bool GenMips) {
    this->number_RT = nrt;
    this->color_format = cf;
    this->depth_format = df;
    this->w = w;
    this->h = h;
    this->GenMips = GenMips;
    this->perColorFormats.clear();
    return LoadAPIRT();
  }

  bool BaseRT::LoadRT(int nrt, const std::vector<int>& perCF, int df, int w, int h, bool GenMips) {
    this->number_RT = nrt;
    this->color_format = perCF.empty() ? RGBA8 : perCF[0]; // fallback
    this->depth_format = df;
    this->w = w;
    this->h = h;
    this->GenMips = GenMips;
    this->perColorFormats = perCF;
    return LoadAPIRT();
  }

  void BaseRT::release() {
    DestroyAPIRT();
    delete this;
  }

  bool ShaderBase::CreateShader(std::string src_vs, std::string src_fs, ShaderKey key, const std::string& vs_name, const std::string& fs_name) {
    std::string Defines;
    if (key.isValid()) {

#if defined(USING_OPENGL)
      if (g_pBaseDriver->m_currentAPI == GraphicsApi::OPENGL) {
        Defines += "#version 330\n\n";
        Defines += "#define ES_30\n\n";
      }
#elif defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
      if (g_pBaseDriver->m_currentAPI == GraphicsApi::OPENGL) {
        Defines += "#version 300 es\n\n";
        Defines += "#define ES_30\n\n";
      }
#endif
#if VDEBUG_NO_LIGHT
      Defines += "#define NO_LIGHT\n\n";
#endif
#if VDEBUG_SIMPLE_COLOR
      Defines += "#define SIMPLE_COLOR\n\n";
#endif

      // Vertex attributes
      if (key.has(ShaderKey::HAS_NORMALS))    Defines += "#define USE_NORMALS\n\n";
      if (key.has(ShaderKey::HAS_TEXCOORD0))  Defines += "#define USE_TEXCOORD0\n\n";
      if (key.has(ShaderKey::HAS_TEXCOORD1))  Defines += "#define USE_TEXCOORD1\n\n";
      if (key.has(ShaderKey::HAS_TEXCOORD2))  Defines += "#define USE_TEXCOORD2\n\n";
      if (key.has(ShaderKey::HAS_TEXCOORD3))  Defines += "#define USE_TEXCOORD3\n\n";
      if (key.has(ShaderKey::HAS_TANGENTS))   Defines += "#define USE_TANGENTS\n\n";
      if (key.has(ShaderKey::HAS_BINORMALS))  Defines += "#define USE_BINORMALS\n\n";

      // Texture maps
      if (key.has(ShaderKey::DIFFUSE_MAP))    Defines += "#define DIFFUSE_MAP\n\n";
      if (key.has(ShaderKey::SPECULAR_MAP))   Defines += "#define SPECULAR_MAP\n\n";
      if (key.has(ShaderKey::GLOSS_MAP))      Defines += "#define GLOSS_MAP\n\n";
      if (key.has(ShaderKey::NORMAL_MAP))     Defines += "#define NORMAL_MAP\n\n";
      if (key.has(ShaderKey::REFLECT_MAP))    Defines += "#define REFLECT_MAP\n#define EMISSIVE_MAP\n\n";
      if (key.has(ShaderKey::HEIGHT_MAP))     Defines += "#define HEIGHT_MAP\n\n";
      if (key.has(ShaderKey::METALLIC_MAP))   Defines += "#define METALLIC_MAP\n\n";
      if (key.has(ShaderKey::CLEARCOAT_MAP))  Defines += "#define CLEARCOAT_MAP\n\n";
      if (key.has(ShaderKey::SHEEN_COLOR_MAP)) Defines += "#define SHEEN_COLOR_MAP\n\n";
      if (key.has(ShaderKey::SHEEN_ROUGHNESS_MAP)) Defines += "#define SHEEN_ROUGHNESS_MAP\n\n";
      if (key.has(ShaderKey::CLEARCOAT_ROUGHNESS_MAP)) Defines += "#define CLEARCOAT_ROUGHNESS_MAP\n\n";
      if (key.has(ShaderKey::OCCLUSION_MAP)) Defines += "#define OCCLUSION_MAP\n\n";
      if (key.has(ShaderKey::SPECULAR_FACTOR_MAP)) Defines += "#define SPECULAR_FACTOR_MAP\n\n";
      if (key.has(ShaderKey::SPECULAR_COLOR_MAP)) Defines += "#define SPECULAR_COLOR_MAP\n\n";
      if (key.has(ShaderKey::TRANSMISSION_MAP)) Defines += "#define TRANSMISSION_MAP\n\n";
      if (key.has(ShaderKey::LIGHTMAP_MAP)) Defines += "#define LIGHTMAP_MAP\n\n";

      // Material conventions
      if (key.has(ShaderKey::GLTF_TANGENT_SPACE)) Defines += "#define GLTF_TANGENT_SPACE\n\n";

      // Skinning
      if (key.has(ShaderKey::HAS_SKINNING))      Defines += "#define USE_SKINNING\n\n";
      if (key.has(ShaderKey::HAS_SKINNING_QT))   Defines += "#define USE_SKINNING_QT\n\n";
      if (key.has(ShaderKey::HAS_SKINNING_TEX))  Defines += "#define USE_SKINNING_TEXTURE\n\n";

      // Special modes
      if (key.has(ShaderKey::NO_LIGHT))       Defines += "#define NO_LIGHT\n\n";
      if (key.has(ShaderKey::OMNI_SHADOWS))   Defines += "#define OMNIDIRECTIONAL_SH\n\n";

      // Effect toggles
      if (key.has(ShaderKey::PARALLAX))       Defines += "#define ENABLE_PARALLAX\n\n";
      if (key.has(ShaderKey::SHADOWS))        Defines += "#define ENABLE_SHADOWS\n\n";
      if (key.has(ShaderKey::SSAO))           Defines += "#define ENABLE_SSAO\n\n";
      if (key.has(ShaderKey::AUTO_FOCUS))     Defines += "#define AUTO_FOCUS\n\n";
      if (key.has(ShaderKey::GOD_RAYS))       Defines += "#define ENABLE_GOD_RAYS\n\n";

      // Pass type
      switch (key.getPass()) {
      case PassType::FORWARD:            break; // default forward path, no define needed
      case PassType::GBUFFER:            Defines += "#define G_BUFFER_PASS\n\n"; break;
      case PassType::SHADOW_MAP:         Defines += "#define SHADOW_MAP_PASS\n\n"; break;
      case PassType::FSQUAD_1_TEX:       Defines += "#define FSQUAD_1_TEX\n\n"; break;
      case PassType::FSQUAD_2_TEX:       Defines += "#define FSQUAD_2_TEX\n\n"; break;
      case PassType::FSQUAD_3_TEX:       Defines += "#define FSQUAD_3_TEX\n\n"; break;
      case PassType::DEFERRED:           Defines += "#define DEFERRED_PASS\n\n"; break;
      case PassType::SHADOW_COMP:        Defines += "#define SHADOW_COMP_PASS\n\n"; break;
      case PassType::VERTICAL_BLUR:      Defines += "#define VERTICAL_BLUR_PASS\n\n"; break;
      case PassType::HORIZONTAL_BLUR:    Defines += "#define HORIZONTAL_BLUR_PASS\n\n"; break;
      case PassType::ONE_PASS_BLUR:      Defines += "#define ONE_PASS_BLUR\n\n"; break;
      case PassType::BRIGHT:             Defines += "#define BRIGHT_PASS\n\n"; break;
      case PassType::HDR_COMP:           Defines += "#define HDR_COMP_PASS\n\n"; break;
      case PassType::ADAPT_LUMINANCE:    Defines += "#define ADAPT_LUMINANCE_PASS\n\n"; break;
      case PassType::COC:                Defines += "#define COC_PASS\n\n"; break;
      case PassType::COMBINE_COC:        Defines += "#define COMBINE_COC_PASS\n\n"; break;
      case PassType::DOF:                Defines += "#define DOF_PASS\n\n"; break;
      case PassType::DOF_2:              Defines += "#define DOF_PASS_2\n\n"; break;
      case PassType::BACKBUFFER:         Defines += "#define BACKBUFFER_PASS\n\n"; break;
      case PassType::GOD_RAY_CALCULATION: Defines += "#define GOD_RAY_CALCULATION_PASS\n\n"; break;
      case PassType::GOD_RAY_BLEND:      Defines += "#define GOD_RAY_BLEND_PASS\n\n"; break;
      case PassType::SSAO:               Defines += "#define SSAO_PASS\n\n"; break;
      case PassType::RAY_MARCH:          Defines += "#define RAY_MARCH\n\n"; break;
      case PassType::RADIAL_DEPTH:       Defines += "#define RADIAL_DEPTH_PASS\n\n"; break;
      case PassType::LIGHT_RAY_MARCHING: Defines += "#define LIGHT_RAY_MARCHING\n\n"; break;
      case PassType::LIGHT_ADD:          Defines += "#define LIGHT_ADD\n\n"; break;
      case PassType::FADE:               Defines += "#define FADE\n\n"; break;
      case PassType::LENS_FLARE_SUN:     Defines += "#define LENS_FLARE_SUN\n\n"; break;
      case PassType::LENS_FLARE_GHOST:   Defines += "#define LENS_FLARE_GHOST\n\n"; break;
      case PassType::DEFERRED_LDR:       Defines += "#define DEFERRED_LDR_PASS\n\n"; break;
      case PassType::DEFERRED_LIGHT_VOLUME: Defines += "#define DEFERRED_LIGHT_VOLUME_PASS\n\n"; break;
      case PassType::CASCADE_DEBUG:      Defines += "#define CASCADE_DEBUG_PASS\n\n"; break;
      default: break;
      }

      src_vs = Defines + src_vs;
      src_fs = Defines + src_fs;
    }
    this->key = key;
    if (!CreateShaderAPI(src_vs, src_fs, vs_name, fs_name)) {
      T8_LOG_ERROR("Shader defines for failed key 0x%016llX [VS='%s' FS='%s']:\n%s", static_cast<unsigned long long>(key.bits), vs_name.c_str(), fs_name.c_str(), Defines.c_str());
      return false;
    }
    ShaderPermutationDump::Record(key, vs_name, fs_name, Defines);
    return true;
  }
  void ShaderBase::release()
  {
    DestroyAPIShader();
    delete this;
  }
  Texture * BaseDriver::GetRTTexture(int id, int index)
  {
    if (id < 0 || id >= (int)RTs.size())
      exit(666);

    if (index == DEPTH_ATTACHMENT) {
      return RTs[id]->pDepthTexture;
    }
    else {
      return RTs[id]->vColorTextures[index];
    }
  }
  ShaderBase * BaseDriver::GetShader(ShaderKey key)
  {
    auto it = m_shaderCache.find(key.bits);
    if (it != m_shaderCache.end())
      return it->second;
    fprintf(stderr, "[ShaderKey] GetShader miss: key 0x%016llX (pass=%d)\n", static_cast<unsigned long long>(key.bits), key.getPass());
    T8_LOG_ERROR("GetShader miss: key 0x%016llX (pass=%d)", static_cast<unsigned long long>(key.bits), key.getPass());
    return nullptr;
  }
  ShaderBase * BaseDriver::GetShaderIdx(int id)
  {
    if (id < 0 || id >= (int)m_shaders.size()) {
      T8_LOG_ERROR("GetShaderIdx: invalid id %d (size=%d)", id, (int)m_shaders.size());
      return nullptr;
    }

    return m_shaders[id];
  }
  Texture * BaseDriver::GetTexture(int id)
  {
    if (id < 0 || id >= (int)Textures.size()) {
      T8_LOG_ERROR("GetTexture: invalid id %d (size=%d)", id, (int)Textures.size());
      return 0;
    }

    return Textures[id];
  }
  void BaseDriver::DestroyShaders()
  {
    for (unsigned int i = 0; i < m_shaders.size(); i++) {
      m_shaders[i]->release();
      m_shaders[i] = nullptr;
    }
    m_shaders.clear();
    m_shaderCache.clear();
  }
  void BaseDriver::DestroyRTs()
  {
    for (unsigned int i = 0; i < RTs.size(); i++) {
      BaseRT *pRT = RTs[i];
      if (!pRT)
        continue;
      pRT->release();
      pRT = nullptr;
    }
    RTs.clear();
  }
  int BaseDriver::CreateTechnique(std::string path)
  {
    int i = 0;
    for (auto &it : m_techniques) {
      if (it->info.m_path == path)
        return i;
      i++;
    }
    m_techniques.push_back(std::move(new Technique(path)));
	return (int)m_techniques.size();
  }
  void BaseDriver::PushRT(int id)
  {
    if (id < 0 || id >= (int)RTs.size())
      return;

    if (IsCurrentOffscreenTarget() && CurrentRT != id)
      CurrentRT = -1;

    if (CurrentRT >= 0 && CurrentRT != id)
      PopRT();

    T8_LOG_TRACE("[BaseDriver] PushRT(%d) colors=%d %dx%d", id, RTs[id]->number_RT, RTs[id]->w, RTs[id]->h);
    CurrentRT = id;
    RTs[id]->Set(*T8DeviceContext);
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int rid = g_renderTracer->RegisterRT(RTs[id], nullptr, id);
      g_renderTracer->EvPushRT(rid, false);
    }
    RefreshTracePendingRenderState();
#endif
  }

  void BaseDriver::PushRTLoad(int id)
  {
    if (id < 0 || id >= (int)RTs.size())
      return;

    if (IsCurrentOffscreenTarget() && CurrentRT != id)
      CurrentRT = -1;

    if (CurrentRT >= 0 && CurrentRT != id)
      PopRT();

    T8_LOG_TRACE("[BaseDriver] PushRTLoad(%d) colors=%d %dx%d", id, RTs[id]->number_RT, RTs[id]->w, RTs[id]->h);
    CurrentRT = id;
    RTs[id]->SetLoad(*T8DeviceContext);
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int rid = g_renderTracer->RegisterRT(RTs[id], nullptr, id);
      g_renderTracer->EvPushRT(rid, true);
    }
    RefreshTracePendingRenderState();
#endif
  }
  Technique * BaseDriver::GetTechnique(int id)
  {
    if (id < (int)m_techniques.size())
      return m_techniques[id];
    return nullptr;
  }
  void BaseDriver::DestroyRT(int id)
  {
    if (id < 0 || id >= (int)RTs.size())
      return;

    if (RTs[id] != nullptr) {
      RTs[id]->release();
      RTs[id] = nullptr;
    }
  }
  void BaseDriver::DestroyTextures()
  {
    for (unsigned int i = 0; i < Textures.size(); i++) {
      if (Textures[i]) {
        Textures[i]->release();
        Textures[i] = nullptr;
      }
    }
    Textures.clear();
  }
  void BaseDriver::DestroyTexture(int id)
  {
    if (id < (int)Textures.size() && id >= 0) {
      if (Textures[id] != nullptr) {
        Textures[id]->release();
        Textures[id] = nullptr;
      }
    }
  }
  void BaseDriver::DestroyTechniques()
  {
    for (auto &it : m_techniques) {
      it->release();
      delete it;
    }
    m_techniques.clear();
  }
  void BaseDriver::DestroyTechnique(int id)
  {
    if (id >= 0 && id < (int)m_techniques.size()) {
      if (m_techniques[id] != nullptr) {
        m_techniques[id]->release();
        m_techniques[id] = nullptr;
      }
    }
  }
  void BaseDriver::DestroyShader(int id)
  {
    if (id >= 0 && id < (int)m_shaders.size()) {
      if (m_shaders[id] != nullptr) {
        if (m_shaders[id]->key.isValid())
          m_shaderCache.erase(m_shaders[id]->key.bits);
        m_shaders[id]->release();
        m_shaders[id] = nullptr;
      }
    }
  }
  int BaseDriver::CreateTexture(std::string path)
  {
    int firstFreeSlot = -1;
    for (unsigned int i = 0; i < Textures.size(); i++) {
      if (Textures[i] == nullptr) {
        if (firstFreeSlot < 0) firstFreeSlot = i;
        continue;
      }
      if (Textures[i]->filepath == "Textures/" + path) {
        return i;
      }
    }
    LoadingProgress::SetCurrent("Loading texture", path, "Creating GPU texture");
    LoadingProgress::ScopedStep loadingStep("Loading texture", path, 0.8f, false);
    Texture *pTex = T8Device->CreateTexture(path);
    if (!pTex) {
      T8_LOG_ERROR("Texture creation failed: '%s'", path.c_str());
      return -1;
    }
    int retIdx;
    if (firstFreeSlot >= 0) {
      Textures[firstFreeSlot] = pTex;
      T8_LOG_DEBUG("Texture created: '%s' -> slot %d (%dx%d)", path.c_str(), firstFreeSlot, pTex->x, pTex->y);
      retIdx = firstFreeSlot;
    } else {
      Textures.push_back(pTex);
      T8_LOG_DEBUG("Texture created: '%s' -> slot %d (%dx%d)", path.c_str(), (int)(Textures.size()-1), pTex->x, pTex->y);
      retIdx = static_cast<int>(Textures.size() - 1);
    }
    T8_TRACE_REGISTER_TEXTURE(pTex, "tex2d");
    return retIdx;
  }

  int BaseDriver::CreateTextureFromMemory(const std::string& key, const unsigned char* data,
                                          int width, int height, int channels)
  {
    if (key.empty() || !data || width <= 0 || height <= 0 || channels <= 0) {
      T8_LOG_ERROR("CreateTextureFromMemory: invalid request key='%s' size=%dx%d channels=%d",
                   key.c_str(), width, height, channels);
      return -1;
    }

    int firstFreeSlot = -1;
    for (unsigned int i = 0; i < Textures.size(); ++i) {
      if (!Textures[i]) {
        if (firstFreeSlot < 0) firstFreeSlot = static_cast<int>(i);
        continue;
      }
      if (Textures[i]->filepath == key) return static_cast<int>(i);
    }

    Texture* texture = T8Device->CreateTextureFromMemory(
      data, width, height, channels, key);
    if (!texture) {
      T8_LOG_ERROR("Memory texture creation failed: '%s'", key.c_str());
      return -1;
    }
    texture->filepath = key;

    const int textureId = firstFreeSlot >= 0
      ? firstFreeSlot
      : static_cast<int>(Textures.size());
    if (firstFreeSlot >= 0) Textures[firstFreeSlot] = texture;
    else Textures.push_back(texture);

    T8_LOG_DEBUG("Memory texture created: '%s' -> slot %d (%dx%d)",
                 key.c_str(), textureId, width, height);
    T8_TRACE_REGISTER_TEXTURE(texture, "tex2d");
    return textureId;
  }

  int BaseDriver::CreateCubeMap(const unsigned char * buff, int w, int h)
  {
    Texture *pTex = T8Device->CreateCubeMap(buff,w,h);
    Textures.push_back(pTex);
    T8_TRACE_REGISTER_TEXTURE(pTex, "cubemap");
    return static_cast<int>(Textures.size() - 1);
  }
  int BaseDriver::CreateFloatTexture(int w, int h, const float* data)
  {
    Texture *pTex = T8Device->CreateFloatTexture(w, h, data);
    if (!pTex)
      return -1;
    Textures.push_back(pTex);
    T8_LOG_DEBUG("Float texture created -> slot %d (%dx%d)", (int)(Textures.size() - 1), w, h);
    T8_TRACE_REGISTER_TEXTURE(pTex, "float2d");
    return static_cast<int>(Textures.size() - 1);
  }
  int BaseDriver::CreateFloatCubeMap(int size, int mipCount, const float* data)
  {
    Texture *pTex = T8Device->CreateFloatCubeMap(size, mipCount, data);
    if (!pTex)
      return -1;
    Textures.push_back(pTex);
    T8_LOG_DEBUG("Float cubemap created -> slot %d (%dx%d mips=%d)", (int)(Textures.size() - 1), size, size, mipCount);
    T8_TRACE_REGISTER_TEXTURE(pTex, "floatcube");
    return static_cast<int>(Textures.size() - 1);
  }
  int BaseDriver::CreateShader(std::string src_vs, std::string src_fs, ShaderKey key, const std::string& vs_name, const std::string& fs_name)
  {
    if (key.isValid()) {
      auto it = m_shaderCache.find(key.bits);
      if (it != m_shaderCache.end()) {
        // Already compiled — find its index
        for (int i = 0; i < (int)m_shaders.size(); i++) {
          if (m_shaders[i] == it->second)
            return i;
        }
      }
    }
    const std::string shaderName =
      (!vs_name.empty() || !fs_name.empty())
        ? (vs_name + " / " + fs_name)
        : std::string("runtime shader");
    LoadingProgress::SetCurrent("Compiling shader", shaderName, "Creating API shader/pipeline state");
    LoadingProgress::ScopedStep loadingStep("Compiling shader", shaderName, 0.45f, false);
    ShaderBase* shader = T8Device->CreateShader(src_vs, src_fs, key, vs_name, fs_name);
    if (shader != nullptr) {
      m_shaders.push_back(shader);
      int idx = static_cast<int>(m_shaders.size() - 1);
      if (key.isValid()) {
        m_shaderCache[key.bits] = shader;
        T8_LOG_DEBUG("Shader compiled: key=0x%016llX pass=%d -> idx %d", static_cast<unsigned long long>(key.bits), key.getPass(), idx);
      }
      T8_TRACE_REGISTER_SHADER(shader, key.bits, vs_name, fs_name);
      return idx;
    }
    T8_LOG_ERROR("Shader compilation FAILED: key=0x%016llX pass=%d", static_cast<unsigned long long>(key.bits), key.getPass());
    return -1;
  }
  int BaseDriver::CreateRT(int nrt, int cf, int df, int w, int h, bool genMips)
  {
    if (w == 0)
      w = width;
    if (h == 0)
      h = height;
    LoadingProgress::ScopedStep loadingStep(
      "Creating render target",
      std::to_string(w) + "x" + std::to_string(h) + " (" + std::to_string(nrt) + " color)",
      0.3f);
    BaseRT	*pRT = T8Device->CreateRT(nrt,cf,df,w,h,genMips);
    pRT->number_RT = nrt;
    if (pRT!= nullptr) {
      for (std::size_t i = 0; i < RTs.size(); ++i) {
        if (!RTs[i]) {
          RTs[i] = pRT;
          T8_LOG_DEBUG("RenderTarget created: reused handle %d (%dx%d, %d color attachments)", (int)i, w, h, nrt);
          T8_TRACE_REGISTER_RT(pRT, nullptr, (int)i);
          return static_cast<int>(i);
        }
      }
      RTs.push_back(pRT);
      T8_LOG_DEBUG("RenderTarget created: handle %d (%dx%d, %d color attachments)", (int)(RTs.size()-1), w, h, nrt);
      T8_TRACE_REGISTER_RT(pRT, nullptr, (int)(RTs.size() - 1));
      return static_cast<int>(RTs.size() - 1);
    }
    return -1;
  }
  int BaseDriver::CreateRT(int nrt, const std::vector<int>& perColorFormats, int df, int w, int h, bool genMips)
  {
    if (w == 0) w = width;
    if (h == 0) h = height;
    LoadingProgress::ScopedStep loadingStep(
      "Creating render target",
      std::to_string(w) + "x" + std::to_string(h) + " (" + std::to_string(nrt) + " color)",
      0.3f);
    int cf = perColorFormats.empty() ? BaseRT::RGBA8 : perColorFormats[0];
    BaseRT* pRT = T8Device->CreateRT(nrt, cf, df, w, h, genMips);
    if (pRT) {
      // Reload with per-attachment formats
      pRT->DestroyAPIRT();
      pRT->LoadRT(nrt, perColorFormats, df, w, h, genMips);
      for (std::size_t i = 0; i < RTs.size(); ++i) {
        if (!RTs[i]) {
          RTs[i] = pRT;
          T8_LOG_DEBUG("RenderTarget created (per-format): reused handle %d (%dx%d, %d colors)", (int)i, w, h, nrt);
          T8_TRACE_REGISTER_RT(pRT, nullptr, (int)i);
          return static_cast<int>(i);
        }
      }
      RTs.push_back(pRT);
      T8_LOG_DEBUG("RenderTarget created (per-format): handle %d (%dx%d, %d colors)", (int)(RTs.size()-1), w, h, nrt);
      T8_TRACE_REGISTER_RT(pRT, nullptr, (int)(RTs.size() - 1));
      return static_cast<int>(RTs.size() - 1);
    }
    return -1;
  }
  void BaseDriver::ModifyRT(int RTID, int nrt, int cf, int df, int w, int h, bool genMips)
  {
    DestroyRT(RTID);
    if (w == 0)
      w = width;
    if (h == 0)
      h = height;
    BaseRT	*pRT = T8Device->CreateRT(nrt, cf, df, w, h, genMips);
    pRT->number_RT = nrt;
    RTs[RTID] = pRT;
  }

  bool BaseDriver::IsOffscreenEnabled() const {
    return g_config.flags.offscreen;
  }

  bool BaseDriver::IsOffscreenDebugEnabled() const {
    return g_config.flags.offscreen && g_config.flags.offscreenDebug;
  }

  bool BaseDriver::EnsureOffscreenTargets() {
    if (!IsOffscreenEnabled())
      return false;

    const int targetWidth = width > 0 ? width : g_config.width;
    const int targetHeight = height > 0 ? height : g_config.height;
    if (!m_offscreenRTs.empty() && m_offscreenWidth == targetWidth && m_offscreenHeight == targetHeight)
      return true;

    DestroyOffscreenTargets();
    m_offscreenWidth = targetWidth;
    m_offscreenHeight = targetHeight;
    m_offscreenFrameIndex = 0;
    m_offscreenFrameCounter = 0;
    m_offscreenDebugDir.clear();
    m_offscreenDebugTimerStarted = false;

    static constexpr int kOffscreenBufferCount = 3;
    for (int i = 0; i < kOffscreenBufferCount; ++i) {
      int rt = CreateRT(1, BaseRT::RGBA8, BaseRT::F32, targetWidth, targetHeight, false);
      if (rt < 0) {
        T8_LOG_ERROR("[Offscreen] Failed to create offscreen RT %d (%dx%d)", i, targetWidth, targetHeight);
        DestroyOffscreenTargets();
        return false;
      }
      m_offscreenRTs.push_back(rt);
    }

    T8_LOG_INFO("[Offscreen] Created %zu offscreen RTs (%dx%d)", m_offscreenRTs.size(), targetWidth, targetHeight);
    return true;
  }

  void BaseDriver::DestroyOffscreenTargets() {
    if (!m_offscreenRTs.empty() &&
        std::find(m_offscreenRTs.begin(), m_offscreenRTs.end(), CurrentRT) != m_offscreenRTs.end()) {
      CurrentRT = -1;
    }
    for (int rt : m_offscreenRTs) {
      DestroyRT(rt);
    }
    m_offscreenRTs.clear();
    m_offscreenWidth = 0;
    m_offscreenHeight = 0;
    m_offscreenFrameIndex = 0;
  }

  int BaseDriver::GetActiveOffscreenRT() const {
    if (m_offscreenRTs.empty())
      return -1;
    int index = m_offscreenFrameIndex % static_cast<int>(m_offscreenRTs.size());
    return m_offscreenRTs[index];
  }

  bool BaseDriver::IsCurrentOffscreenTarget() const {
    if (CurrentRT < 0 || m_offscreenRTs.empty())
      return false;
    return std::find(m_offscreenRTs.begin(), m_offscreenRTs.end(), CurrentRT) != m_offscreenRTs.end();
  }

  bool BaseDriver::BindOffscreenTarget(bool clear) {
    if (!EnsureOffscreenTargets())
      return false;

    const int rt = GetActiveOffscreenRT();
    if (rt < 0 || rt >= static_cast<int>(RTs.size()) || !RTs[rt])
      return false;

    CurrentRT = rt;
    if (clear)
      RTs[rt]->Set(*T8DeviceContext);
    else
      RTs[rt]->SetLoad(*T8DeviceContext);

#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int rid = g_renderTracer->RegisterRT(RTs[rt], nullptr, rt);
      g_renderTracer->EvPushRT(rid, !clear);
    }
    RefreshTracePendingRenderState();
#endif
    return true;
  }

  void BaseDriver::CompleteOffscreenFrame() {
    if (!IsOffscreenEnabled() || m_offscreenRTs.empty())
      return;

    CurrentRT = -1;
    const int completedRT = GetActiveOffscreenRT();
    ++m_offscreenFrameCounter;

    if (IsOffscreenDebugEnabled() && completedRT >= 0) {
      const auto now = std::chrono::steady_clock::now();
      if (!m_offscreenDebugTimerStarted) {
        m_lastOffscreenDebugDump = now;
        m_offscreenDebugTimerStarted = true;
      } else if (now - m_lastOffscreenDebugDump >= std::chrono::seconds(1)) {
        SaveRTToFile(completedRT, BaseDriver::COLOR0_ATTACHMENT, BuildOffscreenDebugPath(m_offscreenFrameCounter));
        m_lastOffscreenDebugDump = now;
      }
    }

    if (!m_offscreenRTs.empty())
      m_offscreenFrameIndex = (m_offscreenFrameIndex + 1) % static_cast<int>(m_offscreenRTs.size());
  }

  const char* BaseDriver::OffscreenApiTag() const {
    return (m_currentAPI == GraphicsApi::OPENGL) ? "gl"
         : (m_currentAPI == GraphicsApi::D3D12)  ? "d3d12"
         : (m_currentAPI == GraphicsApi::VULKAN) ? "vulkan"
         : "d3d11";
  }

  std::string BaseDriver::BuildOffscreenDebugDirectory() {
    if (!m_offscreenDebugDir.empty())
      return m_offscreenDebugDir;

    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef OS_WINDOWS
    localtime_s(&localTime, &nowTime);
#else
    localtime_r(&nowTime, &localTime);
#endif

    std::ostringstream out;
    out << "dumps_" << OffscreenApiTag()
        << "_offscreen_f" << m_offscreenFrameCounter << "_"
        << std::put_time(&localTime, "%Y%m%d_%H%M%S");
    m_offscreenDebugDir = out.str();
    std::filesystem::create_directories(m_offscreenDebugDir);
    T8_LOG_INFO("[Offscreen] Debug dumps -> %s/", m_offscreenDebugDir.c_str());
    return m_offscreenDebugDir;
  }

  std::string BaseDriver::BuildOffscreenDebugPath(unsigned long long frameNumber) {
    std::ostringstream file;
    file << BuildOffscreenDebugDirectory()
         << "/RT_Dump_Offscreen_f"
         << std::setw(6) << std::setfill('0') << frameNumber;
    return file.str();
  }
}
