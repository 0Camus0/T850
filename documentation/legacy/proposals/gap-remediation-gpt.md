# Game Engine Gap Remediation Suggestions

Date: 2026-06-29
Author: Copilot / GPT remediation plan

Companion documents:

- `documentation/legacy/proposals/game-logic-and-ai-integration.md`
- `documentation/legacy/proposals/editor-game-logic-integration.md`
- `documentation/legacy/proposals/assessment.md`
- `documentation/legacy/proposals/assessment-gpt.md`

## Purpose

This document converts the proposal gaps into concrete suggestions. It is not another high-level critique. It is an implementation-oriented remediation guide for turning T850 from a rendering-focused engine with editor/runtime scene loading into a broader game engine with gameplay simulation, authoring, debugging, and validation.

The recommendations are grounded in current T850 architecture:

- `.t8scene` uses `t850::scene::EditorSceneFile` as the shared editor/runtime schema.
- T8ditor already owns `EditorWorld::gameEntities` and saves `game_entities`.
- SceneTemplate currently loads objects, physics, navigation, splines, cameras, lights, profiles, God Rays, and ragdolls.
- No `Framework/game` runtime module exists yet.
- Current `SceneGameEntityDesc` is metadata only: name, kind, mesh link, physics links, camera, ragdoll, `ai`, visibility/frozen/wire flags.

The main principle: build a narrow runtime-first vertical slice before building the full editor UX.

## Priority Map

| Priority | Gap | Main Recommendation |
|---|---|---|
| P0 | Runtime ownership and tick model | Make `GameLogicSystem` SceneTemplate-owned first, with fixed gameplay ticks. |
| P0 | Stable identity | Add stable serialized IDs before groups, components, events, or state machines. |
| P0 | Runtime consumption of `game_entities` | Create runtime game objects from existing scene records before adding advanced systems. |
| P0 | Component lifecycle | Define attach/create/update/destroy phases and deferred mutation rules. |
| P0 | Event ordering | Use a queued per-tick event bus, not immediate recursive dispatch. |
| P0 | Validation and schema migration | Add explicit validation and version/default migration for game-logic data. |
| P0 | Physics/nav write boundaries | Centralize physics/nav commands and queries; do not let arbitrary components mutate engine state. |
| P1 | Play Scene iteration | Keep fidelity temp-scene export, add fast in-memory play mode later. |
| P1 | Component data model | Keep simple params for v1, add optional `config_json` and typed factories. |
| P1 | State machine semantics | Compile transition conditions after load; avoid string evaluation per frame. |
| P1 | Editor rollout | Ship identity/link/component basics before graph editors and RTS panels. |
| P1 | Testing | Add schema, registry, event, state-machine, and Play Scene lifecycle tests. |
| P1 | Telemetry and performance | Use existing `RuntimeTelemetry` and `Profiler` from the first runtime slice. |
| P1 | Build hygiene | Update primary `.vcxproj` and secondary CMake when adding new files. |
| P2 | RTS-specific features | Move flocking/formations/combat/resource examples out of core. |
| P2 | Visual graph editor | Build after runtime state-machine semantics are stable. |
| P2 | Overlays and polish | Use existing line/text/debug overlay systems. |

## Implementation Principles

1. Runtime semantics first, editor polish second.

   A polished editor for unstable gameplay semantics will churn. Start by proving that SceneTemplate can load, update, inspect, and unload runtime game objects created from `.t8scene` data.

2. Stable IDs before references.

   Gameplay references should not depend on display names or editor vector indices. Add IDs early so rename, duplicate, reorder, undo, and group membership work.

3. Fixed tick for gameplay.

   Rendering can stay variable timestep. Gameplay state machines, timers, cooldowns, events, and deterministic tests should use a fixed tick accumulator.

4. Explicit phase ordering.

   Components, events, state machines, physics commands, nav requests, and entity destruction need known phases. Avoid mutation during iteration.

5. Validation is a runtime and editor concern.

   Validation should live in Framework or shared scene/game code, not only in T8ditor UI. SceneTemplate should report invalid game-logic data too.

6. Game-engine core should stay genre-neutral.

   RTS systems are useful demos, but they should not define the core runtime abstractions.

7. Use existing infrastructure.

   Use `ResourceLocator`, `RuntimeTelemetry`, `Profiler`, `DevGuiContext`, `LineRenderer`, `TextRenderer`, `EditorSceneFile`, `EditorWorld`, and Play Scene rather than adding parallel systems.

## P0 Gap 1: Runtime Ownership And Fixed Tick Model

### Current Gap

The proposals do not precisely say who owns `GameLogicSystem`, when it updates, how it interacts with SceneTemplate, physics, navigation, animation, and rendering, or whether gameplay uses fixed or variable timestep.

Without this, event ordering, state-machine timers, component updates, physics writes, and tests will become inconsistent.

### Suggested Solution

Start with `GameLogicSystem` owned by `SceneTemplate` only. Do not make it a global `EngineContext` service in the first milestone.

Initial ownership:

```cpp
class SceneTemplate : public SceneBase {
  ...
  t850::game::GameLogicSystem m_gameLogic;
};
```

Initial lifecycle:

```text
SceneTemplate::LoadEditorSceneAssets(scenePath)
  -> load EditorSceneFile
  -> load render objects / physics / nav / cameras / lights
  -> m_gameLogic.LoadFromScene(scene, runtime links)

SceneTemplate::OnUpdate(dt)
  -> update input/camera/navigation helpers as currently done
  -> m_gameLogic.Update(dt)
  -> apply game output to PrimitiveInst/physics/nav commands at known phases

SceneTemplate::OnDestroyScene or unload
  -> m_gameLogic.Shutdown()
```

Use an internal fixed timestep:

```cpp
struct GameLogicSettings {
  float fixedDeltaSeconds = 1.0f / 60.0f;
  int maxStepsPerFrame = 4;
};

void GameLogicSystem::Update(float deltaSeconds) {
  accumulator_ += std::min(deltaSeconds, maxFrameDeltaSeconds_);
  int steps = 0;
  while (accumulator_ >= fixedDeltaSeconds_ && steps < maxStepsPerFrame_) {
    Tick(fixedDeltaSeconds_);
    accumulator_ -= fixedDeltaSeconds_;
    ++steps;
  }
}
```

### Recommended Tick Phases

Use deterministic phases from the beginning:

```text
Tick(dt)
  1. BeginFrameStats
  2. ApplyDeferredEntityCreates
  3. DispatchQueuedEventsFromPreviousTick
  4. UpdateComponents_PrePhysics
  5. FlushPhysicsCommands
  6. UpdateComponents_PostPhysicsOrQueries
  7. ResolveNavRequestsCompletedThisTick
  8. EvaluateStateMachines
  9. UpdateGroupsOrHighLevelSystems
 10. DispatchOrQueueEventsForNextTick
 11. ApplyDeferredEntityDestroys
 12. EndFrameStats
```

For v1, several phases can be empty. The important part is that the order exists and is documented.

### Affected Files

- Add `T850/Framework/include/game/GameLogicSystem.h`
- Add `T850/Framework/src/game/GameLogicSystem.cpp`
- Add `T850/Framework/include/game/GameObject.h`
- Add `T850/Framework/include/game/GameObjectRegistry.h`
- Add `T850/Framework/src/game/GameObjectRegistry.cpp`
- Update `T850/DayScene/SceneTemplate.h` and `.cpp`
- Update `T850/Framework/Framework.vcxproj`
- Update `T850/Framework/CMakeLists.txt` if keeping CMake aligned

### Acceptance Criteria

- SceneTemplate owns and initializes a `GameLogicSystem`.
- `GameLogicSystem::LoadFromScene()` is called once per loaded `.t8scene`.
- `GameLogicSystem::Shutdown()` clears all runtime objects and subscriptions.
- Fixed tick accumulator is active and capped.
- Runtime DevGui can show fixed tick count, accumulator, object count, and component count.

### Tests

- Load a scene with zero game entities: no crash, registry empty.
- Load a scene with one game entity: registry count is one.
- Call update with `dt = 1/60`: one tick runs.
- Call update with a large `dt`: no more than `maxStepsPerFrame` ticks run.
- Unload scene: registry count returns to zero.

## P0 Gap 2: Stable Identity And Reference Model

### Current Gap

Current scene references use names and editor vector indices in many places. This is acceptable for early render authoring, but fragile for gameplay.

Problems:

- Names can be duplicated.
- Names can be edited by the user.
- Editor vectors reorder during load, undo, delete, or import.
- Group membership by display name breaks on rename.
- Event targets need stable identity.

### Suggested Solution

Add stable serialized IDs to gameplay records.

Use strings for IDs to avoid adding a binary UUID dependency. Format can be simple at first:

```text
ge_2b7c1f2e8a4b4e9a9a0db2385c43c071
comp_9ab41c5d8d4c4f1e9adf5f6c0efc3d8e
group_a5c44e2e8b9e4edb85ac31ad7f9d4d2a
```

Schema sketch:

```cpp
struct SceneComponentDesc {
  std::string id;
  std::string type;
  bool enabled = true;
  std::map<std::string, std::string> params;
  std::string config_json;
};

struct SceneGameEntityDesc {
  std::string id;
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
  std::vector<SceneComponentDesc> components;
  std::optional<SceneStateMachineDesc> behavior;
};

struct SceneGroupDesc {
  std::string id;
  std::string name;
  std::vector<std::string> member_entity_ids;
};
```

### ID Generation Rules

- Generate ID when an entity/component/group is created in T8ditor.
- Preserve ID on rename.
- Preserve ID on save/load.
- Duplicate entity should create a new entity ID and new component IDs.
- Copy/paste should create new IDs unless explicitly importing a prefab with stable identity semantics.
- Missing IDs in old scenes should be generated during migration/defaulting and marked dirty.

### Compatibility Strategy

Older scenes have no IDs. On load:

```text
if scene.version < GameLogicSchemaVersionWithIds:
  for each game entity without id:
    generate stable id
  for each component without id:
    generate stable id
```

Do not require users to manually edit old scenes.

### Acceptance Criteria

- Every saved game entity has a non-empty `id`.
- Every saved component has a non-empty `id`.
- Rename does not change IDs.
- Duplicate creates new IDs.
- Validation reports duplicate IDs.
- Runtime registry can look up by stable ID and runtime ID.

### Tests

