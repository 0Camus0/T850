# Editor Game Logic Integration Proposal

## T8ditor — Game Logic Authoring, Inspection, and Runtime Play

---

## Overview

This proposal details how T8ditor will consume the shared framework scene types from `Framework/include/scene/EditorSceneFile.h` to provide full game logic authoring capabilities: component editing, state machine design, group management, and runtime play testing.

The editor does **not** duplicate scene schema types — it uses the types defined in the Framework layer, following the established pattern where `EditorWorld` owns `std::vector<t850::scene::SceneGameEntityDesc>` just as it owns `std::vector<t850::scene::SceneObjectDesc>`.

---

## Architecture Reference

### Shared Framework Types

| Type | Location | Used By |
|---|---|---|
| `SceneGameEntityDesc` | `Framework/include/scene/EditorSceneFile.h` | T8ditor EditorWorld, SceneTemplate runtime |
| `SceneComponentDesc` | `Framework/include/scene/EditorSceneFile.h` (NEW) | T8ditor Inspector, GameLogicSystem |
| `SceneStateMachineDesc` | `Framework/include/scene/EditorSceneFile.h` (NEW) | T8ditor State Machine editor, StateMachine runtime |
| `SceneTransitionRuleDesc` | `Framework/include/scene/EditorSceneFile.h` (NEW) | T8ditor State Machine editor, StateMachine runtime |
| `SceneGroupDesc` | `Framework/include/scene/EditorSceneFile.h` (NEW) | T8ditor Hierarchy/Inspector, GroupManager |
| `SceneFlockConfigDesc` | `Framework/include/scene/EditorSceneFile.h` (NEW) | T8ditor Inspector, Flocking runtime |
| `SceneFormationConfigDesc` | `Framework/include/scene/EditorSceneFile.h` (NEW) | T8ditor Inspector, Formation runtime |
| `SceneGameLogicSettingsDesc` | `Framework/include/scene/EditorSceneFile.h` (NEW) | T8ditor settings, GameLogicSystem |

### Editor Data Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                      T8ditor Editor Data Flow                       │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  EditorWorld (T8ditor/)          EditorSceneFile.h (Framework/)     │
│  ┌─────────────────────────┐     ┌──────────────────────────────┐  │
│  │ gameEntities            │────►│ SceneGameEntityDesc          │  │
│  │ gameGroups              │────►│ SceneGroupDesc               │  │
│  │ selection (type 9, 10)  │────►│ SelectionRef                 │  │
│  │ undoStack               │────►│ EditorUndoEntry              │  │
│  └─────────────────────────┘     └──────────────────────────────┘  │
│           │                                      │                  │
│           ▼                                      ▼                  │
│  BuildEditorSceneSnapshot()              Glaze JSON Serialize      │
│           │                                      │                  │
│           ▼                                      ▼                  │
│  .t8scene JSON File ◄────────────────────────── (shared format)    │
│           │                                                          │
│           ▼ (Load)                                                  │
│  LoadEditorSceneFile() → ApplyEditorUndoState()                    │
│           │                                                          │
│           ▼ (Play Scene)                                            │
│  EditorSceneFile → SceneTemplate → GameLogicSystem::OnLoadScene()  │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## EditorWorld Extensions

### Current Structure

```cpp
// T8ditor/EditorWorld.h (existing)
struct EditorWorld {
    std::vector<t850::scene::SceneObjectDesc> objects;
    std::vector<t850::scene::SceneCameraDesc> cameras;
    std::vector<t850::scene::SceneLightDesc> lights;
    std::vector<t850::scene::SceneGameEntityDesc> gameEntities;  // ← Already exists
    std::vector<t850::scene::ScenePhysicsCharacterDesc> physicsEntities;
    // ... cameras, splines, godRays, groups, selection, undo, profiles
};
```

### Extensions Required

```cpp
// T8ditor/EditorWorld.h (extensions)
struct EditorWorld {
    // ... existing fields ...

    std::vector<t850::scene::SceneGroupDesc> gameGroups;  // NEW: Group authoring data
    std::optional<t850::scene::SceneGameLogicSettingsDesc> gameLogicSettings;  // NEW: Global settings

    // Selection extensions (SelectionRef.type values)
    // type 9 = gameEntity (select a SceneGameEntityDesc by index)
    // type 10 = gameGroup (select a SceneGroupDesc by index)

    // Multi-selection support for game entities (existing multiEntitySelect extended)
    // std::set<uint32_t> multiEntitySelect;  // Already exists, extended to support type 9, 10
};
```

### Selection Type Enum Extension

```cpp
// Current selection types (from editor-overview.md):
// 0 = mesh, 1 = camera, 2 = light, 3 = physics, 4 = nav, 
// 5 = spline, 6 = lightCamera, 7 = splinePoint, 8 = godRays

// New selection types:
// 9 = gameEntity
// 10 = gameGroup
```

---

## Hierarchy Panel Integration

### Tree Structure

