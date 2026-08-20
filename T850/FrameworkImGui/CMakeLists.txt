project(FrameworkImGui LANGUAGES CXX)

add_library(FrameworkImGui STATIC
  src/ImGuiSystem.cpp
  src/DevGuiContext.cpp
  src/RagdollEditorGui.cpp)

t850_apply_common(FrameworkImGui)
t850_apply_library_output(FrameworkImGui)

set_target_properties(FrameworkImGui PROPERTIES DEBUG_POSTFIX d)
target_compile_definitions(FrameworkImGui PRIVATE _LIB)

target_include_directories(FrameworkImGui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${T850_SOURCE_DIR}/Framework
  ${T850_SOURCE_DIR}/Framework/include
  ${T850_VCPKG_STATIC}/include
  ${T850_VCPKG_DYNAMIC}/include)

if(WIN32)
  target_include_directories(FrameworkImGui PUBLIC ${T850_SOURCE_DIR}/Librerias/SDL3/include)
  target_link_libraries(FrameworkImGui PUBLIC
    Framework
    $<$<CONFIG:Debug>:imguid>
    $<$<NOT:$<CONFIG:Debug>>:imgui>)
else()
  target_link_libraries(FrameworkImGui PUBLIC
    Framework
    imgui::imgui)
endif()

install(TARGETS FrameworkImGui DESTINATION Lib)