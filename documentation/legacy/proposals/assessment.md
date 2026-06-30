# Game Logic & Editor Integration Proposals — Assessment

**Date:** 2026-06-29
**Author:** Copilot Analysis
**Proposals Reviewed:**
- `game-logic-and-ai-integration.md` — Runtime game logic system (components, state machines, AI, groups)
- `editor-game-logic-integration.md` — T8ditor integration for game logic authoring

---

## Executive Summary

Both proposals are **exceptionally well-documented** with professional-quality ASCII mockups, code examples, file structures, and phased effort estimates (~40.5 days for editor alone). The core architectural decisions (shared Framework types, Glaze JSON serialization, component-based extension) align well with T850's existing patterns.

However, the proposals have significant gaps in **thread safety**, **performance profiling**, **Play Scene iteration speed**, and are **heavily RTS-specific** despite positioning themselves as a generic game engine feature.

**Overall Verdict: Strong Foundation, Needs Hardening.** The proposals are ~80% ready. The remaining 20% is infrastructure: threading, performance, testing, schema versioning, and Play Scene iteration speed.

---

## ✅ What Makes Sense (Architectural Alignment)

### 1. Shared Framework Schema Pattern

The proposal correctly identifies `EditorSceneFile.h` as the shared contract between Editor and Runtime. This follows the existing pattern where `EditorWorld` owns `std::vector<t850::scene::SceneGameEntityDesc>` — the same approach used for meshes, cameras, and lights.

**Verdict:** ✅ Correct approach. No new format duplication.

### 2. Glaze JSON Serialization

Using Glaze `Object` macros for all new types is consistent with the codebase. The `.t8scene` format extension is clean and follows established conventions.

**Verdict:** ✅ Consistent with existing serialization strategy.

### 3. Layer Separation

The Framework → Editor → Runtime layering is respected:

| Layer | Responsibility | Examples |
|-------|---------------|----------|
| **Framework** | Shared types + runtime implementation | `EditorSceneFile.h`, `Framework/game/` |
| **Editor** | Authoring UI, validation, undo | `T8ditor/` panels |
| **Runtime** | GameLogicSystem, components, gameplay | `SceneTemplate` |

**Verdict:** ✅ Clean separation matching T850 architecture.

### 4. Selection Type Extension

Adding `type 9 = gameEntity` and `type 10 = gameGroup` follows the existing selection enum pattern (0–8 already defined for mesh, camera, light, physics, nav, spline, lightCamera, splinePoint, godRays).

**Verdict:** ✅ Follows established convention.

### 5. Undo System Design

Specific command structs (`AddComponentCommand`, `RemoveComponentCommand`, `EditComponentCommand`, etc.) with clear Undo/Redo methods match the existing editor undo pattern.

**Verdict:** ✅ Correct approach following editor conventions.

### 6. Play Scene Flow Integration

The proposal correctly identifies the existing Play Scene flow:

```
EditorWorld → BuildEditorSceneSnapshot() → .t8scene JSON → SceneTemplate
```

Extending this to call `GameLogicSystem::OnLoadScene()` is the right integration point.

**Verdict:** ✅ Correct integration with existing Play Scene lifecycle.

---

## ✅ What's Done Right (Positive Aspects)

### 1. Documentation Quality ⭐⭐⭐⭐⭐

The ASCII wireframes for every panel (hierarchy, inspector, state machine, validation, event log, play scene) are **exceptional**. This level of visual design documentation is rare and incredibly valuable for implementation.

### 2. Minimal Current Schema Recognition

The proposal correctly identifies that `SceneGameEntityDesc` is currently very minimal (no components array, no state machine, no group membership beyond physics entities). The extension plan is logical.

### 3. Comprehensive Validation System

The validation system with Error/Warning/Info severity levels, entity-specific reports, and "Jump to Entity" actions is professional-grade. Often overlooked in engine proposals.

### 4. Debug Overlays

Wireframe visualization for sensor radius, combat range, health bars, state labels, and formation lines in the viewport is excellent for gameplay balancing and debugging.

### 5. State Machine Visualizer

Dedicated graph view (nodes + edges) with drag, zoom, pan, and detail panels is a professional feature that many engines lack.

### 6. Component-Based Design

Extending to a generic component system rather than hardcoding properties is architecturally sound. Components are data-driven (`SceneComponentDesc`) and editor-friendly.

### 7. Phased Implementation

Breaking ~40 days of editor work into 7 phases (E1–E7) with clear dependencies and deliverables is excellent project management.

### 8. EventBus Abstraction

