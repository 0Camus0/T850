#include <core/DevLayer.h>
#include <cstdio>

namespace t800 {

DevLayer::DevLayer()
    : m_framework(nullptr)
    , m_activeScene(nullptr)
    , m_showDebugOverlay(false) {
}

void DevLayer::Init(RootFramework* framework) {
  m_framework = framework;
}

void DevLayer::SetActiveScene(SceneBase* scene) {
  m_activeScene = scene;
}

SceneBase* DevLayer::GetActiveScene() const {
  return m_activeScene;
}

void DevLayer::Update(float dt) {
  if (m_activeScene) {
    m_activeScene->OnUpdate(dt);
  }
}

void DevLayer::Draw() {
  if (m_activeScene) {
    m_activeScene->OnDraw();
  }

  if (m_showDebugOverlay) {
    DrawDebugOverlay();
  }
}

void DevLayer::ProcessInput(InputManager* input) {
  if (m_activeScene) {
    m_activeScene->OnInput(input);
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
}

void DevLayer::UnloadScene() {
  if (m_activeScene) {
    m_activeScene->OnDestoryScene();
    m_activeScene = nullptr;
  }
}

void DevLayer::DrawDebugOverlay() {
  // Placeholder: future debug GUI, RT visualization quads, etc.
  // This runs after the scene draws but before swap buffers.
}

} // namespace t800