- Load old scene without IDs, save, reload: IDs persist.
- Rename entity, save, reload: ID unchanged.
- Duplicate entity: original and clone IDs differ.
- Scene with duplicated ID: validation reports error.

## P0 Gap 3: Minimal Runtime Consumption Of `game_entities`

### Current Gap

T8ditor saves `game_entities`, but SceneTemplate does not currently appear to create runtime game objects from them.

### Suggested Solution

Create a minimal runtime registry before adding complex gameplay behavior.

Runtime data sketch:

```cpp
using RuntimeGameObjectId = uint32_t;

struct GameObjectLinkRefs {
  int meshSlot = -1;
  t850::PhysicsBodyHandle primaryPhysicsBody;
  std::vector<t850::PhysicsBodyHandle> physicsBodies;
  int cameraIndex = -1;
  int ragdollIndex = -1;
};

struct GameObject {
  RuntimeGameObjectId runtimeId = 0;
  std::string sceneId;
  std::string name;
  std::string kind;
  bool enabled = true;
  GameObjectLinkRefs links;
  std::vector<std::unique_ptr<Component>> components;
};
```

`LoadFromScene()` should first link what already exists:

- `mesh_object` -> loaded SceneTemplate mesh/object slot
- `primary_physics_entity` -> runtime physics entity/body handle where possible
- `physics_entities[]` -> physics body handles where possible
- `camera` -> runtime camera index or named camera record
- `ragdoll_object` -> existing ragdoll runtime slot where possible

If a link cannot resolve, keep the object but record a validation/runtime warning.

### First Useful Behavior

The first behavior does not need combat or AI. It should prove the plumbing:

- Runtime DevGui lists game objects.
- Selecting a runtime game object shows scene ID, name, kind, mesh link, physics link, component count.
- Optional debug component toggles mesh visibility or wire flag through a controlled API.

### Acceptance Criteria

- SceneTemplate can load a scene with game entities and show them in DevGui.
- Missing mesh or physics reference produces a validation warning, not a crash.
- Game objects are destroyed on scene unload.
- Runtime IDs are unique and not serialized.
- Scene IDs are preserved from `.t8scene`.

### Tests

- Entity with valid mesh link resolves to mesh slot.
- Entity with invalid mesh link remains in registry with warning.
- Entity with valid physics link resolves when physics is initialized.
- Entity with physics link behaves gracefully when Jolt is unavailable.

## P0 Gap 4: Component Lifecycle And Mutation Rules

### Current Gap

The proposal describes components, but not lifecycle, dependencies, update ordering, creation/destruction timing, or what happens when components mutate the world during update.

### Suggested Solution

Define a small component contract before adding many component types.

```cpp
enum class ComponentUpdatePhase {
  PrePhysics,
  PostPhysics,
  Logic,
  Late
};

class Component {
public:
  virtual ~Component() = default;
  virtual std::string_view Type() const = 0;
  virtual ComponentUpdatePhase Phase() const { return ComponentUpdatePhase::Logic; }
  virtual void OnAttach(GameObject& owner, GameLogicSystem& system) {}
  virtual void OnCreate() {}
  virtual void Update(float fixedDt) {}
  virtual void OnDestroy() {}
  virtual void OnDetach() {}
};
```

Initial lifecycle:

```text
Create entity
  allocate runtime object
  create components from descriptors
  OnAttach for all components
  OnCreate for all components

Fixed tick
  update enabled components by phase

Destroy entity
  mark destroy-pending
  after update loop: OnDestroy, OnDetach, erase
```

### Component Dependencies

Support simple dependency validation first:

```cpp
struct ComponentTypeInfo {
  std::string type;
  std::vector<std::string> requiredComponentTypes;
  bool allowMultiple = false;
};
```

Examples:

- `combat` requires `health` only if it can receive damage or target health-bearing entities.
- `state_machine` may require no component but may reference named events/conditions.
- `sensor` may require a mesh or physics link depending on implementation.

Do not allow components to directly erase other components while iterating. Use commands:

```text
RequestAddComponent(entityId, desc)
RequestRemoveComponent(entityId, componentId)
```

Apply those in a deferred mutation phase.

### Component Factory

Use a registry for built-in types:

```cpp
using ComponentFactoryFn = std::unique_ptr<Component>(*)(const SceneComponentDesc&, ComponentLoadContext&);

class ComponentFactoryRegistry {
public:
  void Register(std::string type, ComponentFactoryFn factory, ComponentTypeInfo info);
  std::unique_ptr<Component> Create(const SceneComponentDesc& desc, ComponentLoadContext& ctx) const;
};
```

Unknown component type should not crash. It should produce a load warning and optionally create an inert `UnknownComponent` so the editor can preserve data.

### Acceptance Criteria

- Components get attach/create/destroy callbacks exactly once.
- Components update only when enabled and owner is enabled.
- Unknown components preserve data and report warning.
- Component dependencies validate before runtime update.
- Deferred component add/remove does not invalidate update iteration.

### Tests

- Component callback order test.
- Remove component during update: removal happens after tick.
- Duplicate disallowed component: validation error.
- Unknown component: scene loads with warning and data remains serializable.

## P0 Gap 5: Event Ordering, Reentrancy, And Event Log

### Current Gap

