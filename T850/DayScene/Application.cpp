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

#include <Application.h>
#include <video/BaseDriver.h>
#ifndef OS_ANDROID
#include <video/gl/GLTexture.h>
#endif
#include <utils/InputManager.h>
#ifndef OS_ANDROID
#include <SDL3/SDL.h>
#endif
#include <utils/Log.h>
#include <utils/ResourceLocator.h>
#include <utils/Utils.h>
#include <debug/Profiler.h>
#include <debug/RenderTrace.h>
#include <debug/LoadingProgress.h>
#include <debug/RuntimeTelemetry.h>
#include <core/Config.h>
#include <core/EngineContext.h>
#include <scene/MeshAssetCache.h>
#include <scene/MaterialAsset.h>
#include <imgui/DevGuiContext.h>
#include <imgui.h>
#ifndef OS_ANDROID

#include <imgui_impl_vulkan.h>
#endif

#ifdef OS_WINDOWS
#  include <video/d3d11/D3D11Texture.h>
#  include <video/d3d12/D3D12Driver.h>
#  include <video/d3d12/D3D12Texture.h>
#  include <video/vulkan/VulkanTexture.h>
#endif
#ifdef OS_ANDROID
#  include <android/input.h>
#  include <core/android/AndroidFramework.h>
#  include <video/vulkan/VulkanDriver.h>
#  include <unistd.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(OS_LINUX)
#include <sys/time.h>
#endif

#include <iostream>
#include <string>
#include <system_error>
#include <vector>

using namespace t850;
extern std::vector<std::string> g_args;

namespace t850 {
  extern Device*       T8Device;
}

#ifndef OS_ANDROID
namespace {
  struct DebugRTEntry {
    std::string key;
    std::string label;
    t850::BaseDriver* driver = nullptr;
    t850::Texture* texture = nullptr;
    ImTextureID image = (ImTextureID)nullptr;
    bool flipV = false;
    bool opaqueBlend = false;
  };

#ifdef OS_WINDOWS
  uint64_t StoreVkDescriptorSet(VkDescriptorSet descriptor) {
#if defined(VK_USE_64_BIT_PTR_DEFINES) && VK_USE_64_BIT_PTR_DEFINES
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(descriptor));
#else
    return static_cast<uint64_t>(descriptor);
#endif
  }

  VkDescriptorSet LoadVkDescriptorSet(uint64_t descriptor) {
#if defined(VK_USE_64_BIT_PTR_DEFINES) && VK_USE_64_BIT_PTR_DEFINES
    return reinterpret_cast<VkDescriptorSet>(static_cast<uintptr_t>(descriptor));
#else
    return static_cast<VkDescriptorSet>(descriptor);
#endif
  }

  bool IsSingleChannelFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
      return true;
    default:
      return false;
    }
  }

  UINT D3D12OpaquePreviewMapping(DXGI_FORMAT format) {
    int green = IsSingleChannelFormat(format) ? D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0
                                              : D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1;
    int blue = IsSingleChannelFormat(format) ? D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0
                                             : D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2;
    return D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
      D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
      green,
      blue,
      D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
  }
