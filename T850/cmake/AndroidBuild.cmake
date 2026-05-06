message(STATUS "Configuring T850 Android NativeActivity Vulkan build")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(T850_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
  message(FATAL_ERROR "T850 Android currently supports arm64-v8a only. Requested: ${ANDROID_ABI}")
endif()

if(NOT CMAKE_ANDROID_NDK AND DEFINED ANDROID_NDK)
  set(CMAKE_ANDROID_NDK ${ANDROID_NDK})
endif()

add_library(native_app_glue STATIC
  ${CMAKE_ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC
  ${CMAKE_ANDROID_NDK}/sources/android/native_app_glue)

set(T850_ANDROID_FRAMEWORK_SOURCES
  ${T850_SOURCE_DIR}/Framework/pch.cpp
  ${T850_SOURCE_DIR}/Framework/src/core/Config.cpp
  ${T850_SOURCE_DIR}/Framework/src/core/Core.cpp
  ${T850_SOURCE_DIR}/Framework/src/core/android/AndroidFramework.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/AndroidAssets.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/Log.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/InputManager.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/ResourceManager.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/Timer.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/Utils.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/Camera.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/XDataBase.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/XMaths.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/Technique.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/ConfigRuntime.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/SPIRVReflection.cpp
  ${T850_SOURCE_DIR}/Framework/src/utils/ThreadPool.cpp
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
  ${T850_SOURCE_DIR}/Framework/src/gui/GUIAtlas.cpp
  ${T850_SOURCE_DIR}/Framework/src/gui/GUIElement.cpp
  ${T850_SOURCE_DIR}/Framework/src/gui/GUIManager.cpp
  ${T850_SOURCE_DIR}/Librerias/tinyxml2/tinyxml2.cpp
  ${T850_SOURCE_DIR}/Librerias/mikktspace/src/mikktspace.c)

add_library(T850Android SHARED
  ${T850_SOURCE_DIR}/DayScene/AndroidEntry.cpp
  ${T850_SOURCE_DIR}/DayScene/Application.cpp
  ${T850_SOURCE_DIR}/DayScene/DayScene.cpp
  ${T850_SOURCE_DIR}/DayScene/SandboxScene.cpp
  ${T850_ANDROID_FRAMEWORK_SOURCES})

target_compile_definitions(T850Android PRIVATE OS_ANDROID T850_ANDROID_NATIVE_ACTIVITY)
target_compile_options(T850Android PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_precompile_headers(T850Android PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${T850_SOURCE_DIR}/Framework/pch.h>)
target_include_directories(T850Android PRIVATE
  ${T850_SOURCE_DIR}/DayScene
  ${T850_SOURCE_DIR}/Framework
  ${T850_SOURCE_DIR}/Framework/include
  ${T850_SOURCE_DIR}/Librerias/glaze/include
  ${T850_SOURCE_DIR}/Librerias/tinyxml2/include
  ${T850_SOURCE_DIR}/Librerias/stb/include
  ${T850_SOURCE_DIR}/Librerias/mikktspace/include
  ${T850_SOURCE_DIR}/Librerias/VulkanMemoryAllocator/include
  ${T850_SOURCE_DIR}/GLSLParser/Include
  ${CMAKE_ANDROID_NDK}/sources/android/native_app_glue)

target_link_libraries(T850Android PRIVATE android log vulkan native_app_glue)