```
Scene Hierarchy
├── 📦 Meshes
│   ├── marine_model_001
│   ├── marine_model_002
│   └── command_center_model
├── 📷 Cameras
│   └── MainCamera
├── 💡 Lights
│   └── DirectionalLight
├── 🎮 Game Entities                    ← NEW section
│   ├── 🟢 marine_001 [Idle]           ← Icon + name + current state
│   │   ├── ❤️ Health (60/60)         ← Component sub-items
│   │   ├── ⚔️ Combat                  │
│   │   └── 👁️ Sensor                  │
│   ├── 🟢 marine_002 [Patrol]
│   │   ├── ❤️ Health (60/60)
│   │   └── ⚔️ Combat
│   ├── 🟡 command_center [Produce]
│   │   ├── ❤️ Health (1500/1500)
│   │   └── 🏭 Production
│   └── ➕ Add Game Entity             ← Context menu / button
├── 👥 Game Groups                     ← NEW section
│   ├── 🔷 Squad Alpha (3 members)    ← Name + member count
│   └── ➕ Add Game Group
├── 🧱 Physics
│   └── marine_physics_001
└── 🗺️ Navigation
    └── NavMesh
```

### Visual Indicators

| Element | Indicator | Meaning |
|---|---|---|
| Game Entity node | 🟢 Green dot | Has state machine, states are valid |
| Game Entity node | 🟡 Yellow dot | State machine exists, no transitions defined |
| Game Entity node | 🔴 Red dot | State machine has errors (missing state, invalid transition) |
| Component node | Toggle icon | Enabled/disabled state |
| Group node | Member count badge | Number of member entities |
| Selected entity | Highlighted background | Current selection |
| Multi-selected | Blue highlight | Part of multi-selection |

### Context Menu Actions

```
Right-click on Game Entity:
├── Add Component...
│   ├── Health Component
│   ├── Combat Component
│   ├── Sensor Component
│   ├── Resource Component
│   └── Formation Slot Component
├── Remove Component
├── Edit State Machine...
├── Add to Group...
│   ├── Squad Alpha
│   ├── Squad Bravo
│   └── New Group...
├── Remove from Group
├── Set Initial State...
│   ├── Idle
│   ├── Patrol
│   ├── Attack
│   └── Flee
├── Link to Mesh...
├── Link to Physics Entity...
└── Delete Game Entity

Right-click on Game Group:
├── Add Member...
├── Remove Member
├── Edit Formation...
│   ├── Wedge
│   ├── Line
│   ├── Column
│   ├── Box
│   └── Circle
├── Edit Flock Config...
├── Set Group Strategy...
│   ├── Formation
│   └── Flock
└── Delete Group
```

---

## Inspector Panel Integration

### Game Entity Inspector

