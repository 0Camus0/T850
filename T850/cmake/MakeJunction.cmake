if(NOT DEFINED LINK_PATH OR NOT DEFINED TARGET_PATH)
  message(FATAL_ERROR "LINK_PATH and TARGET_PATH are required")
endif()

if(EXISTS "${LINK_PATH}")
  return()
endif()

# Ensure parent directory exists (Rebuild/Clean may have removed it)
get_filename_component(LINK_PARENT "${LINK_PATH}" DIRECTORY)
file(MAKE_DIRECTORY "${LINK_PARENT}")

file(TO_NATIVE_PATH "${LINK_PATH}" NATIVE_LINK_PATH)
file(TO_NATIVE_PATH "${TARGET_PATH}" NATIVE_TARGET_PATH)

if(WIN32)
  execute_process(
    COMMAND cmd /C mklink /J "${NATIVE_LINK_PATH}" "${NATIVE_TARGET_PATH}"
    RESULT_VARIABLE JUNCTION_RESULT)
else()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E create_symlink "${TARGET_PATH}" "${LINK_PATH}"
    RESULT_VARIABLE JUNCTION_RESULT)
endif()

if(NOT JUNCTION_RESULT EQUAL 0)
  if(EXISTS "${LINK_PATH}")
    return()
  endif()
  message(FATAL_ERROR "Failed to create junction: ${LINK_PATH} -> ${TARGET_PATH}")
endif()