#endif

  ImTextureID GetDebugTextureID(t850::BaseDriver* driver, t850::Texture* texture,
                                std::unordered_map<void*, uint64_t>& textureDescriptors,
                                std::unordered_map<void*, uint64_t>& opaqueTextureDescriptors) {
    if (!driver || !texture) return (ImTextureID)nullptr;

#ifdef OS_WINDOWS
    if (driver->m_currentAPI == t850::GraphicsApi::D3D11) {
      auto* d3dTexture = static_cast<t850::D3DXTexture*>(texture);
      return (ImTextureID)d3dTexture->pSRVTex.Get();
    }
    if (driver->m_currentAPI == t850::GraphicsApi::D3D12) {
      auto* d3dTexture = static_cast<t850::D3D12Texture*>(texture);
      if (!d3dTexture->pTexResource) return (ImTextureID)nullptr;

      auto found = opaqueTextureDescriptors.find(texture);
      if (found != opaqueTextureDescriptors.end()) {
        return (ImTextureID)found->second;
      }

      auto* d3d12Driver = static_cast<t850::D3D12Driver*>(driver);
      auto& srvHeap = d3d12Driver->GetHeap(t850::D3D12Heap::CBV_SRV_UAV_VISIBLE);
      D3D12_CPU_DESCRIPTOR_HANDLE srvCPU = srvHeap.AllocateCPU();
      D3D12_GPU_DESCRIPTOR_HANDLE srvGPU = srvHeap.AllocateGPU();

      D3D12_RESOURCE_DESC resourceDesc = d3dTexture->pTexResource->GetDesc();
      DXGI_FORMAT srvFormat = resourceDesc.Format;
      if (resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
        if (resourceDesc.Format == DXGI_FORMAT_R32_TYPELESS) srvFormat = DXGI_FORMAT_R32_FLOAT;
        if (resourceDesc.Format == DXGI_FORMAT_R16_TYPELESS) srvFormat = DXGI_FORMAT_R16_FLOAT;
      }

      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
      srvDesc.Format = srvFormat;
      srvDesc.Shader4ComponentMapping = D3D12OpaquePreviewMapping(srvFormat);
      if (resourceDesc.DepthOrArraySize == 6 && (resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = 1;
      } else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
      }

      Microsoft::WRL::ComPtr<ID3D12Device> device;
      if (FAILED(d3dTexture->pTexResource->GetDevice(IID_PPV_ARGS(device.GetAddressOf())))) {
        return (ImTextureID)d3dTexture->srvGPU.ptr;
      }

      device->CreateShaderResourceView(d3dTexture->pTexResource.Get(), &srvDesc, srvCPU);
      opaqueTextureDescriptors[texture] = static_cast<uint64_t>(srvGPU.ptr);
      return (ImTextureID)srvGPU.ptr;
    }
#endif

    if (driver->m_currentAPI == t850::GraphicsApi::OPENGL) {
      return (ImTextureID)(intptr_t)texture->id;
    }

#ifdef OS_WINDOWS
    if (driver->m_currentAPI == t850::GraphicsApi::VULKAN) {
      auto* vkTexture = static_cast<t850::VulkanTexture*>(texture);
      if (!vkTexture->m_sampler || !vkTexture->m_imageView) return (ImTextureID)nullptr;

      auto found = textureDescriptors.find(texture);
      if (found != textureDescriptors.end()) {
        return (ImTextureID)found->second;
      }

      VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(
        vkTexture->m_sampler,
        vkTexture->m_imageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      textureDescriptors[texture] = StoreVkDescriptorSet(descriptor);
      return (ImTextureID)descriptor;
    }
#endif

    return (ImTextureID)nullptr;
  }

  float TextureAspect(t850::Texture* texture) {
    if (!texture || texture->x == 0 || texture->y == 0) return 16.0f / 9.0f;
    float aspect = (float)texture->x / (float)texture->y;
    return (std::max)(0.25f, (std::min)(aspect, 4.0f));
  }

  ImVec2 FitImageSize(t850::Texture* texture, ImVec2 available, float maxHeight) {
    float aspect = TextureAspect(texture);
    ImVec2 size((std::max)(1.0f, available.x), (std::max)(1.0f, available.x / aspect));
    if (maxHeight > 0.0f && size.y > maxHeight) {
      size.y = maxHeight;
      size.x = size.y * aspect;
    }
    if (size.x > available.x) {
      size.x = available.x;
      size.y = size.x / aspect;
    }
    return size;
  }

  std::vector<DebugRTEntry> BuildDebugRTEntries(t850::BaseDriver* driver,
                                                std::unordered_map<void*, uint64_t>& textureDescriptors,
                                                std::unordered_map<void*, uint64_t>& opaqueTextureDescriptors) {
    std::vector<DebugRTEntry> entries;
    if (!driver) return entries;

    for (int rtIndex = 0; rtIndex < (int)driver->RTs.size(); ++rtIndex) {
      t850::BaseRT* rt = driver->RTs[rtIndex];
      if (!rt) continue;

      for (int colorIndex = 0; colorIndex < (int)rt->vColorTextures.size(); ++colorIndex) {
        t850::Texture* texture = rt->vColorTextures[colorIndex];
        if (!texture) continue;

        char key[64];
        char label[160];
        snprintf(key, sizeof(key), "rt%d_color%d", rtIndex, colorIndex);
        snprintf(label, sizeof(label), "RT %d Color %d  %ux%u", rtIndex, colorIndex, texture->x, texture->y);
        ImTextureID image = GetDebugTextureID(driver, texture, textureDescriptors, opaqueTextureDescriptors);
        bool opaqueBlend = driver->m_currentAPI == t850::GraphicsApi::D3D11;
        entries.push_back({ key, label, driver, texture, image, driver->NeedsVFlip(), opaqueBlend });
      }

      if (rt->pDepthTexture) {
        t850::Texture* texture = rt->pDepthTexture;
        char key[64];
        char label[160];
        snprintf(key, sizeof(key), "rt%d_depth", rtIndex);
        snprintf(label, sizeof(label), "RT %d Depth  %ux%u", rtIndex, texture->x, texture->y);
        ImTextureID image = GetDebugTextureID(driver, texture, textureDescriptors, opaqueTextureDescriptors);
        bool opaqueBlend = driver->m_currentAPI == t850::GraphicsApi::D3D11;
        entries.push_back({ key, label, driver, texture, image, driver->NeedsVFlip(), opaqueBlend });
      }
    }

    return entries;
  }

  void PruneDebugTextureDescriptors(t850::BaseDriver* driver,
                                    const std::vector<DebugRTEntry>& entries,
                                    std::unordered_map<void*, uint64_t>& textureDescriptors,
                                    std::unordered_map<void*, uint64_t>& opaqueTextureDescriptors) {
    if (!driver) return;

    std::unordered_set<void*> liveTextures;
    for (const DebugRTEntry& entry : entries) {
      liveTextures.insert(entry.texture);
    }

    if (driver->m_currentAPI == t850::GraphicsApi::VULKAN) {
      for (auto it = textureDescriptors.begin(); it != textureDescriptors.end();) {
        if (liveTextures.find(it->first) == liveTextures.end()) {
#ifdef OS_WINDOWS
          ImGui_ImplVulkan_RemoveTexture(LoadVkDescriptorSet(it->second));
#endif
          it = textureDescriptors.erase(it);
        } else {
          ++it;
        }
      }
    }

    if (driver->m_currentAPI == t850::GraphicsApi::D3D12) {
      for (auto it = opaqueTextureDescriptors.begin(); it != opaqueTextureDescriptors.end();) {
        if (liveTextures.find(it->first) == liveTextures.end()) {
          it = opaqueTextureDescriptors.erase(it);
        } else {
          ++it;
        }
      }
    }
  }

  void ReleaseDebugTextureDescriptors(std::unordered_map<void*, uint64_t>& textureDescriptors,
                                      std::unordered_map<void*, uint64_t>& opaqueTextureDescriptors) {
#ifdef OS_WINDOWS
    for (auto& entry : textureDescriptors) {
      ImGui_ImplVulkan_RemoveTexture(LoadVkDescriptorSet(entry.second));
    }
#endif
    textureDescriptors.clear();
    opaqueTextureDescriptors.clear();
  }

  void BeginOpaquePreviewCallback(const ImDrawList*, const ImDrawCmd* command) {
    auto* driver = static_cast<t850::BaseDriver*>(command->UserCallbackData);
    if (driver) driver->SetBlendState(t850::BaseDriver::BLEND_DEFAULT);
  }

  void DrawDebugImage(const DebugRTEntry& entry, ImVec2 min, ImVec2 max, ImVec2 uv0, ImVec2 uv1) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (entry.opaqueBlend) {
      drawList->AddCallback(BeginOpaquePreviewCallback, entry.driver);
    }
    drawList->AddImage(entry.image, min, max, uv0, uv1);
    if (entry.opaqueBlend) {
      drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }
  }

  bool DrawDebugTextureTile(const DebugRTEntry& entry, ImVec2 size) {
    ImGui::PushID(entry.key.c_str());
    ImGui::BeginGroup();

    bool clicked = ImGui::InvisibleButton("preview", size);
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 bg = ImGui::GetColorU32(ImGuiCol_FrameBg);
    ImU32 border = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Border);
    drawList->AddRectFilled(min, max, bg, 3.0f);
    if (entry.image) {
      ImVec2 uv0(0.0f, entry.flipV ? 1.0f : 0.0f);
      ImVec2 uv1(1.0f, entry.flipV ? 0.0f : 1.0f);
      DrawDebugImage(entry, min, max, uv0, uv1);
    } else {
      const char* text = "Preview unavailable";
      ImVec2 textSize = ImGui::CalcTextSize(text);
      ImVec2 pos(min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f);
      drawList->AddText(pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
    }
    drawList->AddRect(min, max, border, 3.0f);

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size.x);
    ImGui::TextUnformatted(entry.label.c_str());
    ImGui::PopTextWrapPos();

    ImGui::EndGroup();
    ImGui::PopID();
    return clicked;
  }

  void DrawDebugRenderTargetPanel(const std::vector<DebugRTEntry>& entries,
                                  std::unordered_set<std::string>& openTargets,
                                  bool* panelOpen) {
    if (!panelOpen || !*panelOpen) return;

    ImGui::SetNextWindowSize(ImVec2(520.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("DEBUG", panelOpen)) {
      ImGui::End();
      return;
    }

    if (entries.empty()) {
      ImGui::TextDisabled("No render targets are currently registered.");
      ImGui::End();
      return;
    }

    float contentWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float minTileWidth = 150.0f;
    int columns = (std::max)(1, (int)((contentWidth + spacing) / (minTileWidth + spacing)));
    float tileWidth = (contentWidth - spacing * (float)(columns - 1)) / (float)columns;
    tileWidth = (std::max)(96.0f, tileWidth);
    ImVec2 tileSize(tileWidth, tileWidth * 0.56f);

    for (int index = 0; index < (int)entries.size(); ++index) {
      const DebugRTEntry& entry = entries[index];
      if (DrawDebugTextureTile(entry, tileSize)) {
        openTargets.insert(entry.key);
      }
      if ((index + 1) % columns != 0) {
        ImGui::SameLine();
      }
    }

    ImGui::End();
  }

  void DrawDebugPreviewWindows(const std::vector<DebugRTEntry>& entries,
                               std::unordered_set<std::string>& openTargets) {
    std::vector<std::string> closeKeys;
    std::unordered_set<std::string> liveKeys;

    for (const DebugRTEntry& entry : entries) {
      liveKeys.insert(entry.key);
      if (openTargets.find(entry.key) == openTargets.end()) continue;

      bool open = true;
      std::string title = "DEBUG - " + entry.label + "###" + entry.key;
      ImGui::SetNextWindowSize(ImVec2(760.0f, 480.0f), ImGuiCond_FirstUseEver);
      if (ImGui::Begin(title.c_str(), &open)) {
        ImGui::TextUnformatted(entry.label.c_str());
        ImGui::Separator();
        ImVec2 available = ImGui::GetContentRegionAvail();
        if (entry.image) {
          ImVec2 imageSize = FitImageSize(entry.texture, available, available.y);
          ImVec2 uv0(0.0f, entry.flipV ? 1.0f : 0.0f);
          ImVec2 uv1(1.0f, entry.flipV ? 0.0f : 1.0f);
          ImGui::InvisibleButton("preview", imageSize);
          DrawDebugImage(entry, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), uv0, uv1);
        } else {
          ImGui::TextDisabled("Live preview is not available for the current graphics backend.");
        }
      }
      ImGui::End();

      if (!open) {
        closeKeys.push_back(entry.key);
      }
    }

    for (const std::string& key : openTargets) {
      if (liveKeys.find(key) == liveKeys.end()) {
        closeKeys.push_back(key);
      }
    }
    for (const std::string& key : closeKeys) {
      openTargets.erase(key);
    }
  }
}
#endif // !OS_ANDROID