```
┌─────────────────────────────────────────────────────────────┐
│  Inspector: marine_001                                      │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌─ Identity ──────────────────────────────────────────┐   │
│  │  Name:        [marine_001              ]           │   │
│  │  Kind:        [Unit              ▼    ]            │   │
│  │  Visible:     [x]                                   │   │
│  │  Frozen:      [ ]                                   │   │
│  │  Show Wire:   [x]                                   │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌─ Links ──────────────────────────────────────────────┐   │
│  │  Mesh Object:   [marine_model_001        ▼]         │   │
│  │  Physics:       [marine_physics_001    ▼]           │   │
│  │  Ragdoll:       [marine_ragdoll          ▼]         │   │
│  │  Camera:        [None                  ▼]           │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌─ Group Membership ───────────────────────────────────┐   │
│  │  Group:         [Squad Alpha             ▼]         │   │
│  │  Formation Slot:[Auto                    ▼]         │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌─ Components (3)                [+ Add] [+ Duplicate]   │   │
│  │                                                                       │   │
│  │  ┌─ ❤️ Health ──────────────────────────────────────────┐          │   │
│  │  │ [x] Enabled  [🗑️]  [▲ Up]  [▼ Down]                       │   │
│  │  │  ID:           [health_main                   ]        │   │
│  │  │  Max HP:       [60         ]  [▶ Apply]                │   │
│  │  │  Current HP:   [60         ]  [▶ Apply]                │   │
│  │  │  Armor:        [1          ]  [▶ Apply]                │   │
│  │  │  Regen Rate:   [0.0        ]  [▶ Apply]                │   │
│  │  └───────────────────────────────────────────────────────┘          │   │
│  │                                                                       │   │
│  │  ┌─ ⚔️ Combat ───────────────────────────────────────────┐          │   │
│  │  │ [x] Enabled  [🗑️]  [▲ Up]  [▼ Down]                       │   │
│  │  │  ID:           [combat_main                   ]        │   │
│  │  │  Damage:       [6          ]                         │   │
│  │  │  Range:        [12.0       ]                         │   │
│  │  │  Attack Speed: [0.5        ]                         │   │
│  │  │  Damage Type:  [Ballistic             ▼]             │   │
│  │  │  Projectile:   [80.0       ]                         │   │
│  │  └───────────────────────────────────────────────────────┘          │   │
│  │                                                                       │   │
│  │  ┌─ 👁️ Sensor ──────────────────────────────────────────┐          │   │
│  │  │ [x] Enabled  [🗑️]  [▲ Up]  [▼ Down]                       │   │
│  │  │  Detection Radius:  [20.0      ]                      │   │
│  │  │  FOV Angle:         [180.0     ]                      │   │
│  │  │  Detect Stealth:    [ ]                               │   │
│  │  └───────────────────────────────────────────────────────┘          │   │
│  └───────────────────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌─ State Machine ─────────────────────────────────────────┐   │
│  │  Initial State: [idle                 ▼]               │   │
│  │  States (4):                                       │   │
│  │  ┌─────────────────────────────────────────────────┐ │   │
│  │  │ idle        🟢 (current)  [✏️ Edit] [🗑️]       │ │   │
│  │  │ patrol      ⚪             [✏️ Edit] [🗑️]       │ │   │
│  │  │ attack      ⚪             [✏️ Edit] [🗑️]       │ │   │
│  │  │ flee        ⚪             [✏️ Edit] [🗑️]       │ │   │
│  │  └─────────────────────────────────────────────────┘ │   │
│  │  [+ Add State]  [📊 Visualize]                       │   │
│  │                                                       │   │
│  │  Transitions (4):                                     │   │
│  │  ┌───────────────────────────────────────────────────┐ │   │
│  │  │ idle    ──[enemy_spotted]──►  attack  P:10       │ │   │
│  │  │ attack  ──[target_dead]──►     idle     P:10     │ │   │
│  │  │ idle    ──[move_command]──►   patrol   P:5       │ │   │
│  │  │ *       ──[health<20]──►      flee     P:20      │ │   │
│  │  └───────────────────────────────────────────────────┘ │   │
│  │  [+ Add Transition]  [📊 Graph View]                   │   │
│  └───────────────────────────────────────────────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Game Group Inspector

```
┌─────────────────────────────────────────────────────────────┐
│  Inspector: Squad Alpha                                     │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌─ Identity ──────────────────────────────────────────┐   │
│  │  ID:          [squad_alpha               ]         │   │
│  │  Name:        [Squad Alpha               ]         │   │
│  │  Strategy:    [Formation             ▼]             │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌─ Members (3) ─────────────────────────────────────────┐   │
│  │  ┌─────────────────────────────────────────────────┐ │   │
│  │  │ marine_001  [Slot: Auto]  [✖ Remove]           │ │   │
│  │  │ marine_002  [Slot: Auto]  [✖ Remove]           │ │   │
│  │  │ marine_003  [Slot: Auto]  [✖ Remove]           │ │   │
│  │  └─────────────────────────────────────────────────┘ │   │
│  │  [+ Add Member]  [Auto-assign from selection]        │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌─ Formation Config ─────────────────────────────────────┐   │
│  │  Type:          [Wedge                 ▼]             │   │
│  │  Spacing:       [3.0          ]                       │   │
│  │  Depth Step:    [3.0          ]                       │   │
│  │  Leader:        [marine_001            ▼]             │   │
│  └───────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌─ Flock Config ─────────────────────────────────────────┐   │
│  │  Separation:    [1.0          ]  ◉────────●           │   │
│  │  Alignment:     [0.8          ]  ◉───────●            │   │
│  │  Cohesion:      [0.6          ]  ◉────●────────       │   │
│  │  Separation R:  [2.0          ]                       │   │
│  │  Neighbor R:    [5.0          ]                       │   │
│  │  Max Speed:     [10.0         ]                       │   │
│  └───────────────────────────────────────────────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## State Machine Editor (Dedicated Panel)

A dedicated panel for visual state machine design, accessible via a button in the Inspector or as a standalone dockable panel.

```
┌───────────────────────────────────────────────────────────────────┐
│  State Machine: marine_001                            [✕ Close]  │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  ┌─────────────────────────────────────────────────────────────┐  │
│  │  ┌─────────────────────────────────────────────────────┐   │  │
│  │  │                   State Graph View                  │   │  │
│  │  │                                                     │   │  │
│  │  │        ┌─────────┐                                  │   │  │
│  │  │        │  🟢idle │◄──────────┐                      │   │  │
│  │  │        └─┬───────┘          │                      │   │  │
│  │  │          │ enemy_spotted     │ target_dead          │   │  │
│  │  │          │                  │                      │   │  │
│  │  │          ▼                  │                      │   │  │
│  │  │        ┌─────────┐          │                      │   │  │
│  │  │   ┌────│  attack  │◄────────┘                      │   │  │
│  │  │   │    └─────────┘                                 │   │  │
│  │  │   │                                                │   │  │
│  │  │   │ health<20 (P:20)                               │   │  │
│  │  │   │                                                │   │  │
│  │  │   ▼                                                │   │  │
│  │  │ ┌─────────┐                                        │   │  │
│  │  │ │  flee    │                                       │   │  │
│  │  │ └─────────┘                                        │   │  │
│  │  │                                                     │   │  │
│  │  │ ┌─────────┐                                        │   │  │
│  │  │ │  patrol  │  (transition from idle)               │   │  │
│  │  │ └─────────┘                                        │   │  │
│  │  └─────────────────────────────────────────────────────┘   │  │
│  └─────────────────────────────────────────────────────────────┘  │
│                                                                   │
│  ┌─ State Details: idle ────────────────────────────────────────┐  │
│  │  Name:       [idle                    ]                     │  │
│  │  Duration:   [-1.0 (infinite)           ]                   │  │
│  │  Entry Action: [None              ▼]                        │  │
│  │  Exit Action:  [None              ▼]                        │  │
│  │  Params:      (none)                                        │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                                                                   │
│  ┌─ Transition Details: idle → attack ──────────────────────────┐  │
│  │  From:        idle                                           │  │
│  │  To:          [attack                ▼]                      │  │
│  │  Condition:   [on_event:enemy_spotted      ]                │  │
│  │  Priority:    [10.0                    ]                     │  │
│  │  Cooldown:    [0.0                     ]                     │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                                                                   │
│  [Add State]  [Add Transition]  [Force Transition]  [Save]       │
└───────────────────────────────────────────────────────────────────┘
```

