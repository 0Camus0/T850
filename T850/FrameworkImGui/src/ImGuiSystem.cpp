#include <imgui/ImGuiSystem.h>

#include <Config.h>
#include <Descriptors.h>
#include <core/Core.h>
#include <utils/Log.h>
#include <video/BaseDriver.h>

#include <imgui.h>
#ifndef OS_ANDROID
#include <imgui_impl_opengl3.h>
#endif
#include <imgui_impl_vulkan.h>

#ifdef OS_WINDOWS
#  include <core/windows/Win32Framework.h>
#  include <d3d11.h>
#  include <imgui_impl_dx11.h>
#  include <imgui_impl_sdl3.h>
#  include <SDL3/SDL.h>
#  include <video/d3d11/D3D11Texture.h>
#  if __has_include(<imgui_impl_dx12.h>)
#    define T850_IMGUI_HAS_DX12 1
#    include <imgui_impl_dx12.h>
#    include <video/d3d12/D3D12Driver.h>
#  else
#    define T850_IMGUI_HAS_DX12 0
#  endif
#endif
#ifdef OS_ANDROID
#  include <android/input.h>
#  include <android/native_window.h>
#  include <core/android/AndroidFramework.h>
#  include <imgui_impl_android.h>
#endif

#include <video/vulkan/VulkanDriver.h>

#include <cstring>

namespace t850 {
  extern Device* T8Device;
  extern DeviceContext* T8DeviceContext;
}

namespace {
#ifndef OS_ANDROID
  static bool sdlEventWatcher(void* userdata, SDL_Event* event) {
    ImGui_ImplSDL3_ProcessEvent(event);
    auto* system = static_cast<t850::ImGuiSystem*>(userdata);
    if (system && event->type == SDL_EVENT_MOUSE_WHEEL) {
      system->AddWheelDelta(event->wheel.y);
    }
    return true;
  }
#endif
}

namespace t850 {

ImGuiSystem::~ImGuiSystem() {
  Shutdown();
}

bool ImGuiSystem::Init(RootFramework* framework, const char* iniFileName, bool enableDocking) {
  if (m_inited) return true;
  if (!framework || !framework->pVideoDriver) return false;

#ifdef OS_ANDROID
  auto* androidFramework = static_cast<AndroidFramework*>(framework);
  m_androidWindow = androidFramework ? androidFramework->GetNativeWindow() : nullptr;
  m_sdlWindow = nullptr;
#elif defined(OS_WINDOWS)
  auto* w32 = static_cast<Win32Framework*>(framework);
  m_sdlWindow = w32 ? w32->m_pWindow : nullptr;
#else
  m_sdlWindow = nullptr;
#endif

#ifdef OS_ANDROID
  if (!m_androidWindow) {
    T8_LOG_ERROR("[ImGuiSystem] Init failed: no Android native window");
    return false;
  }
#else
  if (!m_sdlWindow) {
    T8_LOG_ERROR("[ImGuiSystem] Init failed: no SDL window");
    return false;
  }
#endif

  m_api = framework->pVideoDriver->m_currentAPI;
  m_dockingEnabled = enableDocking;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  if (m_dockingEnabled) {
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  }
  io.IniFilename = iniFileName;

  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 4.0f;
  style.FrameRounding = 2.0f;
  style.GrabRounding = 2.0f;
  style.ScrollbarRounding = 3.0f;

  bool platformOK = false;
#ifdef OS_ANDROID
  platformOK = ImGui_ImplAndroid_Init(m_androidWindow);
#elif defined(OS_WINDOWS)
  if (m_api == GraphicsApi::OPENGL) {
    platformOK = ImGui_ImplSDL3_InitForOpenGL(m_sdlWindow, nullptr);
  } else if (m_api == GraphicsApi::VULKAN) {
    platformOK = ImGui_ImplSDL3_InitForVulkan(m_sdlWindow);
  } else {
    platformOK = ImGui_ImplSDL3_InitForD3D(m_sdlWindow);
  }
#else
  platformOK = ImGui_ImplSDL3_InitForOpenGL(m_sdlWindow, nullptr);
#endif

  if (!platformOK) {
    T8_LOG_ERROR("[ImGuiSystem] Platform backend init failed");
    ImGui::DestroyContext();
    return false;
  }

  bool rendererOK = false;

#ifdef OS_WINDOWS
  if (m_api == GraphicsApi::D3D11) {
    ID3D11Device* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    ID3D11DeviceContext* ctx = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    rendererOK = ImGui_ImplDX11_Init(device, ctx);
  }
#if T850_IMGUI_HAS_DX12
  else if (m_api == GraphicsApi::D3D12) {
    auto* d3d12Driver = static_cast<D3D12Driver*>(framework->pVideoDriver);
    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
    auto& srvHeap = d3d12Driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE);
    m_d3d12SrvHeap = srvHeap.GetHeap();
    D3D12_CPU_DESCRIPTOR_HANDLE fontCpu = srvHeap.AllocateCPU();
    D3D12_GPU_DESCRIPTOR_HANDLE fontGpu = srvHeap.AllocateGPU();

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = device;
    initInfo.CommandQueue = d3d12Driver->GetCmdQueue();
    initInfo.NumFramesInFlight = D3D12Driver::kBackBufferCount;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    initInfo.SrvDescriptorHeap = m_d3d12SrvHeap;
    initInfo.LegacySingleSrvCpuDescriptor = fontCpu;
    initInfo.LegacySingleSrvGpuDescriptor = fontGpu;
    rendererOK = ImGui_ImplDX12_Init(&initInfo);
  }
#endif
#endif

#ifndef OS_ANDROID
  if (m_api == GraphicsApi::OPENGL) {
    rendererOK = ImGui_ImplOpenGL3_Init("#version 300 es");
  }
#endif