#ifdef T850_RENDER_TRACE
namespace {
  t850::BaseDriver* s_traceDriver = nullptr;

  void EnsureRenderTracer(t850::BaseDriver* driver) {
    if (!driver) return;
    if (!t850::g_renderTracer) {
      t850::g_renderTracer = new t850::RenderTracer();
      s_traceDriver = nullptr;
    }
    if (s_traceDriver != driver) {
      t850::g_renderTracer->Init(driver);
      s_traceDriver = driver;
      T8_LOG_INFO("[RenderTracer] initialized for active driver");
    }
  }
}
#endif



#include <DayScene.h>
#include <SandboxScene.h>

#ifdef OS_ANDROID
namespace {
  constexpr int kAndroidGuiPanelControls = 0;
  constexpr int kAndroidGuiPanelPhysics = 1;
  constexpr int kAndroidTapSideLeft = 0;
  constexpr int kAndroidTapSideRight = 1;
}
#endif

void App::InitVars() {
  //t850::Technique tech("Techniques/test_technique.xml");
	DtTimer.Init();
	DtTimer.Update();
	srand((unsigned int)DtTimer.GetDTSecs());
  FirstFrame = true;

  m_scenes.emplace_back(std::make_unique<SandboxScene>());
  m_scenes.emplace_back(std::make_unique<DayScene>());
  t850::EngineContext& engineContext = t850::GetEngineContext();
  engineContext.physics = &m_physics;
  if (!m_physics.Initialize() && m_physics.IsAvailable()) {
    T8_LOG_ERROR("[Physics] Failed to initialize Jolt physics system");
  }
  for (auto &it : m_scenes) {
    it->pFramework = pFramework;
    it->SetEngineContext(&engineContext);
    //it->InitVars();
  }
  int sceneIdx = (g_config.startScene >= 0 && g_config.startScene < (int)m_scenes.size()) ? g_config.startScene : 0;
  if (!g_config.sceneFilePath.empty()) {
    sceneIdx = 0;
  }
  if (g_config.flags.benchmark && m_scenes.size() > 1) {
    sceneIdx = 1;
  }
  m_actualScene = m_scenes[sceneIdx].get();
  m_actualScene->InitVars();

#ifndef OS_ANDROID
  m_devLayer.Init(pFramework);
  m_devLayer.SetActiveScene(m_actualScene);
#endif

  Cam.InitPerspective(XVECTOR3(0.0f, 1.0f, 10.0f), Deg2Rad(46.8f), 1280.0f / 720.0f, 2.0f, 12000.0f);
  Cam.Speed = 10.0f;
  Cam.Eye = XVECTOR3(0.0f, 9.75f, -31.0f);
  Cam.Pitch = 0.14f;
  Cam.Roll = 0.0f;
  Cam.Yaw = 0.020f;
  Cam.Update(0.0f);
  SceneProp.AddCamera(&Cam);
  fading = false;
}