### Graph View Interactions

| Action | Result |
|---|---|
| Click state node | Select state, show details in panel below |
| Drag state node | Reposition in graph view |
| Click transition edge | Select transition, show details |
| Right-click state | Edit, Delete, Force Transition to this state |
| Right-click empty area | Add State, Add Transition |
| Scroll | Zoom in/out |
| Middle-mouse drag | Pan view |
| Double-click state | Open state parameter editor |

---

## Save/Load Integration

### BuildEditorSceneSnapshot Extensions

```cpp
// T8ditor/EditorScene.cpp — BuildEditorSceneSnapshot() extensions
t850::scene::EditorSceneFile BuildEditorSceneSnapshot(const EditorWorld& world) {
    t850::scene::EditorSceneFile sf;

    // ... existing: objects, cameras, lights, physicsEntities, etc. ...

    // Game entities (already exists — extended with new fields)
    sf.game_entities = world.gameEntities;  // Includes components, behavior, group_id

    // Game groups (NEW)
    sf.game_groups = world.gameGroups;

    // Game logic global settings (NEW)
    if (world.gameLogicSettings.has_value())
        sf.game_logic_settings = world.gameLogicSettings;

    return sf;
}
```

### Load & Apply

```cpp
// T8ditor/EditorScene.cpp — Load extensions
void LoadSceneIntoEditorWorld(const t850::scene::EditorSceneFile& sf) {
    // ... existing: objects, cameras, lights, etc. ...

    g_gameEntities = sf.game_entities;
    g_gameGroups = sf.game_groups;
    g_gameLogicSettings = sf.game_logic_settings;

    // Auto-infer game entities for meshes/physics without one (existing)
    EnsureInferredGameEntities();

    // Validate group memberships
    ValidateGroupMemberships();

    // Validate state machine references
    ValidateStateMachines();
}
```

---

## Undo/Redo System

### New Undo Commands

Following the existing editor convention where "All changes MUST have undo support":

```cpp
// Component commands
struct AddComponentCommand {
    int entityIndex;
    SceneComponentDesc component;
    void Redo() { world.gameEntities[entityIndex].components.push_back(component); }
    void Undo() { world.gameEntities[entityIndex].components.pop_back(); }
};

struct RemoveComponentCommand {
    int entityIndex;
    int componentIndex;
    SceneComponentDesc component;  // Store for undo restore
    void Undo() { world.gameEntities[entityIndex].components.insert(
        world.gameEntities[entityIndex].components.begin() + componentIndex, component); }
    void Redo() { world.gameEntities[entityIndex].components.erase(
        world.gameEntities[entityIndex].components.begin() + componentIndex); }
};

struct EditComponentCommand {
    int entityIndex;
    int componentIndex;
    SceneComponentDesc oldValue;
    SceneComponentDesc newValue;
    void Undo() { world.gameEntities[entityIndex].components[componentIndex] = oldValue; }
    void Redo() { world.gameEntities[entityIndex].components[componentIndex] = newValue; }
};

// State machine commands
struct AddStateCommand {
    int entityIndex;
    SceneStateMachineStateDesc state;
    void Redo() { world.gameEntities[entityIndex].behavior->states.push_back(state); }
    void Undo() { world.gameEntities[entityIndex].behavior->states.pop_back(); }
};

struct AddTransitionCommand {
    int entityIndex;
    SceneTransitionRuleDesc transition;
    void Redo() { world.gameEntities[entityIndex].behavior->transitions.push_back(transition); }
    void Undo() { world.gameEntities[entityIndex].behavior->transitions.pop_back(); }
};

// Group commands
struct AddToGroupCommand {
    std::string groupId;
    uint32_t entityId;
    std::string entityName;
    void Redo() { FindGroup(groupId)->member_entity_ids.push_back(entityName); }
    void Undo() { RemoveFromGroup(groupId, entityName); }
};

struct CreateGroupCommand {
    SceneGroupDesc group;
    void Redo() { world.gameGroups.push_back(group); }
    void Undo() { world.gameGroups.pop_back(); }
};
```