Type-safe EventBus with combat/selection/AI events provides clean decoupling between systems. Good foundation for game logic messaging.

---

## ❌ What's Missing (Critical Gaps)

---

### Gap 1: Component Params Are Too Restrictive ★★

`SceneComponentDesc` uses `std::map<std::string, std::string> params` for configuration. This is **insufficient** for:

- Nested data structures (vectors of values, arrays of configs)
- Binary data (mesh references, material IDs)
- Complex behaviors (ability definitions, skill trees, trigger chains)

**Example Problem:** A `StatusEffectComponent` with multiple effects, each with duration, severity, stacking rules, and resistance checks — this doesn't fit in flat key-value string pairs.

**Recommendation:** Use `std::variant` or a structured config type per component, OR use nested JSON objects that Glaze can serialize:

```cpp
struct SceneComponentDesc {
    std::string id;
    std::string type;
    bool enabled;
    // Keep for simple cases
    std::map<std::string, std::string> params;
    // For complex nested configs
    std::string config_json;
};
```

---

### Gap 2: Play Scene JSON Round-Trip Bottleneck ★★★

The proposal shows:

```
EditorWorld → BuildEditorSceneSnapshot() → .t8scene JSON → SceneTemplate
```

This means **every "Play" click does full JSON serialization/deserialization**. For rapid game logic iteration (dozens of Play/Stop cycles per hour), this adds significant latency.

**Recommendation:** For Play Scene specifically, bypass JSON serialization and use direct memory sharing:

```cpp
// Option A: Direct struct copy (fast, but runtime mutation risk)
SceneTemplate->gameLogicSystem->OnLoadScene(editorWorld.GetSceneData());

// Option B: Copy-on-write (safe, moderate overhead)
auto sceneCopy = editorWorld.CloneSceneData();
SceneTemplate->gameLogicSystem->OnLoadScene(std::move(sceneCopy));
```

Only use `.t8scene` JSON for **save/load to disk**, not for Play Scene iteration.

---

### Gap 3: No Thread Safety Discussion ★★★

**Zero mention of threading** in either proposal. This is critical because:

| System | Threading Concern |
|--------|------------------|
| **Jolt Physics** | Runs on separate threads with lockstep simulation |
| **Detour Pathfinding** | Can be expensive for complex meshes (hundreds of ms) |
| **EventBus** | Publishing from one thread while another subscribes = data race |
| **State Machines** | Transitions that query physics/nav = race conditions |

**Specific Questions Unanswered:**

- How does `UpdateSpatialGrid()` handle concurrent access?
- Does `GroupManager::UpdateFormations()` hold locks during transform writes?
- Is `EventBus::Publish()` thread-safe?
- How are physics body updates synchronized with game logic state changes?

**Recommendation:** Add a threading model section specifying:

- GameLogicSystem runs on main thread
- Physics queries use Jolt's `BodyInterface` (thread-safe queue)
- Nav pathfinding uses async requests or worker thread pool
- EventBus uses lock-free queue or mutex protection
- Clear ownership rules for data modified by multiple systems

---

### Gap 4: No Performance Profiling or Targets ★★

The proposal shows `SpatialGrid` for spatial partitioning but lacks:

- **Entity count targets:** What's the expected entity count? 10? 100? 1,000?
- **Query cost analysis:** What is the expected complexity per system per frame?
- **Benchmark strategy:** How do we know it's fast enough?
- **Memory targets:** Component allocation patterns, cache locality, SIMD utilization

**Specific Concerns:**

- EventBus with 1,000 entities each subscribing to combat events = message explosion
- State machine polling with string-based condition evaluation = hash map lookups per tick
- Group formation repositioning with 50+ members = transform calculation bottleneck

**Recommendation:** Add performance budget section:

```
Target: 1,000 entities, 60 FPS, <1ms total game logic update
  - SpatialGrid:       <0.1ms (uniform grid, O(1) queries)
  - StateMachine:      <0.1ms (state lookup by enum, not string)
  - GroupFormation:    <0.2ms (dirty flag update)
  - EventBus:          <0.2ms (per-type, no global broadcast storm)
  - Sensor/Query:      <0.1ms (spatial grid + early exit)
  - Total budget:      ~1ms reserve
```

---

### Gap 5: Component Lifecycle & Event Flow Unclear ★★

**Missing Documentation:**

- When a `HealthComponent`'s `currentHp` drops to 0, how does it notify "entity is dead"? Via EventBus? Direct callback?
- When does `OnComponentEnable()` fire? During component creation or later?
- What is the destruction order? Components → GameObject → PrimitiveInst? Or reverse?
- How are component dependencies handled? (e.g., `CombatComponent` requires `HealthComponent` to exist)

