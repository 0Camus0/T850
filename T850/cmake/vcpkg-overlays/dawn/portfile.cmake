include("${CMAKE_CURRENT_LIST_DIR}/../RequirePinnedVcpkg.cmake")
if(NOT "d3d12" IN_LIST FEATURES)
  message(FATAL_ERROR "T850 requires Dawn's d3d12 feature.")
endif()
list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS
  "-DTINT_BUILD_SPV_READER=ON"
  "-DTINT_BUILD_GLSL_VALIDATOR=OFF"
  "-DTINT_ENABLE_INSTALL=ON"
  "-DCMAKE_PROJECT_INCLUDE=${CMAKE_CURRENT_LIST_DIR}/TintInstall.cmake")
set(CURRENT_PORT_DIR "${VCPKG_ROOT_DIR}/ports/dawn")
include("${VCPKG_ROOT_DIR}/ports/dawn/portfile.cmake")
file(COPY "${CURRENT_PACKAGES_DIR}/include/src/tint/src/tint/"
  DESTINATION "${CURRENT_PACKAGES_DIR}/include/src/tint")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/include/src/tint/src")