### Undo Integration Points

| User Action | Undo Command |
|---|---|
| Add component via Inspector | `AddComponentCommand` |
| Remove component via Inspector | `RemoveComponentCommand` |
| Edit component parameter | `EditComponentCommand` |
| Add state via State Machine editor | `AddStateCommand` |
| Add transition | `AddTransitionCommand` |
| Delete state/transition | Store-and-remove pattern |
| Add entity to group | `AddToGroupCommand` |
| Create new group | `CreateGroupCommand` |
| Delete group | Store-and-delete pattern |
| Change group strategy | `EditGroupCommand` |
| Edit flock weights | `EditFlockConfigCommand` |

---

## ImGui Debug Panels (DevGuiContext Integration)

### Integration with Existing DevGuiContext

The editor already has ImGui panels for debug visualization. Game logic panels integrate as additional tabs or dockable windows.

```cpp
// T8ditor/GameLogicEditorPanel.cpp
class GameLogicEditorPanel {
public:
    void Initialize(EditorWorld* world);
    void Render();  // Called from EditorApp ImGui loop

    // Individual panels
    void RenderGameObjectPanel();
    void RenderComponentEditor();
    void RenderStateMachineVisualizer();
    void RenderGroupManager();
    void RenderEventLog();
    void RenderSpatialGridPanel();
    void RenderAIConfigPanel();

private:
    EditorWorld* editorWorld_ = nullptr;
    int selectedGameEntityIndex_ = -1;
    int selectedGameGroupIndex_ = -1;
};
```

### Panel: Game Object List

```
┌─────────────────────────────────────────────────────────────┐
│  Game Objects (5)                              [Search...]  │
├─────────────────────────────────────────────────────────────┤
│  Layer: [All ▼]  Team: [All ▼]  State: [All ▼]             │
├─────────────────────────────────────────────────────────────┤
│  ┌───────────────────────────────────────────────────────┐  │
│  │ 🟢 marine_001     Unit    Team:1   Comp:3   [Idle]   │  │
│  │ 🟢 marine_002     Unit    Team:1   Comp:3   [Patrol]  │  │
│  │ 🟢 marine_003     Unit    Team:1   Comp:2   [Idle]   │  │
│  │ 🟡 command_center Building Team:1  Comp:2   [Produce]│  │
│  │ 🔴 supply_depot   Building Team:2  Comp:2   [Error]  │  │
│  └───────────────────────────────────────────────────────┘  │
│                                                             │
│  Click to select  │  Double-click to open State Machine    │
└─────────────────────────────────────────────────────────────┘
```

### Panel: Event Log

