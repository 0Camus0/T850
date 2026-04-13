#include <core/DevLayer.h>
#include <cstdio>

namespace t800 {

DevLayer::DevLayer()
    : m_framework(nullptr)
    , m_activeScene(nullptr)
    , m_guiInited(false) {
}

void DevLayer::Init(RootFramework* framework) {
  m_framework = framework;
}

void DevLayer::Destroy() {
  if (m_guiInited) {
    m_gui.Destroy();
    m_guiInited = false;
  }
}

void DevLayer::SetActiveScene(SceneBase* scene) {
  m_activeScene = scene;
}

SceneBase* DevLayer::GetActiveScene() const {
  return m_activeScene;
}

void DevLayer::RebuildGUIForScene() {
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
}

void DevLayer::SetEditMode(bool e) { m_gui.SetEditMode(e); }
void DevLayer::SetSnapToGrid(bool s) { m_gui.SetSnapToGrid(s); }

void DevLayer::Update(float dt) {
  if (m_activeScene) {
    if (!m_paused) {
      m_activeScene->OnUpdate(dt);
    }
    // Push changed slider values into scene props each frame (even when paused)
    if (m_gui.IsVisible()) {
      m_activeScene->SyncFromGUI(m_gui);
    }
  }
}

void DevLayer::Draw() {
  if (m_activeScene) {
    m_activeScene->OnDraw();
  }
  // GUI draws on top of the scene
  m_gui.Draw();
}

void DevLayer::ProcessInput(InputManager* input) {
  // Toggle GUI with G key
  if (input->PressedOnceKey(T800K_g)) {
    m_gui.ToggleVisible();
  }
  // Tab: save layout when in edit mode, dump scene state otherwise
  if (input->PressedOnceKey(T800K_TAB)) {
    if (m_gui.IsEditMode()) {
      m_gui.SaveLayout(kLayoutPath);
    } else if (m_activeScene) {
      m_activeScene->SaveSceneState();
    }
  }
  // +/-: adjust grid size in edit mode (consume keys so scene doesn't see them)
  if (m_gui.IsEditMode()) {
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
  }

  // Pause toggle
  if (input->PressedOnceKey(T800K_p)) {
    m_paused = !m_paused;
    printf("[DevLayer] %s\n", m_paused ? "PAUSED" : "RESUMED");
  }

  // Forward input to the active scene (skip when paused so mouse/keys don't move cameras)
  if (m_activeScene && !m_paused) {
    m_activeScene->OnInput(input);
  }
  // Update GUI interaction
  if (m_guiInited) {
    m_gui.Update(*input, g_pBaseDriver->width, g_pBaseDriver->height);
  }
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

} // namespace t800
