# Game Logic and Editor Integration Proposals - GPT Assessment

Date: 2026-06-29
Author: Copilot / GPT assessment

Reviewed material:

- `documentation/legacy/proposals/game-logic-and-ai-integration.md`
- `documentation/legacy/proposals/editor-game-logic-integration.md`
- `documentation/legacy/proposals/assessment.md`
- Current engine documentation under `documentation/`
- Current source anchors in `T850/Framework`, `T850/T8ditor`, and `T850/DayScene`

## Executive Summary

The proposals are pointed in the right direction: use the existing Framework scene schema as the shared contract, let T8ditor author the data, and make SceneTemplate consume it at runtime. That matches how T850 already works.

However, the proposals are not implementation-ready yet. They describe a large editor/runtime feature set, but the current engine only has a minimal `SceneGameEntityDesc` metadata layer and no runtime game-logic module. The missing work is not just editor UI. It is a new runtime simulation layer with ownership rules, fixed update policy, identity/reference semantics, component lifecycle, event ordering, physics/nav integration, debugability, tests, and performance budgets.

My verdict: strong architectural direction, but the first implementation should be much smaller and more vertical. Build a minimal generic game-object/component/runtime-load path first, then add state machines, events, editor panels, and finally RTS-style groups/flocking/formations as optional examples.

## Current Engine Reality

T850 is already more than a renderer. It is a rendering-focused engine foundation with these active systems:

- `RootFramework` / `AppBase` / `SceneBase` application and scene lifecycle.
- Multi-backend rendering: D3D11, D3D12, OpenGL, Vulkan.
- JSON render graph with render targets, pass descriptors, texture inputs, and pass signatures.
- glTF and legacy `.x` loading into `XDataBase`, `RenderMesh`, `RenderSkinnedMesh`, mesh pools, and material caches.
- PBR, IBL, shadows, SSAO, bloom, HDR, DOF, God Rays, parallax, debug RT views.
- Animation controller, GPU bone texture skinning, pose snapshots, ragdoll handoff.
- Jolt-backed physics wrapper with static triangle mesh cooking, body handles, casts, ragdolls, and debug rendering.
- Recast/Detour navigation with authored volumes, links, baked/generated `.t8nav`, path queries, and debug views.
- `ResourceLocator` for portable desktop/Android asset and cache paths.
- `RuntimeTelemetry`, `Profiler`, `FrameDumper`, and optional render tracing.
- FrameworkImGui / `DevGuiContext` for runtime debug UI and hosted panels.
- T8ditor for `.t8scene` authoring, editor overlays, undo/redo, Play Scene, Mesh Edit, Ragdoll Edit, and NavMesh authoring.

The missing engine layer is gameplay simulation: runtime game objects, components, state machines, event/messaging, gameplay collision categories, gameplay queries, gameplay persistence, and game-specific authoring workflows.

## Source-Verified Current State

### Scene Schema

