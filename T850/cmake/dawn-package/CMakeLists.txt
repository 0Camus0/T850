cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)
project(T850DawnPackage LANGUAGES CXX)
enable_testing()
add_executable(ShaderPreprocessorProbe PreprocessorProbe.cpp ../../Framework/src/utils/ShaderPreprocessor.cpp
  ../../Librerias/simplecpp/simplecpp.cpp)
target_include_directories(ShaderPreprocessorProbe PRIVATE ../../Framework/include ../../Librerias/simplecpp)
target_compile_features(ShaderPreprocessorProbe PRIVATE cxx_std_17)
target_compile_definitions(ShaderPreprocessorProbe PRIVATE _CRT_SECURE_NO_WARNINGS)
add_test(NAME ShaderPreprocessor COMMAND ShaderPreprocessorProbe)
option(T850_PREPROCESSOR_ONLY "Build portable shader preprocessing tests without Dawn" OFF)
if(T850_PREPROCESSOR_ONLY)
  return()
endif()
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "T850 Dawn package probe requires Windows x64 MSVC.")
endif()
if(NOT T850_DAWN_PACKAGE_ROOT)
  message(FATAL_ERROR "T850_DAWN_PACKAGE_ROOT is required.")
endif()
list(PREPEND CMAKE_PREFIX_PATH "${T850_DAWN_PACKAGE_ROOT}")
find_package(Dawn CONFIG REQUIRED)
add_executable(DawnSurfaceProbe SurfaceProbe.cpp ../../Framework/src/video/webgpu/WebGPUContext.cpp
  ../../Framework/src/utils/Log.cpp)