```
┌─────────────────────────────────────────────────────────────┐
│  Event Log (127 events)                      [Clear] [Pause]│
├─────────────────────────────────────────────────────────────┤
│  Filter: [All ▼]  Source: [All ▼]                          │
├─────────────────────────────────────────────────────────────┤
│  ┌───────────────────────────────────────────────────────┐  │
│  │ [14:23:01] CombatHit      marine_001 → supply_depot  │  │
│  │ [14:23:01] HealthChanged  supply_depot 100→94        │  │
│  │ [14:23:02] StateChange    marine_001 Idle→Attack     │  │
│  │ [14:23:02] StateChange    marine_002 Idle→Attack     │  │
│  │ [14:23:05] CombatHit      marine_002 → supply_depot  │  │
│  │ [14:23:05] EntityDeath    supply_depot               │  │
│  │ [14:23:05] StateChange    marine_001 Attack→Idle     │  │
│  │ [14:23:05] StateChange    marine_002 Attack→Idle     │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

---

## Viewport Overlay Rendering

### Component Visualization Overlays

Rendered in `RenderEditorSceneFrame()` or the viewport's overlay pass:

```cpp
// T8ditor/HostedViewport.cpp — overlay rendering extensions
void RenderGameLogicOverlays(const EditorWorld& world, const EditorCamera& cam) {
    for (const auto& entity : world.gameEntities) {
        PrimitiveInst* inst = FindPrimitiveInstByName(entity.name);
        if (!inst) continue;

        const auto& transform = inst->GetWorldTransform();

        // Sensor component visualization (wireframe sphere + FOV cone)
        for (const auto& comp : entity.components) {
            if (comp.type == "sensor" && comp.enabled) {
                float radius = comp.params.value("detectionRadius", "20.0").stof();
                float fov = comp.params.value("fovAngle", "180.0").stof();
                DrawWireframeSphere(transform.translation, radius, Colors::SensorBlue);
                DrawFOVCone(transform, radius, fov, Colors::SensorBlue);
            }

            // Combat range visualization
            if (comp.type == "combat" && comp.enabled) {
                float range = comp.params.value("range", "12.0").stof();
                DrawWireframeSphere(transform.translation, range, Colors::CombatRed);
            }

            // Health bar above entity
            if (comp.type == "health" && comp.enabled) {
                float maxHp = std::stof(comp.params.value("maxHp", "100"));
                float curHp = std::stof(comp.params.value("currentHp", "100"));
                DrawHealthBar(transform.translation, curHp / maxHp, 1.0f, 0.1f);
            }
        }
    }

    // Group formation lines
    for (const auto& group : world.gameGroups) {
        RenderGroupFormationLines(group, Colors::GroupCyan);
    }
}
```

### Overlay Toggle Options

```
View → Overlays → Game Logic
├── [x] Sensor Radius
├── [x] Combat Range
├── [x] Health Bars
├── [x] State Labels
├── [x] Group Formation Lines
├── [x] Group Member Connections
├── [ ] Spatial Grid Cells
└── [ ] NavMesh Path Lines
```

### Color Scheme

| Overlay | Color | Opacity |
|---|---|---|
| Sensor radius | Blue (0.2, 0.4, 1.0) | 30% |
| Combat range | Red (1.0, 0.3, 0.2) | 25% |
| Health bar (healthy) | Green (0.2, 1.0, 0.2) | 80% |
| Health bar (low) | Yellow/Red gradient | 80% |
| State label | White with black outline | 90% |
| Group formation lines | Cyan (0.2, 1.0, 1.0) | 40% |
| Group member connections | Purple (0.8, 0.3, 1.0) | 30% |
| Selection highlight | Yellow ring | 70% |

---

## Play Scene Runtime Lifecycle

### Play Scene Flow

The editor's "Play Scene" feature (`HostedViewportPanel`) tests game logic at runtime:

```
┌──────────────────────────────────────────────────────────────────┐
│                    Play Scene Lifecycle                           │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  1. User clicks "Play Scene" button                              │
│                                                                  │
│  2. EditorWorld → BuildEditorSceneSnapshot()                     │
│     └─ Serializes gameEntities (with components, behavior)       │
│     └─ Serializes gameGroups                                     │
│     └─ Serializes gameLogicSettings                              │
│                                                                  │
│  3. EditorSceneFile → SceneTemplate (runtime load)               │
│     └─ Creates PrimitiveInst from scene objects                  │
│     └─ Initializes PhysicsWorld, NavigationWorld                 │
│                                                                  │
│  4. SceneTemplate → GameLogicSystem::OnLoadScene(sceneFile)      │
│     └─ Creates GameObjects from gameEntities                     │
│     └─ Creates Components from SceneComponentDesc                │
│     └─ Creates StateMachines from SceneStateMachineDesc          │
│     └─ Creates Groups from gameGroups                            │
│                                                                  │
│  5. GameLogicSystem::Update(dt) runs during scene playback       │
│     └─ UpdateSpatialGrid()                                       │
│     └─ UpdateStateMachines()                                     │
│     └─ UpdateGroups()                                            │
│     └─ ProcessEvents()                                           │
│                                                                  │
│  6. ImGui debug panels show runtime state                        │
│     └─ State changes visible in real-time                        │
│     └─ Event log populated                                       │
│     └─ Group formation updates                                   │
│                                                                  │
│  7. User clicks "Stop Scene" button                              │
│                                                                  │
│  8. GameLogicSystem::Shutdown()                                  │
│     └─ Destroys GameObjects, Groups                              │
│     └─ Clears registry                                           │
│                                                                  │
│  9. Editor returns to edit mode                                  │
│     └─ EditorWorld state preserved (runtime state NOT saved)     │
│     └─ Overlay rendering resumes                                 │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

### Play Scene Panel Extensions

```
┌─────────────────────────────────────────────────────────────┐
│  Play Scene Runtime                                        │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  [▶ Play Scene]  [⏹ Stop Scene]  [⏸ Pause]                │
│                                                             │
│  ┌─ Runtime Stats ────────────────────────────────────────┐ │
│  │  GameObjects:    5                                     │ │
│  │  Active States:  5                                     │ │
│  │  Groups:         2                                     │ │
│  │  Events/sec:     12                                    │ │
│  │  FPS:            60                                    │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                             │
│  ┌─ Quick Commands ───────────────────────────────────────┐ │
│  │  [Send Event...]  [Force State Change...]              │ │
│  │  [Add Damage...]   [Spawn Entity...]                   │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                             │
│  ┌─ State Overview ───────────────────────────────────────┐ │
│  │  marine_001:  [Idle    ]  ◄─ current state            │ │
│  │  marine_002:  [Patrol  ]                              │ │
│  │  marine_003:  [Attack  ]                              │ │
│  │  command_center: [Produce]                            │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Menu & Toolbar Extensions

### Menu Bar Additions

```
Game Logic Menu:
├── New
│   ├── Game Entity...          (Ctrl+Shift+E)
│   ├── Game Group...           (Ctrl+Shift+G)
│   └── Component...
│       ├── Health Component
│       ├── Combat Component
│       ├── Sensor Component
│       └── Resource Component
├── Edit
│   ├── Add Component...        (Ctrl+Shift+A)
│   ├── Remove Component        (Ctrl+Shift+R)
│   ├── Edit State Machine...   (Ctrl+Shift+S)
│   └── Validate All            (Checks for errors)
├── View
│   ├── Show Game Logic Panels
│   ├── Show Overlays
│   │   ├── Sensor Radius
│   │   ├── Combat Range
│   │   ├── Health Bars
│   │   └── Formation Lines
│   └── Debug
│       ├── State Machine Graph
│       ├── Event Log
│       └── Spatial Grid
├── Play
│   ├── Play Scene              (F5)
│   ├── Stop Scene              (Shift+F5)
│   └── Pause Scene             (F6)
└── Settings
    ├── Game Logic Settings...
    └── Default Flock Config...
