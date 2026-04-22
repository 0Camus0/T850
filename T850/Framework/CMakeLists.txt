cmake_minimum_required (VERSION 2.6)
project (T800Framework)

set(CMAKE_CXX_STANDARD 11)

if(HEADLESS)
message(STATUS ">>> HEADLESS")
add_definitions(-DT850_HEADLESS)
include_directories(${PROJECT_SOURCE_DIR}/../Librerias/freeglut-linux 
${PROJECT_SOURCE_DIR}/../Librerias/freeglut-linux/include
${PROJECT_SOURCE_DIR}/../Librerias/glew-2.0.0/include 
${PROJECT_SOURCE_DIR}/../Librerias/SDL-1.2.14/include 
${PROJECT_SOURCE_DIR}/../Librerias/stb/include
${PROJECT_SOURCE_DIR}/../Librerias/tinyxml2/include
${PROJECT_SOURCE_DIR}/../Librerias/mikktspace/include
${PROJECT_SOURCE_DIR}/../Librerias/mesa-libs/include)
else()
include_directories(${PROJECT_SOURCE_DIR}/../Librerias/freeglut-linux 
${PROJECT_SOURCE_DIR}/../Librerias/freeglut-linux/include
${PROJECT_SOURCE_DIR}/../Librerias/glew-2.0.0/include 
${PROJECT_SOURCE_DIR}/../Librerias/OGLES2/pvr/include 
${PROJECT_SOURCE_DIR}/../Librerias/SDL-1.2.14/include 
${PROJECT_SOURCE_DIR}/../Librerias/stb/include
${PROJECT_SOURCE_DIR}/../Librerias/tinyxml2/include
${PROJECT_SOURCE_DIR}/../Librerias/mikktspace/include)
endif()
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/../Lib)
file(GLOB SOURCES src/video/*.cpp 
src/video/gl/*.cpp 
src/video/d3d11/*.cpp 
src/utils/*.cpp 
src/utils/gltf/*.cpp 
src/scene/*.cpp 
src/core/*.cpp
${PROJECT_SOURCE_DIR}/../Librerias/tinyxml2/tinyxml2.cpp
${PROJECT_SOURCE_DIR}/../Librerias/mikktspace/src/mikktspace.c)

add_library(T800Framework ${SOURCES})
target_include_directories(T800Framework PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/include
$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/../Librerias/glaze/include
$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/../GLSLParser/Include)

# Draco mesh compression (optional — if not found, Draco-compressed glTF files won't load)
find_package(draco CONFIG QUIET)
if(draco_FOUND)
  target_link_libraries(T800Framework draco::draco)
  message(STATUS ">>> Draco found")
else()
  # Try pkg-config fallback
  find_library(DRACO_LIB draco)
  if(DRACO_LIB)
    target_link_libraries(T800Framework ${DRACO_LIB})
    message(STATUS ">>> Draco found via find_library: ${DRACO_LIB}")
  else()
    message(WARNING ">>> Draco not found — KHR_draco_mesh_compression will not be available")
  endif()
endif()

install(TARGETS T800Framework DESTINATION Lib)