void App::LoadScene(int id) {
  if (id < 0 || id >= static_cast<int>(m_scenes.size())) {
    T8_LOG_ERROR("[App] Ignoring invalid scene id %d (available scenes=%zu)", id, m_scenes.size());
    return;
  }

  if (m_actualScene != nullptr) {
    FadeFX(0.5, true);
    m_actualScene->OnDestoryScene();
  }

  m_actualScene = m_scenes[id].get();
  m_actualScene->SetEngineContext(&t850::GetEngineContext());
  m_actualScene->OnLoadScene();
#ifndef OS_ANDROID
  m_devLayer.SetActiveScene(m_actualScene);
#endif
  FadeFX(0.5,false);
}

void App::LoadAssets()
{
  if (g_config.flags.profile && !t850::g_profiler) {
    t850::g_profiler = new t850::Profiler();
    t850::g_profiler->Init(pFramework->pVideoDriver);
  }
#ifdef T850_RENDER_TRACE
  EnsureRenderTracer(pFramework->pVideoDriver);
#endif
}

void App::CreateAssets() {
  if (g_config.flags.profile && !t850::g_profiler) {
    t850::g_profiler = new t850::Profiler();
    t850::g_profiler->Init(pFramework->pVideoDriver);
  }
#ifdef T850_RENDER_TRACE
  EnsureRenderTracer(pFramework->pVideoDriver);
#endif
  if (!m_actualScene) {
    T8_LOG_ERROR("[App] CreateAssets skipped: active scene is null");
    return;
  }
  m_imguiVisible = g_config.flags.guiOnStart;
  if (!g_config.flags.benchmark) {
    t850::LoadingProgress::Reset(160.0f, "Starting", "Preparing renderer");
    m_imguiReady = m_imgui.Init(pFramework, "imgui_runtime_layout.ini", true);
    if (!m_imguiReady) {
      T8_LOG_ERROR("[App] Runtime ImGui init failed");
      t850::LoadingProgress::Clear();
    } else {
      m_imgui.InstallLoadingProgressRenderer();
      t850::LoadingProgress::RequestFrame(true);
    }
  }

  t850::LoadingProgress::Advance(5.0f);
  {
    t850::LoadingProgress::ScopedStep sceneStep("Loading scene", "Creating scene assets", 35.0f);
    m_actualScene->CreateAssets();
  }
  {
    t850::LoadingProgress::ScopedStep fontStep("Loading font", "Fonts/Martius-LV9L4.ttf", 4.0f);
    m_textRender.LoadFromFile(36,"Fonts/Martius-LV9L4.ttf",512.0f);
  }
  {
    t850::LoadingProgress::ScopedStep primitiveStep("Preparing primitives", "Runtime draw helpers", 6.0f);
    PrimitiveMgr.SetEngineContext(&t850::GetEngineContext());
    PrimitiveMgr.Init();
    PrimitiveMgr.SetVP(&VP);
    PrimitiveMgr.SetSceneProps(&SceneProp);
    Quads[0].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &VP);
  }
#ifdef OS_ANDROID
  {
    t850::LoadingProgress::ScopedStep androidGuiStep("Loading UI settings", "Android GUI profile", 2.0f);
    LoadAndroidGuiSettings();
  }
#endif

  if (!g_config.flags.benchmark && m_imguiReady) {
    t850::LoadingProgress::Complete("Ready", "Starting renderer");
    m_imgui.ClearLoadingProgressRenderer();
    t850::LoadingProgress::Clear();
  }

  if (!g_config.flags.dumpShaderPermutations) {
    FadeFX(0.5, false);
  }

  // Initialize profiler if requested (after driver is fully set up)
  if (g_config.flags.profile && !t850::g_profiler) {
    t850::g_profiler = new t850::Profiler();
    t850::g_profiler->Init(pFramework->pVideoDriver);
  }
}

void App::DestroyAssets() {
   if (t850::g_profiler) {
     delete t850::g_profiler;
     t850::g_profiler = nullptr;
   }
#ifdef T850_RENDER_TRACE
   if (t850::g_renderTracer) {
     t850::g_renderTracer->Destroy();
     delete t850::g_renderTracer;
     t850::g_renderTracer = nullptr;
     s_traceDriver = nullptr;
   }
#endif
#ifndef OS_ANDROID
   m_devLayer.Destroy();
#endif
   if (m_imguiReady) {
#ifndef OS_ANDROID
     ReleaseDebugTextureDescriptors(m_debugTextureDescriptors, m_debugOpaqueTextureDescriptors);
#endif
     m_imgui.Shutdown();
     m_imguiReady = false;
   }
   m_textRender.Destroy();
   PrimitiveMgr.DestroyPrimitives();
   if (m_actualScene) {
     m_actualScene->DestroyAssets();
   }
   t850::MeshAssetCache::Get().Clear();
   t850::MaterialAssetCache::Get().Clear();
   m_physics.Shutdown();
   t850::GetEngineContext().physics = nullptr;
}

