#include <pch.h>

#include <imgui/ImGuiRendererBackend.h>

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <video/vulkan/VulkanDriver.h>
#include <video/vulkan/VulkanTexture.h>

#ifdef OS_ANDROID
#include <android/input.h>
#include <android/native_window.h>
#include <imgui_impl_android.h>
#else
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>
#endif

#include <cstring>
#include <unordered_map>

namespace t850 {
namespace {

class ImGuiVulkanBackend final : public ImGuiRendererBackend {
public:
  ~ImGuiVulkanBackend() override { Shutdown(); }

  bool Init(RootFramework* framework, void* nativeWindow) override {
    if (!framework || !framework->pVideoDriver ||
        framework->pVideoDriver->m_currentAPI != GraphicsApi::VULKAN ||
        !InitPlatform(nativeWindow)) return false;
    m_driver = static_cast<VulkanDriver*>(framework->pVideoDriver);

    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = m_driver->GetInstance();
    initInfo.PhysicalDevice = m_driver->GetPhysicalDevice();
    initInfo.Device = m_driver->GetDevice();
    initInfo.QueueFamily = m_driver->GetGraphicsQueueFamily();
    initInfo.Queue = m_driver->GetGraphicsQueue();
    initInfo.DescriptorPoolSize = 64;
    initInfo.MinImageCount = VulkanDriver::kBackBufferCount;
    initInfo.ImageCount = VulkanDriver::kBackBufferCount;
    initInfo.PipelineInfoMain.RenderPass = m_driver->GetBackbufferRenderPass();
    m_rendererInitialized = ImGui_ImplVulkan_Init(&initInfo);
    if (!m_rendererInitialized) Shutdown();
    return m_rendererInitialized;
  }

  void Shutdown() override {
    ReleaseTextureIDs();
    if (m_rendererInitialized) ImGui_ImplVulkan_Shutdown();
    ShutdownPlatform();
    m_driver = nullptr;
    m_rendererInitialized = false;
  }

  void NewFrame() override {
    ImGui_ImplVulkan_NewFrame();
#ifdef OS_ANDROID
    ImGui_ImplAndroid_NewFrame();
#else
    ImGui_ImplSDL3_NewFrame();
#endif
  }

  void RenderDrawData(ImDrawData* drawData) override {
    std::memset(m_driver->m_pendingTextures, 0, sizeof(m_driver->m_pendingTextures));
    m_driver->EnsureBackbufferRenderPass();
    VkCommandBuffer commandBuffer = m_driver->GetCurrentCommandBuffer();
    if (commandBuffer && drawData)
      ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
  }

  bool ShouldDeferPlatformWindowsUpdate() const override {
#ifndef OS_ANDROID
    ImGuiIO& io = ImGui::GetIO();
    if (!io.MouseDown[0]) return false;
    for (ImGuiViewport* viewport : ImGui::GetPlatformIO().Viewports) {
      if (viewport && viewport->PlatformRequestResize) return true;
    }
#endif
    return false;
  }