**Recommendation:** Add component lifecycle diagram:

```
OnLoadScene()
  → CreateGameObject()
    → CreateComponents()
      → Component::OnAttach(gameObject)
      → Component::OnCreate()
  → RegisterWithEventBus()
  → SetInitialState()

OnUpdate(dt)
  → UpdateComponents() [order: transform → physics → sensor → AI → combat]
  → ProcessEvents()
  → CleanupDeadEntities()
    → Component::OnDestroy()
    → Component::OnDetach()
  → UnregisterFromEventBus()
  → DestroyGameObject()
```

---

### Gap 6: No Schema Versioning Strategy ★★

**Problem:** What happens when you load a `.t8scene` saved with an older version that doesn't have `components`, `behavior`, or `game_groups`?

Glaze JSON will fail to deserialize missing fields if `glz::Object` macros are strict. Need:

- Version field in `EditorSceneFile` (currently has `version` but no migration logic)
- Migration functions for version upgrades
- Backward compatibility for older scene files

**Recommendation:** Add version field and migration:

```cpp
struct EditorSceneFile {
    uint32_t version = 2;  // Increment on schema change
    // ...
};

void MigrateSceneFile(EditorSceneFile& scene, uint32_t fromVersion) {
    if (fromVersion < 2) {
        // Add default components for entities without any
        // Migrate 'ai' string field to behavior state machine
    }
}
```

---

### Gap 7: No Testing Strategy ★★

**Zero mention of testing.** For a system this complex, you need:

| Test Type | Scope |
|-----------|-------|
| **Unit Tests** | State machine transition logic, component init, group formation math |
| **Integration Tests** | Play Scene load/unload cycle, entity create/destroy lifecycle |
| **Deterministic Replay** | Same input → same state transitions (critical for AI debugging) |

**Recommendation:** Add testing chapter:

- State machine: Test all state transitions and priority resolution
- Group formation: Verify member positioning math
- EventBus: Test pub/sub with multiple handlers
- Integration: Scene save/load round-trip test
- Performance: Frame time budget test with 1,000 entities

---

### Gap 8: Entity ID Strategy Underdefined ★

The proposal uses `uint32_t` `GameObjectID` but doesn't specify:

- Allocation strategy? (Auto-incrementing counter, free list, UUID?)
- ID reuse? (If GameObject A is destroyed, can a new GameObject get the same ID?)
- Entity name vs ID? (Names are used as identifiers in some places — `group.member_entity_ids` — but names can be duplicated)

**Problem:** Using entity names as group member references (`std::string`) is fragile. What if two entities have the same name? What if a name changes during editing?

**Recommendation:** Use stable IDs:

```cpp
struct SceneGameEntityDesc {
    UUID uuid;            // Stable across save/load
    uint32_t runtime_id;  // Assigned at runtime (free-list based)
    std::string name;     // Display name only
    // ...
};

struct SceneGroupDesc {
    std::vector<UUID> member_uuids;  // Reference by UUID, not name
    // ...
};
```

---

### Gap 9: Hot Reload Gap ★

For rapid iteration, developers need:

