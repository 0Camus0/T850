# T850 Game Entity System — Implementation Prompts (for local coding agents)

Status: completed implementation archive; audited on 2026-08-19.

P0-P14 have been executed. Use these blocks as acceptance contracts for maintenance, not as evidence that a task is still unimplemented. Current results are recorded in [Current status and roadmap](../current-status-and-roadmap.md).

## How to use this document

Do not execute P0-P14 as an implementation roadmap: all blocks have already been completed. They are preserved as historical acceptance contracts that explain the intended dependency order and original gates.

For maintenance:

1. Start from the current [game entity system specification](game-entity-system-spec.md), owning source, and nearest self-test.
2. Consult only the prompt covering the affected slice when its original acceptance criteria are useful.
3. Treat current source, the specification's implementation record, and the operational skills as authoritative when a historical prompt differs.
4. Run the focused current gate from [Verification](../testing/verification.md); do not recreate files or rerun completed milestones merely because a block says `CREATE` or `EDIT`.

Original prompt map (each maps to the delivered record in [§15](game-entity-system-spec.md#15-implementation-record)):

| Prompt | Spec milestone | Adds files? | Build gate |
|---|---|---|---|
| P0 | baseline | no | x64 + ARM64 |
| P1 | M1 schema | no (edits header) | x64 + ARM64 |
| P2 | M1 ids/validation | yes | x64 + ARM64 |
| P3 | M1 self-test harness | yes | x64 + ARM64 + selftest |
| P4 | M2 registry/components | yes | x64 + ARM64 |
| P5 | M2 system/events | yes | x64 + ARM64 |
| P6 | M2 scene integration | yes | x64 + ARM64 + selftest |
| P7 | M3 control/movement | yes | x64 + ARM64 + selftest |
| P8 | M4 components/events | yes | x64 + ARM64 + selftest |
| P9 | M5 state machines | yes | x64 + ARM64 + selftest |
| P10 | M6 physics layers/queries | yes | x64 + ARM64 + selftest |
| P11 | M7 navigation | yes | x64 + ARM64 + selftest |
| P12 | M8 groups/examples | yes | x64 + ARM64 |
| P13 | M9 editor authoring | yes | x64 + ARM64 |
| P14 | M10 polish + full sanity | maybe | **x64 + ARM64 + Steam Deck + Android** |

---

## PROMPT P0 — Baseline build (no code changes)

```
=== SHARED PREAMBLE (identical in every prompt — do not modify) ===
You are implementing the T850 Game Entity & Logic System. The attached file
game-entity-system-spec.md is the authoritative spec: use its exact code,
signatures, namespaces, and diagrams. When a task says "per §X", open that
section and implement it verbatim; do not invent alternative designs.

PATHS
- REPO_ROOT = the git root (folder containing documentation/ and BuildAndroidFastApk.ps1).
- SRC = <REPO_ROOT>/T850  (contains T850.sln, scripts/, Framework/, DayScene/, T8ditor/).
- Game core headers -> SRC/Framework/include/game/*.h
- Game core sources -> SRC/Framework/src/game/*.cpp
- Game examples     -> SRC/Framework/include/game/examples/*.h and SRC/Framework/src/game/examples/*.cpp

HARD RULES
1. C++23. namespace t850::game for runtime code, t850::scene for schema types.
2. game/core must NOT include anything from game/examples, T8ditor, or ImGui.
3. Every NEW .h/.cpp under Framework/ MUST be registered in BOTH build systems in
   the SAME change, following spec Appendix F:
     - SRC/Framework/Framework.vcxproj  (<ClInclude> for .h, <ClCompile> for .cpp)
     - SRC/Framework/CMakeLists.txt     (add .cpp to set(FRAMEWORK_SOURCES ...))
   Files added under DayScene/ or T8ditor/ update their own .vcxproj + CMakeLists.
4. Do NOT edit files unrelated to the current task. Do NOT reformat existing code.
5. Prefer std::unique_ptr, std::optional, std::string_view, std::span, std::unordered_map.
6. No third-party ECS/reflection/UUID/scripting deps. IDs are strings via MakeStableId.

BUILD GATE (run from SRC after finishing the task; spec Appendix E):
   pwsh -File scripts\build.ps1 -Config Debug -Platform x64
   pwsh -File scripts\build.ps1 -Config Debug -Platform ARM64
Both MUST succeed with no new warnings-as-errors. Fix compile/link errors before
reporting done. If a symbol links on x64 but fails ARM64, a .cpp is missing from
CMakeLists.txt (rule 3).

SELF-TEST (only when the task adds/changes tests): after building, run the DayScene
output with `--game-selftest` and confirm exit code 0 (spec §13).

DEFINITION OF DONE (report back):
- list of files created/edited (with the build-file entries added),
- x64 and ARM64 build results,
- selftest output if applicable,
- anything you could not complete and why. Do not claim done if a build failed.

=== TASK P0 ===
TASK: Establish a known-good baseline. Make NO source changes.

STEPS
1. From SRC, run the BUILD GATE (x64 + ARM64, Debug).
2. Report whether the current tree builds clean on both platforms.
3. If either fails, capture the first 20 lines of the error and STOP — do not
   attempt fixes; the baseline must be green before feature work starts.

DONE WHEN: both x64 and ARM64 Debug builds succeed with the unmodified tree.
```

---

## PROMPT P1 — M1 schema types (header-only, no new files)

```
=== SHARED PREAMBLE (identical in every prompt — do not modify) ===
You are implementing the T850 Game Entity & Logic System. The attached file
game-entity-system-spec.md is the authoritative spec: use its exact code,
signatures, namespaces, and diagrams. When a task says "per §X", open that
section and implement it verbatim; do not invent alternative designs.

PATHS
- REPO_ROOT = the git root (folder containing documentation/ and BuildAndroidFastApk.ps1).
- SRC = <REPO_ROOT>/T850  (contains T850.sln, scripts/, Framework/, DayScene/, T8ditor/).
- Game core headers -> SRC/Framework/include/game/*.h
- Game core sources -> SRC/Framework/src/game/*.cpp
- Game examples     -> SRC/Framework/include/game/examples/*.h and SRC/Framework/src/game/examples/*.cpp

HARD RULES
1. C++23. namespace t850::game for runtime code, t850::scene for schema types.
2. game/core must NOT include anything from game/examples, T8ditor, or ImGui.
3. Every NEW .h/.cpp under Framework/ MUST be registered in BOTH build systems in
   the SAME change, following spec Appendix F:
     - SRC/Framework/Framework.vcxproj  (<ClInclude> for .h, <ClCompile> for .cpp)
     - SRC/Framework/CMakeLists.txt     (add .cpp to set(FRAMEWORK_SOURCES ...))
   Files added under DayScene/ or T8ditor/ update their own .vcxproj + CMakeLists.
4. Do NOT edit files unrelated to the current task. Do NOT reformat existing code.
5. Prefer std::unique_ptr, std::optional, std::string_view, std::span, std::unordered_map.
6. No third-party ECS/reflection/UUID/scripting deps. IDs are strings via MakeStableId.

BUILD GATE (run from SRC after finishing the task; spec Appendix E):
   pwsh -File scripts\build.ps1 -Config Debug -Platform x64
   pwsh -File scripts\build.ps1 -Config Debug -Platform ARM64
Both MUST succeed with no new warnings-as-errors. Fix compile/link errors before
reporting done. If a symbol links on x64 but fails ARM64, a .cpp is missing from
CMakeLists.txt (rule 3).

SELF-TEST (only when the task adds/changes tests): after building, run the DayScene
output with `--game-selftest` and confirm exit code 0 (spec §13).

DEFINITION OF DONE (report back):
- list of files created/edited (with the build-file entries added),
- x64 and ARM64 build results,
- selftest output if applicable,
- anything you could not complete and why. Do not claim done if a build failed.

=== TASK P1 ===
TASK: Add the game-logic schema types to the shared scene schema per spec §6.
This edits ONE existing header; no new files, so no build-file changes.

EDIT: SRC/Framework/include/scene/EditorSceneFile.h
- Add these structs (exact fields from §6.1 and §6.2), in namespace t850::scene,
  ABOVE struct EditorSceneFile:
    SceneComponentDesc, SceneControlDesc, SceneStateDesc, SceneTransitionDesc,
    SceneStateMachineDesc, SceneFlockConfigDesc, SceneFormationConfigDesc,
    SceneGroupDesc, SceneSpatialGridSettingsDesc, SceneGameLogicSettingsDesc.
- Extend struct SceneGameEntityDesc with the v2 fields from §6.2:
    id, team, control, group_id, components, behavior (std::optional<SceneStateMachineDesc>).
  Keep ALL existing fields unchanged (including legacy `ai`).
- Extend struct EditorSceneFile per §6.3: add
    std::vector<SceneGroupDesc> game_groups;
    std::optional<SceneGameLogicSettingsDesc> game_logic_settings;
  Do NOT change `int version = 1;` here (migration bumps it at runtime).
- Ensure <optional>, <map>, <vector>, <string> includes are present.

NOTES
- Glaze serializes by reflection (no macros needed). New std types must be
  default-constructible with the defaults shown in §6.
- Do not touch EditorSceneFile.cpp in this prompt.

BUILD GATE: x64 + ARM64 Debug.
DONE WHEN: both build; EditorSceneFile.h contains all §6 types and the extended
EditorSceneFile compiles where it is already used.
```

---

## PROMPT P2 — M1 identity, validation, migration (new files)

```
=== SHARED PREAMBLE (identical in every prompt — do not modify) ===
You are implementing the T850 Game Entity & Logic System. The attached file
game-entity-system-spec.md is the authoritative spec: use its exact code,
signatures, namespaces, and diagrams. When a task says "per §X", open that
section and implement it verbatim; do not invent alternative designs.

PATHS
- REPO_ROOT = the git root (folder containing documentation/ and BuildAndroidFastApk.ps1).
- SRC = <REPO_ROOT>/T850  (contains T850.sln, scripts/, Framework/, DayScene/, T8ditor/).
- Game core headers -> SRC/Framework/include/game/*.h
- Game core sources -> SRC/Framework/src/game/*.cpp
- Game examples     -> SRC/Framework/include/game/examples/*.h and SRC/Framework/src/game/examples/*.cpp

HARD RULES
1. C++23. namespace t850::game for runtime code, t850::scene for schema types.
2. game/core must NOT include anything from game/examples, T8ditor, or ImGui.
3. Every NEW .h/.cpp under Framework/ MUST be registered in BOTH build systems in
   the SAME change, following spec Appendix F:
     - SRC/Framework/Framework.vcxproj  (<ClInclude> for .h, <ClCompile> for .cpp)
     - SRC/Framework/CMakeLists.txt     (add .cpp to set(FRAMEWORK_SOURCES ...))
   Files added under DayScene/ or T8ditor/ update their own .vcxproj + CMakeLists.
4. Do NOT edit files unrelated to the current task. Do NOT reformat existing code.
5. Prefer std::unique_ptr, std::optional, std::string_view, std::span, std::unordered_map.
6. No third-party ECS/reflection/UUID/scripting deps. IDs are strings via MakeStableId.

BUILD GATE (run from SRC after finishing the task; spec Appendix E):
   pwsh -File scripts\build.ps1 -Config Debug -Platform x64
   pwsh -File scripts\build.ps1 -Config Debug -Platform ARM64
Both MUST succeed with no new warnings-as-errors. Fix compile/link errors before
reporting done. If a symbol links on x64 but fails ARM64, a .cpp is missing from
CMakeLists.txt (rule 3).

SELF-TEST (only when the task adds/changes tests): after building, run the DayScene
output with `--game-selftest` and confirm exit code 0 (spec §13).

DEFINITION OF DONE (report back):
- list of files created/edited (with the build-file entries added),
- x64 and ARM64 build results,
- selftest output if applicable,
- anything you could not complete and why. Do not claim done if a build failed.

=== TASK P2 ===
TASK: Add stable-id helpers, validation, and migration per spec §5.1, §6.4, §6.5.

CREATE (register BOTH build systems per rule 3 / Appendix F):
- SRC/Framework/include/game/GameIds.h        -> RuntimeGameObjectId, kInvalid..., MakeStableId (§5.1)
- SRC/Framework/src/game/GameIds.cpp          -> MakeStableId impl (hex from a counter+time; NO external UUID dep)
- SRC/Framework/include/game/GameValidation.h -> declares, in t850::scene:
      SceneValidationSeverity, SceneValidationIssue, SceneValidationReport (§6.5),
      SceneValidationReport ValidateEditorSceneGameLogic(const EditorSceneFile&);
      bool MigrateEditorSceneGameLogic(EditorSceneFile&, std::string* log=nullptr);   // §6.4
      constexpr int kSceneSchemaV1=1; constexpr int kSceneSchemaV2_GameLogic=2;
- SRC/Framework/src/game/GameValidation.cpp   -> implement validation checks (§6.5 list)
      and migration steps (§6.4): assign ge_/comp_ ids, map legacy `ai`
      ("player"->control.mode="player"; "nav_agent"->"ai" + default movement+path_follow
      components if none; ""->"none"), set version=2.

BUILD GATE: x64 + ARM64 Debug.
DONE WHEN: both build; a scene with duplicate entity ids reports an Error and a
v1 scene gains ids after MigrateEditorSceneGameLogic (verified by P3 tests).
```

---

## PROMPT P3 — M1 self-test harness + schema/validation tests

```
=== SHARED PREAMBLE (identical in every prompt — do not modify) ===
You are implementing the T850 Game Entity & Logic System. The attached file
game-entity-system-spec.md is the authoritative spec: use its exact code,
signatures, namespaces, and diagrams. When a task says "per §X", open that
section and implement it verbatim; do not invent alternative designs.

PATHS
- REPO_ROOT = the git root (folder containing documentation/ and BuildAndroidFastApk.ps1).
- SRC = <REPO_ROOT>/T850  (contains T850.sln, scripts/, Framework/, DayScene/, T8ditor/).
- Game core headers -> SRC/Framework/include/game/*.h
- Game core sources -> SRC/Framework/src/game/*.cpp
- Game examples     -> SRC/Framework/include/game/examples/*.h and SRC/Framework/src/game/examples/*.cpp

HARD RULES
1. C++23. namespace t850::game for runtime code, t850::scene for schema types.
2. game/core must NOT include anything from game/examples, T8ditor, or ImGui.
3. Every NEW .h/.cpp under Framework/ MUST be registered in BOTH build systems in
   the SAME change, following spec Appendix F:
     - SRC/Framework/Framework.vcxproj  (<ClInclude> for .h, <ClCompile> for .cpp)
     - SRC/Framework/CMakeLists.txt     (add .cpp to set(FRAMEWORK_SOURCES ...))
   Files added under DayScene/ or T8ditor/ update their own .vcxproj + CMakeLists.
4. Do NOT edit files unrelated to the current task. Do NOT reformat existing code.
5. Prefer std::unique_ptr, std::optional, std::string_view, std::span, std::unordered_map.
6. No third-party ECS/reflection/UUID/scripting deps. IDs are strings via MakeStableId.

BUILD GATE (run from SRC after finishing the task; spec Appendix E):
   pwsh -File scripts\build.ps1 -Config Debug -Platform x64
   pwsh -File scripts\build.ps1 -Config Debug -Platform ARM64
Both MUST succeed with no new warnings-as-errors. Fix compile/link errors before
reporting done. If a symbol links on x64 but fails ARM64, a .cpp is missing from
CMakeLists.txt (rule 3).

SELF-TEST (only when the task adds/changes tests): after building, run the DayScene
output with `--game-selftest` and confirm exit code 0 (spec §13).

DEFINITION OF DONE (report back):
- list of files created/edited (with the build-file entries added),
- x64 and ARM64 build results,
- selftest output if applicable,
- anything you could not complete and why. Do not claim done if a build failed.

=== TASK P3 ===
TASK: Add the v1 CLI self-test harness (spec §13) and the first tests.

CREATE (register BOTH build systems):
- SRC/Framework/include/game/GameSelfTest.h -> int RunGameSelfTests();  // returns failure count
- SRC/Framework/src/game/GameSelfTest.cpp   -> a static list of {id, fn} test cases;
      RunGameSelfTests() runs all, prints "PASS <id>" / "FAIL <id>: <msg>", returns fails.
  Implement these cases (assert with clear messages):
      T-SCHEMA-01, T-SCHEMA-02, T-SCHEMA-03, T-VALID-01, T-VALID-02, T-VALID-03,
      T-VALID-04   (definitions in spec §13; use §6.4/§6.5 semantics).
  Use LoadEditorSceneFile/SaveEditorSceneFile round-trips against temp files in the
  system temp dir; do not depend on Assets.

EDIT: SRC/DayScene/App.cpp
- Before window/engine init, if argv contains "--game-selftest":
      int fails = t850::game::RunGameSelfTests();
      return fails == 0 ? 0 : 1;   // exit immediately, no window.

BUILD GATE: x64 + ARM64 Debug, THEN run the DayScene x64 output with --game-selftest.
DONE WHEN: both build and `DayScene.exe --game-selftest` prints all PASS and exits 0.
```

---

## PROMPT P4 — M2 GameObject, registry, component base, factory

```
=== SHARED PREAMBLE (identical in every prompt — do not modify) ===
You are implementing the T850 Game Entity & Logic System. The attached file
game-entity-system-spec.md is the authoritative spec: use its exact code,
signatures, namespaces, and diagrams. When a task says "per §X", open that
section and implement it verbatim; do not invent alternative designs.

PATHS
- REPO_ROOT = the git root (folder containing documentation/ and BuildAndroidFastApk.ps1).
- SRC = <REPO_ROOT>/T850  (contains T850.sln, scripts/, Framework/, DayScene/, T8ditor/).
- Game core headers -> SRC/Framework/include/game/*.h
- Game core sources -> SRC/Framework/src/game/*.cpp
- Game examples     -> SRC/Framework/include/game/examples/*.h and SRC/Framework/src/game/examples/*.cpp

HARD RULES
1. C++23. namespace t850::game for runtime code, t850::scene for schema types.
2. game/core must NOT include anything from game/examples, T8ditor, or ImGui.
3. Every NEW .h/.cpp under Framework/ MUST be registered in BOTH build systems in
   the SAME change, following spec Appendix F:
     - SRC/Framework/Framework.vcxproj  (<ClInclude> for .h, <ClCompile> for .cpp)
     - SRC/Framework/CMakeLists.txt     (add .cpp to set(FRAMEWORK_SOURCES ...))
   Files added under DayScene/ or T8ditor/ update their own .vcxproj + CMakeLists.
4. Do NOT edit files unrelated to the current task. Do NOT reformat existing code.
5. Prefer std::unique_ptr, std::optional, std::string_view, std::span, std::unordered_map.
6. No third-party ECS/reflection/UUID/scripting deps. IDs are strings via MakeStableId.

BUILD GATE (run from SRC after finishing the task; spec Appendix E):
   pwsh -File scripts\build.ps1 -Config Debug -Platform x64
   pwsh -File scripts\build.ps1 -Config Debug -Platform ARM64
Both MUST succeed with no new warnings-as-errors. Fix compile/link errors before
reporting done. If a symbol links on x64 but fails ARM64, a .cpp is missing from
CMakeLists.txt (rule 3).

SELF-TEST (only when the task adds/changes tests): after building, run the DayScene
output with `--game-selftest` and confirm exit code 0 (spec §13).

DEFINITION OF DONE (report back):
- list of files created/edited (with the build-file entries added),
- x64 and ARM64 build results,
- selftest output if applicable,
- anything you could not complete and why. Do not claim done if a build failed.

=== TASK P4 ===
TASK: Add the runtime object/component core per spec §5.2, §5.3.

CREATE (register BOTH build systems):
- include/game/GameObject.h          -> GameObjectLinks, GameObject (§5.2)
- include/game/Component.h           -> ComponentUpdatePhase, Component, ComponentTypeInfo,
                                        ComponentLoadContext, ComponentFactoryFn (§5.3)
- include/game/ComponentFactory.h    -> ComponentFactoryRegistry (§5.3) + UnknownComponent decl
- include/game/GameObjectRegistry.h  -> GameObjectRegistry (§5.2)
- src/game/GameObjectRegistry.cpp    -> Create/Get/FindBySceneId/RequestDestroy/ApplyDeferredDestroys
- src/game/ComponentFactory.cpp      -> Register/Create; unknown type => warning + UnknownComponent
                                        that preserves type/params/config_json (§5.3)

RULES
- No update loop yet; this is data + lookup + factory only.
- Link fields reference existing engine types: t850::PrimitiveInst*, PhysicsBodyHandle
  (do NOT create or own them here).

BUILD GATE: x64 + ARM64 Debug.
DONE WHEN: both build; registry compiles and factory returns UnknownComponent for
an unregistered type without throwing.
```

---

## PROMPT P5 — M2 EventBus + GameLogicSystem (fixed tick, phases)

```
=== SHARED PREAMBLE (identical in every prompt — do not modify) ===
You are implementing the T850 Game Entity & Logic System. The attached file
game-entity-system-spec.md is the authoritative spec: use its exact code,
signatures, namespaces, and diagrams. When a task says "per §X", open that
section and implement it verbatim; do not invent alternative designs.

PATHS
- REPO_ROOT = the git root (folder containing documentation/ and BuildAndroidFastApk.ps1).
- SRC = <REPO_ROOT>/T850  (contains T850.sln, scripts/, Framework/, DayScene/, T8ditor/).
- Game core headers -> SRC/Framework/include/game/*.h
- Game core sources -> SRC/Framework/src/game/*.cpp
- Game examples     -> SRC/Framework/include/game/examples/*.h and SRC/Framework/src/game/examples/*.cpp

HARD RULES
1. C++23. namespace t850::game for runtime code, t850::scene for schema types.
2. game/core must NOT include anything from game/examples, T8ditor, or ImGui.
3. Every NEW .h/.cpp under Framework/ MUST be registered in BOTH build systems in
   the SAME change, following spec Appendix F:
     - SRC/Framework/Framework.vcxproj  (<ClInclude> for .h, <ClCompile> for .cpp)
     - SRC/Framework/CMakeLists.txt     (add .cpp to set(FRAMEWORK_SOURCES ...))
   Files added under DayScene/ or T8ditor/ update their own .vcxproj + CMakeLists.
4. Do NOT edit files unrelated to the current task. Do NOT reformat existing code.
5. Prefer std::unique_ptr, std::optional, std::string_view, std::span, std::unordered_map.
6. No third-party ECS/reflection/UUID/scripting deps. IDs are strings via MakeStableId.

BUILD GATE (run from SRC after finishing the task; spec Appendix E):
   pwsh -File scripts\build.ps1 -Config Debug -Platform x64
   pwsh -File scripts\build.ps1 -Config Debug -Platform ARM64
Both MUST succeed with no new warnings-as-errors. Fix compile/link errors before
reporting done. If a symbol links on x64 but fails ARM64, a .cpp is missing from
CMakeLists.txt (rule 3).

SELF-TEST (only when the task adds/changes tests): after building, run the DayScene
output with `--game-selftest` and confirm exit code 0 (spec §13).

DEFINITION OF DONE (report back):
- list of files created/edited (with the build-file entries added),
- x64 and ARM64 build results,
- selftest output if applicable,
- anything you could not complete and why. Do not claim done if a build failed.

=== TASK P5 ===
TASK: Add the queued event bus and the orchestrator per spec §5.6, §5.8, §7.1.

CREATE (register BOTH build systems):
- include/game/EventBus.h        -> GameEvent, Subscription, EventBus (§5.6)
- src/game/EventBus.cpp          -> Subscribe/Publish(next queue)/DispatchQueued(swap+FIFO)/ring buffer
- include/game/GameLogicSystem.h -> GameLogicSettings, GameSceneRuntimeLinks, GameLogicStats,
                                    GameLogicSystem (§5.8 tick, §7.1 API)
- src/game/GameLogicSystem.cpp   -> Initialize/Update(accumulator per §5.8)/Tick(phase order per §5.8)/
                                    Shutdown; deferred create/destroy; UpdateComponents(phase).
                                    Phases may be empty except: dispatch events, update components,
                                    apply deferred creates/destroys.

RULES
- Tick phase ORDER must match §5.8 exactly (event dispatch before component logic;
  deferred destroys last).
- Update(dt) must clamp dt to maxFrameDeltaSeconds and run at most maxStepsPerFrame ticks.
- GameLogicSystem owns controllers_ vector (empty for now) and the registry/eventbus.

BUILD GATE: x64 + ARM64 Debug.
DONE WHEN: both build; Update() advances tickIndex_ deterministically and never
exceeds maxStepsPerFrame ticks for a large dt.
```

---

## PROMPT P6 — M2 SceneTemplate integration + DevGui + tests

```
=== SHARED PREAMBLE (identical in every prompt — do not modify) ===
You are implementing the T850 Game Entity & Logic System. The attached file
game-entity-system-spec.md is the authoritative spec: use its exact code,
signatures, namespaces, and diagrams. When a task says "per §X", open that
section and implement it verbatim; do not invent alternative designs.

PATHS
- REPO_ROOT = the git root (folder containing documentation/ and BuildAndroidFastApk.ps1).
- SRC = <REPO_ROOT>/T850  (contains T850.sln, scripts/, Framework/, DayScene/, T8ditor/).
- Game core headers -> SRC/Framework/include/game/*.h
- Game core sources -> SRC/Framework/src/game/*.cpp
- Game examples     -> SRC/Framework/include/game/examples/*.h and SRC/Framework/src/game/examples/*.cpp

HARD RULES
1. C++23. namespace t850::game for runtime code, t850::scene for schema types.
2. game/core must NOT include anything from game/examples, T8ditor, or ImGui.
3. Every NEW .h/.cpp under Framework/ MUST be registered in BOTH build systems in
   the SAME change, following spec Appendix F:
     - SRC/Framework/Framework.vcxproj  (<ClInclude> for .h, <ClCompile> for .cpp)
     - SRC/Framework/CMakeLists.txt     (add .cpp to set(FRAMEWORK_SOURCES ...))
   Files added under DayScene/ or T8ditor/ update their own .vcxproj + CMakeLists.
4. Do NOT edit files unrelated to the current task. Do NOT reformat existing code.
5. Prefer std::unique_ptr, std::optional, std::string_view, std::span, std::unordered_map.
6. No third-party ECS/reflection/UUID/scripting deps. IDs are strings via MakeStableId.

BUILD GATE (run from SRC after finishing the task; spec Appendix E):
   pwsh -File scripts\build.ps1 -Config Debug -Platform x64
   pwsh -File scripts\build.ps1 -Config Debug -Platform ARM64
Both MUST succeed with no new warnings-as-errors. Fix compile/link errors before
reporting done. If a symbol links on x64 but fails ARM64, a .cpp is missing from
CMakeLists.txt (rule 3).

SELF-TEST (only when the task adds/changes tests): after building, run the DayScene
output with `--game-selftest` and confirm exit code 0 (spec §13).

DEFINITION OF DONE (report back):
- list of files created/edited (with the build-file entries added),
- x64 and ARM64 build results,
- selftest output if applicable,
- anything you could not complete and why. Do not claim done if a build failed.

=== TASK P6 ===
TASK: Make the runtime consume game_entities and expose them, per spec §7.2, §11-DevGui.

EDIT: SRC/DayScene/SceneTemplate.h
- Add member: t850::game::GameLogicSystem m_gameLogic;  (+ include)

EDIT: SRC/DayScene/SceneTemplate.cpp  (inside the EXISTING hooks; do not add new virtuals)
- OnLoadScene(): after existing mesh/physics/nav/camera load, build a
  GameSceneRuntimeLinks (resolve mesh slot by mesh_object name, PrimitiveInst* for slot,
  PhysicsBodyHandle by physics-entity name, camera index by name). Then:
      m_gameLogic.Initialize(*GetEngineContext(), settingsFromScene);
      t850::scene::SceneValidationReport report;
      m_gameLogic.LoadFromScene(loadedScene, links, &report);
      log each report issue via T8_LOG_*.
- OnUpdate(dt): call m_gameLogic.Update(dt) after existing updates.
- OnDestoryScene(): call m_gameLogic.Shutdown() (note the misspelled hook name).
- DrawDevGui(gui): add a simple panel listing runtime game objects
  (runtimeId, sceneId, name, kind, component count, resolved mesh/physics link).

ADD TESTS to src/game/GameSelfTest.cpp: T-REG-01, T-REG-02, T-LIFE-01, T-TICK-01 (§13).

BUILD GATE: x64 + ARM64 Debug, then run --game-selftest (exit 0).
DONE WHEN: both build; selftest passes; loading a scene with game_entities creates
registry entries and the DevGui panel lists them; broken mesh link logs a warning,
not a crash.
```

---

## PROMPT P7 — M3 control/possession + movement (the genre seam)

```
=== SHARED PREAMBLE (identical in every prompt — do not modify) ===
You are implementing the T850 Game Entity & Logic System. The attached file
game-entity-system-spec.md is the authoritative spec: use its exact code,
signatures, namespaces, and diagrams. When a task says "per §X", open that
section and implement it verbatim; do not invent alternative designs.

PATHS
- REPO_ROOT = the git root (folder containing documentation/ and BuildAndroidFastApk.ps1).
- SRC = <REPO_ROOT>/T850  (contains T850.sln, scripts/, Framework/, DayScene/, T8ditor/).
- Game core headers -> SRC/Framework/include/game/*.h
- Game core sources -> SRC/Framework/src/game/*.cpp
- Game examples     -> SRC/Framework/include/game/examples/*.h and SRC/Framework/src/game/examples/*.cpp

HARD RULES
1. C++23. namespace t850::game for runtime code, t850::scene for schema types.
2. game/core must NOT include anything from game/examples, T8ditor, or ImGui.
3. Every NEW .h/.cpp under Framework/ MUST be registered in BOTH build systems in
   the SAME change, following spec Appendix F:
     - SRC/Framework/Framework.vcxproj  (<ClInclude> for .h, <ClCompile> for .cpp)
     - SRC/Framework/CMakeLists.txt     (add .cpp to set(FRAMEWORK_SOURCES ...))
   Files added under DayScene/ or T8ditor/ update their own .vcxproj + CMakeLists.
4. Do NOT edit files unrelated to the current task. Do NOT reformat existing code.
5. Prefer std::unique_ptr, std::optional, std::string_view, std::span, std::unordered_map.
6. No third-party ECS/reflection/UUID/scripting deps. IDs are strings via MakeStableId.

BUILD GATE (run from SRC after finishing the task; spec Appendix E):
   pwsh -File scripts\build.ps1 -Config Debug -Platform x64
   pwsh -File scripts\build.ps1 -Config Debug -Platform ARM64
Both MUST succeed with no new warnings-as-errors. Fix compile/link errors before
reporting done. If a symbol links on x64 but fails ARM64, a .cpp is missing from
CMakeLists.txt (rule 3).

SELF-TEST (only when the task adds/changes tests): after building, run the DayScene
output with `--game-selftest` and confirm exit code 0 (spec §13).

DEFINITION OF DONE (report back):
- list of files created/edited (with the build-file entries added),
- x64 and ARM64 build results,
- selftest output if applicable,
- anything you could not complete and why. Do not claim done if a build failed.

=== TASK P7 ===
TASK: Add input intent, controllers, possession, and movement per spec §5.4, §5.5, §8.

CREATE (register BOTH build systems):
- include/game/InputFrame.h        -> InputFrame (§8.1)
- include/game/Controller.h        -> ControllerKind, MovementIntent, IController (§5.4)
- src/game/Controller.cpp          -> PlayerController (InputFrame->intent),
                                      AIController stub (intent from behavior/nav goal)
- include/game/MovementComponent.h -> MovementComponent (§5.5, PrePhysics phase)
- src/game/MovementComponent.cpp   -> consume owner's controller intent, integrate velocity,
                                      call system physics Enqueue* (stub the physics call if
                                      GamePhysicsService not present yet: apply directly to
                                      PrimitiveInst transform as a temporary path, TODO-marked).

EDIT: src/game/GameLogicSystem.cpp
- In LoadFromScene, for each entity create a controller from control.mode
  (none/player/ai) and possess the GameObject (§5.4). Register the movement
  component through the factory.
- In Tick, sample controller intent in the Logic phase (§5.8 step "Sample controller intents").

ADD TEST: T-CTRL-01 (§13) — player vs ai controller produce distinct intents.

BUILD GATE: x64 + ARM64 Debug, then --game-selftest (exit 0).
DONE WHEN: both build; selftest passes; a "player" entity moves from InputFrame and
an "ai" entity moves toward a scripted nav goal in the same scene.
```

---

## PROMPT P8 — M4 components + events depth (Health, event log)

```
=== SHARED PREAMBLE (identical in every prompt — do not modify) ===
You are implementing the T850 Game Entity & Logic System. The attached file
game-entity-system-spec.md is the authoritative spec: use its exact code,
signatures, namespaces, and diagrams. When a task says "per §X", open that
section and implement it verbatim; do not invent alternative designs.

PATHS
- REPO_ROOT = the git root (folder containing documentation/ and BuildAndroidFastApk.ps1).
- SRC = <REPO_ROOT>/T850  (contains T850.sln, scripts/, Framework/, DayScene/, T8ditor/).
- Game core headers -> SRC/Framework/include/game/*.h
- Game core sources -> SRC/Framework/src/game/*.cpp
- Game examples     -> SRC/Framework/include/game/examples/*.h and SRC/Framework/src/game/examples/*.cpp

HARD RULES
1. C++23. namespace t850::game for runtime code, t850::scene for schema types.
2. game/core must NOT include anything from game/examples, T8ditor, or ImGui.
3. Every NEW .h/.cpp under Framework/ MUST be registered in BOTH build systems in
   the SAME change, following spec Appendix F:
     - SRC/Framework/Framework.vcxproj  (<ClInclude> for .h, <ClCompile> for .cpp)
     - SRC/Framework/CMakeLists.txt     (add .cpp to set(FRAMEWORK_SOURCES ...))
   Files added under DayScene/ or T8ditor/ update their own .vcxproj + CMakeLists.
4. Do NOT edit files unrelated to the current task. Do NOT reformat existing code.
5. Prefer std::unique_ptr, std::optional, std::string_view, std::span, std::unordered_map.
6. No third-party ECS/reflection/UUID/scripting deps. IDs are strings via MakeStableId.

BUILD GATE (run from SRC after finishing the task; spec Appendix E):
   pwsh -File scripts\build.ps1 -Config Debug -Platform x64
   pwsh -File scripts\build.ps1 -Config Debug -Platform ARM64
Both MUST succeed with no new warnings-as-errors. Fix compile/link errors before
reporting done. If a symbol links on x64 but fails ARM64, a .cpp is missing from
CMakeLists.txt (rule 3).

SELF-TEST (only when the task adds/changes tests): after building, run the DayScene
output with `--game-selftest` and confirm exit code 0 (spec §13).

DEFINITION OF DONE (report back):
- list of files created/edited (with the build-file entries added),
- x64 and ARM64 build results,
- selftest output if applicable,
- anything you could not complete and why. Do not claim done if a build failed.

=== TASK P8 ===
TASK: Finalize component lifecycle and add the first example component + event log,
per spec §5.3, §5.6, §12.

CREATE (register BOTH build systems):
- include/game/examples/HealthComponent.h  + src/game/examples/HealthComponent.cpp
  (parse maxHp/currentHp/armor once in OnCreate; subscribe to "damage" events;
   publish "died" when hp<=0). This is the FIRST examples-module file.

EDIT
- src/game/GameLogicSystem.cpp: register HealthComponent in the factory; ensure the
  full lifecycle (OnAttach->OnCreate->Update->OnDestroy->OnDetach) per §5.3, and that
  RequestRemoveComponent is applied in the deferred phase.
- DrawDevGui: add an event log view backed by EventBus::RecentEvents() (§5.6).
- Add telemetry counters game.entities.total / game.components.total /
  game.events.queued / game.events.dispatched via RuntimeTelemetry (§12).

ADD TESTS: T-COMP-01, T-COMP-02, T-EVENT-01, T-EVENT-02, T-EVENT-03 (§13).

BUILD GATE: x64 + ARM64 Debug, then --game-selftest (exit 0).
DONE WHEN: both build; selftest passes; a scene-authored health component takes a
"damage" event and the DevGui event log shows the resulting events in order.
```

---

## PROMPT P9 — M5 state machines (compiled)

```
=== SHARED PREAMBLE (identical in every prompt — do not modify) ===
You are implementing the T850 Game Entity & Logic System. The attached file
game-entity-system-spec.md is the authoritative spec: use its exact code,
signatures, namespaces, and diagrams. When a task says "per §X", open that
section and implement it verbatim; do not invent alternative designs.

PATHS
- REPO_ROOT = the git root (folder containing documentation/ and BuildAndroidFastApk.ps1).
- SRC = <REPO_ROOT>/T850  (contains T850.sln, scripts/, Framework/, DayScene/, T8ditor/).
- Game core headers -> SRC/Framework/include/game/*.h
- Game core sources -> SRC/Framework/src/game/*.cpp
- Game examples     -> SRC/Framework/include/game/examples/*.h and SRC/Framework/src/game/examples/*.cpp

HARD RULES
1. C++23. namespace t850::game for runtime code, t850::scene for schema types.
2. game/core must NOT include anything from game/examples, T8ditor, or ImGui.
3. Every NEW .h/.cpp under Framework/ MUST be registered in BOTH build systems in
   the SAME change, following spec Appendix F:
     - SRC/Framework/Framework.vcxproj  (<ClInclude> for .h, <ClCompile> for .cpp)
     - SRC/Framework/CMakeLists.txt     (add .cpp to set(FRAMEWORK_SOURCES ...))
   Files added under DayScene/ or T8ditor/ update their own .vcxproj + CMakeLists.
4. Do NOT edit files unrelated to the current task. Do NOT reformat existing code.
5. Prefer std::unique_ptr, std::optional, std::string_view, std::span, std::unordered_map.
6. No third-party ECS/reflection/UUID/scripting deps. IDs are strings via MakeStableId.

BUILD GATE (run from SRC after finishing the task; spec Appendix E):
   pwsh -File scripts\build.ps1 -Config Debug -Platform x64
   pwsh -File scripts\build.ps1 -Config Debug -Platform ARM64
Both MUST succeed with no new warnings-as-errors. Fix compile/link errors before
reporting done. If a symbol links on x64 but fails ARM64, a .cpp is missing from
CMakeLists.txt (rule 3).

SELF-TEST (only when the task adds/changes tests): after building, run the DayScene
output with `--game-selftest` and confirm exit code 0 (spec §13).

DEFINITION OF DONE (report back):
- list of files created/edited (with the build-file entries added),
- x64 and ARM64 build results,
- selftest output if applicable,
- anything you could not complete and why. Do not claim done if a build failed.

=== TASK P9 ===
TASK: Add the compiled state machine per spec §5.7.

CREATE (register BOTH build systems):
- include/game/StateMachine.h -> TransitionConditionKind, CompiledTransition, StateMachine (§5.7)
- src/game/StateMachine.cpp   -> Compile(SceneStateMachineDesc) with condition grammar
      (always | on_event:<name> | timer_elapsed | health_below:<f> | param_equals:<k>:<v>);
      Evaluate: filter current/wildcard from-state, sort by priority then descriptor order,
      honor cooldown, at most one transition/tick, emit "state_changed" event.

EDIT
- src/game/GameLogicSystem.cpp: in LoadFromScene, if entity.behavior present, compile it
  into GameObject::behavior and SetInitialState; in Tick EvaluateStateMachines phase, run it.
- GameValidation.cpp: add initial-state-missing and transition-target-missing checks (§6.5)
  if not already present.
- Add DevGui "force transition" control for the selected runtime object.

ADD TESTS: T-SM-01, T-SM-02, T-SM-03, T-SM-04 (§13).

BUILD GATE: x64 + ARM64 Debug, then --game-selftest (exit 0).
DONE WHEN: both build; selftest passes; an entity transitions on an event during play.
```

---

## PROMPT P10 — M6 physics gameplay layers + queries

```
=== SHARED PREAMBLE (identical in every prompt — do not modify) ===
You are implementing the T850 Game Entity & Logic System. The attached file
game-entity-system-spec.md is the authoritative spec: use its exact code,
signatures, namespaces, and diagrams. When a task says "per §X", open that
section and implement it verbatim; do not invent alternative designs.

PATHS
- REPO_ROOT = the git root (folder containing documentation/ and BuildAndroidFastApk.ps1).
- SRC = <REPO_ROOT>/T850  (contains T850.sln, scripts/, Framework/, DayScene/, T8ditor/).
- Game core headers -> SRC/Framework/include/game/*.h
- Game core sources -> SRC/Framework/src/game/*.cpp
- Game examples     -> SRC/Framework/include/game/examples/*.h and SRC/Framework/src/game/examples/*.cpp

HARD RULES
1. C++23. namespace t850::game for runtime code, t850::scene for schema types.
2. game/core must NOT include anything from game/examples, T8ditor, or ImGui.
3. Every NEW .h/.cpp under Framework/ MUST be registered in BOTH build systems in
   the SAME change, following spec Appendix F:
     - SRC/Framework/Framework.vcxproj  (<ClInclude> for .h, <ClCompile> for .cpp)
     - SRC/Framework/CMakeLists.txt     (add .cpp to set(FRAMEWORK_SOURCES ...))
   Files added under DayScene/ or T8ditor/ update their own .vcxproj + CMakeLists.
4. Do NOT edit files unrelated to the current task. Do NOT reformat existing code.
5. Prefer std::unique_ptr, std::optional, std::string_view, std::span, std::unordered_map.
6. No third-party ECS/reflection/UUID/scripting deps. IDs are strings via MakeStableId.

BUILD GATE (run from SRC after finishing the task; spec Appendix E):
   pwsh -File scripts\build.ps1 -Config Debug -Platform x64
   pwsh -File scripts\build.ps1 -Config Debug -Platform ARM64
Both MUST succeed with no new warnings-as-errors. Fix compile/link errors before
reporting done. If a symbol links on x64 but fails ARM64, a .cpp is missing from
CMakeLists.txt (rule 3).

SELF-TEST (only when the task adds/changes tests): after building, run the DayScene
output with `--game-selftest` and confirm exit code 0 (spec §13).

DEFINITION OF DONE (report back):
- list of files created/edited (with the build-file entries added),
- x64 and ARM64 build results,
- selftest output if applicable,
- anything you could not complete and why. Do not claim done if a build failed.

=== TASK P10 ===
TASK: Add gameplay collision layers and the physics service per spec §9. This adds
NEW engine API to JoltPhysicsSystem.

CREATE (register BOTH build systems):
- include/physics/GameplayLayers.h  -> enum class GameplayLayer (§9.2)
- include/game/GamePhysicsService.h -> GameQueryFilter, GameHit, GamePhysicsService (§9.4)
- src/game/GamePhysicsService.cpp   -> LineOfSight via existing CastCapsule; OverlapSphere
                                      via new Jolt query; command buffer Enqueue*/Flush.

EDIT (engine additions, minimal and reviewed):
- SRC/Framework/include/physics/JoltPhysicsSystem.h + .cpp:
    * add OverlapSphere(center, radius, layerMask, out bodies) [new],
    * add optional gameplay-layer parameter/user-data so a hit resolves to entityId,
    * extend ObjectLayerPairFilterImpl + BroadPhaseLayerInterfaceImpl to the §9.3 matrix
      WITHOUT changing existing NonMoving/Moving behavior for current callers.
- Map SceneObjectPhysicsDesc.collision_layer string -> GameplayLayer at body creation.
- MovementComponent: replace the temporary direct-transform path from P7 with
  GamePhysicsService Enqueue* calls (§5.5).

ADD TEST: T-PHYS-01 (§13) — queries return empty / commands no-op when Jolt unavailable.

BUILD GATE: x64 + ARM64 Debug, then --game-selftest (exit 0).
DONE WHEN: both build; selftest passes; existing physics scenes still behave; a sensor
query can find entities within a radius.
```

---

## PROMPT P11 — M7 navigation service + path follow

```
=== SHARED PREAMBLE (identical in every prompt — do not modify) ===
You are implementing the T850 Game Entity & Logic System. The attached file
game-entity-system-spec.md is the authoritative spec: use its exact code,
signatures, namespaces, and diagrams. When a task says "per §X", open that
section and implement it verbatim; do not invent alternative designs.

PATHS
- REPO_ROOT = the git root (folder containing documentation/ and BuildAndroidFastApk.ps1).
- SRC = <REPO_ROOT>/T850  (contains T850.sln, scripts/, Framework/, DayScene/, T8ditor/).
- Game core headers -> SRC/Framework/include/game/*.h
- Game core sources -> SRC/Framework/src/game/*.cpp
- Game examples     -> SRC/Framework/include/game/examples/*.h and SRC/Framework/src/game/examples/*.cpp

HARD RULES
1. C++23. namespace t850::game for runtime code, t850::scene for schema types.
2. game/core must NOT include anything from game/examples, T8ditor, or ImGui.
3. Every NEW .h/.cpp under Framework/ MUST be registered in BOTH build systems in
   the SAME change, following spec Appendix F:
     - SRC/Framework/Framework.vcxproj  (<ClInclude> for .h, <ClCompile> for .cpp)
     - SRC/Framework/CMakeLists.txt     (add .cpp to set(FRAMEWORK_SOURCES ...))
   Files added under DayScene/ or T8ditor/ update their own .vcxproj + CMakeLists.
4. Do NOT edit files unrelated to the current task. Do NOT reformat existing code.
5. Prefer std::unique_ptr, std::optional, std::string_view, std::span, std::unordered_map.
6. No third-party ECS/reflection/UUID/scripting deps. IDs are strings via MakeStableId.

BUILD GATE (run from SRC after finishing the task; spec Appendix E):
   pwsh -File scripts\build.ps1 -Config Debug -Platform x64
   pwsh -File scripts\build.ps1 -Config Debug -Platform ARM64
Both MUST succeed with no new warnings-as-errors. Fix compile/link errors before
reporting done. If a symbol links on x64 but fails ARM64, a .cpp is missing from
CMakeLists.txt (rule 3).

SELF-TEST (only when the task adds/changes tests): after building, run the DayScene
output with `--game-selftest` and confirm exit code 0 (spec §13).

DEFINITION OF DONE (report back):
- list of files created/edited (with the build-file entries added),
- x64 and ARM64 build results,
- selftest output if applicable,
- anything you could not complete and why. Do not claim done if a build failed.

=== TASK P11 ===
TASK: Add the navigation service and a path-follow example per spec §10.

CREATE (register BOTH build systems):
- include/game/GameNavigationService.h -> GameNavigationService (§10)
- src/game/GameNavigationService.cpp   -> RequestPath/TryGetResult/ProjectToNavmesh backed by
                                          NavMesh::FindPath(NavPathRequest)/FindPaths (§10).
- include/game/examples/PathFollowComponent.h + src/game/examples/PathFollowComponent.cpp
  -> consume nav result, emit MovementIntent.navGoal; reuse SceneObjectDesc.nav_agent_* fields.

EDIT: src/game/GameLogicSystem.cpp
- Bind GameNavigationService to the scene NavMesh in LoadFromScene; resolve completed
  path requests in the ResolveNavigationResults phase (§5.8).
- AIController uses GameNavigationService for its nav goal.

ADD TEST: T-NAV-01 (§13) — RequestPath with no navmesh fails gracefully.

BUILD GATE: x64 + ARM64 Debug, then --game-selftest (exit 0).
DONE WHEN: both build; selftest passes; an ai entity paths on an authored navmesh and
falls back to direct steering when none exists.
```

---

## PROMPT P12 — M8 groups + genre examples

```
=== SHARED PREAMBLE (identical in every prompt — do not modify) ===
You are implementing the T850 Game Entity & Logic System. The attached file
game-entity-system-spec.md is the authoritative spec: use its exact code,
signatures, namespaces, and diagrams. When a task says "per §X", open that
section and implement it verbatim; do not invent alternative designs.

PATHS
- REPO_ROOT = the git root (folder containing documentation/ and BuildAndroidFastApk.ps1).
- SRC = <REPO_ROOT>/T850  (contains T850.sln, scripts/, Framework/, DayScene/, T8ditor/).
- Game core headers -> SRC/Framework/include/game/*.h
- Game core sources -> SRC/Framework/src/game/*.cpp
- Game examples     -> SRC/Framework/include/game/examples/*.h and SRC/Framework/src/game/examples/*.cpp

HARD RULES
1. C++23. namespace t850::game for runtime code, t850::scene for schema types.
2. game/core must NOT include anything from game/examples, T8ditor, or ImGui.
3. Every NEW .h/.cpp under Framework/ MUST be registered in BOTH build systems in
   the SAME change, following spec Appendix F:
     - SRC/Framework/Framework.vcxproj  (<ClInclude> for .h, <ClCompile> for .cpp)
     - SRC/Framework/CMakeLists.txt     (add .cpp to set(FRAMEWORK_SOURCES ...))
   Files added under DayScene/ or T8ditor/ update their own .vcxproj + CMakeLists.
4. Do NOT edit files unrelated to the current task. Do NOT reformat existing code.
5. Prefer std::unique_ptr, std::optional, std::string_view, std::span, std::unordered_map.
6. No third-party ECS/reflection/UUID/scripting deps. IDs are strings via MakeStableId.

BUILD GATE (run from SRC after finishing the task; spec Appendix E):
   pwsh -File scripts\build.ps1 -Config Debug -Platform x64
   pwsh -File scripts\build.ps1 -Config Debug -Platform ARM64
Both MUST succeed with no new warnings-as-errors. Fix compile/link errors before
reporting done. If a symbol links on x64 but fails ARM64, a .cpp is missing from
CMakeLists.txt (rule 3).

SELF-TEST (only when the task adds/changes tests): after building, run the DayScene
output with `--game-selftest` and confirm exit code 0 (spec §13).

DEFINITION OF DONE (report back):
- list of files created/edited (with the build-file entries added),
- x64 and ARM64 build results,
- selftest output if applicable,
- anything you could not complete and why. Do not claim done if a build failed.

=== TASK P12 ===
TASK: Add gameplay groups and one RTS + one FPS example per spec §5, §8, Appendix B.

CREATE (register BOTH build systems):
- include/game/examples/GroupManager.h + src/game/examples/GroupManager.cpp
  -> load SceneGroupDesc by stable member ids; formation/flock target positions.
- include/game/examples/RtsCommandController.h + .cpp  -> command controller (§8.3): move/attack/hold.
- include/game/examples/WeaponComponent.h + .cpp       -> FPS hitscan via GamePhysicsService (§8.4);
                                                          publishes "damage".

EDIT
- GameValidation.cpp: group member id / leader-not-member checks (§6.5).
- GameLogicSystem.cpp: load game_groups; update groups in UpdateGroups phase (§5.8).

BUILD GATE: x64 + ARM64 Debug.  (Selftest optional; add group-validation cases if quick.)
DONE WHEN: both build; group membership survives a name change + save/load (ids stable);
the core still builds with the examples translation units removed from a scene at runtime.
```

---

## PROMPT P13 — M9 editor authoring + overlays

```
=== SHARED PREAMBLE (identical in every prompt — do not modify) ===
You are implementing the T850 Game Entity & Logic System. The attached file
game-entity-system-spec.md is the authoritative spec: use its exact code,
signatures, namespaces, and diagrams. When a task says "per §X", open that
section and implement it verbatim; do not invent alternative designs.

PATHS
- REPO_ROOT = the git root (folder containing documentation/ and BuildAndroidFastApk.ps1).
- SRC = <REPO_ROOT>/T850  (contains T850.sln, scripts/, Framework/, DayScene/, T8ditor/).
- Game core headers -> SRC/Framework/include/game/*.h
- Game core sources -> SRC/Framework/src/game/*.cpp
- Game examples     -> SRC/Framework/include/game/examples/*.h and SRC/Framework/src/game/examples/*.cpp

HARD RULES
1. C++23. namespace t850::game for runtime code, t850::scene for schema types.
2. game/core must NOT include anything from game/examples, T8ditor, or ImGui.
3. Every NEW .h/.cpp under Framework/ MUST be registered in BOTH build systems in
   the SAME change, following spec Appendix F:
     - SRC/Framework/Framework.vcxproj  (<ClInclude> for .h, <ClCompile> for .cpp)
     - SRC/Framework/CMakeLists.txt     (add .cpp to set(FRAMEWORK_SOURCES ...))
   Files added under DayScene/ or T8ditor/ update their own .vcxproj + CMakeLists.
4. Do NOT edit files unrelated to the current task. Do NOT reformat existing code.
5. Prefer std::unique_ptr, std::optional, std::string_view, std::span, std::unordered_map.
6. No third-party ECS/reflection/UUID/scripting deps. IDs are strings via MakeStableId.

BUILD GATE (run from SRC after finishing the task; spec Appendix E):
   pwsh -File scripts\build.ps1 -Config Debug -Platform x64
   pwsh -File scripts\build.ps1 -Config Debug -Platform ARM64
Both MUST succeed with no new warnings-as-errors. Fix compile/link errors before
reporting done. If a symbol links on x64 but fails ARM64, a .cpp is missing from
CMakeLists.txt (rule 3).

SELF-TEST (only when the task adds/changes tests): after building, run the DayScene
output with `--game-selftest` and confirm exit code 0 (spec §13).

DEFINITION OF DONE (report back):
- list of files created/edited (with the build-file entries added),
- x64 and ARM64 build results,
- selftest output if applicable,
- anything you could not complete and why. Do not claim done if a build failed.

=== TASK P13 ===
TASK: Add T8ditor authoring per spec §11. Update T8ditor build files for any NEW files.

EDIT: SRC/T8ditor/EditorWorld.h
- Add: std::vector<t850::scene::SceneGroupDesc> gameGroups;
        std::optional<t850::scene::SceneGameLogicSettingsDesc> gameLogicSettings;
- Extend the selection-type comment/enum with 9=gameEntity, 10=gameGroup (§11.1).

EDIT: SRC/T8ditor/EditorApp.cpp (and split into new panel files only if needed; if you add
files, update T8ditor.vcxproj + T8ditor/CMakeLists.txt):
- Make Game Entity rows selectable as selectionType 9.
- Inspector (§11.3): Identity (id read-only, name, kind, team, flags), Control
  (mode none/player/ai, controller, player_slot), Links, Components (add/remove/enable,
  params table, config_json), Behavior (initial state, states, transitions tables).
- BuildEditorSceneSnapshot: write game_entities/game_groups/game_logic_settings; call
  EnsureGameEntityIds + MigrateEditorSceneGameLogic (§11.2).
- Validation panel calling ValidateEditorSceneGameLogic with Jump-to-entity (§11.4).
- Overlays via LineRenderer/TextRenderer with SAFE param parsing (§11.6).
- Undo: rely on existing whole-scene EditorUndoState snapshots (§11.2), no granular commands yet.

BUILD GATE: x64 + ARM64 Debug (build the solution, which includes T8ditor).
DONE WHEN: both build; you can author a game entity + components in T8ditor, save,
reload, and see identical data (round-trip), and validation reports broken links.
```

---

## PROMPT P14 — M10 polish + FULL platform sanity checks

```
=== SHARED PREAMBLE (identical in every prompt — do not modify) ===
You are implementing the T850 Game Entity & Logic System. The attached file
game-entity-system-spec.md is the authoritative spec: use its exact code,
signatures, namespaces, and diagrams. When a task says "per §X", open that
section and implement it verbatim; do not invent alternative designs.

PATHS
- REPO_ROOT = the git root (folder containing documentation/ and BuildAndroidFastApk.ps1).
- SRC = <REPO_ROOT>/T850  (contains T850.sln, scripts/, Framework/, DayScene/, T8ditor/).
- Game core headers -> SRC/Framework/include/game/*.h
- Game core sources -> SRC/Framework/src/game/*.cpp
- Game examples     -> SRC/Framework/include/game/examples/*.h and SRC/Framework/src/game/examples/*.cpp

HARD RULES
1. C++23. namespace t850::game for runtime code, t850::scene for schema types.
2. game/core must NOT include anything from game/examples, T8ditor, or ImGui.
3. Every NEW .h/.cpp under Framework/ MUST be registered in BOTH build systems in
   the SAME change, following spec Appendix F:
     - SRC/Framework/Framework.vcxproj  (<ClInclude> for .h, <ClCompile> for .cpp)
     - SRC/Framework/CMakeLists.txt     (add .cpp to set(FRAMEWORK_SOURCES ...))
   Files added under DayScene/ or T8ditor/ update their own .vcxproj + CMakeLists.
4. Do NOT edit files unrelated to the current task. Do NOT reformat existing code.
5. Prefer std::unique_ptr, std::optional, std::string_view, std::span, std::unordered_map.
6. No third-party ECS/reflection/UUID/scripting deps. IDs are strings via MakeStableId.

BUILD GATE (run from SRC after finishing the task; spec Appendix E):
   pwsh -File scripts\build.ps1 -Config Debug -Platform x64
   pwsh -File scripts\build.ps1 -Config Debug -Platform ARM64
Both MUST succeed with no new warnings-as-errors. Fix compile/link errors before
reporting done. If a symbol links on x64 but fails ARM64, a .cpp is missing from
CMakeLists.txt (rule 3).

SELF-TEST (only when the task adds/changes tests): after building, run the DayScene
output with `--game-selftest` and confirm exit code 0 (spec §13).

DEFINITION OF DONE (report back):
- list of files created/edited (with the build-file entries added),
- x64 and ARM64 build results,
- selftest output if applicable,
- anything you could not complete and why. Do not claim done if a build failed.

=== TASK P14 ===
TASK: Final milestone. Light polish, then a FULL cross-platform sanity build.

POLISH (small, optional — skip if risky):
- fast in-memory Play mode: SceneTemplate::LoadSceneFromEditorSceneFile(copy) (§11.5),
- DevGui pause of the fixed tick,
- a benchmark scene under Assets/Scenes/Test with 100 and 1,000 lightweight entities (§12).

REGRESSION
1. Build gate x64 + ARM64 (Debug AND Release) from SRC (spec Appendix E).
2. Run DayScene --game-selftest on x64 Release; confirm exit 0 and all PASS.

CROSS-PLATFORM SANITY (spec Appendix E) — these prove the CMake build files were kept
in sync (rule 3). Report each result explicitly:
3. STEAM DECK (Linux/Vulkan, SteamRT sniper SDK container). From SRC:
      ./steamdeck/BuildSteamRuntime.sh --configuration Release
   (if a full container build is unavailable in this environment, run
      ./steamdeck/BuildSteamRuntime.sh --configuration Release --configure-only
   and report that only configure was possible). A link error naming a game/*.cpp
   symbol means that file is missing from Framework/CMakeLists.txt — fix and rerun.
4. ANDROID (arm64-v8a native). From REPO_ROOT:
      pwsh -File BuildAndroidFastApk.ps1 Release
   or from SRC/android:  .\gradlew.bat :app:externalNativeBuildRelease
   Confirm the native library compiles for arm64-v8a.

DONE WHEN: x64 + ARM64 (Debug+Release) build, selftest exits 0, and Steam Deck and
Android native builds either succeed or fail ONLY for environment/toolchain reasons
you clearly report (never for missing-source/link errors from unregistered files).
```

---

## Notes for the operator

- If a prompt is still too large for the local model, split its CREATE list in half and run the halves as two sessions (build only needs to pass at the end of the second).
- Keep the attached spec identical across sessions. The shared preamble is embedded identically at the top of every prompt, so the cache prefix is preserved automatically — do not edit it between prompts.
- After P6, P7, P8, P9, P10, P11, always run `--game-selftest`; a red selftest blocks the next prompt.
- The four build targets (x64, ARM64, Steam Deck, Android) all read the CMake source list except the two Windows MSBuild ones, which read the `.vcxproj`. That is why [Appendix F](game-entity-system-spec.md#appendix-f-adding-a-source-file-to-both-build-systems) requires editing both every time.
