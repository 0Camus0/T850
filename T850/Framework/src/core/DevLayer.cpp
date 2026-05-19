#include <pch.h>
#include <core/DevLayer.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <string>
#include <utils/Log.h>
#include <utils/Utils.h>

namespace t850 {

extern Device* T8Device;
extern DeviceContext* T8DeviceContext;

namespace {
  constexpr unsigned short kCullingFrustumLineIndices[] = {
    0, 1, 1, 3, 3, 2, 2, 0,
    4, 5, 5, 7, 7, 6, 6, 4,
    0, 4, 1, 5, 2, 6, 3, 7
  };
}

DevLayer::DevLayer()
    : m_framework(nullptr)
    , m_activeScene(nullptr)
    , m_guiInited(false) {
}

void DevLayer::Init(RootFramework* framework) {
  m_framework = framework;
}

void DevLayer::Destroy() {
  DestroyCullingDebugResources();
  if (m_guiInited) {
    m_gui.Destroy();
    m_guiInited = false;
  }
}

bool DevLayer::EnsureCullingDebugResources() {
  if (!g_pBaseDriver || !T8Device) return false;
  if (m_cullingDebugDriver != g_pBaseDriver) {
    DestroyCullingDebugResources();
    m_cullingDebugDriver = g_pBaseDriver;
  }
  if (m_cullingDebugShader && m_cullingDebugVB && m_cullingDebugIB && m_cullingDebugCB) return true;

  char* vsSource = nullptr;
  char* fsSource = nullptr;
  if (g_pBaseDriver->UsesGLSL()) {
    vsSource = file2string("Shaders/VS_W.glsl");
    fsSource = file2string("Shaders/FS_W.glsl");
  } else {
    vsSource = file2string("Shaders/VS_W.hlsl");
    fsSource = file2string("Shaders/FS_W.hlsl");
  }

  if (!vsSource || !fsSource) {
    T8_LOG_ERROR("[DevLayer] Failed to load culling debug shaders");
    if (vsSource) free(vsSource);
    if (fsSource) free(fsSource);
    return false;
  }

  std::string vertexSource(vsSource);
  std::string fragmentSource(fsSource);
  free(vsSource);
  free(fsSource);

  if (g_pBaseDriver->UsesGLSL()) {
#if defined(USING_OPENGL)
    std::string defines;
    defines += "#version 130\n\n";
    defines += "#define lowp \n\n";
    defines += "#define mediump \n\n";
    defines += "#define highp \n\n";
    vertexSource = defines + vertexSource;
    fragmentSource = defines + fragmentSource;
#elif defined(USING_GL_COMMON)
    std::string defines;
    defines += "#version 300 es\n\n";
    defines += "#define ES_30\n\n";
    vertexSource = defines + vertexSource;
    fragmentSource = defines + fragmentSource;
#endif
    if (g_pBaseDriver->m_currentAPI == GraphicsApi::VULKAN) {
      std::string defines;
      defines += "#version 450\n\n";
      defines += "#define ES_30\n\n";
      vertexSource = defines + vertexSource;
      fragmentSource = defines + fragmentSource;
    }
  }

  int shaderID = g_pBaseDriver->CreateShader(vertexSource, fragmentSource);
  m_cullingDebugShader = g_pBaseDriver->GetShaderIdx(shaderID);
  if (!m_cullingDebugShader) {
    T8_LOG_ERROR("[DevLayer] Failed to create culling debug shader");
    return false;
  }

  CullingDebugVert initialVerts[8] = {};
  BufferDesc bdesc;
  bdesc.byteWidth = sizeof(m_cullingDebugCBuffer);
  bdesc.usage = BufferUsage::DEFAULT;
  m_cullingDebugCB = (ConstantBuffer*)T8Device->CreateBuffer(BufferType::CONSTANT, bdesc);

  bdesc.byteWidth = sizeof(initialVerts);
  bdesc.usage = BufferUsage::DINAMIC;
  m_cullingDebugVB = (VertexBuffer*)T8Device->CreateBuffer(BufferType::VERTEX, bdesc, initialVerts);

  bdesc.byteWidth = sizeof(kCullingFrustumLineIndices);
  bdesc.usage = BufferUsage::DEFAULT;
  m_cullingDebugIB = (IndexBuffer*)T8Device->CreateBuffer(BufferType::INDEX, bdesc, (void*)kCullingFrustumLineIndices);

  if (!m_cullingDebugCB || !m_cullingDebugVB || !m_cullingDebugIB) {
    T8_LOG_ERROR("[DevLayer] Failed to create culling debug buffers");
    DestroyCullingDebugResources();
    return false;
  }

  m_cullingDebugCameraSphere.Create(8, 16);
  return true;
}

void DevLayer::DestroyCullingDebugResources() {
  if (m_cullingDebugVB) { m_cullingDebugVB->release(); m_cullingDebugVB = nullptr; }
  if (m_cullingDebugIB) { m_cullingDebugIB->release(); m_cullingDebugIB = nullptr; }
  if (m_cullingDebugCB) { m_cullingDebugCB->release(); m_cullingDebugCB = nullptr; }
  m_cullingDebugCameraSphere.Destroy();
  m_cullingDebugShader = nullptr;
  m_cullingDebugDriver = nullptr;
}

void DevLayer::BuildCullingFrustumVertices(const Camera& camera, CullingDebugVert* outVerts) const {
  if (!outVerts) return;

  XVECTOR3 forward = camera.Look;
  if (!camera.LeftHanded) forward *= -1.0f;

  const float nearDist = camera.NPlane;
  const float farDist = camera.FPlane;
  const XVECTOR3 nearCenter = camera.Eye + forward * nearDist;
  const XVECTOR3 farCenter = camera.Eye + forward * farDist;

  float nearHalfWidth = 0.0f;
  float nearHalfHeight = 0.0f;
  float farHalfWidth = 0.0f;
  float farHalfHeight = 0.0f;
  if (camera.Ortho) {
    nearHalfWidth = farHalfWidth = camera.Width * 0.5f;
    nearHalfHeight = farHalfHeight = camera.Height * 0.5f;
  } else {
    const float tanHalfFov = std::tan(camera.Fov * 0.5f);
    nearHalfHeight = tanHalfFov * nearDist;
    nearHalfWidth = nearHalfHeight * camera.AspectRatio;
    farHalfHeight = tanHalfFov * farDist;
    farHalfWidth = farHalfHeight * camera.AspectRatio;
  }

  const XVECTOR3 nearLeft = camera.Right * -nearHalfWidth;
  const XVECTOR3 nearRight = camera.Right * nearHalfWidth;
  const XVECTOR3 nearUp = camera.Up * nearHalfHeight;
  const XVECTOR3 nearDown = camera.Up * -nearHalfHeight;
  const XVECTOR3 farLeft = camera.Right * -farHalfWidth;
  const XVECTOR3 farRight = camera.Right * farHalfWidth;
  const XVECTOR3 farUp = camera.Up * farHalfHeight;
  const XVECTOR3 farDown = camera.Up * -farHalfHeight;

  XVECTOR3 corners[8] = {
    nearCenter + nearLeft + nearUp,
    nearCenter + nearRight + nearUp,
    nearCenter + nearLeft + nearDown,
    nearCenter + nearRight + nearDown,
    farCenter + farLeft + farUp,
    farCenter + farRight + farUp,
    farCenter + farLeft + farDown,
    farCenter + farRight + farDown
  };

  for (int i = 0; i < 8; ++i) {
    outVerts[i] = { corners[i].x, corners[i].y, corners[i].z, 1.0f };
  }
}

void DevLayer::DrawCullingDebug(const SceneProps& props) {
  if (!props.ShowCullingDebug || props.pCameras.empty() || !props.pCameras[0] || !T8DeviceContext) return;
  if (!EnsureCullingDebugResources()) return;

  Camera* viewCamera = props.pCameras[0];
  Camera* cullingCamera = props.pCullingCamera ? props.pCullingCamera : viewCamera;

  CullingDebugVert verts[8];
  BuildCullingFrustumVertices(*cullingCamera, verts);
  m_cullingDebugVB->UpdateFromBuffer(*T8DeviceContext, verts);

  g_pBaseDriver->SetDepthStencilState(BaseDriver::NONE);
  g_pBaseDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);