The EventBus proposal does not define whether dispatch is immediate or queued, whether handlers can publish recursively, or how entity destruction during events is handled.

### Suggested Solution

Use queued per-tick events for v1.

Event model:

```text
Publish(event)
  append to next queue

Tick start
  swap current queue and next queue
  dispatch current queue in FIFO order
  event handlers may publish into next queue
  entity/component destruction requests are deferred
```

This prevents recursive dispatch surprises and makes event order deterministic.

### Event Types

Start with generic event records that can also be shown in DevGui:

```cpp
struct GameEvent {
  uint64_t sequence = 0;
  uint64_t tick = 0;
  std::string type;
  std::string sourceEntityId;
  std::string targetEntityId;
  std::map<std::string, std::string> params;
};
```

Later, typed C++ events can be added for hot paths. The generic record is useful for editor/dev tooling and scene-authored transitions.

### Event Log

Keep a ring buffer in `GameLogicSystem`:

```cpp
class EventLog {
  std::deque<GameEvent> recentEvents;
  size_t maxEvents = 512;
};
```

DevGui should expose:

- pause/resume event capture,
- clear log,
- filter by type,
- filter by source/target entity,
- show tick and sequence.

### Acceptance Criteria

- Event order is deterministic.
- Handler-published events dispatch on a later tick, not recursively in the same handler.
- Destroying an entity from an event handler is deferred safely.
- DevGui event log shows recent events.

### Tests

- Publish A then B: handlers receive A then B.
- Handler for A publishes C: C is not dispatched until next dispatch cycle.
- Handler requests entity destroy: entity remains valid until dispatch completes.
- Log ring buffer caps at configured size.

## P0 Gap 6: Validation And Schema Migration

### Current Gap

`.t8scene` has a version field, but there is no clear migration layer for game-logic schema changes. The loader ignores unknown keys, which is useful for compatibility but dangerous for typos.

### Suggested Solution

Add validation and migration as explicit Framework functions.

```cpp
namespace t850::scene {
  constexpr int kEditorSceneVersion_GameLogicV1 = 2;

  struct SceneValidationIssue {
    enum class Severity { Info, Warning, Error } severity;
    std::string code;
    std::string message;
    std::string entityId;
    std::string componentId;
  };

  struct SceneValidationReport {
    std::vector<SceneValidationIssue> issues;
    bool HasErrors() const;
  };

  bool MigrateEditorSceneFile(EditorSceneFile& scene, std::string* log = nullptr);
  SceneValidationReport ValidateEditorSceneGameLogic(const EditorSceneFile& scene);
}
```

### Migration Rules

For initial game-logic schema:

- If `scene.version < 2`, add IDs to game entities and components.
- Preserve old `ai` string, but optionally map known values into default components/behavior later.
- Do not auto-create complex state machines from old data unless the mapping is deterministic and documented.

### Validation Rules

Required P0 validation:

- Missing game entity ID: error after migration, warning before migration.
- Duplicate game entity ID: error.
- Duplicate component ID within entity: error.
- Empty component type: error.
- Unknown component type: warning if preserved, error if runtime requires strict load.
- Entity mesh reference missing: warning.
- Entity primary physics reference missing: warning or error depending component requirements.
- Behavior initial state missing: error.
- Transition source or target state missing: error.
- Group member ID missing: error once groups exist.

### Editor UI

T8ditor should show validation results with:

- severity,
- issue code,
- message,
- jump to entity/component/group where possible,
- copy report to clipboard,
- run validation before save and before Play Scene.

### Acceptance Criteria

- Old scenes load and can be migrated/defaulted.
- New scenes with invalid game data report actionable issues.
- SceneTemplate can reject or degrade gracefully when validation has errors.
- T8ditor can jump from validation issue to relevant entity/component.

### Tests

- Missing IDs are generated during migration.
- Duplicate IDs produce validation error.
- Invalid transition produces validation error.
- Unknown component type produces warning and data preservation.

## P0 Gap 7: Physics And Navigation Integration Boundaries

### Current Gap

The proposals mention line of sight, character movement, navmesh pathing, and group movement, but do not define safe ownership boundaries. Current physics uses a simple static/non-moving vs moving layer model, and navigation uses Detour queries with existing authored nav-agent fields.

### Suggested Solution

Introduce service facades inside GameLogicSystem, not direct arbitrary access to Jolt or Detour from every component.

```cpp
class GamePhysicsQueryService {
public:
  bool RaycastLineOfSight(const Vec3& from, const Vec3& to, PhysicsQueryFilter filter, PhysicsHit* hit);
  std::vector<GameObjectId> QueryEntitiesInRadius(const Vec3& center, float radius, EntityQueryFilter filter);
};

class GameNavigationService {
public:
  NavRequestId RequestPath(GameObjectId requester, Vec3 start, Vec3 end, NavQueryOptions options);
  bool TryGetPathResult(NavRequestId id, NavPathResult& out);
};
```

Components ask these services for queries or commands. They do not hold raw physics/nav pointers long term.

### Physics Writes

Centralize writes as commands:

```text
SetKinematicTarget(entity, transform)
ApplyImpulse(entity, impulse)
SetBodyEnabled(entity, enabled)
CreateTriggerVolume(desc)
DestroyPhysicsProxy(id)
```

