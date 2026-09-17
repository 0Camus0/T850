message(STATUS "Configuring T850 Emscripten WebGPU build")

set(T850_EMDAWN_VERSION "v20260219.200501")
set(T850_EMDAWN_PORT "${CMAKE_BINARY_DIR}/emdawnwebgpu-${T850_EMDAWN_VERSION}.remoteport.py")
if(NOT EXISTS "${T850_EMDAWN_PORT}")
  file(DOWNLOAD
    "https://github.com/google/dawn/releases/download/${T850_EMDAWN_VERSION}/emdawnwebgpu-${T850_EMDAWN_VERSION}.remoteport.py"
    "${T850_EMDAWN_PORT}"
    TLS_VERIFY ON
    STATUS T850_EMDAWN_DOWNLOAD)
  list(GET T850_EMDAWN_DOWNLOAD 0 T850_EMDAWN_DOWNLOAD_CODE)
  if(NOT T850_EMDAWN_DOWNLOAD_CODE EQUAL 0)
    file(REMOVE "${T850_EMDAWN_PORT}")
    message(FATAL_ERROR "Cannot download pinned Emdawnwebgpu port: ${T850_EMDAWN_DOWNLOAD}")
  endif()
endif()

file(SHA256 "${T850_EMDAWN_PORT}" T850_EMDAWN_HASH)
if(NOT T850_EMDAWN_HASH STREQUAL "c355e19619aa23e8630a526793353230918635b79cc8b0b0149fbc48747405ab")
  message(FATAL_ERROR "Pinned Emdawnwebgpu port checksum mismatch")
endif()

add_library(t850_webgpu INTERFACE)
target_compile_options(t850_webgpu INTERFACE "--use-port=${T850_EMDAWN_PORT}")
target_link_options(t850_webgpu INTERFACE "--use-port=${T850_EMDAWN_PORT}")

include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "--use-port=${T850_EMDAWN_PORT}")
check_cxx_source_compiles("#include <webgpu/webgpu_cpp.h>
int main() {
  auto instance = wgpu::CreateInstance();
  return instance ? 0 : 1;
}" T850_EMDAWN_API_COMPILES)
unset(CMAKE_REQUIRED_FLAGS)
if(NOT T850_EMDAWN_API_COMPILES)
  message(FATAL_ERROR "Pinned Emdawnwebgpu C++ API failed to compile/link; see CMake configure log")
endif()

set(T850_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(T850_VCPKG_STATIC "${CMAKE_BINARY_DIR}/web-deps")
set(T850_VCPKG_DYNAMIC "${CMAKE_BINARY_DIR}/web-deps")
set(T850_JOLT_ENABLED ON)
set(T850_RECAST_ENABLED ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_compile_options(-pthread -msimd128 "$<$<COMPILE_LANGUAGE:CXX>:-fexceptions>")
add_link_options(-pthread -msimd128 -fexceptions
  -sSTACK_SIZE=2097152 -sDEFAULT_PTHREAD_STACK_SIZE=2097152 -sSTACK_OVERFLOW_CHECK=2)

include(FetchContent)
find_package(Git REQUIRED)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sdl3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.2 GIT_SHALLOW TRUE)
set(DRACO_TESTS OFF CACHE BOOL "" FORCE)
set(DRACO_JS_GLUE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(draco
  GIT_REPOSITORY https://github.com/google/draco.git
  GIT_TAG 1.5.7 GIT_SHALLOW TRUE
  PATCH_COMMAND ${CMAKE_COMMAND}
    "-DPATCH_FILE=${T850_SOURCE_DIR}/cmake/web-patches/draco-ply-algorithm.patch"
    -P "${T850_SOURCE_DIR}/cmake/ApplyDependencyPatch.cmake")
set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(recastnavigation
  GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
  GIT_TAG v1.6.0 GIT_SHALLOW TRUE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_WASM_SIMD ON CACHE BOOL "" FORCE)
FetchContent_Declare(jolt
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG v5.5.0 GIT_SHALLOW TRUE SOURCE_SUBDIR Build)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.7-docking GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(sdl3 draco recastnavigation jolt imgui)
foreach(component Recast Detour DetourCrowd DetourTileCache)
  file(COPY "${recastnavigation_SOURCE_DIR}/${component}/Include/"
    DESTINATION "${T850_VCPKG_STATIC}/include/recastnavigation")
endforeach()
file(COPY "${recastnavigation_BINARY_DIR}/version.h"
  DESTINATION "${T850_VCPKG_STATIC}/include/recastnavigation")
if(NOT TARGET Jolt::Jolt)
  add_library(Jolt::Jolt ALIAS Jolt)
endif()
add_library(t850_web_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp)
add_library(imgui::imgui ALIAS t850_web_imgui)
target_include_directories(t850_web_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(t850_web_imgui PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
target_link_libraries(t850_web_imgui PUBLIC SDL3::SDL3 t850_webgpu)

function(t850_apply_common target)
  target_include_directories(${target} PUBLIC ${T850_SOURCE_DIR}/FrameworkImGui/include)
  target_include_directories(${target} SYSTEM PUBLIC ${draco_SOURCE_DIR}/src ${CMAKE_BINARY_DIR})
  target_link_libraries(${target} PUBLIC t850_webgpu SDL3::SDL3 imgui::imgui draco::draco)
endfunction()
function(t850_apply_library_output target)
  set_target_properties(${target} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endfunction()
function(t850_apply_profiler_defines target)
endfunction()
function(t850_apply_app target)
  t850_apply_common(${target})
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${target} PRIVATE Framework)
  set_target_properties(${target} PROPERTIES SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/site")
  target_link_options(${target} PRIVATE -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=262144
    --emit-symbol-map --profiling-funcs
    -sPROXY_TO_PTHREAD=1 -sOFFSCREENCANVAS_SUPPORT=1
    "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas"
    "--shell-file=${T850_SOURCE_DIR}/web/shell.html" -lidbfs.js
    "-sEXPORTED_RUNTIME_METHODS=['FS','IDBFS','addRunDependency','removeRunDependency']"
    -sALLOW_MEMORY_GROWTH=1 -sPTHREAD_POOL_SIZE=8
    -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=2147483648 -sEXIT_RUNTIME=0)
endfunction()
function(t850_stage_runtime target)
endfunction()

add_subdirectory(Framework)
add_subdirectory(FrameworkImGui)
add_subdirectory(DayScene)
configure_file("${T850_SOURCE_DIR}/web/scenes.json" "${CMAKE_BINARY_DIR}/site/scenes.json" COPYONLY)
configure_file("${T850_SOURCE_DIR}/web/touch-controls.js" "${CMAKE_BINARY_DIR}/site/touch-controls.js" COPYONLY)
file(COPY "${T850_SOURCE_DIR}/web/icons" DESTINATION "${CMAKE_BINARY_DIR}/site")
set_property(TARGET DayScene APPEND PROPERTY LINK_DEPENDS "${T850_SOURCE_DIR}/web/shell.html")

include(CTest)
add_executable(T850WebSelfTests "${T850_SOURCE_DIR}/cmake/web-tests/SelfTest.cpp")
target_link_libraries(T850WebSelfTests PRIVATE FrameworkImGui)
target_link_options(T850WebSelfTests PRIVATE -sASYNCIFY=1 -sENVIRONMENT=node,worker -sEXIT_RUNTIME=1
  -sPTHREAD_POOL_SIZE=8 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456)
set_target_properties(T850WebSelfTests PROPERTIES SUFFIX ".js")
add_test(NAME T850WebSelfTests COMMAND T850WebSelfTests)