target_include_directories(DawnSurfaceProbe PRIVATE ../../Framework/include ../../Framework "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(DawnSurfaceProbe PRIVATE dawn::webgpu_dawn user32)
target_compile_options(DawnSurfaceProbe PRIVATE /Zc:preprocessor)
add_executable(DawnPackageProbe Probe.cpp)
target_link_libraries(DawnPackageProbe PRIVATE dawn::webgpu_dawn)
add_executable(DawnMSBuildProbe Probe.cpp)
target_include_directories(DawnMSBuildProbe PRIVATE "${T850_DAWN_PACKAGE_ROOT}/include")
set_target_properties(DawnMSBuildProbe PROPERTIES
  VS_USER_PROPS "${CMAKE_CURRENT_BINARY_DIR}/Dawn.$(Configuration).props")
find_package(glslang CONFIG REQUIRED)
add_executable(DawnShaderProbe ShaderProbe.cpp ../../Framework/src/video/webgpu/WebGPUShaderCompiler.cpp
  ../../Framework/src/utils/ShaderDiskCache.cpp ../../Framework/src/utils/ResourceLocator.cpp
  ../../Framework/src/utils/Log.cpp)
target_sources(DawnShaderProbe PRIVATE ShaderNumerics.cpp ../../Framework/src/utils/ShaderPreprocessor.cpp
  ../../Librerias/simplecpp/simplecpp.cpp)
target_include_directories(DawnShaderProbe PRIVATE ../../Librerias/simplecpp)
target_compile_definitions(DawnShaderProbe PRIVATE _CRT_SECURE_NO_WARNINGS)
set_source_files_properties(ShaderNumerics.cpp PROPERTIES COMPILE_DEFINITIONS NOMINMAX)
target_include_directories(DawnShaderProbe PRIVATE ../../Framework/include ../../Framework
  ../../Librerias/glaze/include "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(DawnShaderProbe PRIVATE /Zc:preprocessor)
target_link_libraries(DawnShaderProbe PRIVATE dawn::webgpu_dawn glslang::glslang
  glslang::SPIRV glslang::glslang-default-resource-limits d3dcompiler d3d11 dxgi)
foreach(target DawnPackageProbe DawnMSBuildProbe DawnShaderProbe DawnSurfaceProbe)
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${T850_DAWN_PACKAGE_ROOT}/$<$<CONFIG:Debug>:debug/>bin/dxcompiler.dll"
      "${T850_DAWN_PACKAGE_ROOT}/$<$<CONFIG:Debug>:debug/>bin/dxil.dll"
      "$<TARGET_FILE_DIR:${target}>"
    VERBATIM)
endforeach()

set(shader_directory "${CMAKE_CURRENT_SOURCE_DIR}/../../Assets/Shaders")
set(process_cache "${CMAKE_CURRENT_BINARY_DIR}/ctest-process-cache-$<CONFIG>")
add_test(NAME DawnShaderFlows COMMAND DawnShaderProbe "${shader_directory}"
  "${CMAKE_CURRENT_BINARY_DIR}/ctest-flow-cache-$<CONFIG>" flow-test)
set_tests_properties(DawnShaderFlows PROPERTIES TIMEOUT 120)
add_test(NAME DawnComputeV1 COMMAND DawnShaderProbe "${shader_directory}"
  "${CMAKE_CURRENT_BINARY_DIR}/ctest-compute-v1-cache-$<CONFIG>" compute-v1)
set_tests_properties(DawnComputeV1 PROPERTIES TIMEOUT 120)
add_test(NAME DawnShaderDefaultFlow COMMAND DawnShaderProbe "${shader_directory}"
  "${CMAKE_CURRENT_BINARY_DIR}/ctest-default-flow-cache-$<CONFIG>")
set_tests_properties(DawnShaderDefaultFlow PROPERTIES TIMEOUT 120)
add_test(NAME DawnShaderCache COMMAND DawnShaderProbe "${shader_directory}"
  "${CMAKE_CURRENT_BINARY_DIR}/ctest-cache" test)
add_test(NAME WgslVertexPermutations COMMAND DawnShaderProbe "${shader_directory}"
  "${CMAKE_CURRENT_BINARY_DIR}/ctest-cache" permutations)
set_tests_properties(WgslVertexPermutations PROPERTIES TIMEOUT 600)
add_test(NAME WgslRecordedPermutations COMMAND DawnShaderProbe "${shader_directory}"
  "${CMAKE_CURRENT_BINARY_DIR}/ctest-cache" corpus)
set_tests_properties(WgslRecordedPermutations PROPERTIES TIMEOUT 600)
option(T850_SHADER_GPU_TESTS "Run hardware HLSL/WGSL differential tests" OFF)
if(T850_SHADER_GPU_TESTS)
  add_test(NAME DawnSurfaceLifecycle COMMAND DawnSurfaceProbe)
  set_tests_properties(DawnSurfaceLifecycle PROPERTIES TIMEOUT 120 LABELS GPU)
  add_test(NAME WgslGpuNumerics COMMAND DawnShaderProbe "${shader_directory}"
    "${CMAKE_CURRENT_BINARY_DIR}/ctest-gpu-cache" gpu)
  set_tests_properties(WgslGpuNumerics PROPERTIES TIMEOUT 180 LABELS GPU)
endif()
add_test(NAME DawnShaderCacheReset COMMAND ${CMAKE_COMMAND} -E rm -rf "${process_cache}")
set_tests_properties(DawnShaderCacheReset PROPERTIES FIXTURES_SETUP EmptyShaderCache)
add_test(NAME DawnShaderCold COMMAND DawnShaderProbe "${shader_directory}" "${process_cache}" cold)
set_tests_properties(DawnShaderCold PROPERTIES FIXTURES_REQUIRED EmptyShaderCache FIXTURES_SETUP WarmShaderCache)
add_test(NAME DawnShaderWarm COMMAND DawnShaderProbe "${shader_directory}" "${process_cache}" warm)
set_tests_properties(DawnShaderWarm PROPERTIES FIXTURES_REQUIRED WarmShaderCache)
set_tests_properties(DawnShaderCache DawnShaderCold DawnShaderWarm PROPERTIES TIMEOUT 120)