```

### Toolbar Additions

```
[+ Entity]  [+ Group]  [+ Component]  [📊 States]  [▶ Play]  [⏹ Stop]
```

---

## Validation & Error Reporting

### Scene Validation Checks

```cpp
// T8ditor/EditorScene.cpp — validation
struct ValidationIssue {
    enum class Severity { Error, Warning, Info };
    Severity severity;
    std::string message;
    std::string entityName;
    int entityIndex;
};

std::vector<ValidationIssue> ValidateGameLogic(const EditorWorld& world) {
    std::vector<ValidationIssue> issues;

    for (size_t i = 0; i < world.gameEntities.size(); ++i) {
        const auto& entity = world.gameEntities[i];

        // Check mesh link
        if (entity.mesh_object.empty())
            issues.push_back({Severity::Warning, "No mesh linked", entity.name, static_cast<int>(i)});

        // Check state machine validity
        if (entity.behavior.has_value()) {
            const auto& behavior = entity.behavior.value();

            // Initial state must exist in states list
            bool initialExists = false;
            for (const auto& state : behavior.states) {
                if (state.name == behavior.initial_state) {
                    initialExists = true; break;
                }
            }
            if (!initialExists)
                issues.push_back({Severity::Error, "Initial state '" + behavior.initial_state + "' not found", entity.name, static_cast<int>(i)});

            // Transition references must be valid
            for (const auto& trans : behavior.transitions) {
                if (trans.from_state != "*" && !StateExists(behavior.states, trans.from_state))
                    issues.push_back({Severity::Error, "Transition from non-existent state '" + trans.from_state + "'", entity.name, static_cast<int>(i)});
                if (!StateExists(behavior.states, trans.to_state))
                    issues.push_back({Severity::Error, "Transition to non-existent state '" + trans.to_state + "'", entity.name, static_cast<int>(i)});
            }
        }

        // Check group membership
        if (!entity.group_id.empty()) {
            bool groupFound = false;
            for (const auto& group : world.gameGroups) {
                if (group.id == entity.group_id) {
                    groupFound = true; break;
                }
            }
            if (!groupFound)
                issues.push_back({Severity::Error, "Referenced group '" + entity.group_id + "' not found", entity.name, static_cast<int>(i)});
        }
    }

    // Check group memberships are consistent
    for (const auto& group : world.gameGroups) {
        for (const auto& memberName : group.member_entity_ids) {
            bool entityFound = false;
            for (const auto& entity : world.gameEntities) {
                if (entity.name == memberName) {
                    entityFound = true; break;
                }
            }
            if (!entityFound)
                issues.push_back({Severity::Error, "Group '" + group.id + "' references non-existent entity '" + memberName + "'", group.id, -1});
        }
    }

    return issues;
}
```

### Validation Display

```
┌─────────────────────────────────────────────────────────────┐
│  Validation Results (3 issues)                       [🔍 Run]│
├─────────────────────────────────────────────────────────────┤
│  ┌───────────────────────────────────────────────────────┐  │
│  │ 🔴 ERROR: Initial state 'patrolling' not found       │  │
│  │    Entity: marine_002                                │  │
│  │    [Jump to Entity]                                  │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │ 🟡 WARNING: No mesh linked                           │  │
│  │    Entity: phantom_unit                              │  │
│  │    [Jump to Entity]                                  │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │ 🔴 ERROR: Group 'squad_gamma' references non-existent│  │
│  │    entity 'marine_009'                               │  │
│  │    [Jump to Group]                                   │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

---

## Implementation Phases (Editor-Specific)

### Phase E1: EditorWorld & Schema Extensions

| # | Task | Dependencies | Effort |
|---|---|---|---|
| 1 | Extend `SceneGameEntityDesc` with `components`, `behavior`, `group_id` fields | Framework scene types | 1 day |
| 2 | Add `SceneComponentDesc`, `SceneStateMachineDesc`, `SceneTransitionRuleDesc` to EditorSceneFile.h | Glaze Object macros | 1 day |
| 3 | Add `SceneGroupDesc`, `SceneFlockConfigDesc`, `SceneFormationConfigDesc` | — | 1 day |
| 4 | Extend `EditorWorld` with `gameGroups` and `gameLogicSettings` | Types from step 1-3 | 0.5 day |
| 5 | Extend `SelectionRef` with type 9 (gameEntity), 10 (gameGroup) | — | 0.5 day |
| 6 | Update `BuildEditorSceneSnapshot()` to write new fields | Types in place | 1 day |
| 7 | Update `LoadSceneIntoEditorWorld()` to read new fields | — | 1 day |