void App::OnUpdate() {
   DtTimer.Update();
   DtSecs = DtTimer.GetDTSecs();
   if (FirstFrame) {
     DtSecs = 1.0f / 60.0f;
   }
   static uint64_t telemetryFrameIndex = 0;
   t850::RuntimeTelemetry::BeginFrame(telemetryFrameIndex++, DtSecs);
   {
    T8_TELEMETRY_SCOPE("frame.total");
    {
      T8_TELEMETRY_SCOPE("frame.update");
      static float timeAccum = 0;
      timeAccum += DtSecs;

      if (timeAccum > 1.0) {
        T8_TELEMETRY_SCOPE("frame.fps_text_update");
        m_fpsString = "FPS " + std::to_string((int)(1.0 / DtSecs));
        m_fpsCol = XVECTOR3(0.2, 0.8, 0.2);
        timeAccum = 0;
      }

#ifndef OS_ANDROID
      HandleRuntimeGuiToggle("update");
      if (m_imguiVisible && m_actualScene) {
        IManager.xDelta = 0;
        IManager.yDelta = 0;
        m_actualScene->ResetViewInput();
      }
      {
        T8_TELEMETRY_SCOPE("scene.update");
        m_devLayer.Update(DtSecs);
      }
#else
      if (m_actualScene && !bPaused) {
        T8_TELEMETRY_SCOPE("scene.update");
        m_actualScene->OnUpdate(DtSecs);
      }
#endif
      if (t850::GetEngineContext().physics && t850::GetEngineContext().physics->IsInitialized()) {
        T8_TELEMETRY_SCOPE("physics.update");
        t850::GetEngineContext().physics->Update(DtSecs);
      }

      {
        T8_TELEMETRY_SCOPE("input.update");
        OnInput();
      }
    }
    OnDraw();
   }
   t850::RuntimeTelemetry::EndFrame();
}

void App::OnDraw() {
  T8_TELEMETRY_SCOPE("frame.draw");
  if (t850::g_profiler) t850::g_profiler->BeginFrame();
  static int frameCount = 0;
#ifdef T850_RENDER_TRACE
  EnsureRenderTracer(pFramework->pVideoDriver);
  if (t850::g_renderTracer) t850::g_renderTracer->ResetFrame(frameCount);
#endif
  T8_LOG_TRACE("[Frame %d] === OnDraw BEGIN ===", frameCount);
  pFramework->pVideoDriver->Clear();
  FirstFrame = false;

#ifndef OS_ANDROID
  m_devLayer.Draw();
#else
  if (m_actualScene) m_actualScene->OnDraw();
#endif
  if (fading) {
    T8_LOG_TRACE("[Frame %d] Fade quad draw", frameCount);
    pFramework->pVideoDriver->SetBlendState(BaseDriver::ALPHA_BLEND);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::READ);
    //Fade
    ShaderKey fadeKey(0); fadeKey.setPass(PassType::FADE); fadeKey.bits |= ShaderKey::HAS_TEXCOORD0;
    Quads[0].SetGlobalKey(fadeKey);
    if (fadeOut)
      Quads[0].SetBrightness(totalFadeTime / _fadeTime);
    else
      Quads[0].SetBrightness(1.0f-totalFadeTime / _fadeTime);
    Quads[0].Draw();
    pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_DEFAULT);
  }

  DrawRuntimeGui();

  frameCount++;

  if (t850::g_profiler) {
    t850::g_profiler->EndFrame();
    static bool reported = false;
    if (!reported && t850::g_profiler->GetFrameCount() >= g_config.profileFrames) {
      reported = true;
      T8_LOG_INFO("[App] Profiler reached %d frames, printing report...",
                  t850::g_profiler->GetFrameCount());
      t850::g_profiler->Report();
      t850::g_profiler->Reset();
      // Clean shutdown after profiling — use _exit to skip static destructors
      // which may reference already-freed driver/framework objects.
      pFramework->pVideoDriver->FlushGPUResources();
      DestroyAssets();
      pFramework->pVideoDriver->DestroyDriver();
      _exit(0);
    }
  }

  // Skip presenting the first frame (black with only text)
  if (frameCount > 1) {
    T8_LOG_TRACE("[Frame %d] === SwapBuffers ===" , frameCount);
    {
      T8_TELEMETRY_SCOPE("frame.swap_buffers");
      pFramework->pVideoDriver->SwapBuffers();
    }
  } else {
    T8_LOG_TRACE("[Frame %d] === SKIPPED SwapBuffers (first frame) ===" , frameCount);
  }
}

bool App::HandleRuntimeGuiToggle(const char* phase) {
#ifdef OS_ANDROID
  (void)phase;
  return false;
#else
  if (!m_imguiReady) {
    return false;
  }

  const bool imguiConsumesKeyboard =
      m_imguiVisible && (m_imgui.WantsKeyboard() || m_imgui.WantsTextInput());
  if (imguiConsumesKeyboard || !IManager.PressedOnceKey(T800K_g)) {
    return false;
  }

  const bool oldVisible = m_imguiVisible;
  m_imguiVisible = !m_imguiVisible;
  IManager.xDelta = 0;
  IManager.yDelta = 0;
  if (m_actualScene) {
    m_actualScene->ResetViewInput();
  }
  T8_LOG_VERBOSE("[MouseMode] G toggle phase=%s gui %d->%d mouse=(%d,%d) delta reset",
                 phase ? phase : "",
                 oldVisible ? 1 : 0,
                 m_imguiVisible ? 1 : 0,
                 IManager.mouseX,
                 IManager.mouseY);
  return true;
#endif
}