Current `Framework/include/scene/EditorSceneFile.h` already has:

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
};
```

`EditorSceneFile` has `std::vector<SceneGameEntityDesc> game_entities`, but does not have:

- `SceneComponentDesc`
- `SceneStateMachineDesc`
- `SceneTransitionRuleDesc`
- `SceneGroupDesc`
- `SceneFlockConfigDesc`
- `SceneFormationConfigDesc`
- `SceneGameLogicSettingsDesc`
- `game_groups`
- `game_logic_settings`

The serializer uses Glaze with `error_on_unknown_keys = false`. That means extra future keys can load without failing, but typos can also be silently ignored. There is a `version = 1` field, but no visible migration layer for `.t8scene` upgrades.

### Editor Data Model

`T8ditor/EditorWorld.h` already owns `std::vector<t850::scene::SceneGameEntityDesc> gameEntities`.

It also has editor mesh groups:

```cpp
struct SceneGroup {
  std::string name;
  std::set<int> members; // indices into g_objects
  bool persistent = false;
};
```

These groups are editor object groups, not proposed gameplay groups. They are not `SceneGroupDesc`, are not saved as top-level game groups, and reference mesh indices rather than stable gameplay entity IDs.

Selection types currently stop at 8:

- `0` mesh/object
- `1` camera
- `2` light
- `3` physics entity
- `4` NavMesh
- `5` spline
- `6` light camera
- `7` spline point
- `8` God Rays volume

There is no current selection type 9 or 10 for game entity or game group.

### Editor UI

T8ditor already has a "Game Entities" hierarchy section. It calls `EnsureInferredGameEntities()` and displays entity name, kind, mesh link, camera link, physics links, ragdoll links, and `ai` string.

This is a relationship/link view, not a gameplay authoring UI. There is no component editor, state machine editor, gameplay group editor, transition table, or validation panel for game logic.

### Save/Load and Undo

`EditorApp::BuildEditorSceneSnapshot()` writes `sf.game_entities = g_gameEntities`.

`EditorApp::ApplyEditorUndoState()` restores `g_gameEntities = sf.game_entities` and then calls `EnsureInferredGameEntities()`.

T8ditor has a command-pattern undo stack, but also relies heavily on whole-scene snapshot commands around ImGui edits. The proposals' command-specific undo design is good for precision, but the current editor already has a snapshot safety net that could cover early game-logic UI work.

### Runtime SceneTemplate

`SceneTemplate` loads editor-authored objects, physics entities, navigation, splines, cameras, lights, profiles, God Rays, and ragdolls. I did not find current runtime consumption of `scene.game_entities` in `DayScene/SceneTemplate.cpp`.

So the current `game_entities` vector is saved by the editor, but it is not yet the runtime gameplay instantiation path.

### Build System

The project has both `.vcxproj` and CMake files. `AGENTS.md` says MSBuild / `.vcxproj` is the primary supported path and CMake is secondary.

The proposal line "does not require new build system changes" is only true for pure schema-only edits. Any new `Framework/include/game` and `Framework/src/game` module will need project-file updates at least in `Framework.vcxproj`, and probably in CMake too if the secondary build is kept healthy.

## What The Proposals Get Right

### 1. Shared Framework Schema Is The Correct Contract

The proposal correctly avoids duplicating schema in T8ditor. `EditorSceneFile.h` is already the common contract for editor and runtime scene data. Extending it is the right move.

### 2. T8ditor Should Author, SceneTemplate Should Play

The proposed data flow:

```text
EditorWorld -> BuildEditorSceneSnapshot -> EditorSceneFile -> SceneTemplate -> runtime systems
```

matches the current architecture. Play Scene already exists as a high-fidelity path through temporary `.t8scene` export.

### 3. Component + State Machine Is A Reasonable First Gameplay Model

The engine does not currently need a full visual scripting system. A data-authored component model plus a small state machine layer is a good fit for a renderer-heavy codebase moving toward game-engine behavior.

### 4. Editor UX Vision Is Strong

The hierarchy, inspector, state machine view, validation panel, event log, runtime stats, and viewport overlays are all useful and align with T8ditor's current strengths.

### 5. Validation Is Essential

The validation section is one of the most important parts of the editor proposal. Because `.t8scene` unknown keys are ignored, explicit validation is needed for broken references, missing states, invalid component params, and stale group memberships.

### 6. DevGuiContext Is A Natural Debug Surface

The proposed runtime panels fit well with `FrameworkImGui` and `DevGuiContext`. Runtime game-object lists, event logs, state-machine inspection, and spatial-grid overlays should use that layer rather than inventing a separate UI system.

## Where The Proposals Need Correction

### 1. `game_entities` Are Not New, But They Are Not Runtime Gameplay Yet

The proposal sometimes treats `game_entities` as an established runtime bridge. In the current source, `SceneGameEntityDesc` is present and T8ditor authors it, but SceneTemplate does not appear to instantiate gameplay objects from it.

The real first milestone is not "extend game entities." It is "make game entities mean something at runtime."

### 2. `behavior` Is Inconsistently Typed

The proposal shows both:

```cpp
SceneStateMachineDesc behavior;
```

and runtime/editor code that checks:

```cpp
if (entityDesc.behavior.has_value())
```

It should be specified as optional, or the empty/default state machine semantics should be explicitly defined. I recommend:

```cpp
std::optional<SceneStateMachineDesc> behavior;
```

### 3. Name-Based References Are Too Fragile

The proposal uses entity names for group membership and links. Current T850 already uses names in several authoring paths, but gameplay systems need stable IDs because names can change and duplicates are easy to create.

Add stable IDs before building groups, events, or state-machine references:

```cpp
struct SceneGameEntityDesc {
  std::string id;   // stable serialized id, e.g. UUID string
  std::string name; // display name
  ...
};