  if (m_api == GraphicsApi::VULKAN) {
    auto* vkDriver = static_cast<VulkanDriver*>(framework->pVideoDriver);
    ImGui_ImplVulkan_InitInfo vkInit = {};
    vkInit.ApiVersion = VK_API_VERSION_1_0;
    vkInit.Instance = vkDriver->GetInstance();
    vkInit.PhysicalDevice = vkDriver->GetPhysicalDevice();
    vkInit.Device = vkDriver->GetDevice();
    vkInit.QueueFamily = vkDriver->GetGraphicsQueueFamily();
    vkInit.Queue = vkDriver->GetGraphicsQueue();
    vkInit.DescriptorPoolSize = 64;
    vkInit.MinImageCount = VulkanDriver::kBackBufferCount;
    vkInit.ImageCount = VulkanDriver::kBackBufferCount;
    vkInit.PipelineInfoMain.RenderPass = vkDriver->GetBackbufferRenderPass();
    rendererOK = ImGui_ImplVulkan_Init(&vkInit);
  }

  if (!rendererOK) {
    T8_LOG_ERROR("[ImGuiSystem] Renderer backend init failed (api=%d)", (int)m_api);
#ifdef OS_WINDOWS
#if T850_IMGUI_HAS_DX12
    if (m_d3d12SrvHeap) {
      m_d3d12SrvHeap = nullptr;
    }
#endif
#endif
#ifdef OS_ANDROID
    ImGui_ImplAndroid_Shutdown();
#else
    ImGui_ImplSDL3_Shutdown();
#endif
    ImGui::DestroyContext();
    m_sdlWindow = nullptr;
#ifdef OS_ANDROID
    m_androidWindow = nullptr;
#endif
    return false;
  }

#ifndef OS_ANDROID
  SDL_AddEventWatch(sdlEventWatcher, this);
#endif
  m_wheelAccum = 0.0f;
  m_inited = true;
  T8_LOG_INFO("[ImGuiSystem] Initialized (api=%d)", (int)m_api);
  return true;
}

