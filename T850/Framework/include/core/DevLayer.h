#ifndef T800_DEVLAYER_H
#define T800_DEVLAYER_H

#include <core/Core.h>
#include <gui/T8_GUI.h>

namespace t800 {

  class DevLayer {
  public:
    DevLayer();

    void Init(RootFramework* framework);
    void Destroy();

    // Scene lifecycle
    void SetActiveScene(SceneBase* scene);
    SceneBase* GetActiveScene() const;

    // Per-frame forwarding
    void Update(float dt);
    void Draw();
    void ProcessInput(InputManager* input);

    // Scene transitions
    void LoadScene(SceneBase* scene);
    void UnloadScene();

    GUIManager& GetGUI() { return m_gui; }
    void RebuildGUIForScene();

    void SetEditMode(bool e);
    void SetSnapToGrid(bool s);

  private:
    static constexpr const char* kLayoutPath = "gui_layout.json";

    RootFramework* m_framework;
    SceneBase* m_activeScene;
    GUIManager m_gui;
    bool m_guiInited;
  };

} // namespace t800

#endif
