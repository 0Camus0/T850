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
	EditorMath.cpp
	EditorUtil.cpp
	EditorViewportUtil.cpp
	HostedViewportPanel.cpp
	EditorSceneSerialization.cpp
	EditorWorld.cpp
	PlayScenePanel.cpp
	MeshEditorPanel.cpp
	RagdollEditorPanel.cpp
	EditorImGui.cpp)

t850_apply_app(T8ditor)
t850_stage_runtime(T8ditor)

if(WIN32)
	target_link_libraries(T8ditor PRIVATE
		FrameworkImGui
		$<$<CONFIG:Debug>:imguid>
		$<$<NOT:$<CONFIG:Debug>>:imgui>
		imguizmo)
else()
	target_link_libraries(T8ditor PRIVATE
		FrameworkImGui
		imgui::imgui
		imguizmo::imguizmo)
endif()