  m_cullingDebugCBuffer.WVP = viewCamera->VP;
  m_cullingDebugIB->Set(*T8DeviceContext, 0, IndexBufferFormat::R16);
  m_cullingDebugVB->Set(*T8DeviceContext, sizeof(CullingDebugVert), 0);
  T8DeviceContext->SetPrimitiveTopology(Topology::LINE_LIST);
  m_cullingDebugShader->Set(*T8DeviceContext);
  m_cullingDebugCB->UpdateFromBuffer(*T8DeviceContext, &m_cullingDebugCBuffer.WVP[0]);
  m_cullingDebugCB->Set(*T8DeviceContext);
  T8DeviceContext->DrawIndexed((unsigned)(sizeof(kCullingFrustumLineIndices) / sizeof(kCullingFrustumLineIndices[0])), 0, 0);

  const float sphereRadius = std::max(1.0f, cullingCamera->Ortho ? std::min(cullingCamera->Width, cullingCamera->Height) * 0.025f : cullingCamera->NPlane * 0.75f);
  T8DeviceContext->SetPrimitiveTopology(Topology::LINE_LIST);
  m_cullingDebugCameraSphere.Draw(viewCamera->VP, cullingCamera->Eye, sphereRadius);
  T8DeviceContext->SetPrimitiveTopology(Topology::TRIANLE_LIST);