Flush those commands in a known phase. If the engine continues to update Jolt outside SceneTemplate, the command flush should happen before the app-level physics step or should be clearly documented if it happens after.

### Navigation

Do not assume DetourCrowd is wired. For v1:

- use `NavMesh::FindPath` or existing batch path APIs,
- implement simple path-follow component,
- project target points to navmesh,
- use existing object nav-agent fields for yaw/front/formation metadata where applicable.

### Gameplay Collision Categories

Long term, extend physics filtering beyond static/moving:

- world static,
- dynamic prop,
- player,
- NPC,
- projectile,
- trigger/sensor,
- ragdoll,
- editor-only/debug.

Do this carefully because Jolt layer setup is centralized.

### Acceptance Criteria

- Components do not directly mutate Jolt body state during arbitrary callbacks.
- Physics/nav unavailable cases do not crash.
- Basic path-follow component works with current NavMesh.
- Line-of-sight helper uses physics query filtering through a service.

### Tests

- Game logic loads with Jolt disabled or uninitialized.
- Game logic loads with Recast disabled or no authored NavMesh.
- Path request for invalid start/end returns failure, not crash.
- Physics query for missing body returns no hit / warning.

## P1 Gap 8: Play Scene Iteration Speed

### Current Gap

Play Scene currently exports a temporary `.t8scene` and loads it through SceneTemplate. This is high fidelity but can slow fast gameplay iteration.

### Suggested Solution

Keep two modes:

1. Fidelity Play

   Current temp `.t8scene` export path. This verifies real save/load behavior and should remain the default for final validation.

2. Fast Play

   Build an in-memory `EditorSceneFile` snapshot and pass a cloned copy directly to SceneTemplate/GameLogicSystem. Avoid disk serialization for quick iteration.

### API Sketch

```cpp
class SceneTemplate {
public:
  bool LoadSceneFromFile(const std::string& path);
  bool LoadSceneFromEditorSceneFile(t850::scene::EditorSceneFile scene, SceneLoadOptions options);
};
```

Fast Play must use a copy, not the live `EditorWorld`, so runtime mutation cannot corrupt editor-authored state.

### Cross-Check Rule

Add a debug command that runs both paths and compares validation reports / loaded entity counts.

### Acceptance Criteria

- Fidelity Play still works exactly as today.
- Fast Play can start without writing temp JSON.
- Runtime cannot mutate `EditorWorld` through shared references.
- Differences between fast and fidelity load are logged.

### Tests

- Same scene via file and memory produces same game object count.
- Same scene via file and memory produces same component count.
- Mutating runtime component value in Fast Play does not alter editor snapshot after Stop.

## P1 Gap 9: Component Data Model

### Current Gap

Flat string params are easy to author but weak for nested component data and inefficient for runtime hot paths.

### Suggested Solution

Use three layers:

1. Serialized generic descriptor:

```cpp
struct SceneComponentDesc {
  std::string id;
  std::string type;
  bool enabled = true;
  std::map<std::string, std::string> params;
  std::string config_json;
};
```

2. Load-time typed config:

```cpp
struct HealthComponentConfig {
  float maxHp = 100.0f;
  float currentHp = 100.0f;
  float armor = 0.0f;
  float regenPerSecond = 0.0f;
};
```

3. Runtime component with parsed fields:

```cpp
class HealthComponent final : public Component {
  float maxHp_;
  float currentHp_;
  float armor_;
  float regenPerSecond_;
};
```

Do all string parsing at load time or edit-apply time, not every frame.

### Editor Strategy

For unknown or generic components:

- show params table,
- add/remove key,
- edit string value,
- show raw `config_json` editor if present.

For built-in components:

- use typed controls,
- write back to descriptor params/config,
- validate ranges.

### Acceptance Criteria

- Runtime components do not parse string params during hot update.
- Unknown components remain editable as generic data.
- Built-in components have typed editor controls over time.

### Tests

- Invalid float param reports validation error.
- Missing optional param uses default.
- Unknown extra param is preserved.
- Nested config_json round trips unchanged.

## P1 Gap 10: State Machine Semantics

### Current Gap

The proposal defines state and transition descriptors, but not exact condition semantics, priority ordering, cooldowns, wildcard rules, or runtime compilation.

### Suggested Solution

Define a minimal deterministic state machine first.

Descriptor:

```cpp
struct SceneStateMachineStateDesc {
  std::string name;
  std::string script;
  std::map<std::string, std::string> params;
  float default_duration = -1.0f;
};

struct SceneTransitionRuleDesc {
  std::string from_state;
  std::string to_state;
  std::string condition;
  float priority = 0.0f;
  float cooldown = 0.0f;
};

struct SceneStateMachineDesc {
  std::string initial_state = "idle";
  std::vector<SceneStateMachineStateDesc> states;
  std::vector<SceneTransitionRuleDesc> transitions;
};
```

Runtime compiled transition:

```cpp
enum class TransitionConditionKind {
  Always,
  OnEvent,
  TimerElapsed,
  HealthBelow,
  ParamEquals
};

struct CompiledTransitionRule {
  uint32_t fromStateIndex;
  uint32_t toStateIndex;
  TransitionConditionKind conditionKind;
  uint32_t eventTypeId;
  float threshold;
  float priority;
  float cooldown;
  float cooldownRemaining;
};
```

