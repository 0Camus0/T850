find_package(Git REQUIRED)
execute_process(
  COMMAND "${GIT_EXECUTABLE}" apply --reverse --check --ignore-space-change "${PATCH_FILE}"
  RESULT_VARIABLE already_applied
  OUTPUT_QUIET ERROR_QUIET)
if(NOT already_applied EQUAL 0)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --ignore-space-change "${PATCH_FILE}"
    RESULT_VARIABLE patch_result)
  if(NOT patch_result EQUAL 0)
    message(FATAL_ERROR "Cannot apply dependency patch: ${PATCH_FILE}")
  endif()
endif()