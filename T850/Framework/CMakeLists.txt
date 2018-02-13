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
${PROJECT_SOURCE_DIR}/../Librerias/mesa-libs/include)
else()
include_directories(${PROJECT_SOURCE_DIR}/../Librerias/freeglut-linux 
${PROJECT_SOURCE_DIR}/../Librerias/freeglut-linux/include
${PROJECT_SOURCE_DIR}/../Librerias/glew-2.0.0/include 
${PROJECT_SOURCE_DIR}/../Librerias/OGLES2/pvr/include 
${PROJECT_SOURCE_DIR}/../Librerias/SDL-1.2.14/include 
${PROJECT_SOURCE_DIR}/../Librerias/stb/include
${PROJECT_SOURCE_DIR}/../Librerias/tinyxml2/include)
endif()
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/../Lib)
file(GLOB SOURCES src/video/*.cpp 
src/utils/*.cpp 
src/scene/*.cpp 
src/core/*.cpp
${PROJECT_SOURCE_DIR}/../Librerias/tinyxml2/tinyxml2.cpp)

add_library(T800Framework ${SOURCES})
target_include_directories(T800Framework PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/include
$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/../GLSLParser/Include)

target_link_libraries(T800Framework )

install(TARGETS T800Framework DESTINATION Lib)