  ImTextureID GetTextureID(Texture* texture, ImGuiTextureMode) override {
    auto* vkTexture = static_cast<VulkanTexture*>(texture);
    if (!vkTexture->m_sampler || !vkTexture->m_imageView) return (ImTextureID)nullptr;
    auto found = m_textureIDs.find(texture);
    if (found != m_textureIDs.end()) return (ImTextureID)found->second;

    VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(
      vkTexture->m_sampler, vkTexture->m_imageView,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_textureIDs[texture] = descriptor;
    return (ImTextureID)descriptor;
  }

  void PruneTextureIDs(const std::unordered_set<Texture*>& liveTextures) override {
    for (auto iterator = m_textureIDs.begin(); iterator != m_textureIDs.end();) {
      if (!liveTextures.contains(iterator->first)) {
        ImGui_ImplVulkan_RemoveTexture(iterator->second);
        iterator = m_textureIDs.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }

  void ReleaseTextureIDs() override {
    for (const auto& entry : m_textureIDs)
      ImGui_ImplVulkan_RemoveTexture(entry.second);
    m_textureIDs.clear();
  }

  bool SetNativeWindow(void* nativeWindow) override {
#ifdef OS_ANDROID
    auto* window = static_cast<ANativeWindow*>(nativeWindow);
    if (m_platformInitialized && m_nativeWindow == window && window) return true;
    ShutdownPlatform();
    return InitPlatform(nativeWindow);
#else
    return nativeWindow != nullptr;
#endif
  }

  bool HandlePlatformInput(void* event) override {
#ifdef OS_ANDROID
    auto* inputEvent = static_cast<AInputEvent*>(event);
    if (!m_platformInitialized || !inputEvent) return false;
    const bool handled = ImGui_ImplAndroid_HandleInputEvent(inputEvent) != 0;
    if (AInputEvent_getType(inputEvent) != AINPUT_EVENT_TYPE_MOTION) return handled;

    const int32_t rawAction = AMotionEvent_getAction(inputEvent);
    const int32_t action = rawAction & AMOTION_EVENT_ACTION_MASK;
    int32_t pointerIndex =
      (rawAction & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
      AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    const size_t pointerCount = AMotionEvent_getPointerCount(inputEvent);
    if (pointerCount == 0) return handled;
    if (pointerIndex < 0 || pointerIndex >= static_cast<int32_t>(pointerCount)) pointerIndex = 0;
    const int32_t toolType = AMotionEvent_getToolType(inputEvent, pointerIndex);
    if (toolType != AMOTION_EVENT_TOOL_TYPE_STYLUS &&
        toolType != AMOTION_EVENT_TOOL_TYPE_ERASER) return handled;

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    switch (action) {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
      io.AddMousePosEvent(AMotionEvent_getX(inputEvent, pointerIndex),
                          AMotionEvent_getY(inputEvent, pointerIndex));
      io.AddMouseButtonEvent(0, true);
      return true;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP:
    case AMOTION_EVENT_ACTION_CANCEL:
      io.AddMousePosEvent(AMotionEvent_getX(inputEvent, pointerIndex),
                          AMotionEvent_getY(inputEvent, pointerIndex));
      io.AddMouseButtonEvent(0, false);
      return true;
    case AMOTION_EVENT_ACTION_MOVE:
      io.AddMousePosEvent(AMotionEvent_getX(inputEvent, pointerIndex),
                          AMotionEvent_getY(inputEvent, pointerIndex));
      return true;
    default:
      return handled;
    }
#else
    (void)event;
    return false;
#endif
  }

private:
  bool InitPlatform(void* nativeWindow) {
#ifdef OS_ANDROID
    auto* window = static_cast<ANativeWindow*>(nativeWindow);
    if (!window || !ImGui_ImplAndroid_Init(window)) return false;
    m_nativeWindow = window;
#else
    auto* window = static_cast<SDL_Window*>(nativeWindow);
    if (!window || !ImGui_ImplSDL3_InitForVulkan(window)) return false;
#endif
    m_platformInitialized = true;
    return true;
  }

  void ShutdownPlatform() {
    if (!m_platformInitialized) return;
#ifdef OS_ANDROID
    ImGui_ImplAndroid_Shutdown();
    m_nativeWindow = nullptr;
#else
    ImGui_ImplSDL3_Shutdown();
#endif
    m_platformInitialized = false;
  }

  VulkanDriver* m_driver = nullptr;
  bool m_platformInitialized = false;
  bool m_rendererInitialized = false;
  std::unordered_map<Texture*, VkDescriptorSet> m_textureIDs;
#ifdef OS_ANDROID
  ANativeWindow* m_nativeWindow = nullptr;
#endif
};

} // namespace

std::unique_ptr<ImGuiRendererBackend> CreateImGuiVulkanBackend() {
  return std::make_unique<ImGuiVulkanBackend>();
}

} // namespace t850
