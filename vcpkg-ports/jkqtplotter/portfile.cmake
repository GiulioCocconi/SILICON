# SILICON overlay: use JKQtPlotter 5.0's modern CMake target names, but build
# only the JKQTMathText equation renderer and its required JKQTCommon library.
# The upstream vcpkg port currently packages an older, incompatible revision.

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/jkriege2/JKQtPlotter.git
    REF b8cc2909f2498f7bb3ee1830151b3dc71bcf6375
    FETCH_REF v5.0.0-beta2
)

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" JKQT_BUILD_SHARED)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DQT_VERSION_MAJOR=6
        -DBUILD_SHARED_LIBS=${JKQT_BUILD_SHARED}
        -DJKQtPlotter_BUILD_LIB_JKQTPLOTTER=OFF
        -DJKQtPlotter_BUILD_LIB_JKQTFASTPLOTTER=OFF
        -DJKQtPlotter_BUILD_LIB_JKQTMATH=OFF
        -DJKQtPlotter_BUILD_LIB_JKQTMATHTEXT=ON
        -DJKQtPlotter_BUILD_EXAMPLES=OFF
        -DJKQtPlotter_BUILD_TOOLS=OFF
        -DJKQtPlotter_BUILD_TESTS=OFF
        -DJKQtPlotter_BUILD_WITH_PRECOMPILED_HEADERS=OFF
        -DJKQtPlotter_BUILD_DECORATE_LIBNAMES_WITH_BUILDTYPE=OFF
        -DCMAKE_DISABLE_FIND_PACKAGE_OpenCV=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME JKQTMathText6 CONFIG_PATH lib/cmake/JKQTPlotter6)
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
