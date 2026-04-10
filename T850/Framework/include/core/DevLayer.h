#ifndef T800_DEVLAYER_H
#define T800_DEVLAYER_H

#include <core/Core.h>

namespace t800 {

  // DevLayer sits between the Framework and the Scene.
  // The Framework calls DevLayer, which forwards to the active SceneBase
  // and can inject debug/GUI rendering around the scene's own drawing.
  class DevLayer {
  public:
    DevLayer();

    void Init(RootFramework* framework);

    // Scene lifecycle
    void SetActiveScene(SceneBase* scene);
    SceneBase* GetActiveScene() const;

    // Per-frame forwarding (call these instead of calling scene directly)
    void Update(float dt);
    void Draw();
    void ProcessInput(InputManager* input);

    // Scene transitions
    void LoadScene(SceneBase* scene);
    void UnloadScene();

    // Debug controls
    void SetShowDebugOverlay(bool show) { m_showDebugOverlay = show; }
    bool GetShowDebugOverlay() const { return m_showDebugOverlay; }

  private:
    void DrawDebugOverlay();

    RootFramework* m_framework;
    SceneBase* m_activeScene;
    bool m_showDebugOverlay;
  };

} // namespace t800

#endif