**Deliverable:** Scene files can save/load game logic data. EditorWorld holds game groups and settings.

**Total: ~6 days**

### Phase E2: Hierarchy & Inspector Integration

| # | Task | Dependencies | Effort |
|---|---|---|---|
| 8 | Add Game Entity tree section to Hierarchy panel | Phase E1 | 2 days |
| 9 | Add Game Group tree section to Hierarchy panel | Phase E1 | 1 day |
| 10 | Implement Game Entity Inspector (identity, links, group) | Phase E1 | 1.5 days |
| 11 | Implement Component editor in Inspector | Phase E1 | 2 days |
| 12 | Implement State Machine editor in Inspector (states + transitions lists) | Phase E1 | 2 days |
| 13 | Implement Group Inspector | Phase E1 | 1.5 days |
| 14 | Context menu actions (Add Component, Add to Group, etc.) | Hierarchy + Inspector | 1.5 days |

**Deliverable:** Full tree navigation and property editing for game entities, components, state machines, and groups.

**Total: ~11 days**

### Phase E3: Undo System

| # | Task | Dependencies | Effort |
|---|---|---|---|
| 15 | `AddComponentCommand` + `RemoveComponentCommand` | Phase E2 | 1 day |
| 16 | `EditComponentCommand` | Phase E2 | 0.5 day |
| 17 | `AddStateCommand` + `AddTransitionCommand` | Phase E2 | 1 day |
| 18 | `AddToGroupCommand` + `CreateGroupCommand` | Phase E2 | 1 day |
| 19 | Wire all commands to EditorApp undo system | Phase E2 | 0.5 day |

**Deliverable:** All game logic changes are undoable/redoable.

**Total: ~4 days**

### Phase E4: State Machine Visual Panel

| # | Task | Dependencies | Effort |
|---|---|---|---|
| 20 | State graph view (nodes + edges rendering with ImGui) | Phase E2 | 2 days |
| 21 | Node drag, edge click, zoom, pan interactions | Graph view | 1.5 days |
| 22 | State details panel + Transition details panel | Graph view | 1 day |
| 23 | Force transition button during Play Scene | Graph view | 0.5 day |

**Deliverable:** Visual state machine editor with full interaction.

**Total: ~5 days**

### Phase E5: Viewport Overlays

| # | Task | Dependencies | Effort |
|---|---|---|---|
| 24 | Sensor radius wireframe overlay | Editor render pass | 1 day |
| 25 | Combat range overlay | — | 0.5 day |
| 26 | Health bar overlay | — | 1 day |
| 27 | State label overlay (3D text) | — | 0.5 day |
| 28 | Group formation line overlay | — | 1 day |
| 29 | Overlay toggle menu items | All overlays | 0.5 day |

**Deliverable:** Visual component and group overlays in viewport.

**Total: ~4.5 days**

### Phase E6: Play Scene Integration

| # | Task | Dependencies | Effort |
|---|---|---|---|
| 30 | Wire `GameLogicSystem::OnLoadScene()` in Play Scene flow | Framework game logic | 1 day |
| 31 | Play Scene runtime stats panel | — | 1 day |
| 32 | Runtime state overview (current states display) | — | 0.5 day |
| 33 | Quick command panel (send event, force state, add damage) | — | 1 day |
| 34 | Stop Scene cleanup (destroy GameObjects, restore editor state) | — | 0.5 day |

**Deliverable:** Full Play Scene integration with game logic runtime testing.

**Total: ~4 days**

### Phase E7: Validation & Polish

| # | Task | Dependencies | Effort |
|---|---|---|---|
| 35 | Scene validation system | Phase E1 | 1.5 days |
| 36 | Validation results panel | — | 1 day |
| 37 | Menu/Toolbar additions | All phases | 1 day |
| 38 | Event Log panel | Framework EventBus | 1 day |
| 39 | Keyboard shortcuts | — | 0.5 day |
| 40 | Demo scene creation | All phases | 1 day |

**Deliverable:** Complete editor integration with validation, menus, and demo content.

**Total: ~6 days**

### Total Editor Effort: ~40.5 days (single developer)

---

## What This Does NOT Do

- **Does not create a new scene format** — extends existing `EditorSceneFile.h` in Framework layer.
- **Does not duplicate scene types** — uses shared `t850::scene::` namespace types.
- **Does not replace EditorWorld** — extends existing data model.
- **Does not require new build system changes** — uses existing MSBuild `.vcxproj` structure.
- **Does not create a visual script editor** — state machines use structured data, not node-based scripting.
- **Does not implement multiplayer editing** — single-user editor.

---

## References

- Main Game Logic Proposal: `game-logic-and-ai-integration.md`
- Editor Overview: `documentation/editor/editor-overview.md`
- Scene Format: `documentation/scenes/scene-format-and-runtime.md`
- EditorSceneFile: `Framework/include/scene/EditorSceneFile.h`
- EditorWorld: `T8ditor/EditorWorld.h`
- EditorApp: `T8ditor/EditorApp.cpp`