struct SceneGroupDesc {
  std::string id;
  std::vector<std::string> member_entity_ids; // stable ids, not display names
};
```

### 4. `std::map<std::string, std::string>` Params Are Too Weak Alone

The current assessment is right that flat string params are limiting. They are acceptable for a first generic prototype, but not enough for abilities, nested configs, triggers, inventories, behavior trees, or complex effects.

A practical compromise:

```cpp
struct SceneComponentDesc {
  std::string id;
  std::string type;
  bool enabled = true;
  std::map<std::string, std::string> params; // simple editor-friendly values
  std::string config_json;                   // optional nested component config
};
```

For core built-in components, prefer typed descriptors or typed runtime factories after load. Do not evaluate strings every frame.

### 5. `SceneGameLogicSettingsDesc` Is Referenced But Not Defined Enough

The proposals reference `game_logic_settings` and spatial-grid settings, but the type set does not fully define a `SceneSpatialGridSettingsDesc` or clear defaults. This should be explicit before schema work starts.

### 6. RTS Features Should Not Be Core Engine Features

Flocking, formations, harvesting, combat, resources, and squad commands are useful examples, but they should not define the core game-engine layer.

Recommended split:

- `Framework/game/core`: game object registry, component interface, state machine, event queue, fixed tick, scene load.
- `Framework/game/examples` or `Framework/game/rts`: health/combat/sensor/resource/group/flock/formation examples.

### 7. Visual State Machine Editor Is Too Early As A Core Milestone

The visual graph editor should come after the runtime state machine and inspector list editor are proven. Otherwise the project risks spending weeks on graph UI before the semantics are stable.

## Assessment Of `assessment.md`

The existing `assessment.md` is largely correct and useful. Its strongest points are:

- It identifies thread safety as a P0 gap.
- It identifies Play Scene iteration speed as a real UX issue.
- It correctly flags missing performance targets, tests, schema migration, component lifecycle, and ID strategy.
- It correctly calls out the RTS bias.
- It correctly sees the shared Framework schema approach as the strongest architectural decision.

Places I would sharpen or adjust:

### Play Scene JSON Round-Trip

The assessment calls the temp `.t8scene` Play Scene path a bottleneck. That is true for fast iteration, but the current path also has a major advantage: it tests the real runtime scene loader.

I would not replace it outright. I would add two modes:

- **Fidelity Play:** current temp `.t8scene` export path. This catches serialization/load bugs.
- **Fast Play:** direct in-memory `EditorSceneFile` clone into SceneTemplate/GameLogicSystem. This speeds iteration.

The two paths should be cross-tested so fast play cannot diverge silently.

### Thread Safety

The assessment asks many thread-safety questions. Good. The first answer should be conservative: game logic runs on a fixed main-thread tick, and worker threads are used only for read-only/batched jobs with explicit result handoff.

Do not start by making every component and event callback thread-safe. That would add complexity before the engine has gameplay semantics.

### ECS Suggestion

The assessment mentions ECS for 1,000+ entities. That may become necessary, but I would not make ECS a P0 requirement. T850 already has `PrimitiveInst`, scene objects, physics handles, and editor snapshots. A registry plus contiguous component storage for hot components is a better first step than a full ECS rewrite.

### Build System

The assessment repeats the proposal's MSBuild focus. That is right for primary development, but the repo does contain CMake files. New source files should update `.vcxproj` first and CMake second, unless the team explicitly chooses to abandon CMake.

### Current Game Entity UI

The assessment implies `SceneGameEntityDesc` is minimal, which is true, but it does not emphasize that T8ditor already has a "Game Entities" hierarchy section and inference path. That is an important implementation foothold.

## Critical Gaps I Found

### P0: Runtime Ownership And Tick Model

The proposals need a precise runtime model:

- Who owns `GameLogicSystem`?
- Is it a `SceneBase` member, an `EngineContext` service, or a SceneTemplate-only member first?
- When does it update relative to animation, physics, input, nav, and render?
- Is gameplay fixed timestep or variable timestep?
- How are runtime mutations isolated from editor-authored state during Play Scene?

Recommendation:

```text
SceneTemplate::OnLoadScene
  -> GameLogicSystem::LoadFromScene(scene)