  g_pBaseDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
  g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_DEFAULT);
}

void DevLayer::SetActiveScene(SceneBase* scene) {
  m_activeScene = scene;
}

SceneBase* DevLayer::GetActiveScene() const {
  return m_activeScene;
}

void DevLayer::RebuildGUIForScene() {
  if (!m_legacyGuiEnabled) return;

  // Initialise the GUI system once (textures, shader, font).
  if (!m_guiInited && g_pBaseDriver) {
    m_gui.Init(g_pBaseDriver->width, g_pBaseDriver->height);
    m_gui.AddFPSLabel();
    m_guiInited = true;
  }

  // Clear old sliders and let the new scene populate them.
  m_gui.ClearSliders();
  m_gui.AddFPSLabel();
  if (m_activeScene) {
    m_activeScene->PopulateGUI(m_gui);
    m_gui.LayoutSliders(g_pBaseDriver->width, g_pBaseDriver->height);
    m_activeScene->SyncToGUI(m_gui);
  }

  // Try to load a saved layout (overrides default positions)
  m_gui.LoadLayout(kLayoutPath);
  m_gui.LoadControlLayout(kControlLayoutPath);
}

void DevLayer::SetEditMode(bool e) { m_gui.SetEditMode(e); }
void DevLayer::SetSnapToGrid(bool s) { m_gui.SetSnapToGrid(s); }
void DevLayer::SetControlEditMode(bool e) { m_gui.SetControlEditMode(e); }
bool DevLayer::SetControlEditTargetByName(const std::string& targetName) { return m_gui.SetControlEditTargetByName(targetName); }

void DevLayer::SetLegacyGuiEnabled(bool enabled) {
  m_legacyGuiEnabled = enabled;
  if (!m_legacyGuiEnabled && m_guiInited) {
    m_gui.Destroy();
    m_guiInited = false;
  }
}

bool DevLayer::IsLegacyPopupActive() const {
  return m_legacyGuiEnabled && m_gui.IsPopupActive();
}

void DevLayer::Update(float dt) {
  if (m_activeScene) {
    if (!m_paused) {
      m_activeScene->OnUpdate(dt);
    }
    // Push changed slider values into scene props each frame (even when paused)
    if (m_legacyGuiEnabled && m_gui.IsVisible()) {
      m_activeScene->SyncFromGUI(m_gui);
      m_activeScene->SyncToGUI(m_gui);
    }
  }
}

void DevLayer::Draw() {
  if (m_activeScene) {
    T8_LOG_TRACE("[DevLayer] Scene OnDraw");
    m_activeScene->OnDraw();
    DrawCullingDebug(m_activeScene->SceneProp);
  }
  // GUI draws on top of the scene
  if (m_legacyGuiEnabled) {
    T8_LOG_TRACE("[DevLayer] GUI Draw (visible=%d)", (int)m_gui.IsVisible());
    m_gui.Draw();
  }
}