void ImGuiSystem::Shutdown() {
  if (!m_inited) return;

#ifdef OS_WINDOWS
  if (m_api == GraphicsApi::D3D11) {
    ImGui_ImplDX11_Shutdown();
  }
#if T850_IMGUI_HAS_DX12
  else if (m_api == GraphicsApi::D3D12) {
    ImGui_ImplDX12_Shutdown();
    m_d3d12SrvHeap = nullptr;
  }
#endif
#endif

#ifndef OS_ANDROID
  if (m_api == GraphicsApi::OPENGL) {
    ImGui_ImplOpenGL3_Shutdown();
  }
#endif
  if (m_api == GraphicsApi::VULKAN) {
    ImGui_ImplVulkan_Shutdown();
  }

#ifdef OS_ANDROID
  ImGui_ImplAndroid_Shutdown();
#else
  ImGui_ImplSDL3_Shutdown();
  SDL_RemoveEventWatch(sdlEventWatcher, this);
#endif
  ImGui::DestroyContext();

  m_sdlWindow = nullptr;
#ifdef OS_ANDROID
  m_androidWindow = nullptr;
#endif
  m_wheelAccum = 0.0f;
  m_inited = false;
  T8_LOG_INFO("[ImGuiSystem] Shutdown complete");
}

void ImGuiSystem::NewFrame(bool createDockspace) {
  if (!m_inited) return;

#ifdef OS_WINDOWS
  if (m_api == GraphicsApi::D3D11) {
    ImGui_ImplDX11_NewFrame();
  }
#if T850_IMGUI_HAS_DX12
  else if (m_api == GraphicsApi::D3D12) {
    ImGui_ImplDX12_NewFrame();
  }
#endif
#endif

#ifndef OS_ANDROID
  if (m_api == GraphicsApi::OPENGL) {
    ImGui_ImplOpenGL3_NewFrame();
  }
#endif
  if (m_api == GraphicsApi::VULKAN) {
    ImGui_ImplVulkan_NewFrame();
  }

#ifdef OS_ANDROID
  ImGui_ImplAndroid_NewFrame();
#else
  ImGui_ImplSDL3_NewFrame();
#endif
  ImGui::NewFrame();

  if (m_dockingEnabled && createDockspace) {
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
  }
}

void ImGuiSystem::Render() {
  if (!m_inited) return;

  BuildDrawData();
  RenderDrawData();
}

void ImGuiSystem::BuildDrawData() {
  if (!m_inited) return;

  ImGui::Render();
}

void ImGuiSystem::RenderDrawData() {
  if (!m_inited) return;

  ImDrawData* drawData = ImGui::GetDrawData();

#ifdef OS_WINDOWS
  if (m_api == GraphicsApi::D3D11) {
    ImGui_ImplDX11_RenderDrawData(drawData);
  }
#if T850_IMGUI_HAS_DX12
  else if (m_api == GraphicsApi::D3D12) {
    auto* d3d12Driver = static_cast<D3D12Driver*>(g_pBaseDriver);
    ID3D12GraphicsCommandList* cmdList = d3d12Driver->GetCmdList();
    ID3D12DescriptorHeap* heaps[] = { m_d3d12SrvHeap };
    cmdList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(drawData, cmdList);
  }
#endif
#endif

#ifndef OS_ANDROID
  if (m_api == GraphicsApi::OPENGL) {
    ImGui_ImplOpenGL3_RenderDrawData(drawData);
  }
#endif
  if (m_api == GraphicsApi::VULKAN) {
    auto* vkDriver = static_cast<VulkanDriver*>(g_pBaseDriver);
    std::memset(vkDriver->m_pendingTextures, 0, sizeof(vkDriver->m_pendingTextures));
    vkDriver->EnsureBackbufferRenderPass();
    VkCommandBuffer cmd = static_cast<VulkanDeviceContext*>(T8DeviceContext)->GetCommandBuffer();
    if (cmd && drawData) {
      ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
    } else {
      T8_LOG_ERROR("[ImGuiSystem] Vulkan render skipped: cmd=%p drawData=%p", (void*)cmd, (void*)drawData);
    }
  }
}

bool ImGuiSystem::WantsKeyboard() const {
  if (!m_inited) return false;
  return ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiSystem::WantsMouse() const {
  if (!m_inited) return false;
  return ImGui::GetIO().WantCaptureMouse;
}

float ImGuiSystem::ConsumeWheelDelta() {
  float delta = m_wheelAccum;
  m_wheelAccum = 0.0f;
  return delta;
}

#ifdef OS_ANDROID
bool ImGuiSystem::HandleAndroidInputEvent(AInputEvent* event) {
  if (!m_inited || !event) return false;
  return ImGui_ImplAndroid_HandleInputEvent(event) != 0;
}
#endif

} // namespace t850
