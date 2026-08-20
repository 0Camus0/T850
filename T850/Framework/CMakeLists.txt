project(Framework LANGUAGES C CXX)

set(FRAMEWORK_SOURCES
  pch.cpp
  ../Librerias/tinyxml2/tinyxml2.cpp
  ../Librerias/mikktspace/src/mikktspace.c
  src/core/Core.cpp
  src/core/DevLayer.cpp
  src/core/Config.cpp
  src/core/EngineContext.cpp
  src/game/ComponentFactory.cpp
  src/game/Controller.cpp
  src/game/EventBus.cpp
  src/game/GameIds.cpp
  src/game/GameLogicSystem.cpp
  src/game/GameNavigationService.cpp
  src/game/GameSelfTest.cpp
  src/game/GameValidation.cpp
  src/game/GameObjectRegistry.cpp
  src/game/GamePhysicsService.cpp
  src/game/MovementComponent.cpp
  src/game/StateMachine.cpp
  src/game/examples/HealthComponent.cpp
  src/game/examples/GroupManager.cpp
  src/game/examples/PathFollowComponent.cpp
  src/game/examples/RtsCommandController.cpp
  src/game/examples/WeaponComponent.cpp
  src/physics/GameplayLayers.cpp
  src/physics/CharacterController.cpp
  src/physics/JoltPhysicsSystem.cpp
  src/physics/PhysicsDebugRenderer.cpp
  src/physics/Q3BspCollision.cpp
  src/physics/PhysicsAuthoring.cpp
  src/physics/RagdollEditorTool.cpp
  src/navigation/NavigationSystem.cpp
  src/navigation/NavigationDebugRenderer.cpp
  src/scene/LensFlare.cpp
  src/scene/RenderMesh.cpp
  src/scene/RenderSkinnedMesh.cpp
  src/scene/LineRenderer.cpp
  src/scene/WireframeGeometry.cpp
  src/scene/AnimationController.cpp
  src/scene/PrimitiveInstance.cpp
  src/scene/PrimitiveManager.cpp
  src/scene/RenderQuad.cpp
  src/scene/MeshAssetCache.cpp
  src/scene/MaterialAssetCache.cpp
  src/scene/MeshPool.cpp
  src/scene/MutableMeshData.cpp
  src/scene/MutableMesh.cpp
  src/scene/RenderQueue.cpp
  src/scene/RenderContainer.cpp
  src/scene/RenderResourceRegistry.cpp
  src/scene/IBLResources.cpp
  src/scene/RenderViewport.cpp
  src/debug/FrameDumper.cpp
  src/debug/CrashDiagnostics.cpp
  src/debug/FrameDumperIO.cpp
  src/debug/Profiler.cpp
  src/debug/RuntimeTelemetry.cpp
  src/debug/RenderTrace.cpp
  src/scene/EditorSceneFile.cpp
  src/scene/RenderGraph.cpp
  src/scene/SceneDescriptor.cpp
  src/scene/SceneSetup.cpp
  src/scene/SceneProp.cpp
  src/scene/SplineWireframe.cpp
  src/scene/WireframeSphere.cpp
  src/scene/WireframeArrow.cpp
  src/scene/Quad.cpp
  src/scene/TextRenderer.cpp
  src/terrain/BlockRegistry.cpp
  src/terrain/VoxelChunk.cpp
  src/terrain/VoxelMesher.cpp
  src/terrain/VoxelWorld.cpp
  src/terrain/VoxelStreaming.cpp
  src/terrain/VoxelPersistence.cpp
  src/utils/cil.cpp
  src/utils/Log.cpp
  src/utils/InputManager.cpp
  src/utils/ResourceManager.cpp
  src/utils/ResourceLocator.cpp
  src/utils/Spline.cpp
  src/utils/Technique.cpp
  src/utils/Timer.cpp
  src/utils/Utils.cpp
  src/utils/Camera.cpp
  src/utils/CameraProfiles.cpp
  src/utils/Picking.cpp
  src/utils/XDataBase.cpp
  src/utils/XMaths.cpp
  src/utils/ConfigRuntime.cpp
  src/utils/RuntimeProfile.cpp
  src/utils/ShaderPermutationDump.cpp
  src/utils/SPIRVReflection.cpp
  src/utils/ShaderDiskCache.cpp
  src/video/BaseDriver.cpp
  src/utils/gltf/GLTFLoader.cpp
  src/utils/gltf/GLTFJson.cpp
  src/utils/gltf/GLTFAccessor.cpp
  src/utils/gltf/GLTFBase64.cpp
  src/utils/gltf/GLTFImage.cpp
  src/utils/gltf/GLTFMaterial.cpp
  src/utils/gltf/GLTFMesh.cpp
  src/utils/gltf/GLTFAnimation.cpp
  src/utils/ThreadPool.cpp)