void DevLayer::ProcessInput(InputManager* input) {
  // Let the GUI (including a modal popup) consume input first.
  if (m_legacyGuiEnabled && m_guiInited) {
    m_gui.UpdateButtons(*input);
    m_gui.Update(*input, g_pBaseDriver->width, g_pBaseDriver->height);
  }
  // When a line-edit popup is active, all other keyboard/mouse handling is suppressed.
  if (m_legacyGuiEnabled && m_gui.IsPopupActive()) {
    return;
  }
  if (m_blockSceneInput) {
    return;
  }
  if (m_legacyGuiEnabled) {
    // Toggle GUI with G key
    if (input->PressedOnceKey(T800K_g)) {
      m_gui.ToggleVisible();
    }
    // F1: toggle group edit mode
    if (input->PressedOnceKey(T800K_F1) && m_gui.IsVisible()) {
      if (m_gui.IsGroupEditMode()) {
        m_gui.ExitGroupEditMode();
      } else {
        m_gui.EnterGroupEditMode();
      }
    }
    // F2 in group edit mode: delete all custom groups
    if (m_gui.IsGroupEditMode() && input->PressedOnceKey(T800K_F2)) {
      m_gui.DeleteAllCustomGroups();
      m_gui.ExitGroupEditMode();
    }
    // Enter in group edit mode: open group name popup
    if (m_gui.IsGroupEditMode() && input->PressedOnceKey(T800K_RETURN)) {
      m_gui.OpenGroupNamePopup();
    }
    // Tab: save layout when in edit mode or when user has made label edits via the popup in
    // the regular flow, otherwise dump scene state.
    if (input->PressedOnceKey(T800K_TAB)) {
      if (m_gui.IsControlEditMode()) {
        m_gui.HandleControlEditTab(kControlLayoutPath);
      } else if (m_gui.IsEditMode()) {
        m_gui.SaveLayout(kLayoutPath);
      } else if (m_gui.IsVisible() && m_gui.IsLayoutDirty()) {
        m_gui.SaveLayout(kLayoutPath);
      } else if (m_activeScene) {
        m_activeScene->SaveSceneState();
      }
    }
    // +/- controls:
    // - regular edit mode: grid size
    // - control edit mode: preview visual scale only (not saved)
    if (m_gui.IsControlEditMode()) {
      if (input->PressedOnceKey(T800K_PLUS) || input->PressedOnceKey(T800K_KP_PLUS)) {
        m_gui.AdjustControlPreviewScale(0.05f);
        input->KeyStates[0][T800K_PLUS]    = false;
        input->KeyStates[0][T800K_KP_PLUS] = false;
      }
      if (input->PressedOnceKey(T800K_MINUS) || input->PressedOnceKey(T800K_KP_MINUS)) {
        m_gui.AdjustControlPreviewScale(-0.05f);
        input->KeyStates[0][T800K_MINUS]    = false;
        input->KeyStates[0][T800K_KP_MINUS] = false;
      }
    } else if (m_gui.IsEditMode()) {
      if (input->PressedOnceKey(T800K_PLUS) || input->PressedOnceKey(T800K_KP_PLUS)) {
        m_gui.GrowGrid(5.0f);
        input->KeyStates[0][T800K_PLUS]    = false;
        input->KeyStates[0][T800K_KP_PLUS] = false;
      }
      if (input->PressedOnceKey(T800K_MINUS) || input->PressedOnceKey(T800K_KP_MINUS)) {
        m_gui.GrowGrid(-5.0f);
        input->KeyStates[0][T800K_MINUS]    = false;
        input->KeyStates[0][T800K_KP_MINUS] = false;
      }
      // Enter: apply last-edited element's scale to all elements of the same kind
      if (input->PressedOnceKey(T800K_RETURN)) {
        m_gui.ApplyUniformScale();
      }
      // Left/Right arrows: switch group while in edit mode
      if (input->PressedOnceKey(T800K_LEFT)) {
        m_gui.SwitchToPrevGroup();
      }
      if (input->PressedOnceKey(T800K_RIGHT)) {
        m_gui.SwitchToNextGroup();
      }
    }
  } else if (input->PressedOnceKey(T800K_TAB) && m_activeScene) {
    m_activeScene->SaveSceneState();
    }

  // Pause toggle
  if (input->PressedOnceKey(T800K_p)) {
    m_paused = !m_paused;
    T8_LOG_INFO("[DevLayer] %s", m_paused ? "PAUSED" : "RESUMED");
  }

  // F10 dump works even when paused. Space is reserved for gameplay/camera jump.
  if (input->PressedOnceKey(T800K_F10)) {
    if (m_activeScene) m_activeScene->RequestDump();
  }

  // Forward input to the active scene (skip when paused so mouse/keys don't move cameras)
  if (m_activeScene && !m_paused && !m_blockSceneInput) {
    m_activeScene->OnInput(input);
  }
  // GUI update already ran at the top of ProcessInput (so popups can pre-empt input).
}

void DevLayer::LoadScene(SceneBase* scene) {
  if (m_activeScene) {
    m_activeScene->OnDestoryScene();
  }
  m_activeScene = scene;
  if (m_activeScene) {
    m_activeScene->OnLoadScene();
  }
  RebuildGUIForScene();
}

void DevLayer::UnloadScene() {
  if (m_activeScene) {
    m_activeScene->OnDestoryScene();
    m_activeScene = nullptr;
  }
  m_gui.ClearSliders();
}

} // namespace t850