SceneTemplate::OnUpdate(dt)
  -> collect input/commands
  -> GameLogicSystem::UpdateFixed(dt accumulator)
  -> apply transform/animation/physics outputs

Application frame
  -> physics update remains owned by app/EngineContext until a larger refactor
```

Start with fixed gameplay ticks, even if rendering remains variable.

### P0: Stable Identity And References

Before components/groups/state machines ship, add stable IDs:

- Game entity ID.
- Component ID unique inside entity.
- Group ID.
- Optional runtime ID allocated on load.

Names should be display labels, not durable references.

### P0: Minimal Runtime Consumption Of `game_entities`

The first real engine milestone should be:

1. SceneTemplate reads `scene.game_entities`.
2. It creates a `GameObject` record per valid entity.
3. It links the record to the loaded `PrimitiveInst` by `mesh_object`.
4. It links physics/camera/ragdoll references where present.
5. It exposes the registry in DevGui.
6. It shuts down cleanly on scene unload.

Do this before adding health, combat, state machines, groups, flocking, or visual graph editors.

### P0: Component Lifecycle

The proposal needs lifecycle rules. A minimal version:

```text
Load scene
  create GameObject
  create components
  Component::OnAttach(owner)
  Component::OnCreate()
  StateMachine::SetInitialState()

Fixed tick
  process queued events from previous tick
  update components in deterministic phase order
  evaluate state machines
  queue commands/events
  apply deferred creates/destroys

Unload scene
  Component::OnDestroy()
  Component::OnDetach()
  clear event subscriptions
  clear registry