set(T850_VULKAN_SOURCES
  src/video/vulkan/VulkanDriver.cpp
  src/video/vulkan/VulkanVertexBuffer.cpp
  src/video/vulkan/VulkanIndexBuffer.cpp
  src/video/vulkan/VulkanConstantBuffer.cpp
  src/video/vulkan/VulkanTexture.cpp
  src/video/vulkan/VulkanRT.cpp
  src/video/vulkan/VulkanShader.cpp
  src/video/vulkan/VulkanDeviceContext.cpp
  src/video/vulkan/VulkanDevice.cpp
  src/video/vulkan/VulkanUtils.cpp)

if(WIN32)
  list(APPEND FRAMEWORK_SOURCES
    src/core/windows/Win32Framework.cpp
    src/video/gl/GLDriver.cpp
    src/video/gl/GLDeviceContext.cpp
    src/video/gl/GLDevice.cpp
    src/video/gl/GLVertexBuffer.cpp
    src/video/gl/GLIndexBuffer.cpp
    src/video/gl/GLConstantBuffer.cpp
    src/video/gl/GLRT.cpp
    src/video/gl/GLShader.cpp
    src/video/gl/GLSLParser.cpp
    src/video/gl/GLTexture.cpp
    src/video/d3d11/D3D11DeviceContext.cpp
    src/video/d3d11/D3D11Device.cpp
    src/video/d3d11/D3D11VertexBuffer.cpp
    src/video/d3d11/D3D11IndexBuffer.cpp
    src/video/d3d11/D3D11ConstantBuffer.cpp
    src/video/d3d11/D3D11Driver.cpp
    src/video/d3d11/D3D11RT.cpp
    src/video/d3d11/D3D11Shader.cpp
    src/video/d3d11/D3D11Texture.cpp
    src/video/d3d12/D3D12Driver.cpp
    src/video/d3d12/D3D12Heap.cpp
    src/video/d3d12/D3D12VertexBuffer.cpp
    src/video/d3d12/D3D12IndexBuffer.cpp
    src/video/d3d12/D3D12ConstantBuffer.cpp
    src/video/d3d12/D3D12DeviceContext.cpp
    src/video/d3d12/D3D12Device.cpp
    src/video/d3d12/D3D12Shader.cpp
    src/video/d3d12/D3D12RT.cpp
    src/video/d3d12/D3D12Texture.cpp
    ${T850_VULKAN_SOURCES})
elseif(T850_PLATFORM_STEAM_DECK)
  list(APPEND FRAMEWORK_SOURCES
    src/core/LinuxFramework.cpp
    ${T850_VULKAN_SOURCES})
endif()

add_library(Framework STATIC ${FRAMEWORK_SOURCES})
add_library(T800Framework ALIAS Framework)

if(MSVC)
  set_source_files_properties(../Librerias/tinyxml2/tinyxml2.cpp PROPERTIES COMPILE_OPTIONS /wd4267)
endif()

t850_apply_common(Framework)
t850_apply_library_output(Framework)
t850_apply_profiler_defines(Framework)

set_target_properties(Framework PROPERTIES DEBUG_POSTFIX d)
target_compile_definitions(Framework PRIVATE _LIB)
target_precompile_headers(Framework PRIVATE $<$<COMPILE_LANGUAGE:CXX>:pch.h>)
if(T850_JOLT_ENABLED)
  target_compile_definitions(Framework PUBLIC T850_ENABLE_JOLT=1)
  target_link_libraries(Framework PUBLIC Jolt::Jolt)
endif()
if(T850_RECAST_ENABLED)
  target_compile_definitions(Framework PUBLIC T850_ENABLE_RECAST=1)
  target_link_libraries(Framework PUBLIC
    RecastNavigation::Recast
    RecastNavigation::Detour
    RecastNavigation::DetourCrowd
    RecastNavigation::DetourTileCache)
endif()
target_include_directories(Framework PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${T850_SOURCE_DIR}/Librerias/tinyxml2/include
  ${T850_VCPKG_STATIC}/include
  ${T850_VCPKG_DYNAMIC}/include
  ${T850_SOURCE_DIR}/Librerias/stb/include
  ${T850_SOURCE_DIR}/Librerias/mikktspace/include
  ${T850_SOURCE_DIR}/GLSLParser/Include)

if(WIN32)
  target_include_directories(Framework PUBLIC ${T850_SOURCE_DIR}/Librerias/SDL3/include)
endif()

target_include_directories(Framework SYSTEM PUBLIC
  ${T850_SOURCE_DIR}/Librerias/glaze/include)

target_link_directories(Framework PRIVATE
  $<$<CONFIG:Debug>:${T850_VCPKG_STATIC}/debug/lib>
  ${T850_VCPKG_STATIC}/lib)

install(TARGETS Framework DESTINATION Lib)
