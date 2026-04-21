cmake_minimum_required (VERSION 2.6)
project (T8ditor)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/../bin)
file(GLOB SOURCES *.cpp)

# Mirror DayScene's link/include layout so the editor uses the same
# Linux toolchain (SDL/EGL/etc.). The editor itself is mostly Windows-first;
# this CMake file just lets the project configure on Linux for parity.
if (HEADLESS)
LINK_DIRECTORIES(${PROJECT_SOURCE_DIR}/../Librerias/freeglut-linux/lib
${PROJECT_SOURCE_DIR}/../Librerias/glew-2.0.0/lib
${PROJECT_SOURCE_DIR}/../Librerias/SDL-1.2.14/lib
${PROJECT_SOURCE_DIR}/../Librerias/tinyxml2/lib
${PROJECT_SOURCE_DIR}/../Librerias/mesa-libs/lib
${PROJECT_SOURCE_DIR}/../bin)
else()
LINK_DIRECTORIES(${PROJECT_SOURCE_DIR}/../Librerias/freeglut-linux/lib
${PROJECT_SOURCE_DIR}/../Librerias/glew-2.0.0/lib
${PROJECT_SOURCE_DIR}/../Librerias/OGLES2/pvr/lib
${PROJECT_SOURCE_DIR}/../Librerias/SDL-1.2.14/lib
${PROJECT_SOURCE_DIR}/../Librerias/tinyxml2/lib
${PROJECT_SOURCE_DIR}/../bin)
endif()

add_executable(T8ditor ${SOURCES})

if (HEADLESS)
include_directories(${PROJECT_SOURCE_DIR}/../Framework
${PROJECT_SOURCE_DIR}/../Framework/include
${PROJECT_SOURCE_DIR}/../Librerias/freeglut-linux
${PROJECT_SOURCE_DIR}/../Librerias/freeglut-linux/include
${PROJECT_SOURCE_DIR}/../Librerias/glew-2.0.0/include
${PROJECT_SOURCE_DIR}/../Librerias/OGLES2/amd/include
${PROJECT_SOURCE_DIR}/../Librerias/SDL-1.2.14/include
${PROJECT_SOURCE_DIR}/../Librerias/stb/include
${PROJECT_SOURCE_DIR}/../Librerias/tinyxml2/include
${PROJECT_SOURCE_DIR}/../Librerias/mesa-libs/include)

target_link_libraries(T8ditor T800Framework freeglut glew EGL GLESv2 wayland-cursor wayland-client wayland-egl xkbcommon gbm)
else()
include_directories(${PROJECT_SOURCE_DIR}/../Framework
${PROJECT_SOURCE_DIR}/../Framework/include
${PROJECT_SOURCE_DIR}/../Librerias/freeglut-linux
${PROJECT_SOURCE_DIR}/../Librerias/freeglut-linux/include
${PROJECT_SOURCE_DIR}/../Librerias/glew-2.0.0/include
${PROJECT_SOURCE_DIR}/../Librerias/OGLES2/amd/include
${PROJECT_SOURCE_DIR}/../Librerias/SDL-1.2.14/include
${PROJECT_SOURCE_DIR}/../Librerias/stb/include
${PROJECT_SOURCE_DIR}/../Librerias/tinyxml2/include)

target_link_libraries(T8ditor T800Framework freeglut glew EGL GLESv2 wayland-cursor wayland-client wayland-egl xkbcommon)
endif()