void App::OnInput() {
	if (FirstFrame)
		return;
#ifndef OS_ANDROID
  const bool imguiConsumesKeyboard =
      m_imguiReady && m_imguiVisible && (m_imgui.WantsKeyboard() || m_imgui.WantsTextInput());
  const bool guiToggled = HandleRuntimeGuiToggle("input");
  if (m_imguiVisible && m_actualScene) {
    IManager.xDelta = 0;
    IManager.yDelta = 0;
    m_actualScene->ResetViewInput();
  }
  m_devLayer.SetSceneInputBlocked(guiToggled || m_imguiVisible || imguiConsumesKeyboard);
  m_devLayer.ProcessInput(&IManager);
#else
  UpdateAndroidGuiHoldToggle();
  if (m_imguiVisible) {
    if (auto* dayScene = dynamic_cast<DayScene*>(m_actualScene)) {
      dayScene->ResetAndroidVirtualControls();
    }
    if (auto* sandboxScene = dynamic_cast<SandboxScene*>(m_actualScene)) {
      sandboxScene->ResetAndroidVirtualControls();
    }
  }
  if (m_imguiVisible && m_imgui.WantsMouse()) return;
  if (m_actualScene && !bPaused) m_actualScene->OnInput(&IManager);
#endif
}

void App::OnPause() {
#ifdef OS_ANDROID
  if (m_imguiReady) {
    m_imgui.SetAndroidNativeWindow(nullptr);
  }
#endif
}

void App::OnResume() {
#ifdef OS_ANDROID
  if (m_imguiReady && pFramework) {
    auto* androidFramework = static_cast<t850::AndroidFramework*>(pFramework);
    m_imgui.SetAndroidNativeWindow(androidFramework ? androidFramework->GetNativeWindow() : nullptr);
  }
#endif
}

void App::OnReset() {

}

bool App::IsModalActive() const {
#ifdef OS_ANDROID
  return false;
#else
  return m_imguiVisible && (m_imgui.WantsKeyboard() || m_imgui.WantsTextInput());
#endif
}

bool App::WantsRelativeMouseMode() const {
#ifdef OS_ANDROID
  return false;
#else
  return m_imguiReady && !m_imguiVisible && !IsModalActive();
#endif
}