### Condition String Policy

Allow authoring strings, but compile them on load.

Initial supported forms:

```text
always
on_event:event_name
timer_elapsed
health_below:25
param_equals:key:value
```

Invalid condition strings are validation errors or warnings depending on strictness.

### Evaluation Rules

- Only evaluate transitions whose `from_state` matches current state or `*`.
- Sort by descending priority.
- Stable tie-breaker: descriptor order.
- Cooldown blocks transition until elapsed.
- At most one transition per state machine per tick for v1.
- On transition: call `OnExit`, set current state, call `OnEnter`, emit `state_changed` event.

### Acceptance Criteria

- Initial state must exist.
- Wildcard transition works.
- Priority resolves conflicts deterministically.
- Cooldown is honored.
- Invalid condition reports validation error.

### Tests

- Initial state missing -> validation error.
- Two valid transitions -> highest priority wins.
- Equal priority -> descriptor order wins.
- Cooldown blocks immediate repeat.
- `on_event` transition triggers only after event is queued/dispatched according to event rules.

## P1 Gap 11: Editor Rollout And Undo Strategy

### Current Gap

The editor proposal includes hierarchy, inspector, context menus, state graph, overlays, validation, play controls, event logs, and toolbar/menu additions. That is too much for the first implementation phase.

### Suggested Solution

Roll out editor support in small layers.

#### Editor Layer 1: Select Game Entities

- Add selection type 9 for game entities.
- Game Entities hierarchy node click selects the entity itself, not just its mesh/physics children.
- Properties panel shows ID, name, kind, visibility, frozen, wire.
- Preserve existing child nodes for mesh/physics/ragdoll links.

#### Editor Layer 2: Link Inspector

- Mesh object dropdown.
- Primary physics dropdown.
- Additional physics list.
- Camera dropdown.
- Ragdoll object dropdown.
- AI legacy string kept for compatibility.

#### Editor Layer 3: Component List

- Add component button.
- Remove component button.
- Enabled checkbox.
- Generic params table.
- Component ID displayed read-only with regenerate button only in advanced mode.

#### Editor Layer 4: Validation Panel

- Run validation.
- Jump to issue target.
- Copy report.
- Run automatically before Play Scene.

#### Editor Layer 5: State Machine List Editor

- Initial state combo.
- States table.
- Transitions table.
- No graph view yet.

#### Editor Layer 6: Visual Graph

- Only after state-machine semantics are proven.

### Undo Strategy

Use existing whole-scene undo snapshots for early component UI. Add granular commands later for high-frequency edits.

Suggested progression:

```text
Early v1:
  automatic before/after EditorUndoState snapshots around ImGui edits

Later:
  AddGameEntityCommand
  DeleteGameEntityCommand
  EditGameEntityLinksCommand
  AddComponentCommand
  RemoveComponentCommand
  EditComponentCommand
  AddStateCommand
  RemoveStateCommand
  AddTransitionCommand
  RemoveTransitionCommand
```

### Acceptance Criteria

- Game entity selection is explicit and visible.
- Component edits are undoable through at least scene-state snapshots.
- Validation panel can jump to a game entity.
- Existing mesh/physics selection behavior is not broken.

### Tests

- Select game entity from hierarchy -> selection type 9.
- Edit entity name -> undo restores old name.
- Add component -> save/load preserves component.
- Remove component -> undo restores component.

## P1 Gap 12: Testing Strategy

### Current Gap

The proposals do not define tests. Manual editor testing alone is not enough for scene schema and runtime gameplay systems.

### Suggested Solution

Add a small game-logic verification executable or test harness. If a formal test framework is not present, start with a command-line tool that returns non-zero on failure.

Possible options:

1. Add `T850/Tests/GameLogicTests` project.
2. Add a `DayScene` command-line verification mode.
3. Add a lightweight `Framework` unit-test executable that links Framework only.

Recommended test groups:

#### Schema Tests

- serialize/deserialize component descriptors,
- migrate old scene with missing IDs,
- validate duplicate IDs,
- validate broken references.

#### Registry Tests

- create object,
- lookup by scene ID,
- lookup by runtime ID,
- destroy object,
- no ID reuse in same scene session unless explicitly designed.

#### Component Tests

- factory creates known component,
- unknown component preserved,
- lifecycle callback order,
- deferred add/remove.

#### Event Tests

- FIFO order,
- handler publishes event for next tick,
- deferred destruction during dispatch.

#### State Machine Tests

- initial state,
- transition priority,
- wildcard transition,
- cooldown,
- invalid condition.

#### SceneTemplate Integration Tests

- load scene with one game entity,
- load scene with broken refs,
- start/stop Play Scene lifecycle if automatable.

### Acceptance Criteria

- Tests run from PowerShell and CI-friendly command line.
- Failures return non-zero exit code.
- Test scenes are small and checked into `Assets/Scenes/Test` or similar.

## P1 Gap 13: Performance And Telemetry

### Current Gap

No entity count targets, frame budgets, telemetry counters, or profiling scopes are defined.

### Suggested Solution

Use existing diagnostics from the first runtime slice.

Telemetry counters:

```text
game.entities.total
game.entities.active
game.components.total
game.components.updated
game.events.queued
game.events.dispatched
game.events.dropped
game.state_machines.total
game.state_machines.transitions
game.physics.queries
game.nav.requests
game.nav.completed
game.validation.errors
game.validation.warnings
```

Profiler/telemetry scopes:

```text
game.update
game.events.dispatch
game.components.pre_physics
game.components.logic
game.state_machines
game.groups
game.spatial_queries
game.debug_gui
```

Initial budgets:

```text
V1 target:
  100 active game entities
  300 simple components
  under 0.5 ms game.update on desktop Release x64

V2 target:
  1,000 lightweight entities
  3,000 simple components
  under 1.5 ms game.update on desktop Release x64
```

### Data-Oriented Follow-Up

Do not start with full ECS. Start with registry objects and move hot components into contiguous storage when profiling proves it is needed.

Potential path:

```text
Phase 1: vector<GameObject> + vector<unique_ptr<Component>>
Phase 2: per-type component arrays for hot built-ins
Phase 3: sparse-set/entity-component storage if needed
```

### Acceptance Criteria

- Runtime telemetry reports game object/component/event counts.
- Profiler can isolate game update cost.
- A synthetic load scene or test can create 100 and 1,000 entities for benchmarking.

## P1 Gap 14: Build And Project Hygiene

### Current Gap

The proposals say no build-system changes are required. That is true only for header-only schema changes. Any new compiled runtime module requires project updates.

### Suggested Solution

For every new `.cpp` file:

- update `T850/Framework/Framework.vcxproj`, because MSBuild is the primary supported build path,
- update `T850/Framework/CMakeLists.txt`, unless CMake is explicitly being retired,
- update filters if desired for Visual Studio organization,
- check all relevant platform configs: Win32, x64, ARM64, Debug, Release.

### File Placement

Recommended core layout:

```text
T850/Framework/include/game/
  GameLogicSystem.h
  GameObject.h
  GameObjectRegistry.h
  Component.h
  ComponentFactory.h
  EventBus.h
  StateMachine.h
  GameValidation.h

T850/Framework/src/game/
  GameLogicSystem.cpp
  GameObjectRegistry.cpp
  ComponentFactory.cpp
  EventBus.cpp
  StateMachine.cpp
  GameValidation.cpp
```

Optional examples later:

```text
T850/Framework/include/game/examples/
T850/Framework/src/game/examples/
```

### Acceptance Criteria

- Build scripts succeed after adding files.
- No source file exists only in CMake or only in `.vcxproj` accidentally.
- CI/build.ps1 path remains primary.

## P2 Gap 15: RTS Feature Boundaries

### Current Gap

The proposals are heavily RTS-flavored: squads, formations, flocking, health, combat, resources, harvesting, production, sensors.

### Suggested Solution

Split core and examples.

Core game module should contain:

- game object registry,
- component base/factory,
- event queue,
- state machine,
- validation,
- runtime debug panels,
- scene load/unload,
- generic spatial query hooks.

Example or optional module can contain:

- `HealthComponent`,
- `CombatComponent`,
- `SensorComponent`,
- `ResourceComponent`,
- `GroupManager`,
- flocking,
- formations,
- RTS command handling.

This keeps the engine suitable for FPS, RPG, racing, platformer, editor tools, and other genres.

### Acceptance Criteria

- A scene can use core game entities without linking or enabling RTS examples.
- RTS demo components are clearly documented as examples.
- Editor UI labels distinguish generic components from example components.

## P2 Gap 16: Visual State Graph Timing

### Current Gap

The visual graph editor is attractive but expensive. If built too early, it will hard-code unstable state-machine rules.

### Suggested Solution

Delay graph UI until after:

- runtime state machine works,
- list/table editor works,
- validation works,
- force-transition DevGui works,
- at least one demo uses it.

When implemented, keep graph layout editor-only:

```cpp
struct SceneStateMachineEditorLayoutDesc {
  std::map<std::string, Vec2f> state_positions;
  float zoom = 1.0f;
  Vec2f pan;
};
```

Do not mix visual layout data with runtime state semantics unless needed.

### Acceptance Criteria

- State-machine data can be edited without graph UI.
- Graph layout can be reset/regenerated.
- Moving graph nodes does not affect runtime behavior.

## P2 Gap 17: Overlay Rendering

### Current Gap

The proposal describes many overlays, but not how they should use current rendering infrastructure.

### Suggested Solution

Use existing editor/debug drawing paths:

- `LineRenderer` for spheres, cones, formation lines, connections.
- `WireframeSphere` or similar helper geometry for radii.
- `TextRenderer` for labels where available.
- Existing physics/nav debug renderers for physics and navigation overlays.
- `RenderEditorSceneFrame()` overlay stage for T8ditor.

Overlay toggles should be scene/editor settings, not component data:

```cpp
struct GameLogicOverlaySettings {
  bool showSensorRadius = true;
  bool showCombatRange = true;
  bool showHealthBars = true;
  bool showStateLabels = true;
  bool showGroupLines = true;
};
```

### Acceptance Criteria

- Overlays can be toggled independently.
- Overlays do not require render graph changes for v1.
- Overlay draw does not mutate gameplay state.
- Overlay rendering handles missing mesh/transform links gracefully.