- Hot reload of state machine configs without restarting Play Scene
- Runtime component parameter tweaking (like Unity's Inspector during play mode)
- Debug pause → edit state → resume capability

**Current Proposal:** Play Scene "Stop" destroys all GameObjects and restores editor state. This means losing runtime state on every stop.

**Recommendation:** Add "Debug Pause" mode where Play Scene pauses but Inspector can still edit component values, force state transitions, or inject events.

---

### Gap 10: Multi-World / Multi-Scene Support ★

What if a game has multiple "worlds" or "maps" with game entities? The current design seems to assume a single scene. Proposal doesn't address:

- Entity transfer between scenes
- Persistent entities (player character that survives scene changes)
- Scene transition during gameplay

*(May be out of scope for v1 — but should be stated as a known limitation.)*

---

## ⚠️ Risks & Concerns

### Risk 1: Heavy SC2/RTS Bias ★★★

The proposal is **heavily StarCraft 2-focused**:

- Group commands (move, attack, hold, spread)
- Flocking algorithms (Boids)
- Formation types (wedge, line, column, box, circle)
- Sensor/detection systems with FOV
- Health/combat/status effects with damage types
- Resource components
- Harvest/build/repair states

**Impact:** The file count shows ~80–90% of the content is RTS-specific. For other genres (FPS, RPG, platformer, racing), most of this is irrelevant.

**Mitigation:** The core component/state machine framework IS generic. The RTS-specific components should be clearly marked as runtime examples. Suggest restructuring:

```
Framework/game/Core/          ← Generic (GameObject, Component, StateMachine)
Framework/game/RTS/           ← RTS examples (Formation, Flocking, Combat)
```

### Risk 2: Frame Rate Independence Gap ★★

The proposal shows `OnUpdate(float dt)` but doesn't detail:

- How state transition timers use `dt` (accumulated delta? Fixed vs variable timestep?)
- How combat cooldowns use `dt` (per tick or frame-based?)
- How health regen uses `dt` (accumulated or per-frame?)

**Recommendation:** Add fixed timestep for game logic:

```cpp
void GameLogicSystem::Update(float dt) {
    accumulator_ += dt;
    while (accumulator_ >= fixedDt_) {
        UpdateStateMachines(fixedDt_);
        UpdateGroups(fixedDt_);
        accumulator_ -= fixedDt_;
    }
}
```

### Risk 3: State Machine String Evaluation Performance ★

Transition conditions use string-based evaluation (`"on_event:get_transform()"`) which requires:

- Hash lookup or string comparison per state per frame
- Conditional parsing and evaluation at runtime

For 1,000 entities × 4 states × 3 transitions each = 12,000 string evaluations per frame.

**Recommendation:** Pre-compile conditions to enums or function pointers:

```cpp
struct SceneTransitionRuleDesc {
    enum class ConditionType {
        OnEvent,
        OnTimer,
        OnHealthThreshold,
        OnDistance
    };
    ConditionType type;
    uint32_t event_id;         // Pre-resolved enum, not string
    float threshold_value;     // Pre-parsed float
    // ...
};
```

---

## 📊 Summary Assessment

| Category | Score | Notes |
|----------|-------|-------|
| **Documentation Quality** | ⭐⭐⭐⭐⭐ | Exceptional ASCII mockups, code examples, file structures |
| **Architectural Alignment** | ⭐⭐⭐⭐ | Respects Framework/Editor/Runtime layering |
| **Editor UX Design** | ⭐⭐⭐⭐⭐ | Professional-grade panels, validation, overlays |
| **Thread Safety** | ⭐ | Not addressed |
| **Performance Strategy** | ⭐⭐ | Missing targets and profiling |
| **Play Scene Iteration** | ⭐⭐ | JSON round-trip too slow for rapid iteration |
| **Generality vs RTS Bias** | ⭐⭐ | Heavy SC2 bias, core is generic |
| **Testing** | ⭐ | Not addressed |
| **Schema Evolution** | ⭐⭐ | No versioning or migration |
| **Component Lifecycle** | ⭐⭐ | Partially addressed |

---

## 🎯 Priority Fixes Before Implementation

| Priority | Fix | Reason |
|----------|-----|--------|
| **P0** | Address thread safety model | Required for Jolt Physics integration |
| **P0** | Fix Play Scene JSON bottleneck | Critical for developer UX |
| **P1** | Add performance targets and profiling plan | Prevent runtime bottlenecks |
| **P1** | Add component lifecycle documentation | Required for correct implementation |
| **P2** | Add schema versioning strategy | Required for save/load compatibility |
| **P2** | Move RTS-specific components to example/demo status | Clarify generic vs specific |

---

## 📝 Final Recommendations

1. **Keep the generic component/state machine framework** — The core design of `GameObject`, `Component`, `StateMachine`, and `EventBus` is solid and genre-agnostic.

2. **Move RTS-specific components to a separate module** — Clearly mark FormationManager, FlockSystem, SensorComponent, CombatComponent, etc. as RTS runtime examples, not core engine features.

3. **Address the JSON I/O bottleneck for Play Scene iteration** — Use direct memory sharing (copy-on-write) for Play Scene, reserve `.t8scene` JSON for disk save/load only.

4. **Add thread safety documentation** — Specify which systems run on which threads, what requires locking, and how to avoid race conditions with Jolt Physics.

5. **Add performance profiling targets and testing strategy** — Define entity count budgets, frame time allocations, and unit/integration test plan.

6. **Consider ECS pattern for large-scale entity management** — Data-oriented design may be needed for 1,000+ entities with cache-local component storage.

7. **Add schema versioning for save/load compatibility** — Increment version on schema change, provide migration functions.

---

*Generated from analysis of `game-logic-and-ai-integration.md` and `editor-game-logic-integration.md`.*