void App::DrawRuntimeGui() {
  T8_TELEMETRY_SCOPE("ui.runtime_gui");
  if (!m_imguiReady) return;

#ifdef OS_ANDROID
  ImGui::GetIO().FontGlobalScale = m_androidGuiScale;
#endif
  if (!m_imgui.NewFrame(m_imguiVisible)) return;

  t850::DevGuiContext gui;
  gui.DrawFrameStatsOverlay(m_fpsString.c_str());

  if (m_imguiVisible) {
#ifdef OS_ANDROID
    const bool androidUndockThisFrame = m_androidGuiUndockRequested;
    const bool androidPhysicsPanel = m_androidGuiPanelMode == kAndroidGuiPanelPhysics;
    const ImGuiIO& io = ImGui::GetIO();
    constexpr float kPanelMargin = 24.0f;
    const float availableW = (std::max)(1.0f, io.DisplaySize.x - kPanelMargin * 2.0f);
    const float availableH = (std::max)(1.0f, io.DisplaySize.y - kPanelMargin * 2.0f);
    const float panelW = (std::min)((std::max)(420.0f, 620.0f * m_androidGuiScale), availableW);
    const float panelH = (std::min)((std::max)(560.0f, 760.0f * m_androidGuiScale), availableH);
    const ImVec2 panelPos(
        androidPhysicsPanel ? kPanelMargin : (std::max)(kPanelMargin, io.DisplaySize.x - panelW - kPanelMargin),
        kPanelMargin);
    const ImVec2 panelSize(panelW, panelH);
    if (androidUndockThisFrame) {
      ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
      ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
      ImGui::SetNextWindowSize(panelSize, ImGuiCond_Always);
    } else {
      ImGui::SetNextWindowPos(panelPos, ImGuiCond_FirstUseEver);
      ImGui::SetNextWindowSize(panelSize, ImGuiCond_FirstUseEver);
    }
    const char* panelTitle = androidPhysicsPanel ? "Physics##Android" : "Scene Controls##Android";
#else
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float panelW = (std::min)(420.0f, (std::max)(320.0f, viewport->WorkSize.x - 48.0f));
    const float panelH = (std::min)(680.0f, (std::max)(320.0f, viewport->WorkSize.y - 48.0f));
    const ImVec2 panelPos(
        (std::max)(viewport->WorkPos.x + 24.0f, viewport->WorkPos.x + viewport->WorkSize.x - panelW - 24.0f),
        viewport->WorkPos.y + 24.0f);
    ImGui::SetNextWindowDockID(0, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(panelPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_Appearing);
    const char* panelTitle = "Scene Controls";
#endif
    const bool panelBegun = gui.BeginPanel(panelTitle, &m_imguiVisible);
#ifdef OS_ANDROID
    if (androidUndockThisFrame) {
      m_androidGuiUndockRequested = false;
      T8_LOG_INFO("[App] Android ImGui panel undocked");
    }
#endif
    if (panelBegun) {
#ifdef OS_ANDROID
      if (ImGui::Button(bPaused ? "Resume Scene" : "Pause Scene")) {
        bPaused = !bPaused;
        T8_LOG_INFO("[App] Android GUI %s", bPaused ? "paused" : "resumed");
      }
      ImGui::SameLine();
      if (ImGui::Button("Close GUI")) {
        m_imguiVisible = false;
        SaveAndroidGuiSettings();
        T8_LOG_INFO("[App] Android close button closed ImGui overlay");
      }
      ImGui::SameLine();
      if (ImGui::Button("Undock")) {
        m_androidGuiUndockRequested = true;
      }
      ImGui::SameLine();
      ImGui::TextUnformatted("Left triple-tap: Physics | Right: Controls");
      float scale = m_androidGuiScale;
      if (ImGui::SliderFloat("GUI scale", &scale, 1.0f, 2.5f, "%.2f")) {
        m_androidGuiScale = (std::max)(1.0f, (std::min)(2.5f, scale));
        SaveAndroidGuiSettings();
      }
      ImGui::Separator();
#endif
      if (m_actualScene) {
        T8_TELEMETRY_SCOPE("ui.scene_dev_gui");
#ifdef OS_ANDROID
        if (androidPhysicsPanel) {
          DrawAndroidPhysicsGui(gui);
        } else
#endif
        {
          m_actualScene->DrawDevGui(gui);
        }
      }
#ifndef OS_ANDROID
      ImGui::Separator();
      ImGui::Checkbox("DEBUG Render Targets", &m_debugPanelVisible);
#endif
    }
    gui.EndPanel();

#ifndef OS_ANDROID
    if (m_debugPanelVisible || !m_debugOpenTargets.empty()) {
      T8_TELEMETRY_SCOPE("ui.debug_rt_panel");
      t850::BaseDriver* driver = pFramework ? pFramework->pVideoDriver : nullptr;
      std::vector<DebugRTEntry> debugRTs = BuildDebugRTEntries(driver, m_debugTextureDescriptors, m_debugOpaqueTextureDescriptors);
      PruneDebugTextureDescriptors(driver, debugRTs, m_debugTextureDescriptors, m_debugOpaqueTextureDescriptors);
      DrawDebugRenderTargetPanel(debugRTs, m_debugOpenTargets, &m_debugPanelVisible);
      DrawDebugPreviewWindows(debugRTs, m_debugOpenTargets);
    }
#endif
  }

#ifdef OS_ANDROID
  if (auto* dayScene = dynamic_cast<DayScene*>(m_actualScene)) {
    dayScene->DrawAndroidVirtualControls(m_imguiVisible);
  } else if (auto* sandboxScene = dynamic_cast<SandboxScene*>(m_actualScene)) {
    sandboxScene->DrawAndroidVirtualControls(m_imguiVisible);
  }

  m_imgui.BuildDrawData();
  if (auto* vkDriver = static_cast<t850::VulkanDriver*>(pFramework ? pFramework->pVideoDriver : nullptr)) {
    vkDriver->SetPrePresentOverlayCallback([this]() {
      m_imgui.RenderDrawData();
    });
  }
#else
  T8_TELEMETRY_SCOPE("ui.imgui_render");
  m_imgui.Render();
#endif
}

#ifdef OS_ANDROID
bool App::HandleAndroidInputEvent(AInputEvent* event) {
  if (event && AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
    const int32_t key = AKeyEvent_getKeyCode(event);
    const int32_t action = AKeyEvent_getAction(event);
    if (key == AKEYCODE_BACK) {
      if (!m_imguiReady || !m_imguiVisible) return false;
      if (action == AKEY_EVENT_ACTION_UP) {
        m_imguiVisible = false;
        SaveAndroidGuiSettings();
        T8_LOG_INFO("[App] Android back closed ImGui overlay");
      }
      return true;
    }
  }

  bool sceneHandled = false;
  bool sceneControlsActiveBefore = false;
  if (!m_imguiVisible && event && AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
    if (auto* dayScene = dynamic_cast<DayScene*>(m_actualScene)) {
      sceneControlsActiveBefore = dayScene->AndroidVirtualControlsActive();
      sceneHandled = dayScene->HandleAndroidVirtualControls(event);
    } else if (auto* sandboxScene = dynamic_cast<SandboxScene*>(m_actualScene)) {
      sceneControlsActiveBefore = sandboxScene->AndroidVirtualControlsActive();
      sceneHandled = sandboxScene->HandleAndroidVirtualControls(event);
    }
  }

  if ((!sceneHandled || !sceneControlsActiveBefore) && m_imguiReady && !m_imguiVisible && event &&
      AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
    const int32_t rawAction = AMotionEvent_getAction(event);
    const int32_t action = rawAction & AMOTION_EVENT_ACTION_MASK;
    if (action == AMOTION_EVENT_ACTION_UP && AMotionEvent_getPointerCount(event) > 0) {
      RegisterAndroidGuiTap(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
    }
  }
  const bool imguiHandled = m_imguiReady && m_imguiVisible && m_imgui.HandleAndroidInputEvent(event);
  return sceneHandled || imguiHandled;
}

void App::OnAndroidNativeWindowChanged(ANativeWindow* window) {
  if (m_imguiReady) {
    m_imgui.SetAndroidNativeWindow(window);
  }
}

void App::RegisterAndroidGuiTap(float x, float y) {
  constexpr float kTapWindowSeconds = 0.75f;
  constexpr int kRequiredTapCount = 3;

  if (!m_imguiReady || m_imguiVisible) return;

  const ImGuiIO& io = ImGui::GetIO();
  float displayWidth = io.DisplaySize.x;
  if (displayWidth <= 1.0f && pFramework && pFramework->pVideoDriver) {
    displayWidth = static_cast<float>(pFramework->pVideoDriver->width);
  }
  if (displayWidth <= 1.0f) {
    displayWidth = (std::max)(1.0f, x * 2.0f);
  }
  const int tapSide = x < displayWidth * 0.5f ? kAndroidTapSideLeft : kAndroidTapSideRight;

  const bool startsNewSequence = (m_androidGuiTapCount == 0 || m_androidGuiTapWindowSecs <= 0.0f);
  if (startsNewSequence) {
    m_androidGuiTapCount = 1;
    m_androidGuiTapSide = tapSide;
    m_androidGuiTapStartX = x;
    m_androidGuiTapStartY = y;
    m_androidGuiTapWindowSecs = kTapWindowSeconds;
    return;
  }

  if (tapSide != m_androidGuiTapSide) {
    m_androidGuiTapCount = 1;
    m_androidGuiTapSide = tapSide;
    m_androidGuiTapStartX = x;
    m_androidGuiTapStartY = y;
    m_androidGuiTapWindowSecs = kTapWindowSeconds;
    return;
  }

  ++m_androidGuiTapCount;
  m_androidGuiTapWindowSecs = kTapWindowSeconds;
  if (m_androidGuiTapCount >= kRequiredTapCount) {
    m_androidGuiPanelMode = tapSide == kAndroidTapSideLeft ? kAndroidGuiPanelPhysics : kAndroidGuiPanelControls;
    m_imguiVisible = true;
    m_androidGuiTapCount = 0;
    m_androidGuiTapSide = -1;
    m_androidGuiTapWindowSecs = 0.0f;
    m_androidGuiHoldSecs = 0.0f;
    m_androidGuiHoldActive = false;
    m_androidGuiHoldSuppressed = true;
    T8_LOG_INFO("[App] Android triple tap opened %s overlay",
                m_androidGuiPanelMode == kAndroidGuiPanelPhysics ? "physics" : "controls");
  }
}

void App::DrawAndroidPhysicsGui(t850::DevGuiContext& gui) {
  if (auto* dayScene = dynamic_cast<DayScene*>(m_actualScene)) {
    dayScene->DrawAndroidPhysicsPanel(gui);
    return;
  }
  if (auto* sandboxScene = dynamic_cast<SandboxScene*>(m_actualScene)) {
    sandboxScene->DrawAndroidPhysicsPanel(gui);
    return;
  }
  if (m_actualScene) {
    m_actualScene->DrawDevGui(gui);
  }
}

void App::LoadAndroidGuiSettings() {
  const std::filesystem::path path = t850::ResourceLocator::Instance().ResolveCachePath("android_gui_settings.txt");
  std::ifstream file(path);
  if (!file.is_open()) return;

  float scale = m_androidGuiScale;
  file >> scale;
  if (file && std::isfinite(scale)) {
    m_androidGuiScale = (std::max)(1.0f, (std::min)(2.5f, scale));
    T8_LOG_INFO("[App] Android GUI scale loaded: %.2f", m_androidGuiScale);
  }
}

void App::SaveAndroidGuiSettings() const {
  const std::filesystem::path path = t850::ResourceLocator::Instance().ResolveCachePath("android_gui_settings.txt");
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    T8_LOG_ERROR("[App] Failed to create Android GUI settings directory '%s': %s",
                 path.parent_path().string().c_str(), ec.message().c_str());
    return;
  }

  std::ofstream file(path, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    T8_LOG_ERROR("[App] Failed to save Android GUI settings '%s'", path.string().c_str());
    return;
  }
  file << m_androidGuiScale << '\n';
}

void App::UpdateAndroidGuiHoldToggle() {
  constexpr float kHoldSeconds = 3.0f;
  constexpr float kMoveTolerancePixels = 72.0f;

  if (m_androidGuiTapWindowSecs > 0.0f) {
    m_androidGuiTapWindowSecs -= DtSecs;
    if (m_androidGuiTapWindowSecs <= 0.0f) {
      m_androidGuiTapCount = 0;
      m_androidGuiTapWindowSecs = 0.0f;
      m_androidGuiTapSide = -1;
    }
  }

  if (auto* dayScene = dynamic_cast<DayScene*>(m_actualScene)) {
    if (dayScene->AndroidVirtualControlsActive()) {
      m_androidGuiHoldSecs = 0.0f;
      m_androidGuiHoldActive = false;
      m_androidGuiHoldSuppressed = true;
      return;
    }
  }
  if (auto* sandboxScene = dynamic_cast<SandboxScene*>(m_actualScene)) {
    if (sandboxScene->AndroidVirtualControlsActive()) {
      m_androidGuiHoldSecs = 0.0f;
      m_androidGuiHoldActive = false;
      m_androidGuiHoldSuppressed = true;
      return;
    }
  }

  if (!m_imguiReady || m_imguiVisible || !IManager.PressedMouseButton(0)) {
    m_androidGuiHoldSecs = 0.0f;
    m_androidGuiHoldActive = false;
    m_androidGuiHoldSuppressed = false;
    return;
  }

  const float x = static_cast<float>(IManager.mouseX);
  const float y = static_cast<float>(IManager.mouseY);
  if (!m_androidGuiHoldActive && !m_androidGuiHoldSuppressed) {
    m_androidGuiHoldActive = true;
    m_androidGuiHoldStartX = x;
    m_androidGuiHoldStartY = y;
    m_androidGuiHoldSecs = 0.0f;
  }

  if (!m_androidGuiHoldActive) return;

  const float dx = x - m_androidGuiHoldStartX;
  const float dy = y - m_androidGuiHoldStartY;
  if ((dx * dx + dy * dy) > (kMoveTolerancePixels * kMoveTolerancePixels)) {
    m_androidGuiHoldSecs = 0.0f;
    m_androidGuiHoldActive = false;
    m_androidGuiHoldSuppressed = true;
    return;
  }

  m_androidGuiHoldSecs += DtSecs;
  if (m_androidGuiHoldSecs >= kHoldSeconds) {
    m_androidGuiPanelMode = kAndroidGuiPanelControls;
    m_imguiVisible = true;
    m_androidGuiTapCount = 0;
    m_androidGuiTapSide = -1;
    m_androidGuiTapWindowSecs = 0.0f;
    m_androidGuiHoldSecs = 0.0f;
    m_androidGuiHoldActive = false;
    m_androidGuiHoldSuppressed = true;
    T8_LOG_INFO("[App] Android long press opened ImGui overlay");
  }
}
#endif
