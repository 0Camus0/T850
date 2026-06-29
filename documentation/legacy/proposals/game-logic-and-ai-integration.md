# Game Logic & AI Integration Proposal

> **Status:** Proposal  
> **Date:** 2026-06-28  
> **Scope:** Core game logic and AI framework layered on top of T850 systems  
> **Build System:** MSBuild (`.vcxproj`)

---

## Table of Contents

1. [Design Principles](#design-principles)
2. [Architecture Overview](#architecture-overview)
3. [Layer 1: GameObject & Components](#layer-1-gameobject--components)
4. [Layer 2: State Machine Framework](#layer-2-state-machine-framework)
5. [Layer 3: Group Behavior System](#layer-3-group-behavior-system)
6. [Layer 4: Event/Message Bus](#layer-4-eventmessage-bus)
7. [Layer 5: GameLogicSystem Orchestration](#layer-5-gamelogicsystem-orchestration)
8. [Integration With Existing T850 Systems](#integration-with-existing-t850-systems)
9. [NavMesh & AI Integration Details](#navmesh--ai-integration-details)
10. [Scene Format Extension](#scene-format-extension)
11. [ImGui Debug Panels](#imgui-debug-panels)
12. [Proposed File Structure](#proposed-file-structure)
13. [Phased Implementation Plan](#phased-implementation-plan)
14. [What This Does NOT Do](#what-this-does-not-do)

---

## Design Principles

1. **Core framework feature** — shipped as part of the T850 framework, built via MSBuild (`.vcxproj`). Always available.
2. **Non-intrusive** — no changes to core rendering, physics, or navmesh systems. All new code lives in a new `game/` module.
3. **Generic** — designed to work for RTS, FPS, RPG, tower defense, or any genre that needs entity behavior.
4. **Composable** — small, independent systems that layer on top of each other. Users enable only what they need at runtime.
5. **Editor-friendly** — ImGui panels for visualization, inspection, and debugging. Scene file (`.t8scene`) extension for authoring.

---

## Shared Framework Architecture

### Core Principle: No Duplication

The T850 framework already establishes a pattern where **scene schema types live in the Framework layer** and are shared between T8ditor (editor) and runtime applications (SceneTemplate, custom games). The `EditorSceneFile.h` header at `Framework/include/scene/EditorSceneFile.h` defines `SceneGameEntityDesc`, `SceneObjectDesc`, `SceneCameraDesc`, `SceneLightDesc`, `ScenePhysicsCharacterDesc`, and all other scene authoring types — in the `t850::scene` namespace.

**Game logic scene types follow this same pattern.** Component descriptors, state machine definitions, and group configurations are added to the Framework scene schema layer, NOT to the editor. The editor then consumes these shared types for UI, selection, save/load, and undo.

```
┌─────────────────────────────────────────────────────────────────────┐
│                        FRAMEWORK LAYER                              │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  Framework/include/scene/  (Shared Scene Schema)           │    │
│  │  ┌─────────────────────────────────────────────────────┐   │    │
│  │  │ EditorSceneFile.h  (existing)                       │   │    │
│  │  │  - SceneGameEntityDesc, SceneObjectDesc             │   │    │
│  │  │  - SceneCameraDesc, SceneLightDesc                  │   │    │
│  │  │  - ScenePhysicsCharacterDesc, SceneNavMeshBuild..   │   │    │
│  │  │  - SceneSplineDesc, SceneGodRaysVolumeDesc          │   │    │
│  │  └─────────────────────────────────────────────────────┘   │    │
│  │  ┌─────────────────────────────────────────────────────┐   │    │
│  │  │ EditorSceneFile.h  (extended — game logic types)    │   │    │
│  │  │  - SceneComponentDesc  (component authoring data)   │   │    │
│  │  │  - SceneStateMachineStateDesc  (state config)       │   │    │
│  │  │  - SceneTransitionRuleDesc  (transition config)     │   │    │
│  │  │  - SceneGroupDesc  (group config)                   │   │    │
│  │  │  - SceneFlockConfigDesc  (flock params)             │   │    │
│  │  │  - SceneGameEntityDesc  (extended with fields)      │   │    │
│  │  └─────────────────────────────────────────────────────┘   │    │
│  └─────────────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  Framework/include/game/  (Runtime Engine)                  │    │
│  │  ┌─────────────────────────────────────────────────────┐   │    │
│  │  │ GameLogicSystem.h/cpp  (game loop, update, load)    │   │    │
│  │  │ StateMachine.h/cpp  (runtime state machine)         │   │    │
│  │  │ GroupBehavior.h/cpp  (flock, formation, etc.)       │   │    │
│  │  │ EventBus.h/cpp  (event system)                      │   │    │
│  │  │ Components/  (CombatComponent.h, etc.)              │   │    │
│  │  └─────────────────────────────────────────────────────┘   │    │
│  └─────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                │ consumes via includes
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        EDITOR LAYER                                 │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  T8ditor/EditorWorld.h                                     │    │
│  │  - std::vector<t850::scene::SceneGameEntityDesc> gameEntities│   │
│  │  - std::vector<t850::scene::SceneGroupDesc> gameGroups      │   │
│  │  - SelectionRef extended (9=gameEntity, 10=gameGroup)       │   │
│  └─────────────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  T8ditor/  (UI, save/load, undo)                            │    │
│  │  - Hierarchy panel: Game entity/group tree nodes            │   │
│  │  - Inspector panel: Component/state/group editors           │   │
│  │  - BuildEditorSceneSnapshot: writes game logic data         │   │
│  │  - Undo commands: AddComponent, RemoveComponent, etc.       │   │
│  │  - Viewport overlays: Health bars, sensor radius, etc.      │   │
│  │  - ImGui debug panels: State machine, event log             │   │
│  └─────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
```

### Framework vs Editor Responsibility Matrix

| Concern | Framework Responsibility | Editor Responsibility |
|---|---|---|
| **Scene Schema Types** | Define `SceneComponentDesc`, `SceneStateMachineStateDesc`, `SceneGroupDesc` in `EditorSceneFile.h` | Use `t850::scene::` types in `EditorWorld` vectors |
| **Glaze Serialization** | `Object()` macros for JSON load/save in `EditorSceneFile.cpp` | Call `BuildEditorSceneSnapshot()` → `SaveEditorSceneFile()` |
| **Runtime Engine** | `GameLogicSystem`, `StateMachine`, `GroupBehavior`, `EventBus` | N/A — runtime not editor concern |
| **UI Panels** | N/A | Hierarchy nodes, Inspector editors, ImGui debug panels |
| **Selection** | N/A | `SelectionRef` types 9+ (gameEntity, gameGroup) |
| **Undo/Redo** | N/A | `AddComponentCommand`, `RemoveComponentCommand`, etc. |
| **Viewport Overlays** | N/A | Health bars, sensor radius, formation lines, state labels |
| **Play Scene** | `GameLogicSystem::OnLoadScene()` processes scene types | `PlayScenePanel` hosts SceneTemplate |

### Scene Schema Type Additions

The following types should be added to `Framework/include/scene/EditorSceneFile.h`, following the existing pattern of `SceneGameEntityDesc`, `SceneObjectDesc`, etc.:

#### Extended `SceneGameEntityDesc`

```cpp
struct SceneGameEntityDesc {
  std::string name = "Game Entity";
  std::string kind = "generic";
  std::string mesh_object;
  std::string primary_physics_entity;
  std::vector<std::string> physics_entities;
  std::string camera;
  std::string ragdoll_object;
  std::string ai;
  bool visible = true;
  bool frozen = false;
  bool show_wire = true;

  // ── Game logic extensions (SHARED FRAMEWORK) ──
  std::string group_id;                           // Group membership
  std::vector<SceneComponentDesc> components;     // Component configs
  SceneStateMachineDesc behavior;                 // State machine config
};
```

#### `SceneComponentDesc`

```cpp
struct SceneComponentDesc {
  std::string type;                               // "health", "combat", "sensor", "harvest", etc.
  std::string id;                                 // Unique identifier within entity
  bool enabled = true;
  std::map<std::string, std::string> params;      // Key-value config (JSON-serializable)
  // Example: combat params: "damage"="25.0", "range"="50.0", "fireRate"="0.5"
};
```

#### `SceneStateMachineDesc`

```cpp
struct SceneStateMachineDesc {
  std::string initial_state = "idle";
  std::vector<SceneStateMachineStateDesc> states;
  std::vector<SceneTransitionRuleDesc> transitions;
};

struct SceneStateMachineStateDesc {
  std::string name;                               // "idle", "patrol", "attack", "flee", etc.
  std::string script;                             // Optional Lua/behavior script
  std::map<std::string, std::string> params;      // State-specific config
  float default_duration = -1.0f;                 // -1 = infinite
};

struct SceneTransitionRuleDesc {
  std::string from_state;
  std::string to_state;
  std::string condition;                          // "on_event:enemy_spotted", "health_below:25", etc.
  float priority = 0.0f;                          // Higher = evaluated first
};
```

#### `SceneGroupDesc`

```cpp
struct SceneGroupDesc {
  std::string id;                                 // Unique group identifier
  std::string name;
  std::string strategy = "flock";                 // "flock", "formation", "swarm", "custom"
  std::vector<std::string> member_entity_ids;     // Entity names
  SceneFlockConfigDesc flock;                     // Flock-specific params
  SceneFormationConfigDesc formation;             // Formation-specific params
};

struct SceneFlockConfigDesc {
  float separation_weight = 1.0f;
  float alignment_weight = 1.0f;
  float cohesion_weight = 1.0f;
  float separation_radius = 5.0f;
  float neighbor_radius = 10.0f;
  float max_speed = 10.0f;
};

struct SceneFormationConfigDesc {
  std::string type = "wedge";                     // "wedge", "line", "column", "circle"
  float spacing = 3.0f;
  float depth_step = 3.0f;
  std::string leader_entity;                      // Leader entity name
};
```

All types use `glz::Object` macros for JSON serialization, following the exact same pattern as existing scene types. Both T8ditor (editor) and SceneTemplate (runtime) deserialize these types via the shared Framework headers.

---

## Architecture Overview

```
┌──────────────────────────────────────────────────────────────────┐
│                      T850 Framework                              │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                  GameLogicSystem (NEW)                      │  │
│  │                                                             │  │
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐              │  │
│  │  │ GameObject │  │  Groups   │  │   AI /    │              │  │
│  │  │ + Comps   │  │ + Forms   │  │  States   │              │  │
│  │  └─────┬─────┘  └─────┬─────┘  └─────┬─────┘              │  │
│  │        │               │               │                    │  │
│  │  ┌─────┴───────────────┴───────────────┴──────┐            │  │
│  │  │            EventBus (decoupled)             │            │  │
│  │  └────────────────────────────────────────────┘            │  │
│  └────────────────────────┬───────────────────────────────────┘  │
│                           │ uses                                 │
│  ┌────────────────────────▼───────────────────────────────────┐  │
│  │                  Existing T850 Systems                      │  │
│  │                                                             │  │
│  │  PrimitiveInst  │  NavigationWorld  │  JoltPhysicsSystem    │  │
│  │  AnimationCtrl  │  InputManager     │  RenderMesh           │  │
│  └─────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

The framework currently uses a **class hierarchy** (`PrimitiveBase` → `RenderMesh` → `RenderSkinnedMesh`) with `PrimitiveInst` as the runtime instance. This proposal adds a **parallel component layer** that wraps `PrimitiveInst` without modifying the existing hierarchy.

---

## Layer 1: GameObject & Components

### GameObject

A lightweight wrapper around `PrimitiveInst` that carries game logic data:

```cpp
struct GameObject {
    PrimitiveInst* instance;                    // Backing render/physics object
    uint32_t id;                                // Unique entity ID
    uint8_t layer;                              // PlayerUnit, EnemyUnit, Building, ResourceNode, etc.
    int16_t teamId;                             // Team/faction affiliation (-1 = neutral)
    std::unordered_map<uint32_t, std::unique_ptr<Component>> components;
    bool enabled;                               // Active/inactive toggle
};
```

### GameObjectRegistry

Central registry for creation, lookup, and spatial queries:

```cpp
class GameObjectRegistry {
public:
    GameObject* Create(PrimitiveInst* inst, const GameObjectDesc& desc);
    void Destroy(uint32_t id);
    GameObject* Get(uint32_t id);

    // Queries
    std::vector<GameObject*> ByLayer(uint8_t layer);
    std::vector<GameObject*> ByTeam(int16_t teamId);
    std::vector<GameObject*> InRadius(const Vec3& pos, float radius);
    std::vector<GameObject*> InBox(const AABB& box);

    // Spatial partitioning (uniform grid or BVH)
    SpatialGrid& GetSpatialGrid();
};
```

### Component Base

```cpp
class Component {
public:
    virtual ~Component() = default;
    virtual uint32_t TypeId() const = 0;
    virtual void Update(GameObject& owner, float dt) = 0;
    virtual void Serialize(nlohmann::json& j) const = 0;
    virtual void Deserialize(const nlohmann::json& j) = 0;
};

// Macro or helper for type ID registration
#define COMPONENT_TYPE_ID(T) static const uint32_tTypeId = RegisterComponentType<T>();
```

### Pre-built Components

| Component | Purpose | Key Data |
|---|---|---|
| `TransformComponent` | Syncs with `PrimitiveInst` transform; smooth interpolation | target position/rotation, lerp speed |
| `HealthComponent` | HP, shields, armor, damage types, regen | current/max HP, armor value, regen rate |
| `StatusEffectComponent` | Buffs, debuffs, DoT, stuns, roots | effect list with durations and tick rates |
| `CombatComponent` | Attack range, damage, cooldown, target acquisition | damage, range, attack speed, projectile type |
| `ResourceComponent` | Minerals, Vespene, cargo (for harvesters) | resource type, current/max capacity |
| `SensorComponent` | Detection radius, FOV, line-of-sight checks | detection radius, FOV angle, stealth visibility |
| `FormationSlotComponent` | Position within a group formation | slot index, offset from leader |

---

## Layer 2: State Machine Framework

### State Machine

Each `GameObject` can have an attached `StateMachine` component:

```cpp
class State {
public:
    virtual ~State() = default;
    virtual std::string_view Name() const = 0;
    virtual void OnEnter(GameObject& owner) = 0;
    virtual std::optional<std::string> Update(GameObject& owner, float dt) = 0; // Returns next state name, or nullopt
    virtual void OnExit(GameObject& owner) = 0;
};

class StateMachine {
public:
    void AddState(std::unique_ptr<State> state);
    void SetInitialState(const std::string& name);
    void Update(GameObject& owner, float dt);  // Handles transitions
    std::string CurrentStateName() const;

private:
    std::unordered_map<std::string, std::unique_ptr<State>> states_;
    std::string currentStateName_;
    State* currentState_ = nullptr;
};
```

### Transition Rules

Transitions can be explicit (state returns next state name) or rule-based:

```cpp
struct TransitionRule {
    std::string fromState;
    std::string toState;
    std::function<bool(GameObject&)> condition;
    float cooldown = 0.0f;          // Min time between transitions
    float elapsedCooldown = 0.0f;
};

// Example:
sm.AddTransition("Idle", "Attack", [](GameObject& g) {
    auto* sensor = g.GetComponent<SensorComponent>();
    return sensor && !sensor->DetectedEnemies().empty();
});
```

### Pre-built States

| State | Description | Exit Condition |
|---|---|---|
| `IdleState` | Wait, patrol waypoints, or hover in place | Enemy detected, move command received |
| `MoveToState` | Navigate to world position via navmesh | Arrived at target, blocked, or interrupted |
| `AttackState` | Face target, attack in range, chase if out of range | Target dead, lost LOS, or retreat ordered |
| `HarvestState` | Move to resource → collect → return to base → deposit | Resource depleted, unit destroyed |
| `BuildState` | Move to build site → play construction animation → spawn building | Build complete, interrupted by attack |
| `FleeState` | Run away from nearest threat, seek cover | Threat gone, health critical (different behavior) |
| `DefendState` | Hold position, engage any threat in detection radius | Position lost, retreat ordered |
| `RepairState` | Move to allied unit/building, play repair animation | Target at full HP, unit reassigned |

### Integration Points

- **`MoveToState`** → uses `NavigationWorld::FindPath()` + Detour path following
- **`AttackState`** → uses `JoltPhysicsSystem` sweep for line-of-sight validation
- **All states** → transition `AnimationController` to matching animation clips (e.g., `AttackState` → attack anim)
- **Character movement** → drives `KinematicCharacterController` velocity input

---

## Layer 3: Group Behavior System

### Group Manager

```cpp
struct Group {
    GroupID id;
    std::vector<uint32_t> unitIds;
    GroupCommand currentCommand;
    FormationType formation = FormationType::Wedge;
    uint32_t leaderId;
    Vec3 commandTarget;              // World position target for group commands
    bool executing = false;
    float completionProgress = 0.0f; // 0.0 to 1.0
};

class GroupManager {
public:
    GroupID CreateGroup(const std::vector<uint32_t>& unitIds);
    void DissolveGroup(GroupID id);
    void AddUnits(GroupID id, const std::vector<uint32_t>& unitIds);
    void RemoveUnits(GroupID id, const std::vector<uint32_t>& unitIds);

    // Commands
    void IssueCommand(GroupID id, const GroupCommand& cmd);
    void Update(float dt);

    // Formation
    void SetFormation(GroupID id, FormationType type);
    std::vector<Vec3> CalculateFormationPositions(const Group& group, const Vec3& center);
};
```

### Group Commands

| Command | Behavior |
|---|---|
| `GroupMove` | Move as formation to target position |
| `GroupAttack` | Each unit acquires nearest enemy in target area |
| `GroupHold` | Hold position, auto-engage threats in sensor range |
| `GroupSpread` | Spread out evenly from center point |
| `GroupGather` | Converge on a point or resource node |
| `GroupRetreat` | Retreat to base/fallback position |
| `GroupPatrol` | Patrol between waypoints as a group |

### Flocking Algorithm (Boids)

For organic group movement and collision avoidance:

```cpp
struct FlockConfig {
    float separationWeight = 1.0f;
    float alignmentWeight = 0.8f;
    float cohesionWeight = 0.6f;
    float avoidanceWeight = 1.5f;       // Navmesh obstacle avoidance
    float neighborRadius = 5.0f;
    float separationRadius = 2.0f;
    float maxSpeed = 10.0f;
    float maxForce = 5.0f;
};

struct FlockBehavior {
    FlockConfig config;

    Vec3 CalculateSteering(const GameObject& agent,
                           const std::span<const GameObject*>& neighbors,
                           const NavigationWorld& nav);

private:
    Vec3 Separation(const GameObject& agent, const std::span<const GameObject*>& neighbors);
    Vec3 Alignment(const GameObject& agent, const std::span<const GameObject*>& neighbors);
    Vec3 Cohesion(const GameObject& agent, const std::span<const GameObject*>& neighbors);
    Vec3 ObstacleAvoidance(const GameObject& agent, const NavigationWorld& nav);
};
```

### Formations

| Formation | Description | Visual |
|---|---|---|
| `Wedge` | Triangle formation, leader at tip | `▲` |
| `Line` | Horizontal line perpendicular to movement | `—` |
| `Column` | Vertical column behind leader | `|` |
| `Box` | Square/rectangular formation | `□` |
| `Circle` | Radial formation around leader | `○` |
| `EchelonRight` | Diagonal line, leader at front-left | `╲` |
| `EchelonLeft` | Diagonal line, leader at front-right | `╱` |
| `Custom` | User-defined offset grid (from `.t8scene` or runtime) | — |

Each formation calculates target positions relative to the group's destination. Each unit's `MoveToState` then navigates to its slot position, with flocking behavior applied for collision avoidance between units.

```cpp
// Formation position calculation
Vec3 CalculateSlotPosition(FormationType type, int slotIndex, int totalUnits,
                           const Vec3& center, const Vec3& direction,
                           float spacing);
```

---

## Layer 4: Event/Message Bus

Type-safe, decoupled event system for component and system communication:

```cpp
class EventBus {
public:
    template<typename T>
    Subscription Subscribe(std::function<void(const T&)> callback);

    template<typename T>
    void Publish(const T& event);

    template<typename T>
    void Unsubscribe(Subscription sub);
};
```

### Event Definitions

```cpp
// Combat events
struct UnitDied { uint32_t killerId; uint32_t victimId; DamageType damageType; };
struct UnitDamaged { uint32_t unitId; float amount; uint32_t sourceId; };
struct UnitHealed { uint32_t unitId; float amount; uint32_t sourceId; };
struct AttackStarted { uint32_t attackerId; uint32_t targetId; };
struct AttackCompleted { uint32_t attackerId; uint32_t targetId; float damageDealt; };

// Selection & command events
struct UnitSelected { std::vector<uint32_t> unitIds; };
struct UnitDeselected { std::vector<uint32_t> unitIds; };
struct CommandIssued { uint32_t unitId; CommandType type; Vec3 target; };

// Resource events
struct ResourceCollected { uint32_t unitId; ResourceType type; float amount; };
struct ResourceDeposited { uint32_t unitId; ResourceType type; float amount; };

// Building events
struct ConstructionStarted { uint32_t builderId; uint32_t buildingId; };
struct ConstructionCompleted { uint32_t buildingId; };
struct ConstructionCancelled { uint32_t buildingId; };

// AI & behavior events
struct StateChanged { uint32_t unitId; std::string fromState; std::string toState; };
struct PathBlocked { uint32_t unitId; Vec3 blockagePosition; };
struct PathFound { uint32_t unitId; float pathLength; };

// Wave & spawning events
struct WaveSpawned { int waveId; int unitCount; };
struct UnitSpawned { uint32_t unitId; Vec3 position; int16_t teamId; };
struct UnitDespawned { uint32_t unitId; };

// Group events
struct GroupCreated { GroupID groupId; std::vector<uint32_t> unitIds; };
struct GroupDissolved { GroupID groupId; };
struct GroupCommandCompleted { GroupID groupId; CommandType command; };
```

---

## Layer 5: GameLogicSystem Orchestration

The top-level system that ties all layers together and integrates with `SceneBase`:

```cpp
class GameLogicSystem : public std::enable_shared_from_this<GameLogicSystem> {
public:
    // Initialization
    void Initialize(EngineContext& ctx, SceneBase& scene);
    void Shutdown();

    // Scene integration — called when .t8scene loads
    void OnLoadScene(const nlohmann::json& gameLogicData);

    // Update cycle — called from SceneBase::OnUpdate
    void Update(float dt);

    // GameObject management
    GameObject* CreateGameObject(PrimitiveInst* instance, const GameObjectDesc& desc);
    void DestroyGameObject(uint32_t id);
    GameObject* GetGameObject(uint32_t id);
    const GameObjectRegistry& GetRegistry() const;

    // Queries
    std::vector<GameObject*> GetInRadius(const Vec3& pos, float radius);
    std::vector<GameObject*> getByLayer(uint8_t layer);
    std::vector<GameObject*> GetByTeam(int16_t teamId);

    // Group management
    GroupID CreateGroup(const std::vector<uint32_t>& unitIds);
    void IssueGroupCommand(GroupID groupId, const GroupCommand& cmd);
    const GroupManager& GetGroupManager() const;

    // Event bus access
    template<typename T> auto EventBusSubscribe(std::function<void(const T&)> cb);
    template<typename T> void EventBusPublish(const T& event);

    // Spatial grid
    SpatialGrid& GetSpatialGrid();

    // AI systems (pluggable)
    void RegisterAISystem(std::unique_ptr<IAISystem> system);

private:
    EngineContext* engineContext_ = nullptr;
    GameObjectRegistry registry_;
    GroupManager groupManager_;
    EventBus eventBus_;
    SpatialGrid spatialGrid_;
    std::vector<std::unique_ptr<IAISystem>> aiSystems_;
};
```

### Update Cycle Integration

```
SceneBase::OnUpdate(dt):
  1. CameraController::Update()          ← existing
  2. Animation updates                    ← existing
  3. NavMesh agent updates                ← existing (enhanced by GameLogic)
  4. GameLogicSystem::Update(dt)          ← NEW
     ├── SpatialGrid::Update()            (dirty rectangle updates)
     ├── EventBus::ProcessQueued()        (deferred event dispatch)
     ├── GameObject component updates     (per-object, ordered by priority)
     │   ├── TransformComponent           (sync/interpolate)
     │   ├── StateMachine                 (behavior logic)
     │   ├── HealthComponent              (regen, DoT ticks)
     │   ├── StatusEffectComponent        (expiry, ticks)
     │   └── SensorComponent              (detection sweep)
     ├── GroupManager::Update()           (formation updates, command progress)
     └── AISystem updates                 (pluggable strategy layer)
  5. Input handling                        ← existing (enhanced with selection)
     └── SelectionSystem::Update()        ← NEW (box/ring/command input)
```

---

## Integration With Existing T850 Systems

### Integration Map

| T850 System | Integration Point | Mechanism |
|---|---|---|
| `PrimitiveInst` | `GameObject` wraps it | `GameObject::instance` pointer; new `AttachGameLogic()` method on `PrimitiveInst` |
| `NavigationWorld` | `MoveToState` pathfinding | `FindPath()` for path generation; Detour for path following |
| `JoltPhysicsSystem` | Collision queries | Line-of-sight via body sweep; unit placement validation via point/shape query |
| `AnimationController` | State → animation mapping | Each state declares required animation clip; `AnimationController::Play()` on transition |
| `KinematicCharacterController` | Character movement | State machine produces velocity/direction input for the controller |
| `InputManager` | Selection & commands | Right-click for move/attack commands; box/ring selection; hotkey group commands |
| `.t8scene` | Game logic authoring | `"gameLogic"` JSON section with entity definitions, components, behaviors |
| `RenderMesh` CBuffer | Team color, selection highlight | Per-instance color tint via existing instance constant buffer; selection glow via material override |
| `ImGui` | Debug & inspection panels | `GameDebugPanels` module with unit inspector, state viewer, group manager |
| `ResourceLocator` | AI config, behavior data | Load behavior configs, formation presets, and AI strategy files |

### PrimitiveInst Extension

Minimal, non-breaking addition to `PrimitiveInst`:

```cpp
// In PrimitiveInstance.h
struct PrimitiveInst {
    // ... existing members ...

    uint32_t gameObjectId = INVALID_GAME_OBJECT_ID;  // Link to GameObject
};
```

---

## NavMesh & AI Integration Details

### Path Following with Detour

```
State: MoveToState
  │
  ├─ NavigationWorld::FindPath(currentPos, targetPos)
  │   → Returns Detour path (polygon refs + corner points)
  │
  ├─ PathFollower::Update(dt)
  │   ├─ Compute desired velocity toward next corner point
  │   ├─ FlockBehavior::CalculateSteering() (separation from nearby units)
  │   ├─ Clamp to max speed and max force
  │   └─ KinematicCharacterController::Move(desiredVelocity)
  │
  ├─ Path validation each frame
  │   ├─ Check if current polygon is still valid
  │   ├─ Re-path if blocked (dynamic obstacle)
  │   └─ Emit PathBlocked event if re-path fails
  │
  └─ Arrival check
      ├─ Distance to target < arrivalThreshold
      ├─ Emit Arrived event
      └─ Transition to next state (e.g., Idle or Attack)
```

### Group Movement with Detour

- Each unit in a formation gets its own path, offset from the formation center point
- `FlockBehavior::separationWeight` prevents unit overlap during movement
- Formation target positions are recalculated each frame based on group velocity and direction
- If a unit's path is blocked, it requests a re-path with a slight position offset (jitter) to find an alternate route
- `DetourCrowd` can be used for built-in crowd simulation, or custom flocking can be layered on top

### Line of Sight (Combat)

```cpp
// In AttackState or SensorComponent
bool CheckLineOfSight(const Vec3& from, const Vec3& to, JoltPhysicsSystem& physics) {
    // Use Jolt's ShapeCast with a thin capsule/box from 'from' to 'to'
    PhysicsSettings::ShapeCastSettings settings;
    settings.m_returnFirstHit = true;

    // Thin capsule for LOS ray
    Ref<Shape> losShape = new CapsuleShape(CapsuleShape::Settings(0.1f, 0.1f));

    Vector3 direction = (to - from).Normalized();
    Vector3 displacement = direction * distance;

    ShapeCastResult result;
    physics.GetSystem()->ShapeCast(losShape, from, from + displacement,
                                    Quaternion::sIdentity(), settings, result);

    return result.mFraction >= 0.95f;  // Reached target without obstruction
}
```

---

## Scene Format Extension

### Schema Anchor: `EditorSceneFile` (Framework Layer)

Game logic scene data is serialized through the existing `EditorSceneFile` schema at `Framework/include/scene/EditorSceneFile.h`. Game logic types are added as fields on `SceneGameEntityDesc` and as new top-level vectors, following the exact same pattern as `SceneObjectDesc`, `SceneCameraDesc`, `SceneLightDesc`, etc.

Both T8ditor (editor) and SceneTemplate (runtime) deserialize these types via the shared Framework headers using Glaze JSON serialization.

### `.t8scene` JSON Extension

```json
{
  "// ... existing scene data ...": "",

  "game_entities": [
    {
      "name": "marine_001",
      "kind": "Unit",
      "mesh_object": "marine_model",
      "primary_physics_entity": "marine_physics_001",
      "physics_entities": ["marine_physics_001"],
      "camera": "",
      "ragdoll_object": "marine_ragdoll",
      "ai": "",
      "visible": true,
      "frozen": false,
      "show_wire": true,

      "// ── Game logic extensions (SHARED FRAMEWORK TYPES) ──": "",
      "group_id": "squad_alpha",
      "components": [
        {
          "type": "health",
          "id": "health_main",
          "enabled": true,
          "params": { "maxHp": "60", "currentHp": "60", "armor": "1", "regenRate": "0.0" }
        },
        {
          "type": "combat",
          "id": "combat_main",
          "enabled": true,
          "params": { "damage": "6", "range": "12.0", "attackSpeed": "0.5", "damageType": "Ballistic", "projectileSpeed": "80.0" }
        },
        {
          "type": "sensor",
          "id": "sensor_main",
          "enabled": true,
          "params": { "detectionRadius": "20.0", "fovAngle": "180.0", "canDetectStealth": "false" }
        }
      ],
      "behavior": {
        "initial_state": "idle",
        "states": [
          { "name": "idle", "params": {}, "default_duration": -1.0 },
          { "name": "patrol", "params": { "patrolSpeed": "5.0" }, "default_duration": -1.0 },
          { "name": "attack", "params": {}, "default_duration": -1.0 },
          { "name": "flee", "params": {}, "default_duration": -1.0 }
        ],
        "transitions": [
          { "from_state": "idle", "to_state": "attack", "condition": "on_event:enemy_spotted", "priority": 10.0 },
          { "from_state": "attack", "to_state": "idle", "condition": "target_dead", "priority": 10.0 },
          { "from_state": "idle", "to_state": "patrol", "condition": "on_command:move", "priority": 5.0 },
          { "from_state": "*", "to_state": "flee", "condition": "health_below:20", "priority": 20.0 }
        ]
      }
    },
    {
      "name": "command_center",
      "kind": "Building",
      "mesh_object": "cc_model",
      "primary_physics_entity": "cc_physics",
      "group_id": "",
      "components": [
        { "type": "health", "id": "health_main", "params": { "maxHp": "1500", "currentHp": "1500", "armor": "3" } },
        { "type": "production", "id": "prod_main", "params": { "queueSize": "3" } }
      ],
      "behavior": {
        "initial_state": "produce",
        "states": [
          { "name": "idle", "params": {} },
          { "name": "produce", "params": { "buildTime": "30.0" } }
        ],
        "transitions": []
      }
    }
  ],

  "game_groups": [
    {
      "id": "squad_alpha",
      "name": "Squad Alpha",
      "strategy": "formation",
      "member_entity_ids": ["marine_001", "marine_002", "marine_003"],
      "flock": {
        "separation_weight": 1.0,
        "alignment_weight": 0.8,
        "cohesion_weight": 0.6,
        "separation_radius": 2.0,
        "neighbor_radius": 5.0,
        "max_speed": 10.0
      },
      "formation": {
        "type": "wedge",
        "spacing": 3.0,
        "depth_step": 3.0,
        "leader_entity": "marine_001"
      }
    },
    {
      "id": "squad_bravo",
      "name": "Squad Bravo",
      "strategy": "flock",
      "member_entity_ids": ["marine_004", "marine_005"],
      "flock": {
        "separation_weight": 1.5,
        "alignment_weight": 1.0,
        "cohesion_weight": 0.5,
        "separation_radius": 3.0,
        "neighbor_radius": 8.0,
        "max_speed": 12.0
      },
      "formation": { "type": "line", "spacing": 2.0, "depth_step": 2.0, "leader_entity": "" }
    }
  ],

  "game_logic_settings": {
    "spatial_grid": {
      "cell_size": 4.0,
      "grid_dimensions": [256, 256]
    },
    "default_flock_config": {
      "separation_weight": 1.0,
      "alignment_weight": 0.8,
      "cohesion_weight": 0.6,
      "neighbor_radius": 5.0,
      "separation_radius": 2.0
    }
  }
}
```

### Schema Mapping (JSON → Framework Types)

| JSON Path | Framework Type | Namespace |
|---|---|---|
| `game_entities[]` | `std::vector<SceneGameEntityDesc>` | `t850::scene` |
| `game_entities[].components[]` | `std::vector<SceneComponentDesc>` | `t850::scene` |
| `game_entities[].behavior` | `SceneStateMachineDesc` | `t850::scene` |
| `game_entities[].behavior.states[]` | `std::vector<SceneStateMachineStateDesc>` | `t850::scene` |
| `game_entities[].behavior.transitions[]` | `std::vector<SceneTransitionRuleDesc>` | `t850::scene` |
| `game_groups[]` | `std::vector<SceneGroupDesc>` | `t850::scene` |
| `game_groups[].flock` | `SceneFlockConfigDesc` | `t850::scene` |
| `game_groups[].formation` | `SceneFormationConfigDesc` | `t850::scene` |
| `game_logic_settings` | `SceneGameLogicSettingsDesc` | `t850::scene` |

All types use `glz::Object` macros for JSON serialization, matching the existing `EditorSceneFile` pattern.

### Scene Loading Integration

```cpp
// In SceneTemplate::OnLoadScene → GameLogicSystem::OnLoadScene()
// Uses SHARED framework types from Framework/include/scene/EditorSceneFile.h
void GameLogicSystem::OnLoadScene(const t850::scene::EditorSceneFile& sceneFile) {
    // Configure spatial grid from global settings
    if (sceneFile.game_logic_settings.has_value()) {
        const auto& settings = sceneFile.game_logic_settings.value();
        if (settings.spatial_grid.has_value())
            ConfigureSpatialGrid(settings.spatial_grid.value());
    }

    // Create game objects from scene game_entities (shared vector<SceneGameEntityDesc>)
    for (const auto& entityDesc : sceneFile.game_entities) {
        PrimitiveInst* inst = FindPrimitiveInstByName(entityDesc.name);
        if (!inst) continue;

        GameObjectDesc goDesc;
        goDesc.id = AllocateObjectId();
        goDesc.name = entityDesc.name;
        goDesc.layer = ParseLayer(entityDesc.kind);  // "Unit", "Building", etc.

        // Deserialize components from shared SceneComponentDesc vector
        for (const auto& compDesc : entityDesc.components) {
            if (!compDesc.enabled) continue;
            IComponent* comp = CreateComponentFromType(compDesc.type, compDesc.params);
            if (comp) goDesc.components.push_back(comp);
        }

        // Deserialize state machine from shared SceneStateMachineDesc
        if (entityDesc.behavior.has_value()) {
            const auto& behavior = entityDesc.behavior.value();
            StateMachine* sm = StateMachine::Create(behavior.states, behavior.transitions);
            sm->SetInitialState(behavior.initial_state);
            goDesc.stateMachine = sm;
        }

        auto* go = registry_.Create(inst, goDesc);
        inst->gameObjectId = goDesc.id;

        // Track group membership for later group creation
        if (!entityDesc.group_id.empty()) {
            groupPendingMembers_[entityDesc.group_id].push_back(goDesc.id);
        }
    }

    // Create groups from scene game_groups (shared vector<SceneGroupDesc>)
    for (const auto& groupDesc : sceneFile.game_groups) {
        std::vector<uint32_t> members = groupPendingMembers_[groupDesc.id];
        GroupConfig config;
        config.strategy = groupDesc.strategy;
        if (groupDesc.flock.has_value())
            config.flock = groupDesc.flock.value();
        if (groupDesc.formation.has_value())
            config.formation = groupDesc.formation.value();
        GroupID gid = groupManager_.CreateGroup(members, config);
        groupDescToId_[groupDesc.id] = gid;
    }
}
```

### Type Flow Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                    Scene Save/Load Flow                          │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  [Editor]                    [Framework]                [Runtime]│
│                                                                  │
│  EditorWorld                EditorSceneFile.h        GameLogicSystem
│  └─ gameEntities ────────►  game_entities ────────►  GameObjectRegistry
│  └─ gameGroups ─────────►  game_groups ─────────►  GroupManager
│                                                                  │
│  T8ditor/EditorScene.cpp    Framework/scene/           Framework/game/
│  BuildEditorSceneSnapshot()  EditorSceneFile.h         GameLogicSystem.h
│  SaveEditorSceneFile()       (Glaze JSON)              GameObject.h
│                              LoadEditorSceneFile()     StateMachine.h
│                              ─┬────────────────────►  OnLoadScene() │
│                               │                                  │
│                          .t8scene JSON                        Runtime
│                     (single shared format)              GameObject instances
└──────────────────────────────────────────────────────────────────┘
```

---

## ImGui Debug Panels

### Panel Overview

| Panel | Content |
|---|---|
| **GameObjects** | List of all GameObjects with search/filter; filter by layer or team; click to select and inspect; shows ID, layer, team, component count, current state |
| **Inspector** | Detailed view of selected GameObject: all component values (editable), current state machine state, group membership, health bar, sensor visualization toggle |
| **State Machine** | Visual state graph (nodes + edges), current state highlighted, transition log (timestamped), ability to force state transitions |
| **Groups** | Group list with member count, current command display, formation type selector (editable), command progress bar, member unit list |
| **Events** | Recent event log (last N events, configurable), filter by event type, timestamp and source display |
| **Spatial Grid** | Grid visualization overlay toggle, cell occupancy heatmap, query radius visualization |
| **AI Config** | Flock weight sliders (separation, alignment, cohesion, avoidance), formation spacing controls, state transition timing, neighbor radius |
| **Selection** | Current selection display, group assignment buttons, command quick-panel (move, attack, hold, spread) |

### Integration with Existing ImGui System

Panels would integrate with the existing `FrameworkImGui` docking system:

```cpp
// In GameDebugPanels.h
class GameDebugPanels {
public:
    void Initialize(GameLogicSystem& gls);
    void Render();  // Called from EditorApp or DayScene ImGui loop

    // Individual panel renderers
    void RenderGameObjectPanel();
    void RenderInspectorPanel();
    void RenderStateMachinePanel();
    void RenderGroupPanel();
    void RenderEventPanel();
    void RenderSpatialGridPanel();
    void RenderAIConfigPanel();
    void RenderSelectionPanel();

private:
    GameLogicSystem* gameLogicSystem_ = nullptr;
    uint32_t selectedObjectId_ = INVALID_GAME_OBJECT_ID;
    // Panel state, filters, etc.
};
```

---

## Proposed File Structure

```
T850/
├── Framework/
│   ├── include/
│   │   ├── scene/                             # SHARED scene schema (used by Editor + Runtime)
│   │   │   ├── EditorSceneFile.h              # Existing: SceneGameEntityDesc (extended)
│   │   │   │   # Extensions added to this file:
│   │   │   │   #   struct SceneComponentDesc
│   │   │   │   #   struct SceneStateMachineStateDesc
│   │   │   │   #   struct SceneTransitionRuleDesc
│   │   │   │   #   struct SceneStateMachineDesc
│   │   │   │   #   struct SceneFlockConfigDesc
│   │   │   │   #   struct SceneFormationConfigDesc
│   │   │   │   #   struct SceneGroupDesc
│   │   │   │   #   struct SceneGameLogicSettingsDesc
│   │   │   │   #   SceneGameEntityDesc gains:
│   │   │   │   #     std::string group_id
│   │   │   │   #     std::vector<SceneComponentDesc> components
│   │   │   │   #     std::optional<SceneStateMachineDesc> behavior
│   │   │   │   #   └── game/                              # Game logic RUNTIME (Framework core)
│   │   │   │   ├── GameLogicSystem.h              # Top-level orchestrator
│   │   │   │   ├── GameObject.h                   # Entity wrapper struct
│   │   │   │   ├── GameObjectRegistry.h           # Create / query / destroy
│   │   │   │   ├── Component.h                    # Abstract component base
│   │   │   │   ├── Components/
│   │   │   │   │   ├── TransformComponent.h
│   │   │   │   │   ├── HealthComponent.h
│   │   │   │   │   ├── CombatComponent.h
│   │   │   │   │   ├── StatusEffectComponent.h
│   │   │   │   │   ├── ResourceComponent.h
│   │   │   │   │   ├── SensorComponent.h
│   │   │   │   └── FormationSlotComponent.h
│   │   │   ├── AI/
│   │   │   │   ├── StateMachine.h             # Generic state machine
│   │   │   │   ├── IState.h                   # State interface
│   │   │   │   ├── States/
│   │   │   │   │   ├── IdleState.h
│   │   │   │   │   ├── MoveToState.h
│   │   │   │   │   ├── AttackState.h
│   │   │   │   │   ├── HarvestState.h
│   │   │   │   │   ├── BuildState.h
│   │   │   │   │   ├── FleeState.h
│   │   │   │   │   ├── DefendState.h
│   │   │   │   │   └── RepairState.h
│   │   │   │   ├── Flocking.h                 # Boids algorithm
│   │   │   │   ├── PathFollower.h             # Detour path following
│   │   │   │   └── AISystem.h                 # Pluggable AI strategy interface
│   │   │   ├── Groups/
│   │   │   │   ├── GroupManager.h             # Group creation and commands
│   │   │   │   ├── Group.h                    # Group data structure
│   │   │   │   ├── GroupCommand.h             # Command type enum and struct
│   │   │   │   └── Formations.h               # Formation types and calculation
│   │   │   ├── EventBus.h                     # Type-safe event system
│   │   │   ├── Events.h                       # Event type definitions
│   │   │   ├── SpatialGrid.h                  # Uniform grid spatial partitioning
│   │   │   └── Selection.h                    # Unit selection (box / ring / lasso)
│   │
│   ├── src/
│   │   └── game/                              # Game logic runtime implementations
│   │       ├── GameLogicSystem.cpp
│   │       ├── GameObjectRegistry.cpp
│   │       ├── Components/
│   │       │   ├── TransformComponent.cpp
│   │       │   ├── HealthComponent.cpp
│   │       │   ├── CombatComponent.cpp
│   │       │   ├── StatusEffectComponent.cpp
│   │       │   ├── ResourceComponent.cpp
│   │       │   └── SensorComponent.cpp
│   │       ├── AI/
│   │       │   ├── StateMachine.cpp
│   │       │   ├── States/
│   │       │   │   ├── IdleState.cpp
│   │       │   │   ├── MoveToState.cpp
│   │       │   │   ├── AttackState.cpp
│   │       │   │   ├── HarvestState.cpp
│   │       │   │   ├── BuildState.cpp
│   │       │   │   ├── FleeState.cpp
│   │       │   │   ├── DefendState.cpp
│   │       │   │   └── RepairState.cpp
│   │       │   ├── Flocking.cpp
│   │       │   └── PathFollower.cpp
│   │       ├── Groups/
│   │       │   ├── GroupManager.cpp
│   │       │   └── Formations.cpp
│   │       ├── EventBus.cpp
│   │       ├── SpatialGrid.cpp
│   │       └── Selection.cpp
│   │
│   └── FrameworkImGui/
│       ├── GameDebugPanels.h                  # ImGui debug panels (shared types)
│       └── GameDebugPanels.cpp
│
│   # Build: MSBuild (.vcxproj) — Framework.vcxproj updated with game/ sources
│
├── T8ditor/
│   ├── EditorWorld.h                                # Editor world state (uses framework scene types)
│   │   # Extensions:
│   │   #   std::vector<t850::scene::SceneGroupDesc> gameGroups;  (NEW)
│   │   #   SelectionRef gains: type 9=gameEntity, 10=gameGroup
│   │
│   ├── EditorScene.cpp                              # Save/Load integration
│   │   # Extensions:
│   │   #   BuildEditorSceneSnapshot() — write gameGroups + game_logic_settings
│   │   #   ApplyEditorUndoState() — restore gameGroups from undo
│   │
│   ├── EditorImGui.cpp                              # Menu/Toolbar/Menu callbacks
│   │   # Extensions:
│   │   #   File → New Game Entity / New Game Group
│   │   #   Edit → Add Component / Remove Component
│   │   #   View → Show Game Logic Panels / Show Component Overlays
│   │
│   ├── HierarchyPanel.cpp                           # Object tree panel
│   │   # Extensions:
│   │   #   Game Entity tree nodes (icon, name, component count, state)
│   │   #   Game Group tree nodes (icon, name, member count, formation)
│   │   #   Context menu: Add Component, Add to Group, Set State
│   │
│   ├── InspectorPanel.cpp                           # Property editor
│   │   # Extensions:
│   │   #   Component editor (type selector, params table, enabled toggle)
│   │   #   State Machine editor (states list, transitions table)
│   │   #   Group membership editor (add/remove entities)
│   │   #   Behavior config (initial state, state params)
│   │
│   ├── UndoSystem.cpp                               # Undo/Redo commands
│   │   # New commands:
│   │   #   AddComponentCommand, RemoveComponentCommand
│   │   #   ChangeStateCommand, AddToGroupCommand, RemoveFromGroupCommand
│   │
│   ├── HostedViewport.cpp                           # Viewport rendering
│   │   # Extensions:
│   │   #   Component overlay rendering (sensor radius, health bars)
│   │   #   Group formation lines
│   │   #   State machine state labels
│   │
│   ├── PlayScenePanel.cpp                           # Play scene runtime
│   │   # Extensions:
│   │   #   StartPlay() → EditorSceneFile → SceneTemplate → GameLogicSystem::OnLoadScene()
│   │   #   StopPlay() → cleanup GameObjects, restore editor state
│   │
│   └── GameLogicEditorPanel.cpp                     # NEW: Game logic specific ImGui panels
│       #   State Machine visualizer (graph view)
│       #   Group manager panel
│       #   Event log viewer
│
├── DayScene/
│   └── GameLogicDemo.cpp                            # Demo scene with RTS-like elements
│
├── Assets/
│   └── Scenes/
│       └── GameLogicDemo.t8scene                    # Demo scene using EditorSceneFile schema
│
└── documentation/
    └── legacy/
        └── proposals/
            ├── game-logic-and-ai-integration.md     # This document (Framework focus)
            └── editor-game-logic-integration.md     # Editor integration proposal
