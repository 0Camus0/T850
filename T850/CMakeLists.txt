cmake_minimum_required(VERSION 3.21)

cmake_policy(SET CMP0091 NEW)

project(T850 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(HEADLESS "HEADLESS" OFF)

if(ANDROID)
	include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AndroidBuild.cmake)
	return()
endif()

if(NOT WIN32)
	message(FATAL_ERROR "The CMake build now mirrors the Visual Studio solution and requires Windows/MSVC. Use the older HEADLESS/Linux build files from history if you need that path.")
endif()

if(NOT MSVC)
	message(FATAL_ERROR "T850's Visual Studio parity CMake build requires MSVC.")
endif()

if(CMAKE_VS_PLATFORM_NAME STREQUAL "Win32" OR CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
	set(T850_ARCH "x86")
	set(T850_VS_PLATFORM "Win32")
	set(T850_VCPKG_TRIPLET "x86-windows")
	set(T850_VCPKG_STATIC_TRIPLET "x86-windows-static")
elseif(CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64" OR CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$")
	set(T850_ARCH "arm64")
	set(T850_VS_PLATFORM "ARM64")
	set(T850_VCPKG_TRIPLET "arm64-windows")
	set(T850_VCPKG_STATIC_TRIPLET "arm64-windows-static")
else()
	set(T850_ARCH "x64")
	set(T850_VS_PLATFORM "x64")
	set(T850_VCPKG_TRIPLET "x64-windows")
	set(T850_VCPKG_STATIC_TRIPLET "x64-windows-static")
endif()

set(T850_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(T850_BIN_DIR ${T850_SOURCE_DIR}/bin/${T850_ARCH}/$<CONFIG>)
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

	set(T850_VCPKG_EXE ${T850_VCPKG_SOURCE_DIR}/vcpkg.exe)
	if(NOT EXISTS ${T850_VCPKG_EXE})
		if(NOT T850_BOOTSTRAP_VCPKG)
			message(FATAL_ERROR "vcpkg.exe was not found at ${T850_VCPKG_EXE}. Enable T850_BOOTSTRAP_VCPKG or bootstrap vcpkg manually.")
		endif()

		if(NOT EXISTS ${T850_VCPKG_SOURCE_DIR}/bootstrap-vcpkg.bat)
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
		t850_execute_process("vcpkg bootstrap"
			COMMAND cmd /C bootstrap-vcpkg.bat -disableMetrics
			WORKING_DIRECTORY ${T850_VCPKG_SOURCE_DIR})
	endif()

	set(T850_VCPKG_PACKAGE_SPECS
		glew:${T850_VCPKG_STATIC_TRIPLET}
		angle:${T850_VCPKG_TRIPLET}
		draco:${T850_VCPKG_TRIPLET}
		draco:${T850_VCPKG_STATIC_TRIPLET}
		vulkan-headers:${T850_VCPKG_STATIC_TRIPLET}
		vulkan-loader:${T850_VCPKG_STATIC_TRIPLET}
		vulkan-memory-allocator:${T850_VCPKG_STATIC_TRIPLET}
		glslang:${T850_VCPKG_STATIC_TRIPLET})

	set(T850_IMGUI_HEADER ${T850_VCPKG_STATIC}/include/imgui.h)
	set(T850_IMGUI_DX11_HEADER ${T850_VCPKG_STATIC}/include/imgui_impl_dx11.h)
	set(T850_IMGUI_DX12_HEADER ${T850_VCPKG_STATIC}/include/imgui_impl_dx12.h)
	set(T850_IMGUI_OPENGL3_HEADER ${T850_VCPKG_STATIC}/include/imgui_impl_opengl3.h)
	set(T850_IMGUI_SDL3_HEADER ${T850_VCPKG_STATIC}/include/imgui_impl_sdl3.h)
	set(T850_IMGUI_VULKAN_HEADER ${T850_VCPKG_STATIC}/include/imgui_impl_vulkan.h)
	set(T850_IMGUI_NEEDS_REINSTALL OFF)
	if(EXISTS ${T850_IMGUI_HEADER})
		foreach(T850_IMGUI_REQUIRED_HEADER
			${T850_IMGUI_DX11_HEADER}
			${T850_IMGUI_OPENGL3_HEADER}
			${T850_IMGUI_SDL3_HEADER}
			${T850_IMGUI_VULKAN_HEADER})
			if(NOT EXISTS ${T850_IMGUI_REQUIRED_HEADER})
				set(T850_IMGUI_NEEDS_REINSTALL ON)
			endif()
		endforeach()

		if(NOT T850_ARCH STREQUAL "x86" AND NOT EXISTS ${T850_IMGUI_DX12_HEADER})
			set(T850_IMGUI_NEEDS_REINSTALL ON)
		endif()
	endif()

	if(EXISTS ${T850_IMGUI_HEADER} AND T850_IMGUI_NEEDS_REINSTALL)
		message(STATUS "imgui is installed without required backend features; removing it for feature reinstall")
		t850_execute_process("vcpkg remove imgui"
			COMMAND ${T850_VCPKG_EXE} remove imgui:${T850_VCPKG_STATIC_TRIPLET} --recurse
			WORKING_DIRECTORY ${T850_VCPKG_SOURCE_DIR})
	endif()

	set(T850_IMGUI_FEATURES "docking-experimental,dx11-binding,vulkan-binding,opengl3-binding,sdl3-binding,win32-binding")
	if(NOT T850_ARCH STREQUAL "x86")
		set(T850_IMGUI_FEATURES "docking-experimental,dx11-binding,dx12-binding,vulkan-binding,opengl3-binding,sdl3-binding,win32-binding")
	endif()

	list(APPEND T850_VCPKG_PACKAGE_SPECS
		"imgui[${T850_IMGUI_FEATURES}]:${T850_VCPKG_STATIC_TRIPLET}")

	if(T850_BUILD_EDITOR)
		list(APPEND T850_VCPKG_PACKAGE_SPECS
			imguizmo:${T850_VCPKG_STATIC_TRIPLET})
	endif()

	message(STATUS "Installing vcpkg packages for ${T850_ARCH}: ${T850_VCPKG_PACKAGE_SPECS}")
	t850_execute_process("vcpkg install"
		COMMAND ${T850_VCPKG_EXE} install --recurse ${T850_VCPKG_PACKAGE_SPECS}
		WORKING_DIRECTORY ${T850_VCPKG_SOURCE_DIR})
endfunction()

t850_ensure_vcpkg()

if(EXISTS ${T850_VCPKG_DYNAMIC}/bin/zlib1.dll)
	set(T850_ZLIB_RUNTIME_DLL ${T850_VCPKG_DYNAMIC}/bin/zlib1.dll)
elseif(EXISTS ${T850_VCPKG_DYNAMIC}/bin/z.dll)
	set(T850_ZLIB_RUNTIME_DLL ${T850_VCPKG_DYNAMIC}/bin/z.dll)
else()
	message(FATAL_ERROR "Could not find vcpkg zlib runtime DLL in ${T850_VCPKG_DYNAMIC}/bin")
endif()

function(t850_apply_common target)
	set_target_properties(${target} PROPERTIES
		CXX_STANDARD 23
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF)

	target_compile_options(${target} PRIVATE /W3 /MP /FS /Zc:preprocessor)
	target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS GLEW_STATIC)

	if(T850_ARCH STREQUAL "x86")
		target_compile_definitions(${target} PRIVATE WIN32)
	endif()
endfunction()

function(t850_apply_runtime_output target)
	set_target_properties(${target} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${T850_BIN_DIR}"
		RUNTIME_OUTPUT_DIRECTORY_DEBUG "${T850_SOURCE_DIR}/bin/${T850_ARCH}/Debug"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${T850_SOURCE_DIR}/bin/${T850_ARCH}/Release"
		RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${T850_SOURCE_DIR}/bin/${T850_ARCH}/RelWithDebInfo"
		RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${T850_SOURCE_DIR}/bin/${T850_ARCH}/MinSizeRel")
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
		${T850_SOURCE_DIR}/Librerias/glaze/include
		${T850_SOURCE_DIR}/Librerias/tinyxml2/include
		${T850_SOURCE_DIR}/DayScene
		${T850_SOURCE_DIR}/Framework
		${T850_SOURCE_DIR}/Framework/include
		${T850_SOURCE_DIR}/Librerias/SDL3/include
		${T850_VCPKG_STATIC}/include
		${T850_VCPKG_DYNAMIC}/include
		${T850_SOURCE_DIR}/Librerias/stb/include)
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

	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${T850_SDL3_ROOT}/lib/${T850_ARCH}/SDL3.dll $<TARGET_FILE_DIR:${target}>
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${T850_VCPKG_DYNAMIC}/bin/libEGL.dll $<TARGET_FILE_DIR:${target}>
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${T850_VCPKG_DYNAMIC}/bin/libGLESv2.dll $<TARGET_FILE_DIR:${target}>
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${T850_ZLIB_RUNTIME_DLL} $<TARGET_FILE_DIR:${target}>
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${T850_SOURCE_DIR}/config.json $<TARGET_FILE_DIR:${target}>
		VERBATIM)

	if(ARG_COPY_DRACO)
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