## P2 Gap 18: Platform And Resource Compatibility

### Current Gap

Gameplay proposals often assume desktop/editor availability. T850 supports Windows, Linux/Steam Deck, Android, multiple graphics APIs, and optional Jolt/Recast availability.

### Suggested Solution

Rules:

- Use `ResourceLocator` for all behavior/config asset reads.
- Use `ResolveCachePath` for generated caches or runtime-saved debug data.
- Keep core runtime game module independent of ImGui.
- Put ImGui debug panels in FrameworkImGui or scene/editor integration code.
- Check physics/nav availability before using them.
- Avoid backend-specific rendering assumptions in gameplay code.

### Acceptance Criteria

- GameLogicSystem can compile without editor UI.
- GameLogicSystem can run with no physics or no navigation backend.
- Component config assets can load from Android packaged assets.
- Generated debug/config output uses cache paths.

## Suggested First Milestone: Game Logic Slice 0

This is the smallest useful milestone that proves the direction.

### Scope

- Add stable IDs to `SceneGameEntityDesc`.
- Add `SceneComponentDesc` with simple params and optional `config_json`.
- Add validation for IDs and links.
- Add `GameLogicSystem` with registry and fixed tick.
- SceneTemplate loads game entities into registry.
- Add DevGui panel listing runtime game objects.
- T8ditor can select a game entity and edit name/kind/links.
- Save/load and Play Scene preserve the data.

### Out Of Scope

- Visual state graph.
- Flocking/formations.
- Combat/resource/harvest/production.
- Hot reload.
- Full ECS.
- Complex component-specific editor UI.

### Definition Of Done

- A simple `.t8scene` with one game entity loads in SceneTemplate.
- Runtime DevGui displays the entity and resolved links.
- T8ditor can edit the entity and save it.
- Validation catches duplicate IDs and missing mesh links.
- Stop/reload clears and recreates registry cleanly.
- Basic test or verification command passes.

## Suggested Second Milestone: Components And Events

### Scope

- Component factory.
- Generic component inspector.
- `HealthComponent` as a simple built-in example.
- Queued event bus.
- Event log DevGui.
- Component lifecycle tests.

### Definition Of Done

- Add Health component in editor.
- Save/load preserves it.
- Runtime creates `HealthComponent` with parsed numeric fields.
- Debug event can damage entity.
- Event log shows damage/health change event.
- Tests cover lifecycle and event ordering.

## Suggested Third Milestone: State Machines

### Scope

- Add optional `SceneStateMachineDesc`.
- Compile transitions on load.
- List/table editor for states and transitions.
- Force transition from DevGui.
- Event-triggered transition.

### Definition Of Done

- Entity starts in initial state.
- Event can transition state.
- Invalid transition validates as error.
- Runtime DevGui shows current state.
- Tests cover priority, cooldown, wildcard, and invalid conditions.

## Suggested Fourth Milestone: Movement And Queries

### Scope

- Navigation service wrapper.
- Basic path-follow component.
- Physics query service wrapper.
- Sensor/radius query example.

### Definition Of Done

- Entity can request a path on authored NavMesh.
- Entity can follow path or report failure.
- Sensor query can find nearby game objects.
- Works gracefully without nav/physics backend.

## Suggested Fifth Milestone: Groups And RTS Demo

### Scope

- Add `SceneGroupDesc` with stable member IDs.
- Add group manager.
- Add formation/flock example systems.
- Add group inspector and group overlays.

### Definition Of Done

- Group membership survives rename and save/load.
- Group validation catches missing members.
- Demo scene shows simple formation movement.
- Core runtime still works without RTS examples.

## Documentation Updates Needed

When implementation starts, update or add these docs:

- `documentation/scenes/scene-format-and-runtime.md`
  - Add game-logic schema fields, validation, migration, and runtime load flow.

- `documentation/editor/editor-overview.md`
  - Add selection type 9, game entity inspector, component editing, validation panel.

- `documentation/dependency-map.md`
  - Add rows for game logic, components, events, state machines, and gameplay queries.

- New `documentation/game/game-logic-runtime.md`
  - Runtime ownership, fixed tick, registry, components, event queue, state machines, physics/nav boundaries.

- New `documentation/game/game-logic-editor.md`
  - T8ditor authoring workflow, validation, undo, overlays, Play Scene behavior.

## Recommended Decision Checklist

Before coding the first runtime module, answer these decisions explicitly:

1. What ID format will be serialized?
2. What scene version introduces game-logic IDs/components?
3. Is `behavior` optional or always present with default empty state machine?
4. What is the fixed tick rate?
5. What is the max catch-up tick count per frame?
6. Are events dispatched same tick or next tick?
7. Are unknown component types warnings or errors?
8. Where does validation live?
9. Does Fast Play exist now or later?
10. Which build files must be updated when adding game module files?

## Final Recommendation

The safest path is:

```text
IDs -> validation -> minimal runtime registry -> DevGui inspection -> editor identity/link editing -> components -> events -> state machines -> movement/query integration -> groups/examples -> visual graph/polish
```

This order keeps risk low. It makes the existing `game_entities` field meaningful at runtime, preserves T850's current scene/editor architecture, and avoids spending weeks on advanced UI or RTS systems before the core game-engine layer exists.