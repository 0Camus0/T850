cmake_minimum_required(VERSION 3.21)

cmake_policy(SET CMP0091 NEW)

project(T850 LANGUAGES C CXX)

set(T850_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD ${T850_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(HEADLESS "HEADLESS" OFF)

if(ANDROID)
	include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AndroidBuild.cmake)
	return()
endif()

if(WIN32 AND NOT MSVC)
	message(FATAL_ERROR "T850's Visual Studio parity CMake build requires MSVC.")
endif()

option(T850_PLATFORM_STEAM_DECK "Build the native Linux/Steam Deck Vulkan target" OFF)

if(WIN32 AND (CMAKE_VS_PLATFORM_NAME STREQUAL "Win32" OR CMAKE_GENERATOR_PLATFORM STREQUAL "Win32"))
	set(T850_ARCH "x86")
	set(T850_VS_PLATFORM "Win32")
	set(T850_VCPKG_TRIPLET "x86-windows")
	set(T850_VCPKG_STATIC_TRIPLET "x86-windows-static")
elseif(WIN32 AND (CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64" OR CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$"))
	set(T850_ARCH "arm64")
	set(T850_VS_PLATFORM "ARM64")
	set(T850_VCPKG_TRIPLET "arm64-windows")
	set(T850_VCPKG_STATIC_TRIPLET "arm64-windows-static")
elseif(WIN32)
	set(T850_ARCH "x64")
	set(T850_VS_PLATFORM "x64")
	set(T850_VCPKG_TRIPLET "x64-windows")
	set(T850_VCPKG_STATIC_TRIPLET "x64-windows-static")
elseif(UNIX AND NOT APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
	set(T850_ARCH "x64")
	set(T850_VS_PLATFORM "SteamDeck")
	set(T850_VCPKG_TRIPLET "x64-linux")
	set(T850_VCPKG_STATIC_TRIPLET "x64-linux")
	set(T850_PLATFORM_STEAM_DECK ON CACHE BOOL "Build the native Linux/Steam Deck Vulkan target" FORCE)
else()
	message(FATAL_ERROR "Unsupported T850 CMake platform. Steam Deck builds require x86_64 Linux; Windows builds require MSVC.")
endif()

set(T850_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(T850_BIN_DIR ${T850_SOURCE_DIR}/bin/${T850_VS_PLATFORM}/$<CONFIG>)
set(T850_LIB_DIR ${T850_SOURCE_DIR}/Lib/$<CONFIG>/${T850_VS_PLATFORM})
set(T850_VCPKG_ROOT ${T850_SOURCE_DIR}/Librerias/vcpkg/installed)
set(T850_VCPKG_SOURCE_DIR ${T850_SOURCE_DIR}/Librerias/vcpkg)
set(T850_VCPKG_STATIC ${T850_VCPKG_ROOT}/${T850_VCPKG_STATIC_TRIPLET})
set(T850_VCPKG_DYNAMIC ${T850_VCPKG_ROOT}/${T850_VCPKG_TRIPLET})
set(T850_SDL3_ROOT ${T850_SOURCE_DIR}/Librerias/SDL3)

set(T850_BUILD_EDITOR_DEFAULT ON)
option(T850_BUILD_EDITOR "Build T8ditor" ${T850_BUILD_EDITOR_DEFAULT})
option(T850_AUTO_INSTALL_VCPKG_DEPS "Install T850 vcpkg dependencies during CMake configure" ON)
option(T850_BOOTSTRAP_VCPKG "Clone/bootstrap vcpkg when T850_AUTO_INSTALL_VCPKG_DEPS is enabled" ON)
option(T850_RENDER_TRACE "Enable per-frame API render tracer (writes trace.json into dump dir)" OFF)
option(T850_TRACE_GEOMETRY "When tracing, also dump VB/IB/CB raw bytes and shader input layouts (implies T850_RENDER_TRACE)" OFF)
option(T850_VULKAN_VALIDATION "Enable Vulkan validation layers and debug-utils output" OFF)
if(T850_TRACE_GEOMETRY AND NOT T850_RENDER_TRACE)
	message(STATUS "T850_TRACE_GEOMETRY requires T850_RENDER_TRACE; enabling T850_RENDER_TRACE")
	set(T850_RENDER_TRACE ON CACHE BOOL "" FORCE)
endif()

function(t850_execute_process description)
	execute_process(${ARGN} RESULT_VARIABLE T850_PROCESS_RESULT)
	if(NOT T850_PROCESS_RESULT EQUAL 0)
		message(FATAL_ERROR "${description} failed with exit code ${T850_PROCESS_RESULT}")
	endif()
endfunction()

function(t850_ensure_vcpkg)
	if(NOT T850_AUTO_INSTALL_VCPKG_DEPS)
		return()
	endif()

	if(WIN32)
		set(T850_VCPKG_EXE ${T850_VCPKG_SOURCE_DIR}/vcpkg.exe)
		set(T850_VCPKG_BOOTSTRAP bootstrap-vcpkg.bat)
	else()
		set(T850_VCPKG_EXE ${T850_VCPKG_SOURCE_DIR}/vcpkg)
		set(T850_VCPKG_BOOTSTRAP bootstrap-vcpkg.sh)
	endif()

	if(NOT EXISTS ${T850_VCPKG_EXE})
		if(NOT T850_BOOTSTRAP_VCPKG)
			message(FATAL_ERROR "vcpkg was not found at ${T850_VCPKG_EXE}. Enable T850_BOOTSTRAP_VCPKG or bootstrap vcpkg manually.")
		endif()

		if(NOT EXISTS ${T850_VCPKG_SOURCE_DIR}/${T850_VCPKG_BOOTSTRAP})
			find_package(Git QUIET)
			if(NOT Git_FOUND)
				message(FATAL_ERROR "Git is required to clone vcpkg. Install Git or clone vcpkg to ${T850_VCPKG_SOURCE_DIR} manually.")
			endif()

			file(MAKE_DIRECTORY ${T850_SOURCE_DIR}/Librerias)
			message(STATUS "Cloning vcpkg into ${T850_VCPKG_SOURCE_DIR}")
			t850_execute_process("vcpkg clone"
				COMMAND ${GIT_EXECUTABLE} clone https://github.com/microsoft/vcpkg.git ${T850_VCPKG_SOURCE_DIR})
		endif()

		message(STATUS "Bootstrapping vcpkg")
		if(WIN32)
			t850_execute_process("vcpkg bootstrap"
				COMMAND cmd /C bootstrap-vcpkg.bat -disableMetrics
				WORKING_DIRECTORY ${T850_VCPKG_SOURCE_DIR})
		else()
			t850_execute_process("vcpkg bootstrap"
				COMMAND sh bootstrap-vcpkg.sh -disableMetrics
				WORKING_DIRECTORY ${T850_VCPKG_SOURCE_DIR})
		endif()
	endif()

	if(NOT WIN32)
		set(T850_VCPKG_LINUX_MANIFEST_DIR ${CMAKE_BINARY_DIR}/vcpkg-linux-manifest)
		set(T850_VCPKG_LINUX_OVERLAY_DIR ${CMAKE_BINARY_DIR}/vcpkg-linux-overlays)
		file(MAKE_DIRECTORY ${T850_VCPKG_LINUX_MANIFEST_DIR})
		file(MAKE_DIRECTORY ${T850_VCPKG_LINUX_OVERLAY_DIR})
		file(COPY ${T850_VCPKG_SOURCE_DIR}/ports/imgui DESTINATION ${T850_VCPKG_LINUX_OVERLAY_DIR})
		file(READ ${T850_VCPKG_LINUX_OVERLAY_DIR}/imgui/vcpkg.json T850_IMGUI_PORT_JSON)
		string(REPLACE
"        \"sdl3\""
"        {
          \"name\": \"sdl3\",
          \"default-features\": false,
          \"features\": [\"vulkan\", \"wayland\", \"x11\"]
        }"
			T850_IMGUI_PORT_JSON "${T850_IMGUI_PORT_JSON}")
		file(WRITE ${T850_VCPKG_LINUX_OVERLAY_DIR}/imgui/vcpkg.json "${T850_IMGUI_PORT_JSON}")
		set(T850_VCPKG_LINUX_EDITOR_DEPS "")
		if(T850_BUILD_EDITOR)
			set(T850_VCPKG_LINUX_EDITOR_DEPS ",\n    \"imguizmo\"")
		endif()
		file(WRITE ${T850_VCPKG_LINUX_MANIFEST_DIR}/vcpkg.json
"{
  \"name\": \"t850-steamdeck-deps\",
  \"version-string\": \"0\",
  \"dependencies\": [
    \"draco\",
    \"glslang\",
    \"joltphysics\",
    \"recastnavigation\",
    \"vulkan-headers\",
    \"vulkan-loader\",
    \"vulkan-memory-allocator\",
    {
      \"name\": \"sdl3\",
      \"default-features\": false,
      \"features\": [\"vulkan\", \"wayland\", \"x11\"]
    },
    {
      \"name\": \"imgui\",
      \"features\": [\"docking-experimental\", \"vulkan-binding\", \"opengl3-binding\", \"sdl3-binding\"]
    }${T850_VCPKG_LINUX_EDITOR_DEPS}
  ]
}
")
		message(STATUS "Installing vcpkg packages for ${T850_ARCH} via manifest: ${T850_VCPKG_LINUX_MANIFEST_DIR}")
		t850_execute_process("vcpkg install"
			COMMAND ${T850_VCPKG_EXE} install
				--x-manifest-root=${T850_VCPKG_LINUX_MANIFEST_DIR}
				--x-install-root=${T850_VCPKG_SOURCE_DIR}/installed
				--overlay-ports=${T850_VCPKG_LINUX_OVERLAY_DIR}
				--triplet=${T850_VCPKG_STATIC_TRIPLET}
				--host-triplet=${T850_VCPKG_STATIC_TRIPLET}
				--no-print-usage
			WORKING_DIRECTORY ${T850_VCPKG_SOURCE_DIR})
		return()
	endif()

	if(WIN32)
		set(T850_VCPKG_PACKAGE_SPECS
			glew:${T850_VCPKG_STATIC_TRIPLET}
			angle:${T850_VCPKG_TRIPLET}
			draco:${T850_VCPKG_TRIPLET}
			draco:${T850_VCPKG_STATIC_TRIPLET}
			vulkan-headers:${T850_VCPKG_STATIC_TRIPLET}
			vulkan-loader:${T850_VCPKG_STATIC_TRIPLET}
			vulkan-memory-allocator:${T850_VCPKG_STATIC_TRIPLET}
			glslang:${T850_VCPKG_STATIC_TRIPLET})
	else()
		set(T850_VCPKG_PACKAGE_SPECS
			draco:${T850_VCPKG_STATIC_TRIPLET}
			"sdl3[core,wayland,x11]:${T850_VCPKG_STATIC_TRIPLET}"
			vulkan-headers:${T850_VCPKG_STATIC_TRIPLET}
			vulkan-loader:${T850_VCPKG_STATIC_TRIPLET}
			vulkan-memory-allocator:${T850_VCPKG_STATIC_TRIPLET}
			glslang:${T850_VCPKG_STATIC_TRIPLET})
	endif()

	list(APPEND T850_VCPKG_PACKAGE_SPECS
		joltphysics:${T850_VCPKG_STATIC_TRIPLET}
		recastnavigation:${T850_VCPKG_STATIC_TRIPLET})

	set(T850_IMGUI_HEADER ${T850_VCPKG_STATIC}/include/imgui.h)
	set(T850_IMGUI_OPENGL3_HEADER ${T850_VCPKG_STATIC}/include/imgui_impl_opengl3.h)
	set(T850_IMGUI_SDL3_HEADER ${T850_VCPKG_STATIC}/include/imgui_impl_sdl3.h)
	set(T850_IMGUI_VULKAN_HEADER ${T850_VCPKG_STATIC}/include/imgui_impl_vulkan.h)
	set(T850_IMGUI_NEEDS_REINSTALL OFF)
	if(EXISTS ${T850_IMGUI_HEADER})
		set(T850_IMGUI_REQUIRED_HEADERS
			${T850_IMGUI_OPENGL3_HEADER}
			${T850_IMGUI_SDL3_HEADER}
			${T850_IMGUI_VULKAN_HEADER})
		if(WIN32)
			list(APPEND T850_IMGUI_REQUIRED_HEADERS ${T850_VCPKG_STATIC}/include/imgui_impl_dx11.h)
			if(NOT T850_ARCH STREQUAL "x86")
				list(APPEND T850_IMGUI_REQUIRED_HEADERS ${T850_VCPKG_STATIC}/include/imgui_impl_dx12.h)
			endif()
		endif()
		foreach(T850_IMGUI_REQUIRED_HEADER IN LISTS T850_IMGUI_REQUIRED_HEADERS)
			if(NOT EXISTS ${T850_IMGUI_REQUIRED_HEADER})
				set(T850_IMGUI_NEEDS_REINSTALL ON)
			endif()
		endforeach()
	endif()

	if(EXISTS ${T850_IMGUI_HEADER} AND T850_IMGUI_NEEDS_REINSTALL)
		message(STATUS "imgui is installed without required backend features; removing it for feature reinstall")
		t850_execute_process("vcpkg remove imgui"
			COMMAND ${T850_VCPKG_EXE} remove imgui:${T850_VCPKG_STATIC_TRIPLET} --recurse
			WORKING_DIRECTORY ${T850_VCPKG_SOURCE_DIR})
	endif()

	if(WIN32)
		set(T850_IMGUI_FEATURES "docking-experimental,dx11-binding,vulkan-binding,opengl3-binding,sdl3-binding,win32-binding")
		if(NOT T850_ARCH STREQUAL "x86")
			set(T850_IMGUI_FEATURES "docking-experimental,dx11-binding,dx12-binding,vulkan-binding,opengl3-binding,sdl3-binding,win32-binding")
		endif()
	else()
		set(T850_IMGUI_FEATURES "docking-experimental,vulkan-binding,opengl3-binding,sdl3-binding")
	endif()

	list(APPEND T850_VCPKG_PACKAGE_SPECS
		"imgui[${T850_IMGUI_FEATURES}]:${T850_VCPKG_STATIC_TRIPLET}")

	if(T850_BUILD_EDITOR)
		list(APPEND T850_VCPKG_PACKAGE_SPECS
			imguizmo:${T850_VCPKG_STATIC_TRIPLET})
	endif()

	message(STATUS "Installing vcpkg packages for ${T850_ARCH}: ${T850_VCPKG_PACKAGE_SPECS}")
	set(T850_VCPKG_INSTALL_ARGS install --recurse)
	if(EXISTS ${T850_SOURCE_DIR}/ports)
		list(APPEND T850_VCPKG_INSTALL_ARGS --overlay-ports=${T850_SOURCE_DIR}/ports)
	endif()
	list(APPEND T850_VCPKG_INSTALL_ARGS ${T850_VCPKG_PACKAGE_SPECS})
	t850_execute_process("vcpkg install"
		COMMAND ${T850_VCPKG_EXE} ${T850_VCPKG_INSTALL_ARGS}
		WORKING_DIRECTORY ${T850_VCPKG_SOURCE_DIR})
endfunction()

t850_ensure_vcpkg()

list(APPEND CMAKE_PREFIX_PATH ${T850_VCPKG_STATIC})
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)
find_package(Jolt CONFIG REQUIRED)
find_package(recastnavigation CONFIG REQUIRED)
if(T850_PLATFORM_STEAM_DECK)
	find_package(Vulkan REQUIRED)
	find_package(SDL3 CONFIG REQUIRED)
	find_package(draco CONFIG REQUIRED)
	find_package(glslang CONFIG REQUIRED)
	find_package(imgui CONFIG REQUIRED)
	if(T850_BUILD_EDITOR)
		find_package(imguizmo CONFIG REQUIRED)
	endif()
endif()
set_target_properties(Jolt::Jolt PROPERTIES
	MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
	MAP_IMPORTED_CONFIG_MINSIZEREL Release)
set_target_properties(
	RecastNavigation::Recast
	RecastNavigation::Detour
	RecastNavigation::DetourCrowd
	RecastNavigation::DetourTileCache
	PROPERTIES
	MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
	MAP_IMPORTED_CONFIG_MINSIZEREL Release)
set(T850_JOLT_ENABLED ON)
set(T850_RECAST_ENABLED ON)

if(WIN32)
	if(EXISTS ${T850_VCPKG_DYNAMIC}/bin/zlib1.dll)
		set(T850_ZLIB_RUNTIME_DLL ${T850_VCPKG_DYNAMIC}/bin/zlib1.dll)
	elseif(EXISTS ${T850_VCPKG_DYNAMIC}/bin/z.dll)
		set(T850_ZLIB_RUNTIME_DLL ${T850_VCPKG_DYNAMIC}/bin/z.dll)
	else()
		message(FATAL_ERROR "Could not find vcpkg zlib runtime DLL in ${T850_VCPKG_DYNAMIC}/bin")
	endif()
endif()

function(t850_apply_common target)
	set_target_properties(${target} PROPERTIES
		CXX_STANDARD ${T850_CXX_STANDARD}
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF)

	if(MSVC)
		target_compile_options(${target} PRIVATE /W3 /MP /FS /Zc:preprocessor)
		target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS GLEW_STATIC)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
		target_compile_definitions(${target} PRIVATE T850_STEAM_DECK=1)
	endif()

	if(T850_ARCH STREQUAL "x86")
		target_compile_definitions(${target} PRIVATE WIN32)
	endif()
