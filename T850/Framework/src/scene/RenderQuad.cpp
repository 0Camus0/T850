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

#include <scene/RenderQuad.h>
#include <scene/RenderGraph.h>
#include <scene/RenderMesh.h>
#include <utils/Utils.h>

#ifndef OS_ANDROID
#include <video/gl/GLShader.h>
#include <video/gl/GLDriver.h>
#endif
#if defined(OS_WINDOWS)
#include <video/d3d11/D3D11Shader.h>
#include <video/d3d11/D3D11Driver.h>
#endif
#include <utils/Log.h>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace t850 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;

  namespace {
    constexpr unsigned int kMaxDeferredLights = 128;
    constexpr int kTiledLightTileSize = 32;
    constexpr unsigned int kMaxTiledLightsPerTile = 128;
    constexpr int kTiledLightHeaderSlot = 16;
    constexpr int kTiledLightIndexSlot = 17;
    constexpr int kLightVolumeViewportPadding = 2;

    struct LightViewportRect {
      int x = 0;
      int y = 0;
      int w = 0;
      int h = 0;
    };

    bool SphereIntersectsFrustum(const XVECTOR3 planes[6], const XVECTOR3& center, float radius) {
      for (int i = 0; i < 6; ++i) {
        const float distance = planes[i].x * center.x +
                               planes[i].y * center.y +
                               planes[i].z * center.z +
                               planes[i].w;
        if (distance < -radius)
          return false;
      }
      return true;
    }

    void SetFullViewportRect(LightViewportRect& out, int width, int height) {
      out.x = 0;
      out.y = 0;
      out.w = (std::max)(0, width);
      out.h = (std::max)(0, height);
    }

    bool BuildProjectedLightViewportRect(const Camera& camera,
                                         const XVECTOR3& center,
                                         float radius,
                                         int width,
                                         int height,
                                         LightViewportRect& out) {
      if (width <= 0 || height <= 0 || radius <= 0.0f)
        return false;

      const float dx = center.x - camera.Eye.x;
      const float dy = center.y - camera.Eye.y;
      const float dz = center.z - camera.Eye.z;
      if ((dx * dx + dy * dy + dz * dz) <= radius * radius) {
        SetFullViewportRect(out, width, height);
        return true;
      }

      float minX = 1.0f;
      float minY = 1.0f;
      float maxX = -1.0f;
      float maxY = -1.0f;
      bool projectedAny = false;

      for (int ox = -1; ox <= 1; ox += 2) {
        for (int oy = -1; oy <= 1; oy += 2) {
          for (int oz = -1; oz <= 1; oz += 2) {
            XVECTOR3 corner(center.x + radius * float(ox),
                            center.y + radius * float(oy),
                            center.z + radius * float(oz),
                            1.0f);
            XVECTOR3 clip;
            XVecTransform(clip, corner, camera.VP);
            if (!std::isfinite(clip.w) || clip.w <= 0.0001f) {
              SetFullViewportRect(out, width, height);
              return true;
            }

            const float invW = 1.0f / clip.w;
            const float ndcX = clip.x * invW;
            const float ndcY = clip.y * invW;
            if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) {
              SetFullViewportRect(out, width, height);
              return true;
            }

            minX = (std::min)(minX, ndcX);
            minY = (std::min)(minY, ndcY);
            maxX = (std::max)(maxX, ndcX);
            maxY = (std::max)(maxY, ndcY);
            projectedAny = true;
          }
        }
      }

      if (!projectedAny || maxX < -1.0f || minX > 1.0f || maxY < -1.0f || minY > 1.0f)
        return false;

      minX = (std::max)(minX, -1.0f);
      minY = (std::max)(minY, -1.0f);
      maxX = (std::min)(maxX, 1.0f);
      maxY = (std::min)(maxY, 1.0f);

      const int x0 = (std::max)(0, int(std::floor((minX * 0.5f + 0.5f) * float(width))) - kLightVolumeViewportPadding);
      const int x1 = (std::min)(width, int(std::ceil((maxX * 0.5f + 0.5f) * float(width))) + kLightVolumeViewportPadding);
      const int y0 = (std::max)(0, int(std::floor((1.0f - (maxY * 0.5f + 0.5f)) * float(height))) - kLightVolumeViewportPadding);
      const int y1 = (std::min)(height, int(std::ceil((1.0f - (minY * 0.5f + 0.5f)) * float(height))) + kLightVolumeViewportPadding);

      if (x1 <= x0 || y1 <= y0)
        return false;

      out.x = x0;
      out.y = y0;
      out.w = x1 - x0;
      out.h = y1 - y0;
      return true;
    }

    void ExtractFrameCB(RenderQuad::FrameCBuffer& dst, const RenderQuad::CBuffer& src) {
      dst.WVP = src.WVP;
      dst.World = src.World;
      dst.WorldView = src.WorldView;
      dst.WVPInverse = src.WVPInverse;
      dst.WVPLight = src.WVPLight;
      dst.Projection = src.Projection;
      dst.CameraPos = src.CameraPos;
      dst.CameraInfo = src.CameraInfo;
      dst.LightCameraPos = src.LightCameraPos;
      dst.LightCameraInfo = src.LightCameraInfo;
    }

    void ExtractPassCB(RenderQuad::PassCBuffer& dst, const RenderQuad::CBuffer& src) {
      std::memcpy(dst.LightPositions, src.LightPositions, sizeof(dst.LightPositions));
      std::memcpy(dst.LightColors, src.LightColors, sizeof(dst.LightColors));
      std::memcpy(dst.LightRadius, src.LightRadius, sizeof(dst.LightRadius));
      dst.brightness = src.brightness;
      dst.toogles = src.toogles;
    }
  }

  void RenderQuad::Create() {
    m_quad.Init();
    sigBase.bits = ShaderKey::HAS_TEXCOORD0;

    char *vsSourceP = nullptr;
    char *fsSourceP = nullptr;
    std::string vsName, fsName;
    if (g_pBaseDriver->UsesGLSL()) {
      vsSourceP = file2string("Shaders/VS_Quad.glsl");
      fsSourceP = file2string("Shaders/FS_Quad.glsl");
      vsName = "VS_Quad.glsl";
      fsName = "FS_Quad.glsl";
    }
    else {
      vsSourceP = file2string("Shaders/VS_Quad.hlsl");
      fsSourceP = file2string("Shaders/FS_Quad.hlsl");
      vsName = "VS_Quad.hlsl";
      fsName = "FS_Quad.hlsl";
    }


    if (!vsSourceP || !fsSourceP) {
      T8_LOG_INFO("RenderQuad::Create failed loading shader source(s): %s, %s",
                  vsName.c_str(), fsName.c_str());
      free(vsSourceP);
      free(fsSourceP);
      return;
    }

    std::string vstr = std::string(vsSourceP);
    std::string fstr = std::string(fsSourceP);

    free(vsSourceP);
    free(fsSourceP);

    g_pBaseDriver->CreateShader(vstr, fstr, sigBase, vsName, fsName);

    // Simple pass variants (no toggle combinations)
    static const uint8_t simplePasses[] = {
      PassType::DEFERRED, PassType::DEFERRED_LDR, PassType::FSQUAD_1_TEX, PassType::FSQUAD_2_TEX,
      PassType::FSQUAD_3_TEX, PassType::VERTICAL_BLUR, PassType::HORIZONTAL_BLUR,
      PassType::ONE_PASS_BLUR, PassType::BRIGHT, PassType::HDR_COMP,
      PassType::ADAPT_LUMINANCE, PassType::COMBINE_COC,
      PassType::DOF, PassType::DOF_2, PassType::BACKBUFFER,
      PassType::GOD_RAY_CALCULATION, PassType::GOD_RAY_BLEND,
      PassType::SSAO, PassType::RAY_MARCH, PassType::LIGHT_ADD, PassType::FADE,
      PassType::LENS_FLARE_SUN, PassType::LENS_FLARE_GHOST
    };
    for (uint8_t p : simplePasses) {
      ShaderKey k(sigBase.bits);
      k.setPass(p);
      g_pBaseDriver->CreateShader(vstr, fstr, k, vsName, fsName);
    }

    // Deferred: variants that either sample the shadow/SSAO factor texture or compile it out.
    for (uint8_t p : { PassType::DEFERRED, PassType::DEFERRED_LDR, PassType::DEFERRED_LIGHT_VOLUME }) {
      for (int sh = 0; sh <= 1; ++sh) {
        for (int ao = 0; ao <= 1; ++ao) {
          ShaderKey k(sigBase.bits);
          k.setPass(p);
          if (sh) k.bits |= ShaderKey::SHADOWS;
          if (ao) k.bits |= ShaderKey::SSAO;
          g_pBaseDriver->CreateShader(vstr, fstr, k, vsName, fsName);
        }
      }
    }

    // SHADOW_COMP: 4 toggle variants (+-SHADOWS +-SSAO), also with OMNI_SHADOWS
    for (int sh = 0; sh <= 1; sh++) {
      for (int ao = 0; ao <= 1; ao++) {
        ShaderKey k(sigBase.bits);
        k.setPass(PassType::SHADOW_COMP);
        if (sh) k.bits |= ShaderKey::SHADOWS;
        if (ao) k.bits |= ShaderKey::SSAO;
        g_pBaseDriver->CreateShader(vstr, fstr, k, vsName, fsName);
        ShaderKey ko(k.bits);
        ko.bits |= ShaderKey::OMNI_SHADOWS;
        g_pBaseDriver->CreateShader(vstr, fstr, ko, vsName, fsName);
      }
    }

    // COC: 2 variants (+-AUTO_FOCUS)
    for (int af = 0; af <= 1; af++) {
      ShaderKey k(sigBase.bits);
      k.setPass(PassType::COC);
      if (af) k.bits |= ShaderKey::AUTO_FOCUS;
      g_pBaseDriver->CreateShader(vstr, fstr, k, vsName, fsName);
    }

    // LIGHT_RAY_MARCHING: 2 variants (+-GOD_RAYS)
    for (int gr = 0; gr <= 1; gr++) {
      ShaderKey k(sigBase.bits);
      k.setPass(PassType::LIGHT_RAY_MARCHING);
      if (gr) k.bits |= ShaderKey::GOD_RAYS;
      g_pBaseDriver->CreateShader(vstr, fstr, k, vsName, fsName);
    }
    T8_LOG_INFO("RenderQuad created: %zu shader variants compiled", g_pBaseDriver->m_shaderCache.size());


    t850::BufferDesc bdesc;
    bdesc.byteWidth = sizeof(CBuffer);
    bdesc.usage = BufferUsage::DEFAULT;
    pd3dConstantBuffer = (t850::ConstantBuffer*)T8Device->CreateBuffer(BufferType::CONSTANT, bdesc);
    bdesc.byteWidth = sizeof(FrameCBuffer);
    FrameCBGPU = (t850::ConstantBuffer*)T8Device->CreateBuffer(BufferType::CONSTANT, bdesc);
    bdesc.byteWidth = sizeof(PassCBuffer);
    PassCBGPU = (t850::ConstantBuffer*)T8Device->CreateBuffer(BufferType::CONSTANT, bdesc);

    /*D3D11_SAMPLER_DESC sdesc;
    sdesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sdesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.MinLOD = 0;
    sdesc.MaxLOD = D3D11_FLOAT32_MAX;
    sdesc.MipLODBias = 0.0f;
    sdesc.MaxAnisotropy = 1;
    sdesc.BorderColor[0] = sdesc.BorderColor[1] = sdesc.BorderColor[2] = sdesc.BorderColor[3] = 0;
    reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject())->CreateSamplerState(&sdesc, &pSampler);*/

    XMatIdentity(transform);
  }

  void RenderQuad::Transform(float *t) {
    transform = t;
  }

  void RenderQuad::Draw(float *t, float *vp) {
    static float time= 0;
    time += 1 / 60.0f;
    if (t)
      transform = t;

    // Build final shader key: base features + global pass + toggles
    ShaderKey finalKey(sigBase.bits);
    finalKey.setPass(gKey.getPass());
    constexpr uint64_t featureMask = (1ull << ShaderKey::PASS_SHIFT) - 1ull;
    finalKey.bits |= (gKey.bits & featureMask);

    uint8_t pass = finalKey.getPass();

    // Add toggle bits based on pass and scene properties
    if (pass == PassType::SHADOW_COMP) {
      if (pScProp->ToogleShadow) finalKey.bits |= ShaderKey::SHADOWS;
      if (pScProp->ToogleSSAO)   finalKey.bits |= ShaderKey::SSAO;
    }
    else if (pass == PassType::DEFERRED || pass == PassType::DEFERRED_LDR || pass == PassType::DEFERRED_LIGHT_VOLUME) {
      if (pScProp->ToogleShadow) finalKey.bits |= ShaderKey::SHADOWS;
      if (pScProp->ToogleSSAO)   finalKey.bits |= ShaderKey::SSAO;
    }
    else if (pass == PassType::COC) {
      if (pScProp->AutoFocus)     finalKey.bits |= ShaderKey::AUTO_FOCUS;
    }
    else if (pass == PassType::LIGHT_RAY_MARCHING) {
      if (pScProp->ToogleGodRays) finalKey.bits |= ShaderKey::GOD_RAYS;
    }

    ShaderBase * s = g_pBaseDriver->GetShader(finalKey);
    if (!s) return;

    Camera *pActualCamera = pScProp->GetPrimaryCamera();
    bool useTiledLightPass = false;
    bool skipCurrentDraw = false;

    auto clearTiledLightTextures = [&]() {
      SetTexture(nullptr, kTiledLightHeaderSlot);
      SetTexture(nullptr, kTiledLightIndexSlot);
    };

    auto ensureTiledLightTextures = [&](int tilesX, int tilesY) -> bool {
      const int tileCount = tilesX * tilesY;
      if (tilesX <= 0 || tilesY <= 0 || tileCount <= 0)
        return false;

      const bool recreateHeaders = m_tiledLightHeaderTex < 0 ||
                                   m_tiledLightTilesX != tilesX ||
                                   m_tiledLightTilesY != tilesY;
      if (recreateHeaders) {
        if (m_tiledLightHeaderTex >= 0)
          g_pBaseDriver->DestroyTexture(m_tiledLightHeaderTex);
        if (m_tiledLightIndexTex >= 0)
          g_pBaseDriver->DestroyTexture(m_tiledLightIndexTex);

        m_tiledLightTilesX = tilesX;
        m_tiledLightTilesY = tilesY;
        m_tiledLightHeaderData.assign(static_cast<size_t>(tileCount) * 4u, 0.0f);
        m_tiledLightIndexData.assign(static_cast<size_t>(tileCount) * kMaxTiledLightsPerTile * 4u, 0.0f);
        m_tiledLightHeaderTex = g_pBaseDriver->CreateFloatTexture(tilesX, tilesY, m_tiledLightHeaderData.data());
        m_tiledLightIndexTex = g_pBaseDriver->CreateFloatTexture(static_cast<int>(kMaxTiledLightsPerTile), tileCount, m_tiledLightIndexData.data());
        if (m_tiledLightHeaderTex < 0 || m_tiledLightIndexTex < 0) {
          if (m_tiledLightHeaderTex >= 0) g_pBaseDriver->DestroyTexture(m_tiledLightHeaderTex);
          if (m_tiledLightIndexTex >= 0) g_pBaseDriver->DestroyTexture(m_tiledLightIndexTex);
          m_tiledLightHeaderTex = -1;
          m_tiledLightIndexTex = -1;
          return false;
        }
      }

      const size_t headerCount = static_cast<size_t>(tileCount) * 4u;
      const size_t indexCount = static_cast<size_t>(tileCount) * kMaxTiledLightsPerTile * 4u;
      if (m_tiledLightHeaderData.size() != headerCount)
        m_tiledLightHeaderData.resize(headerCount);
      if (m_tiledLightIndexData.size() != indexCount)
        m_tiledLightIndexData.resize(indexCount);
      if (m_tiledLightCounts.size() != static_cast<size_t>(tileCount))
        m_tiledLightCounts.resize(static_cast<size_t>(tileCount));
      return true;
    };

    XMATRIX44 VP = pActualCamera->VP;
    XMATRIX44 WV = pActualCamera->View;
    VP.Inverse(&CnstBuffer.WVPInverse);
    CnstBuffer.WVP = transform;
    CnstBuffer.World = transform;
    CnstBuffer.WorldView = WV;
	  CnstBuffer.Projection = pActualCamera->VP;
    CnstBuffer.CameraPos = pActualCamera->Eye;
    CnstBuffer.brightness.x = m_brightness;
	CnstBuffer.brightness.y = pScProp->ShadowMapResolution;
	CnstBuffer.brightness.z = pScProp->PCFScale;

    if (pScProp->pLightCameras.size() > 0) {
      int selected = pScProp->ActiveLightCamera;
      CnstBuffer.WVPLight = pScProp->pLightCameras[selected]->VP;
      CnstBuffer.LightCameraPos = pScProp->pLightCameras[selected]->Eye;
      CnstBuffer.LightCameraInfo = XVECTOR3(pScProp->pLightCameras[selected]->NPlane, pScProp->pLightCameras[selected]->FPlane, pScProp->pLightCameras[selected]->Fov, 1.0f);
    }

    if (pass == PassType::DEFERRED || pass == PassType::DEFERRED_LDR || pass == PassType::DEFERRED_LIGHT_VOLUME) {
      const unsigned int sceneLightCount = static_cast<unsigned int>(pScProp->Lights.size());
      unsigned int activeLightLimit = pScProp->ActiveLights;
      if (activeLightLimit > sceneLightCount)
        activeLightLimit = sceneLightCount;
      unsigned int numLights = activeLightLimit;
      if (numLights > kMaxDeferredLights)
        numLights = kMaxDeferredLights;

      CnstBuffer.toogles.x = pScProp->EnvFactor;
      CnstBuffer.toogles.z = pScProp->IBLFactor;
      CnstBuffer.toogles.w = pScProp->IBLMipCount;
      CnstBuffer.brightness.z = pScProp->IBLDiffuseMipLevel;
      CnstBuffer.brightness.w = pScProp->IBLBRDFLUTEnabled;
      // Ambient intensity: average of AmbientColor components
      CnstBuffer.toogles.y = (pScProp->AmbientColor.x + pScProp->AmbientColor.y + pScProp->AmbientColor.z) / 3.0f;

      unsigned int packedLights = 0;
      unsigned int directionalLights = 0;
      unsigned int disabledLights = 0;
      unsigned int zeroIntensityLights = 0;
      unsigned int frustumCulledLights = 0;
      if (pass == PassType::DEFERRED_LIGHT_VOLUME) {
        const int targetWidth = Textures[0] ? static_cast<int>(Textures[0]->x) : g_pBaseDriver->width;
        const int targetHeight = Textures[0] ? static_cast<int>(Textures[0]->y) : g_pBaseDriver->height;
        const int tilesX = (std::max)(1, (targetWidth + kTiledLightTileSize - 1) / kTiledLightTileSize);
        const int tilesY = (std::max)(1, (targetHeight + kTiledLightTileSize - 1) / kTiledLightTileSize);

        const int tileCount = tilesX * tilesY;
        const size_t headerCount = static_cast<size_t>(tileCount) * 4u;
        const size_t indexCount = static_cast<size_t>(tileCount) * kMaxTiledLightsPerTile * 4u;
        if (m_tiledLightHeaderData.size() != headerCount)
          m_tiledLightHeaderData.resize(headerCount);
        if (m_tiledLightIndexData.size() != indexCount)
          m_tiledLightIndexData.resize(indexCount);
        if (m_tiledLightCounts.size() != static_cast<size_t>(tileCount))
          m_tiledLightCounts.resize(static_cast<size_t>(tileCount));
        std::fill(m_tiledLightHeaderData.begin(), m_tiledLightHeaderData.end(), 0.0f);
        std::fill(m_tiledLightIndexData.begin(), m_tiledLightIndexData.end(), 0.0f);
        std::fill(m_tiledLightCounts.begin(), m_tiledLightCounts.end(), 0u);

        XVECTOR3 frustumPlanes[6];
        RenderMesh::ExtractFrustumPlanes(pActualCamera->VP, frustumPlanes);
        unsigned long long volumePixels = 0;

        for (unsigned int i = 0; i < numLights && packedLights < kMaxDeferredLights; i++) {
            Light& light = pScProp->Lights[i];
            if (!light.Enabled) {
              ++disabledLights;
              continue;
            }
            if (light.Type == LIGHT_POINT && !pScProp->PointLightsEnabled) {
              ++disabledLights;
              continue;
            }

            const float effectiveRadius = light.Type == LIGHT_POINT ? light.radius * (std::max)(0.0f, pScProp->LightRadiusScale) : light.radius;
            const float effectiveIntensity = light.Intensity * (std::max)(0.0f, pScProp->LightIntensityScale);
            if (effectiveIntensity <= 0.0f) {
              ++zeroIntensityLights;
              continue;
            }

            if (light.Type == LIGHT_DIRECTIONAL) {
              ++directionalLights;
              continue;
            }

            const float shaderRange = effectiveRadius * 2.0f;
            if (shaderRange <= 0.0f || !SphereIntersectsFrustum(frustumPlanes, light.Position, shaderRange)) {
              ++frustumCulledLights;
              continue;
            }

            LightViewportRect viewportRect;
            if (!BuildProjectedLightViewportRect(*pActualCamera, light.Position, shaderRange, targetWidth, targetHeight, viewportRect)) {
              ++frustumCulledLights;
              continue;
            }

            const unsigned int lightIndex = packedLights++;
            CnstBuffer.LightPositions[lightIndex] = XVECTOR3(light.Position.x, light.Position.y, light.Position.z, 1.0f);
            CnstBuffer.LightColors[lightIndex] = XVECTOR3(light.Color.x, light.Color.y, light.Color.z, effectiveIntensity);
            CnstBuffer.LightRadius[lightIndex] = effectiveRadius;

            const int minTileX = (std::max)(0, viewportRect.x / kTiledLightTileSize);
            const int maxTileX = (std::min)(tilesX - 1, (viewportRect.x + viewportRect.w - 1) / kTiledLightTileSize);
            const int minTileY = (std::max)(0, viewportRect.y / kTiledLightTileSize);
            const int maxTileY = (std::min)(tilesY - 1, (viewportRect.y + viewportRect.h - 1) / kTiledLightTileSize);
            for (int ty = minTileY; ty <= maxTileY; ++ty) {
              for (int tx = minTileX; tx <= maxTileX; ++tx) {
                const int tileIndex = ty * tilesX + tx;
                unsigned int& count = m_tiledLightCounts[static_cast<size_t>(tileIndex)];
                if (count >= kMaxTiledLightsPerTile)
                  continue;
                const size_t texel = (static_cast<size_t>(tileIndex) * kMaxTiledLightsPerTile + count) * 4u;
                m_tiledLightIndexData[texel] = static_cast<float>(lightIndex);
                ++count;
              }
            }

            volumePixels += static_cast<unsigned long long>(viewportRect.w) * static_cast<unsigned long long>(viewportRect.h);
        }

        for (int tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
            const size_t texel = static_cast<size_t>(tileIndex) * 4u;
            const unsigned int tileLightCount = m_tiledLightCounts[static_cast<size_t>(tileIndex)];
            m_tiledLightHeaderData[texel] = static_cast<float>(tileIndex);
            m_tiledLightHeaderData[texel + 1u] = static_cast<float>(tileLightCount);
        }

        if (packedLights > 0) {
          if (ensureTiledLightTextures(tilesX, tilesY)) {
            Texture* headerTex = g_pBaseDriver->GetTexture(m_tiledLightHeaderTex);
            Texture* indexTex = g_pBaseDriver->GetTexture(m_tiledLightIndexTex);
            if (headerTex && indexTex) {
              headerTex->UpdateFloatData(*T8DeviceContext, tilesX, tilesY, m_tiledLightHeaderData.data());
              indexTex->UpdateFloatData(*T8DeviceContext, static_cast<int>(kMaxTiledLightsPerTile), tileCount, m_tiledLightIndexData.data());
              SetTexture(headerTex, kTiledLightHeaderSlot);
              SetTexture(indexTex, kTiledLightIndexSlot);
              useTiledLightPass = true;
            } else {
              clearTiledLightTextures();
              skipCurrentDraw = true;
            }
          } else {
            clearTiledLightTextures();
            skipCurrentDraw = true;
          }
        } else {
          skipCurrentDraw = true;
        }

        CnstBuffer.LightCameraInfo = XVECTOR3(static_cast<float>(kTiledLightTileSize),
                                                static_cast<float>(tilesX),
                                                static_cast<float>(tilesY),
                                                static_cast<float>(kMaxTiledLightsPerTile));
          pScProp->DebugDeferredLightsSceneTotal = sceneLightCount;
          pScProp->DebugDeferredLightsActiveLimit = activeLightLimit;
          pScProp->DebugDeferredLightsConsidered = numLights;
          pScProp->DebugDeferredLightsPacked = directionalLights + packedLights;
          pScProp->DebugDeferredLightsFrustumCulled = frustumCulledLights;
          pScProp->DebugDeferredLightsDisabled = disabledLights;
          pScProp->DebugDeferredLightsZeroIntensity = zeroIntensityLights;
          pScProp->DebugDeferredLightsMaxCapped = activeLightLimit > numLights ? activeLightLimit - numLights : 0;
          pScProp->DebugDeferredLightsDirectional = directionalLights;
          pScProp->DebugDeferredLightsPointVolumes = packedLights;
          const float fullPixels = float((std::max)(1, targetWidth) * (std::max)(1, targetHeight));
          pScProp->DebugDeferredLightVolumeScreenPercent = fullPixels > 0.0f ? (float(volumePixels) * 100.0f) / fullPixels : 0.0f;
          unsigned int activeTiles = 0;
          unsigned int lightRefs = 0;
          unsigned int maxLightsInTile = 0;
          unsigned int saturatedTiles = 0;
          for (unsigned int count : m_tiledLightCounts) {
            lightRefs += count;
            if (count > 0)
              ++activeTiles;
            if (count > maxLightsInTile)
              maxLightsInTile = count;
            if (count >= kMaxTiledLightsPerTile)
              ++saturatedTiles;
          }
          pScProp->DebugDeferredLightTileSize = static_cast<unsigned int>(kTiledLightTileSize);
          pScProp->DebugDeferredLightTilesX = static_cast<unsigned int>(tilesX);
          pScProp->DebugDeferredLightTilesY = static_cast<unsigned int>(tilesY);
          pScProp->DebugDeferredLightTileCount = static_cast<unsigned int>(tileCount);
          pScProp->DebugDeferredLightActiveTiles = activeTiles;
          pScProp->DebugDeferredLightTileLightRefs = lightRefs;
          pScProp->DebugDeferredLightMaxLightsInTile = maxLightsInTile;
          pScProp->DebugDeferredLightSaturatedTiles = saturatedTiles;
        pScProp->DebugDeferredLightAverageLightsPerTile =
          tileCount > 0 ? static_cast<float>(lightRefs) / static_cast<float>(tileCount) : 0.0f;
      } else {
        clearTiledLightTextures();
        const bool pointLightsUseVolumes = pScProp->DeferredLightVolumesEnabled;
        XVECTOR3 frustumPlanes[6];
        if (!pointLightsUseVolumes)
          RenderMesh::ExtractFrustumPlanes(pActualCamera->VP, frustumPlanes);

        for (unsigned int i = 0; i < numLights && packedLights < kMaxDeferredLights; i++) {
          Light& light = pScProp->Lights[i];
          if (!light.Enabled) {
            ++disabledLights;
            continue;
          }
          if (light.Type == LIGHT_POINT && !pScProp->PointLightsEnabled) {
            ++disabledLights;
            continue;
          }

          const float effectiveIntensity = light.Intensity * (std::max)(0.0f, pScProp->LightIntensityScale);
          if (effectiveIntensity <= 0.0f) {
            ++zeroIntensityLights;
            continue;
          }

          const float effectiveRadius = light.Type == LIGHT_POINT ? light.radius * (std::max)(0.0f, pScProp->LightRadiusScale) : light.radius;
          if (light.Type == LIGHT_DIRECTIONAL) {
            CnstBuffer.LightPositions[packedLights] = XVECTOR3(light.Direction.x, light.Direction.y, light.Direction.z, 0.0f);
            ++directionalLights;
          } else {
            if (pointLightsUseVolumes)
              continue;

            const float shaderRange = effectiveRadius * 2.0f;
            if (shaderRange <= 0.0f || !SphereIntersectsFrustum(frustumPlanes, light.Position, shaderRange)) {
              ++frustumCulledLights;
              continue;
            }
            CnstBuffer.LightPositions[packedLights] = XVECTOR3(light.Position.x, light.Position.y, light.Position.z, 1.0f);
            CnstBuffer.LightRadius[packedLights] = effectiveRadius;
          }
          CnstBuffer.LightColors[packedLights] = XVECTOR3(light.Color.x, light.Color.y, light.Color.z, effectiveIntensity);
          ++packedLights;
        }
      }

      CnstBuffer.CameraInfo = XVECTOR3(pActualCamera->NPlane, pActualCamera->FPlane, pActualCamera->Fov, float(packedLights));
      if (!useTiledLightPass) {
        pScProp->DebugDeferredLightsSceneTotal = sceneLightCount;
        pScProp->DebugDeferredLightsActiveLimit = activeLightLimit;
        pScProp->DebugDeferredLightsConsidered = numLights;
        pScProp->DebugDeferredLightsPacked = packedLights;
        pScProp->DebugDeferredLightsFrustumCulled = frustumCulledLights;
        pScProp->DebugDeferredLightsDisabled = disabledLights;
        pScProp->DebugDeferredLightsZeroIntensity = zeroIntensityLights;
        pScProp->DebugDeferredLightsMaxCapped = activeLightLimit > numLights ? activeLightLimit - numLights : 0;
        pScProp->DebugDeferredLightsDirectional = directionalLights;
        pScProp->DebugDeferredLightsPointVolumes = 0;
        pScProp->DebugDeferredLightVolumeScreenPercent = 0.0f;
        pScProp->DebugDeferredLightTileSize = 0;
        pScProp->DebugDeferredLightTilesX = 0;
        pScProp->DebugDeferredLightTilesY = 0;
        pScProp->DebugDeferredLightTileCount = 0;
        pScProp->DebugDeferredLightActiveTiles = 0;
        pScProp->DebugDeferredLightTileLightRefs = 0;
        pScProp->DebugDeferredLightMaxLightsInTile = 0;
        pScProp->DebugDeferredLightSaturatedTiles = 0;
        pScProp->DebugDeferredLightAverageLightsPerTile = 0.0f;
      }
    }
	else if (pass == PassType::SHADOW_COMP) {
		CnstBuffer.LightPositions[0].x = (float)pScProp->SSAOKernel.KernelSize;
		CnstBuffer.LightPositions[0].y = pScProp->SSAOKernel.Radius;
		CnstBuffer.LightPositions[0].z = (float)Textures[0]->x;
		CnstBuffer.LightPositions[0].w = (float)Textures[0]->y;
		CnstBuffer.brightness.x = pScProp->PCFSamples;
		CnstBuffer.brightness.w = pScProp->SSAOKernel.NoiseSize;
    CnstBuffer.toogles.x = pScProp->ShadowMin;
    CnstBuffer.toogles.z = (float)pScProp->DebugMode;
    CnstBuffer.toogles.w = pScProp->ShadowBias;
		for (unsigned int i = 1; i < pScProp->SSAOKernel.vSSAOKernel.size(); i++) {
			CnstBuffer.LightPositions[i] = pScProp->SSAOKernel.vSSAOKernel[i-1];
		}
	}
    else if (pass == PassType::ONE_PASS_BLUR) {
      CnstBuffer.LightPositions[0].x = pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[0].x;
      CnstBuffer.LightPositions[0].y = pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[0].y;
      CnstBuffer.LightPositions[0].z = (float)Textures[0]->x;
      CnstBuffer.LightPositions[0].w = (float)Textures[0]->y;
      for (unsigned int i = 1; i < pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel.size(); i++) {
        CnstBuffer.LightPositions[i] = pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[i];
      }
    }
    else if (pass == PassType::VERTICAL_BLUR || pass == PassType::HORIZONTAL_BLUR) {
      CnstBuffer.LightPositions[0].x = pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[0].x;
      CnstBuffer.LightPositions[0].y = pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[0].y;
      CnstBuffer.LightPositions[0].z = (float)Textures[0]->x;
      CnstBuffer.LightPositions[0].w = (float)Textures[0]->y;
      for (unsigned int i = 1; i < pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel.size(); i++) {
        CnstBuffer.LightPositions[i].x = roundTo(pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[i].x, 6.0f);
      }
    }
    else if (pass == PassType::HDR_COMP || pass == PassType::BRIGHT || pass == PassType::FSQUAD_3_TEX || pass == PassType::ADAPT_LUMINANCE) {
      Texture* mipSource = Textures[0];
      if ((pass == PassType::ADAPT_LUMINANCE) && Textures[1]) {
        mipSource = Textures[1];
      }

      if (mipSource) {
        CnstBuffer.CameraPos.w = (mipSource->mipmaps > 1) ? (float)(mipSource->mipmaps - 1) : 0.0f;
      }

      if (pass == PassType::BRIGHT) {
        CnstBuffer.LightPositions[0].x = pScProp->BloomThreshold;
        CnstBuffer.LightPositions[0].y = pScProp->Exposure;
        CnstBuffer.LightPositions[0].z = pScProp->ToneMapWhiteLevel;
      }

      if (pass == PassType::HDR_COMP) {
        CnstBuffer.LightPositions[0].x = pScProp->BloomFactor;
        CnstBuffer.LightPositions[0].y = pScProp->Exposure;
        CnstBuffer.LightPositions[0].z = pScProp->ToneMapWhiteLevel;
      }

      if (pass == PassType::ADAPT_LUMINANCE) {
        float adaptDt = pScProp->FrameDeltaSec;
        if (adaptDt < 0.0f) adaptDt = 0.0f;
        if (adaptDt > (1.0f / 30.0f)) adaptDt = (1.0f / 30.0f);
        CnstBuffer.LightPositions[1].x = pScProp->LuminanceTau;
        CnstBuffer.LightPositions[1].y = adaptDt;
        CnstBuffer.LightPositions[1].z = (float)pScProp->LuminanceMode;
        static int adaptLogCount = 0;
        if (adaptLogCount++ % 300 == 0) {
          T8_LOG_INFO("[AdaptLum] mode=%d tau=%.4f dt=%.6f mipLevel=%.0f",
                      pScProp->LuminanceMode,
                      pScProp->LuminanceTau, adaptDt,
                      CnstBuffer.CameraPos.w);
        }
      }
    }
    else if (pass == PassType::COC) {
      CnstBuffer.LightPositions[0].x = pScProp->Aperture;
      CnstBuffer.LightPositions[0].y = pScProp->FocalLength;
      CnstBuffer.LightPositions[0].z = pScProp->FocusDepth;
      CnstBuffer.LightPositions[0].w = pScProp->MaxCoc;
    }
	else if (pass == PassType::DOF || pass == PassType::DOF_2) {
	  CnstBuffer.LightPositions[0].x = pScProp->DOF_Near_Samples_squared;
	  CnstBuffer.LightPositions[0].y = pScProp->DOF_Far_Samples_squared;
	  CnstBuffer.LightPositions[0].z = (float)Textures[0]->x;
	  CnstBuffer.LightPositions[0].w = (float)Textures[0]->y;
	}
    else if (pass == PassType::RAY_MARCH) {
      CnstBuffer.LightPositions[0].x = time;
      CnstBuffer.toogles = pScProp->pLightCameras[1]->Eye;
	}
	else if (pass == PassType::LIGHT_RAY_MARCHING) {
	  CnstBuffer.LightPositions[0].y = pScProp->LightVolumeSteps;
      XVECTOR3 sunDir(0.0f, 1.0f, 0.0f, 0.0f);
      if (pScProp->pLightCameras.size() > 0) {
        int selected = pScProp->ActiveLightCamera;
        sunDir = -pScProp->pLightCameras[selected]->Look;
        sunDir.Normalize();
      }
      CnstBuffer.LightColors[0] = XVECTOR3(sunDir.x, sunDir.y, sunDir.z, 1.0f);
    CnstBuffer.toogles.x = pScProp->GodRaysFactor;
	  CnstBuffer.toogles.z = (float)pScProp->DebugMode;
    CnstBuffer.toogles.w = pScProp->ShadowBias;
	}

    if (skipCurrentDraw)
      return;

    const BaseDriver::FaceCulling previousCull = g_pBaseDriver->m_FaceCulling;
    g_pBaseDriver->SetCullFace(BaseDriver::FRONT_AND_BACK);

    m_quad.Set();
    T8DeviceContext->SetPrimitiveTopology(Topology::TRIANLE_LIST);
    s->Set(*T8DeviceContext);

    auto updateQuadConstants = [&]() {
      if (g_pBaseDriver->UsesGLSL()) {
        pd3dConstantBuffer->UpdateFromBuffer(*T8DeviceContext, &CnstBuffer);
        pd3dConstantBuffer->Set(*T8DeviceContext);
      } else {
        ExtractFrameCB(FrameCB, CnstBuffer);
        ExtractPassCB(PassCB, CnstBuffer);
        FrameCBGPU->UpdateFromBuffer(*T8DeviceContext, &FrameCB);
        PassCBGPU->UpdateFromBuffer(*T8DeviceContext, &PassCB);
        FrameCBGPU->Set(*T8DeviceContext, 0);
        PassCBGPU->Set(*T8DeviceContext, 1);
      }
    };
    updateQuadConstants();

    auto textureNameForSlot = [](int slot) -> const char* {
      switch (slot) {
      case 0: return "tex0";
      case 1: return "tex1";
      case 2: return "tex2";
      case 3: return "tex3";
      case 4: return "tex4";
      case 5: return "tex5";
      case 7: return "tex6";
      case 8: return "tex7";
      case 9: return "tex8";
      case kTiledLightHeaderSlot: return "texTileHeaders";
      case kTiledLightIndexSlot: return "texTileLightIndices";
      case EnvironmentTextureSlot::DiffuseIBL: return "texIBLDiffuse";
      case EnvironmentTextureSlot::SpecularIBL: return "texIBLSpecular";
      case EnvironmentTextureSlot::BrdfLUT: return "texIBLBRDF";
      case EnvironmentTextureSlot::CharlieIBL: return "texIBLCharlie";
      case EnvironmentTextureSlot::CharlieLUT: return "texIBLCharlieLUT";
      case EnvironmentTextureSlot::SheenELUT: return "texIBLSheenELUT";
      default: return nullptr;
      }
    };

    auto passUsesTextureSlot = [&](int slot) -> bool {
      if (pass == PassType::SHADOW_COMP) {
        if (slot == 1)
          return finalKey.has(ShaderKey::SHADOWS);
        if (slot == 2 || slot == 3)
          return finalKey.has(ShaderKey::SSAO);
      } else if (pass == PassType::DEFERRED ||
                 pass == PassType::DEFERRED_LDR ||
                 pass == PassType::DEFERRED_LIGHT_VOLUME) {
        if (slot == 5)
          return finalKey.has(ShaderKey::SHADOWS) || finalKey.has(ShaderKey::SSAO);
        if (slot == kTiledLightHeaderSlot || slot == kTiledLightIndexSlot)
          return pass == PassType::DEFERRED_LIGHT_VOLUME && useTiledLightPass;
      }
      return true;
    };

    for (int slot = 0; slot < MaxPrimitiveTextures; ++slot) {
      const char* textureName = textureNameForSlot(slot);
      if (Textures[slot] && textureName && passUsesTextureSlot(slot))
        Textures[slot]->Set(*T8DeviceContext, slot, textureName);
    }
    if (EnvMap) {
      EnvMap->Set(*T8DeviceContext, 6, "texEnv");
      EnvMap->SetSampler(*T8DeviceContext, 6);
    }

    for (int slot = 0; slot < MaxPrimitiveTextures; ++slot) {
      if ((slot == kTiledLightHeaderSlot || slot == kTiledLightIndexSlot) &&
          pass == PassType::DEFERRED_LIGHT_VOLUME)
        continue;

      if (Textures[slot] && textureNameForSlot(slot) && passUsesTextureSlot(slot))
        Textures[slot]->SetSampler(*T8DeviceContext, slot);
    }

    T8DeviceContext->DrawIndexed(6, 0, 0);
    g_pBaseDriver->SetCullFace(previousCull);
  }

  void RenderQuad::Destroy() {
    m_quad.Destroy();
    if (m_tiledLightHeaderTex >= 0) { g_pBaseDriver->DestroyTexture(m_tiledLightHeaderTex); m_tiledLightHeaderTex = -1; }
    if (m_tiledLightIndexTex >= 0) { g_pBaseDriver->DestroyTexture(m_tiledLightIndexTex); m_tiledLightIndexTex = -1; }
    if (pd3dConstantBuffer) { pd3dConstantBuffer->release(); pd3dConstantBuffer = nullptr; }
    if (FrameCBGPU) { FrameCBGPU->release(); FrameCBGPU = nullptr; }
    if (PassCBGPU) { PassCBGPU->release(); PassCBGPU = nullptr; }
  }
}
