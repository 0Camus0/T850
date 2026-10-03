set(T850_VCPKG_COMMIT "77df67cfff9c12ccfdb52284e07c87c75092f723")
vcpkg_find_acquire_program(GIT)
execute_process(
  COMMAND "${GIT}" -C "${VCPKG_ROOT_DIR}" rev-parse HEAD
  OUTPUT_VARIABLE T850_ACTUAL_VCPKG_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
if(NOT T850_ACTUAL_VCPKG_COMMIT STREQUAL T850_VCPKG_COMMIT)
  message(FATAL_ERROR "T850 Dawn packages require vcpkg ${T850_VCPKG_COMMIT}; found ${T850_ACTUAL_VCPKG_COMMIT}. Do not update unrelated installed dependencies to resolve this mismatch.")
endif()
execute_process(
  COMMAND "${GIT}" -C "${VCPKG_ROOT_DIR}" diff --exit-code HEAD -- ports/dawn ports/imgui
  OUTPUT_QUIET
  COMMAND_ERROR_IS_FATAL ANY)