endfunction()

function(t850_apply_runtime_output target)
	set_target_properties(${target} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${T850_BIN_DIR}"
		RUNTIME_OUTPUT_DIRECTORY_DEBUG "${T850_SOURCE_DIR}/bin/${T850_VS_PLATFORM}/Debug"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${T850_SOURCE_DIR}/bin/${T850_VS_PLATFORM}/Release"
		RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${T850_SOURCE_DIR}/bin/${T850_VS_PLATFORM}/RelWithDebInfo"
		RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${T850_SOURCE_DIR}/bin/${T850_VS_PLATFORM}/MinSizeRel")
endfunction()

function(t850_apply_library_output target)
	set_target_properties(${target} PROPERTIES
		ARCHIVE_OUTPUT_DIRECTORY "${T850_LIB_DIR}"
		ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${T850_SOURCE_DIR}/Lib/Debug/${T850_VS_PLATFORM}"
		ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${T850_SOURCE_DIR}/Lib/Release/${T850_VS_PLATFORM}"
		ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${T850_SOURCE_DIR}/Lib/RelWithDebInfo/${T850_VS_PLATFORM}"
		ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${T850_SOURCE_DIR}/Lib/MinSizeRel/${T850_VS_PLATFORM}")
endfunction()

function(t850_apply_app target)
	t850_apply_common(${target})
	t850_apply_runtime_output(${target})
	target_compile_definitions(${target} PRIVATE _CONSOLE)
	target_include_directories(${target} PRIVATE
		${T850_SOURCE_DIR}/Librerias/tinyxml2/include
		${T850_SOURCE_DIR}/DayScene
		${T850_SOURCE_DIR}/Framework
		${T850_SOURCE_DIR}/Framework/include
		${T850_VCPKG_STATIC}/include
		${T850_VCPKG_DYNAMIC}/include
		${T850_SOURCE_DIR}/Librerias/stb/include)
	if(WIN32)
		target_include_directories(${target} PRIVATE ${T850_SOURCE_DIR}/Librerias/SDL3/include)
	endif()
	target_include_directories(${target} SYSTEM PRIVATE
		${T850_SOURCE_DIR}/Librerias/glaze/include)
	if(WIN32)
		target_link_directories(${target} PRIVATE
			${T850_SOURCE_DIR}/Librerias/tinyxml2/lib
			${T850_SOURCE_DIR}/Librerias/SDL3/lib/${T850_ARCH}
			$<$<CONFIG:Debug>:${T850_VCPKG_STATIC}/debug/lib>
			${T850_VCPKG_STATIC}/lib
			${T850_VCPKG_DYNAMIC}/lib)
		target_link_libraries(${target} PRIVATE
			Framework
			dxguid
			dxgi
			SDL3
			d3d11
			d3dcompiler
			vulkan-1
			${T850_VCPKG_DYNAMIC}/lib/draco.lib
			$<$<CONFIG:Debug>:glslangd>
			$<$<CONFIG:Debug>:SPIRVd>
			$<$<CONFIG:Debug>:glslang-default-resource-limitsd>
			$<$<NOT:$<CONFIG:Debug>>:glslang>
			$<$<NOT:$<CONFIG:Debug>>:SPIRV>
			$<$<NOT:$<CONFIG:Debug>>:glslang-default-resource-limits>)
	else()
		target_link_directories(${target} PRIVATE
			$<$<CONFIG:Debug>:${T850_VCPKG_STATIC}/debug/lib>
			${T850_VCPKG_STATIC}/lib)
		target_link_libraries(${target} PRIVATE
			Framework
			SDL3::SDL3
			Vulkan::Vulkan
			draco::draco
			glslang::glslang
			glslang::SPIRV
			glslang::glslang-default-resource-limits)
	endif()
endfunction()

function(t850_stage_runtime target)
	cmake_parse_arguments(ARG "COPY_DRACO" "" "" ${ARGN})

	foreach(asset Shaders Models Fonts Textures Scenes Layouts)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND}
				-DLINK_PATH=$<TARGET_FILE_DIR:${target}>/${asset}
				-DTARGET_PATH=${T850_SOURCE_DIR}/Assets/${asset}
				-P ${T850_SOURCE_DIR}/cmake/MakeJunction.cmake
			VERBATIM)
	endforeach()

	if(WIN32)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${T850_SDL3_ROOT}/lib/${T850_ARCH}/SDL3.dll $<TARGET_FILE_DIR:${target}>
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${T850_VCPKG_DYNAMIC}/bin/libEGL.dll $<TARGET_FILE_DIR:${target}>
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${T850_VCPKG_DYNAMIC}/bin/libGLESv2.dll $<TARGET_FILE_DIR:${target}>
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${T850_ZLIB_RUNTIME_DLL} $<TARGET_FILE_DIR:${target}>
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${T850_SOURCE_DIR}/config.json $<TARGET_FILE_DIR:${target}>
			VERBATIM)
	else()
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${T850_SOURCE_DIR}/config.json $<TARGET_FILE_DIR:${target}>
			VERBATIM)
	endif()

	if(WIN32 AND ARG_COPY_DRACO)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${T850_VCPKG_DYNAMIC}/bin/draco.dll $<TARGET_FILE_DIR:${target}>
			VERBATIM)
	endif()
endfunction()

function(t850_apply_profiler_defines target)
	if(T850_VULKAN_VALIDATION)
		target_compile_definitions(${target} PRIVATE T8_VULKAN_VALIDATION)
	endif()
	if(T850_RENDER_TRACE)
		target_compile_definitions(${target} PRIVATE T850_RENDER_TRACE)
	endif()
	if(T850_TRACE_GEOMETRY)
		target_compile_definitions(${target} PRIVATE T850_TRACE_GEOMETRY)
	endif()
endfunction()

add_subdirectory(Framework)
add_subdirectory(FrameworkImGui)
add_subdirectory(DayScene)

if(T850_BUILD_EDITOR)
	add_subdirectory(T8ditor)
endif()
