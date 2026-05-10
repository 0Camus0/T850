message(STATUS "Configuring T850 Android NativeActivity Vulkan build")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(T850_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
option(T850_VULKAN_VALIDATION "Enable Vulkan validation layers and debug-utils output" OFF)

if(ANDROID_ABI STREQUAL "arm64-v8a")
  set(T850_ANDROID_VCPKG_TRIPLET arm64-android)
elseif(ANDROID_ABI STREQUAL "x86_64")
  set(T850_ANDROID_VCPKG_TRIPLET x64-android)
else()
  message(FATAL_ERROR "T850 Android supports arm64-v8a device builds and x86_64 emulator builds. Requested: ${ANDROID_ABI}")
endif()

set(T850_ANDROID_VCPKG_ROOT ${T850_SOURCE_DIR}/Librerias/vcpkg/installed/${T850_ANDROID_VCPKG_TRIPLET})
set(T850_ANDROID_DRACO_LIB ${T850_ANDROID_VCPKG_ROOT}/lib/libdraco.a)
list(APPEND CMAKE_PREFIX_PATH ${T850_ANDROID_VCPKG_ROOT})
set(glslang_DIR ${T850_ANDROID_VCPKG_ROOT}/share/glslang)
set(imgui_DIR ${T850_ANDROID_VCPKG_ROOT}/share/imgui)
set(Jolt_DIR ${T850_ANDROID_VCPKG_ROOT}/share/Jolt)
find_package(imgui CONFIG REQUIRED)
find_package(glslang CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
message(STATUS "T850 Android ABI: ${ANDROID_ABI} (${T850_ANDROID_VCPKG_TRIPLET})")

set(T850_ANDROID_API_LEVEL 0)
if(DEFINED ANDROID_PLATFORM AND ANDROID_PLATFORM MATCHES "^android-([0-9]+)$")
  set(T850_ANDROID_API_LEVEL ${CMAKE_MATCH_1})
elseif(DEFINED ANDROID_PLATFORM AND ANDROID_PLATFORM MATCHES "^([0-9]+)$")
  set(T850_ANDROID_API_LEVEL ${CMAKE_MATCH_1})
elseif(DEFINED CMAKE_SYSTEM_VERSION AND CMAKE_SYSTEM_VERSION MATCHES "^([0-9]+)$")
  set(T850_ANDROID_API_LEVEL ${CMAKE_MATCH_1})
endif()

if(T850_ANDROID_API_LEVEL LESS 28)
  message(FATAL_ERROR "T850 Android requires API 28 or newer to match the Android vcpkg dependency triplets. Requested API: ${T850_ANDROID_API_LEVEL}")
endif()

if(NOT EXISTS "${T850_ANDROID_DRACO_LIB}")
  message(FATAL_ERROR "Android Draco library not found at ${T850_ANDROID_DRACO_LIB}. Run SetupAndroidToolchain.bat to install draco:${T850_ANDROID_VCPKG_TRIPLET}.")
endif()

if(NOT CMAKE_ANDROID_NDK AND DEFINED ANDROID_NDK)
  set(CMAKE_ANDROID_NDK ${ANDROID_NDK})
endif()

set(T850_ANDROID_NATIVE_APP_GLUE_DIR
  ${CMAKE_ANDROID_NDK}/sources/android/native_app_glue)
set(T850_ANDROID_NATIVE_APP_GLUE_SOURCE
  ${T850_ANDROID_NATIVE_APP_GLUE_DIR}/android_native_app_glue.c)

set(T850_ANDROID_FRAMEWORK_SOURCES
  ${T850_SOURCE_DIR}/Framework/pch.cpp
  ${T850_SOURCE_DIR}/Framework/src/core/Config.cpp
  ${T850_SOURCE_DIR}/Framework/src/core/Core.cpp
  ${T850_SOURCE_DIR}/Framework/src/core/EngineContext.cpp
  ${T850_SOURCE_DIR}/Framework/src/core/android/AndroidFramework.cpp
  ${T850_SOURCE_DIR}/Framework/src/physics/JoltPhysicsSystem.cpp
  ${T850_SOURCE_DIR}/Framework/src/physics/PhysicsDebugRenderer.cpp
  ${T850_SOURCE_DIR}/Framework/src/physics/PhysicsAuthoring.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/AndroidAssets.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/Log.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/InputManager.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/ResourceManager.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/ResourceLocator.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/Timer.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/Utils.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/Camera.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/XDataBase.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/XMaths.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/Technique.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/ConfigRuntime.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/RuntimeProfile.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/ShaderDiskCache.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/ShaderPermutationDump.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/SPIRVReflection.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/ThreadPool.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/Spline.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/Picking.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/GUIAtlasGenerator.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/cil.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/gltf/GLTFLoader.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/gltf/GLTFJson.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/gltf/GLTFAccessor.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/gltf/GLTFBase64.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/gltf/GLTFImage.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/gltf/GLTFMaterial.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/gltf/GLTFMesh.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/gltf/GLTFAnimation.cpp
  ${T850_SOURCE_DIR}/Framework/src/video/BaseDriver.cpp
  ${T850_SOURCE_DIR}/Framework/src/video/vulkan/VulkanDriver.cpp
  ${T850_SOURCE_DIR}/Framework/src/video/vulkan/VulkanVertexBuffer.cpp
  ${T850_SOURCE_DIR}/Framework/src/video/vulkan/VulkanIndexBuffer.cpp
  ${T850_SOURCE_DIR}/Framework/src/video/vulkan/VulkanConstantBuffer.cpp
  ${T850_SOURCE_DIR}/Framework/src/video/vulkan/VulkanTexture.cpp
  ${T850_SOURCE_DIR}/Framework/src/video/vulkan/VulkanRT.cpp
  ${T850_SOURCE_DIR}/Framework/src/video/vulkan/VulkanShader.cpp
  ${T850_SOURCE_DIR}/Framework/src/video/vulkan/VulkanDeviceContext.cpp
  ${T850_SOURCE_DIR}/Framework/src/video/vulkan/VulkanDevice.cpp
  ${T850_SOURCE_DIR}/Framework/src/video/vulkan/VulkanUtils.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/AnimationController.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/PrimitiveInstance.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/PrimitiveManager.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/SplineWireframe.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/WireframeSphere.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/WireframeArrow.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/RenderQuad.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/RenderMesh.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/RenderSkinnedMesh.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/LineRenderer.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/MeshAssetCache.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/MaterialAssetCache.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/MeshPool.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/RenderQueue.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/IBLResources.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/RenderGraph.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/SceneDescriptor.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/SceneSetup.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/SceneProp.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/TextRenderer.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/Quad.cpp
  ${T850_SOURCE_DIR}/Framework/src/scene/LensFlare.cpp
  ${T850_SOURCE_DIR}/Framework/src/debug/FrameDumper.cpp
  ${T850_SOURCE_DIR}/Framework/src/debug/FrameDumperIO.cpp
  ${T850_SOURCE_DIR}/Framework/src/gui/GUIAtlas.cpp
  ${T850_SOURCE_DIR}/Framework/src/gui/GUIElement.cpp
  ${T850_SOURCE_DIR}/Framework/src/gui/GUIManager.cpp
  ${T850_SOURCE_DIR}/FrameworkImGui/src/ImGuiSystem.cpp
  ${T850_SOURCE_DIR}/FrameworkImGui/src/DevGuiContext.cpp
  ${T850_SOURCE_DIR}/Librerias/tinyxml2/tinyxml2.cpp
  ${T850_SOURCE_DIR}/Librerias/mikktspace/src/mikktspace.c)

set_source_files_properties(
  ${T850_SOURCE_DIR}/Framework/src/utils/cil.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-enum-enum-conversion")
set_source_files_properties(
  ${T850_SOURCE_DIR}/Librerias/mikktspace/src/mikktspace.c
  PROPERTIES COMPILE_OPTIONS "-Wno-unused-but-set-variable")

add_library(T850Android SHARED
  ${T850_ANDROID_NATIVE_APP_GLUE_SOURCE}
  ${T850_SOURCE_DIR}/DayScene/AndroidEntry.cpp
  ${T850_SOURCE_DIR}/DayScene/Application.cpp
  ${T850_SOURCE_DIR}/DayScene/DayScene.cpp
  ${T850_SOURCE_DIR}/DayScene/SandboxScene.cpp
  ${T850_ANDROID_FRAMEWORK_SOURCES})

target_compile_definitions(T850Android PRIVATE
  OS_ANDROID
  T850_ANDROID_NATIVE_ACTIVITY
  T850_ENABLE_DRACO=1
  T850_ENABLE_JOLT=1
  VMA_STATIC_VULKAN_FUNCTIONS=0
  VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
if(T850_VULKAN_VALIDATION)
  target_compile_definitions(T850Android PRIVATE T8_VULKAN_VALIDATION)
endif()
target_compile_options(T850Android PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_precompile_headers(T850Android PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${T850_SOURCE_DIR}/Framework/pch.h>)
target_include_directories(T850Android PRIVATE
  ${T850_SOURCE_DIR}/DayScene
  ${T850_SOURCE_DIR}/FrameworkImGui/include
  ${T850_SOURCE_DIR}/Framework
  ${T850_SOURCE_DIR}/Framework/include
  ${T850_SOURCE_DIR}/Librerias/tinyxml2/include
  ${T850_ANDROID_VCPKG_ROOT}/include
  ${T850_SOURCE_DIR}/GLSLParser/Include
  ${T850_ANDROID_NATIVE_APP_GLUE_DIR})

target_include_directories(T850Android SYSTEM PRIVATE
  ${T850_SOURCE_DIR}/Librerias/glaze/include
  ${T850_SOURCE_DIR}/Librerias/stb/include
  ${T850_SOURCE_DIR}/Librerias/mikktspace/include
  ${T850_SOURCE_DIR}/Librerias/VulkanMemoryAllocator/include)

target_link_libraries(T850Android PRIVATE
  android
  log
  vulkan
  "${T850_ANDROID_DRACO_LIB}"
  Jolt::Jolt
  imgui::imgui
  glslang::glslang
  glslang::glslang-default-resource-limits
  glslang::SPIRV)