```

Also define component dependencies and duplicate-component policy.

### P0: Event Ordering And Reentrancy

The EventBus proposal is underdefined. Important decisions:

- Are events immediate or queued?
- Can handlers publish more events?
- Can handlers destroy entities?
- Are subscriptions allowed to mutate during dispatch?
- Are event logs recorded before or after dispatch?

I recommend queued per-tick dispatch at first. Entity creation/destruction should be deferred until a safe phase.

### P0: Physics Integration Is More Than Queries

Current Jolt integration has only static/non-moving vs moving layers. Gameplay needs:

- gameplay collision categories,
- trigger/sensor semantics,
- line of sight helpers,
- damage volumes/projectiles if combat is included,
- query filtering by entity/team/layer,
- deterministic ownership of physics writes.

Do not let components write Jolt bodies from arbitrary callbacks. Centralize physics commands and apply them at known points.

### P1: Navigation/AI Assumptions

Current navigation supports Recast/Detour mesh queries, authored links, caches, and some batch query support. DetourCrowd is detected but not the active runtime path.

The proposal should avoid assuming crowd simulation is already wired. For v1, use direct path queries plus simple path following. Treat flocking/formations as optional higher-level logic after basic movement works.

Also integrate with existing authored nav-agent fields on `SceneObjectDesc` instead of duplicating target mode, formation slot, yaw offsets, and follow distances elsewhere.

### P1: Editor Integration Scope

The editor proposal is too large for one phase. It should be staged:

1. Display/select game entities directly.
2. Edit identity and links.
3. Add component list with generic key/value params.
4. Add validation and save/load round trip.
5. Add state-machine list editor.
6. Add runtime debug panels.
7. Add visual graph editor.
8. Add gameplay groups and RTS examples.

The existing whole-scene undo snapshot path can support early UI. Granular commands can be added when the UI stabilizes.

### P1: Performance Targets Should Use Existing Telemetry

T850 already has `RuntimeTelemetry` and `Profiler`. The proposal should define counters and scopes from day one:

- `game.entities`
- `game.components`
- `game.events.queued`
- `game.events.dispatched`
- `game.state.transitions`
- `game.update.ms`
- `game.spatial_query.ms`
- `game.nav_requests`
- `game.physics_queries`

Initial budget can be modest and measured:

```text
Target v1: 100 active game entities under 0.5 ms fixed-tick gameplay on desktop.
Target v2: 1,000 lightweight entities under 1.5 ms, with hot components stored contiguously.
```

### P1: Testing Is Missing

Add tests before the editor graph work:

- `.t8scene` schema round trip.
- scene migration/defaulting.
- stable ID preservation on rename.
- component factory creation and unknown component handling.
- state transition priority/cooldown/order.
- event queue ordering and handler mutation rules.
- group membership validation.
- Play Scene load/unload lifecycle.

Even if the repo does not currently have a broad test harness, small command-line or framework-local tests are better than relying only on manual editor testing.

### P1: Schema Versioning And Validation

The existing scene loader ignores unknown keys. That is useful, but game-logic data needs explicit validation:

- duplicate IDs,
- missing referenced entity/component/group,
- invalid behavior initial state,
- invalid transition target,
- invalid component type,
- malformed params/config JSON,
- unsupported schema version,
- old schema migration warnings.

Add a validation report type independent of the editor UI so SceneTemplate can also report runtime load problems.

### P1: Hot Reload And Debug Pause

The existing assessment is right that Play Scene needs faster iteration. I would define:

- Reload component config from current editor snapshot.
- Force state transition from DevGui.
- Inject event from DevGui.
- Pause fixed gameplay tick while leaving render/editor UI active.
- Optionally copy selected runtime values back to editor-authored defaults only when the user explicitly requests it.

### P2: Save/Load Data Size And Editor Undo Cost

Whole-scene undo snapshots are convenient, but component-rich game scenes can become large. Long term, gameplay edits should use command-level undo or structural diffs. Early phases can use snapshots for safety.

### P2: Rendering Overlay Integration

Game overlays should use existing editor/debug infrastructure:

- `LineRenderer` / wireframe geometry for spheres, cones, ranges, and formation lines.
- `TextRenderer` or existing debug text path for labels.
- Existing depth-aware overlay conventions where possible.
- Existing `RenderEditorSceneFrame()` overlay stage.

Avoid pushing gameplay overlay requirements into the render graph unless a pass truly needs render-target data.

### P2: Platform Implications

Because T850 supports Windows, Linux/Steam Deck, and Android, gameplay systems should avoid desktop-only assumptions:

- resource/cache writes through `ResourceLocator`,
- no raw file paths for behavior/config assets,
- no editor-only ImGui dependency in Framework runtime core,
- deterministic behavior independent of graphics backend,
- graceful behavior when Jolt/Recast are compiled out.

## Suggested First Vertical Slice

Do not start with flocking, visual graph editing, or a large RTS demo. Start with a tiny playable runtime path:

1. Add stable `id` to `SceneGameEntityDesc`.
2. Add `SceneComponentDesc` with `type`, `id`, `enabled`, simple params, and optional `config_json`.
3. Add optional `SceneStateMachineDesc behavior` only if needed for the slice.
4. Add `GameLogicSystem` owned by SceneTemplate.
5. On scene load, create a registry entry per `game_entities[]` record and link to mesh/physics by current name fields.
6. Add one built-in component, such as `TransformLinkComponent` or `DebugNameComponent`, plus a simple `HealthComponent` if you want visible behavior.
7. Expose a DevGui panel listing game objects and components.
8. Save/load through T8ditor and verify Play Scene loads the same data.
9. Add validation for duplicate IDs and broken mesh/physics references.
10. Add one round-trip test or standalone verification command.

Only after that should the project add events and state transitions.

## Recommended Implementation Phases

### Phase 0 - Decisions Before Code

- Runtime owner: SceneTemplate first, not global engine-wide service.
- Fixed tick policy and ordering.
- Stable ID format.
- Component lifecycle and event dispatch rules.
- Schema version/migration rules.
- Minimal test harness approach.

Deliverable: short design amendment to the proposal docs.

### Phase 1 - Schema V2 And Validation

- Extend `EditorSceneFile.h` with stable IDs, components, optional behavior.
- Add validation functions independent of UI.
- Keep `game_groups` out until entity IDs and runtime loading are proven.
- Update `.vcxproj`/CMake only if new compiled files are added.

Deliverable: scenes can serialize/deserialize game components without runtime behavior.

### Phase 2 - Minimal Runtime GameLogicSystem

- Add `Framework/include/game` and `Framework/src/game` core files.
- Load `game_entities` in SceneTemplate.
- Link entities to loaded mesh slots and physics names.
- Add registry, component factory, fixed update stub, shutdown.
- Add DevGui listing.

Deliverable: Play Scene shows runtime game objects created from scene data.

### Phase 3 - Editor Authoring Basics

- Selection type 9 for game entities.
- Inspector identity/link/component list editor.
- Add/remove component with undo.
- Validation results panel.

Deliverable: author simple components in T8ditor, save, load, play, inspect runtime.

### Phase 4 - Events And State Machines

- Queued EventBus.
- Runtime state machine with compiled transition conditions.
- State/transition list editor.
- Runtime event log and force-transition DevGui tools.

Deliverable: one entity can transition between simple states during Play Scene.

### Phase 5 - Physics And Navigation Gameplay Integration

- Query helpers with explicit filter semantics.
- Basic path-follow component using current Detour queries.
- Line-of-sight/sensor helpers.
- Clear handling when physics/nav are unavailable.

Deliverable: a game entity can navigate or sense something using existing systems.

### Phase 6 - Groups And RTS Examples

- Add `SceneGroupDesc` with stable member IDs.
- Add group manager.
- Add formation/flock as optional example systems.
- Add group inspector and overlays.

Deliverable: squad/group demo, not required for generic engine core.

### Phase 7 - Visual State Graph And Polish

- Node graph view.
- Drag/zoom/pan interactions.
- Better component-specific editors.
- Hot reload/debug pause.
- Demo scene.

Deliverable: polished authoring workflow.

## Risk Register

| Risk | Severity | Notes |
|---|---:|---|
| Building editor UI before runtime semantics are stable | High | Visual tools will churn if component/state/event rules change. |
| Name-based gameplay references | High | Breaks rename, duplicate names, groups, saved events. |
| String-evaluated transitions per frame | Medium/High | Fine for authoring, bad for runtime hot loops. Compile after load. |
| RTS-specific systems becoming core | Medium/High | Makes the engine less generic and increases maintenance. |
| Play Scene fast path diverging from file-load path | Medium | Keep fidelity mode and tests. |
| Whole-scene undo snapshots growing too large | Medium | Use snapshots first, migrate hot paths to command diffs later. |
| Physics/nav thread races | Medium | Main-thread fixed tick with explicit job handoff first. |
| Silent schema typos | Medium | Unknown keys are ignored; add validation/warnings. |
| Project-file drift | Medium | Primary `.vcxproj` plus secondary CMake must stay aligned. |

## Scoring

| Category | Score | Assessment |
|---|---:|---|
| Architectural direction | 8/10 | Shared schema and SceneTemplate integration are right. |
| Current-code alignment | 6/10 | Good schema/editor direction, but runtime `game_entities` are not consumed yet. |
| Editor UX vision | 9/10 | Strong and detailed, but too much too early. |
| Runtime design completeness | 4/10 | Missing lifecycle, tick, event ordering, ownership, IDs. |
| Physics/nav integration | 5/10 | Good instincts, but current constraints need more precise handling. |
| Performance plan | 3/10 | Needs budgets, telemetry counters, and compiled conditions. |
| Testing/migration plan | 3/10 | Needs explicit tests and schema upgrade strategy. |
| Generic engine suitability | 5/10 | Core idea is generic; many concrete features are RTS-specific. |

Overall: 6.5/10 as an implementation proposal, 8/10 as a product/design direction.

## Final Recommendation

Keep the proposal direction, but rewrite the implementation plan around a small runtime-first vertical slice.

The best first milestone is not a full game-logic editor. It is a SceneTemplate-owned `GameLogicSystem` that loads current `game_entities`, creates stable runtime game objects, links them to render/physics resources, exposes them in DevGui, validates references, and shuts down cleanly. Once that is real, component editing, state machines, event logs, overlays, hot reload, and RTS group behavior have a solid place to attach.

The existing `assessment.md` is a strong critique. My additions are mainly source-grounded: current game entities are editor-authored metadata only, the runtime game module does not exist, editor game groups are mesh-index groups rather than gameplay groups, Play Scene's temp JSON path is both a bottleneck and a valuable fidelity test, and the implementation should preserve MSBuild project hygiene while recognizing the secondary CMake files.