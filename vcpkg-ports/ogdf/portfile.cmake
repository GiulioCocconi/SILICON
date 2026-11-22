vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ogdf/ogdf
    REF foxglove-202510
    SHA512 7a711033790b545e0010d1c83fbe19a4481edf62eb9f62f7cf819f11154e33f41d250cf3f61064cf5b9ac4a03ad150cda36be02b05d860b796e12e1a3a541d9f
    HEAD_REF master
)

# EMSCRIPTEN Fix
if(VCPKG_TARGET_IS_EMSCRIPTEN)
    vcpkg_replace_string(
        "${SOURCE_PATH}/cmake/compiler-specifics.cmake"
        [=[if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT ${CMAKE_SYSTEM_PROCESSOR} MATCHES "^arm")]=]
        [=[if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT EMSCRIPTEN AND NOT ${CMAKE_SYSTEM_PROCESSOR} MATCHES "^arm")]=]
    )
endif()

# Match the imported library even when CMake maps or falls back to another configuration.
set(ogdf_uses_debug_library "$<STREQUAL:$<TARGET_FILE:OGDF>,$<TARGET_PROPERTY:OGDF,IMPORTED_LOCATION_DEBUG>>")
vcpkg_replace_string("${SOURCE_PATH}/cmake/ogdf.cmake"
    "$<INSTALL_INTERFACE:include/ogdf-$<IF:$<CONFIG:Debug>,debug,release>>"
    "$<INSTALL_INTERFACE:include/ogdf-$<IF:${ogdf_uses_debug_library},debug,release>>"
)

# Architecture/Mach Fix (Only applies x86-64-v2 if building for an x64 target)
set(OGDF_OPTIONS "")
if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    list(APPEND OGDF_OPTIONS -DOGDF_ARCH:STRING=x86-64-v2)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS ${OGDF_OPTIONS}
)

vcpkg_cmake_install()

# OGDF natively installs its CMake configs to share/ogdf.
# Calling fixup with no arguments perfectly defaults to fixing share/ogdf.
vcpkg_cmake_config_fixup()

# Keep both generated headers where OGDF's exported target expects them.
# OGDF_DEBUG changes the layout of public types and must match the linked library.
if(NOT VCPKG_BUILD_TYPE STREQUAL "release")
    file(RENAME "${CURRENT_PACKAGES_DIR}/debug/include/ogdf-debug"
        "${CURRENT_PACKAGES_DIR}/include/ogdf-debug")
endif()

# Clean up debug includes, debug share, and leftover docs
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/lib/minisat/doc"
    "${CURRENT_PACKAGES_DIR}/include/ogdf/lib/minisat/doc"
)

# Modern vcpkg standard for installing the license file
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
