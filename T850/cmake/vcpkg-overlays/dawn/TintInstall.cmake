include_guard(GLOBAL)

set(t850_present_source "${CMAKE_SOURCE_DIR}/src/dawn/native/d3d/SwapChainD3D.cpp")
file(READ "${t850_present_source}" t850_present_text)
set(t850_present_before "    HRESULT presentResult = mDXGISwapChain->Present(PresentModeToSwapInterval(GetPresentMode()), 0);")
set(t850_present_after "    UINT presentFlags = 0;
    if (GetPresentMode() == wgpu::PresentMode::Immediate) {
        BOOL fullscreen = FALSE;
        DAWN_TRY(CheckHRESULT(mDXGISwapChain->GetFullscreenState(&fullscreen, nullptr),
                              \"Querying swapchain fullscreen state\"));
        if (!fullscreen) {
            presentFlags = DXGI_PRESENT_ALLOW_TEARING;
        }
    }
    HRESULT presentResult =
        mDXGISwapChain->Present(PresentModeToSwapInterval(GetPresentMode()), presentFlags);")
string(FIND "${t850_present_text}" "${t850_present_before}" t850_present_position)
if(NOT t850_present_position EQUAL -1)
  string(REPLACE "${t850_present_before}" "${t850_present_after}" t850_present_text "${t850_present_text}")
  file(WRITE "${t850_present_source}" "${t850_present_text}")
else()
  string(FIND "${t850_present_text}" "${t850_present_after}" t850_present_position)
  if(t850_present_position EQUAL -1)
    message(FATAL_ERROR "Pinned Dawn immediate presentation patch no longer matches its source.")
  endif()
endif()

set(t850_matrix_source "${CMAKE_SOURCE_DIR}/src/tint/lang/spirv/reader/lower/transpose_row_major.cc")
file(READ "${t850_matrix_source}" t850_matrix_text)
set(t850_matrix_before "            if (new_access_ty != access->Result()->Type()) {
                access->Result()->SetType(new_access_ty);
                results_to_update.Push(access->Result());
            }")
set(t850_matrix_after "            if (new_access_ty != access->Result()->Type()) {
                access->Result()->SetType(new_access_ty);
            }
            results_to_update.Push(access->Result());")
string(FIND "${t850_matrix_text}" "${t850_matrix_before}" t850_matrix_position)
if(NOT t850_matrix_position EQUAL -1)
  string(REPLACE "${t850_matrix_before}" "${t850_matrix_after}" t850_matrix_text "${t850_matrix_text}")
  file(WRITE "${t850_matrix_source}" "${t850_matrix_text}")
else()
  string(FIND "${t850_matrix_text}" "${t850_matrix_after}" t850_matrix_position)
  if(t850_matrix_position EQUAL -1)
    message(FATAL_ERROR "Pinned Tint row-major load patch no longer matches its source.")
  endif()
endif()

set(t850_operand_source "${CMAKE_SOURCE_DIR}/src/tint/lang/core/ir/operand_instruction.h")
file(READ "${t850_operand_source}" t850_operand_text)
set(t850_operand_before "    Value* PopOperand() { return operands_.Pop(); }")
set(t850_operand_after "    Value* PopOperand() {
        auto* operand = operands_.Pop();
        if (operand) {
            operand->RemoveUsage({this, static_cast<uint32_t>(operands_.Length())});
        }
        return operand;
    }")
string(FIND "${t850_operand_text}" "${t850_operand_before}" t850_operand_position)
if(NOT t850_operand_position EQUAL -1)
  string(REPLACE "${t850_operand_before}" "${t850_operand_after}" t850_operand_text "${t850_operand_text}")
  file(WRITE "${t850_operand_source}" "${t850_operand_text}")
else()
  string(FIND "${t850_operand_text}" "${t850_operand_after}" t850_operand_position)
  if(t850_operand_position EQUAL -1)
    message(FATAL_ERROR "Pinned Tint operand usage patch no longer matches its source.")
  endif()
endif()

function(t850_build_installed_tint_libraries directory)
  get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(kind "${target}" TYPE)
    if(target MATCHES "^tint_" AND kind STREQUAL "STATIC_LIBRARY")
      set_property(TARGET "${target}" PROPERTY EXCLUDE_FROM_ALL FALSE)
    endif()
  endforeach()
  get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(child IN LISTS children)
    t850_build_installed_tint_libraries("${child}")
  endforeach()
endfunction()

function(t850_prepare_tint_install)
  if(TINT_ENABLE_INSTALL)
    t850_build_installed_tint_libraries("${CMAKE_SOURCE_DIR}/src/tint")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/src/utils/"
      DESTINATION include/src/utils FILES_MATCHING PATTERN "*.h")
  endif()
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL t850_prepare_tint_install)