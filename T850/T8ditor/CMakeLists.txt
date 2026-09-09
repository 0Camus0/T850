project(T8ditor LANGUAGES CXX)

add_library(T8ditorCore STATIC
	main.cpp
	EditorExtensions.cpp
	EditorApp.cpp
	EditorTutorialCapture.cpp
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
	EditorWorld.cpp
	PlayScenePanel.cpp
	MeshEditorPanel.cpp
	RagdollEditorPanel.cpp
	EditorImGui.cpp
	../DayScene/RagdollEditor.cpp
	../DayScene/Quake3Mock.cpp
	../DayScene/SceneTemplate.cpp)

t850_apply_app(T8ditorCore)
t850_apply_library_output(T8ditorCore)
add_library(T850::Editor ALIAS T8ditorCore)
set_property(TARGET T8ditorCore PROPERTY T850_ASSET_ROOT "${T850_SOURCE_DIR}/Assets")
set_property(TARGET T8ditorCore PROPERTY T850_JUNCTION_SCRIPT "${T850_SOURCE_DIR}/cmake/MakeJunction.cmake")
if(WIN32)
	set_property(TARGET T8ditorCore PROPERTY T850_RUNTIME_DLLS
		"${T850_SDL3_ROOT}/lib/${T850_ARCH}/SDL3.dll"
		"${T850_VCPKG_DYNAMIC}/bin/libEGL.dll"
		"${T850_VCPKG_DYNAMIC}/bin/libGLESv2.dll"
		"${T850_VCPKG_DYNAMIC}/bin/draco.dll"
		"${T850_ZLIB_RUNTIME_DLL}")
endif()
target_compile_features(T8ditorCore PUBLIC cxx_std_23)
target_include_directories(T8ditorCore PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${T850_SOURCE_DIR}/Framework
	${T850_SOURCE_DIR}/Framework/include
	${T850_SOURCE_DIR}/Librerias/glaze/include
	${T850_VCPKG_STATIC}/include)
get_target_property(editor_link_directories T8ditorCore LINK_DIRECTORIES)
target_link_directories(T8ditorCore INTERFACE ${editor_link_directories})
if(MSVC)
	target_compile_options(T8ditorCore PRIVATE /bigobj)
endif()

if(WIN32)
	target_link_libraries(T8ditorCore PRIVATE
		FrameworkImGui
		$<$<CONFIG:Debug>:imguid>
		$<$<NOT:$<CONFIG:Debug>>:imgui>
		imguizmo)
else()
	target_link_libraries(T8ditorCore PRIVATE
		FrameworkImGui
		imgui::imgui
		imguizmo::imguizmo)
endif()

add_executable(T8ditor EditorMain.cpp)
target_link_libraries(T8ditor PRIVATE T850::Editor)
t850_apply_common(T8ditor)
t850_apply_runtime_output(T8ditor)
t850_stage_runtime(T8ditor COPY_DRACO)

function(t850_stage_editor_runtime target)
	cmake_parse_arguments(ARG "ENGINE_ASSETS" "" "" ${ARGN})
	if(WIN32)
		get_target_property(runtime_dlls T8ditorCore T850_RUNTIME_DLLS)
		foreach(runtime_dll IN LISTS runtime_dlls)
			add_custom_command(TARGET ${target} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different "${runtime_dll}" $<TARGET_FILE_DIR:${target}>
				VERBATIM)
		endforeach()
	endif()
	if(ARG_ENGINE_ASSETS)
		get_target_property(asset_root T8ditorCore T850_ASSET_ROOT)
		get_target_property(junction_script T8ditorCore T850_JUNCTION_SCRIPT)
		foreach(asset Shaders Models Fonts Textures Scenes Layouts)
			add_custom_command(TARGET ${target} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -DLINK_PATH=$<TARGET_FILE_DIR:${target}>/${asset}
					-DTARGET_PATH=${asset_root}/${asset} -P "${junction_script}"
				VERBATIM)
		endforeach()
	endif()
endfunction()
