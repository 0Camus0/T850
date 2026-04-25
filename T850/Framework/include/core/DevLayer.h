#ifndef T800_DEVLAYER_H
#define T800_DEVLAYER_H

#include <core/Core.h>
#include <gui/GUIManager.h>
#include <string>

namespace t850 {

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
    const GUIManager& GetGUI() const { return m_gui; }
    void RebuildGUIForScene();

    void SetEditMode(bool e);
    void SetSnapToGrid(bool s);
    void SetControlEditMode(bool e);
    bool SetControlEditTargetByName(const std::string& targetName);
    bool IsPaused() const { return m_paused; }

  private:
    static constexpr const char* kLayoutPath = "Layouts/gui_layout.json";
    static constexpr const char* kControlLayoutPath = "Layouts/gui_controls_layout.json";

    RootFramework* m_framework;
    SceneBase* m_activeScene;
    GUIManager m_gui;
    bool m_guiInited;
    bool m_paused = false;
  };

} // namespace t850

#endif
