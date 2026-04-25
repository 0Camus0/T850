project(T8ditor LANGUAGES CXX)

add_executable(T8ditor
	main.cpp
	EditorApp.cpp
	EditorCamera.cpp
	EditorLineRenderer.cpp
	EditorGrid.cpp
	EditorGizmo.cpp
	EditorMesh.cpp
	EditorScene.cpp
	EditorSceneGizmos.cpp
	EditorImGui.cpp)

t850_apply_app(T8ditor)
t850_stage_runtime(T8ditor)

target_link_libraries(T8ditor PRIVATE
	$<$<CONFIG:Debug>:imguid>
	$<$<NOT:$<CONFIG:Debug>>:imgui>
	imguizmo)
