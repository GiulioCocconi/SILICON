vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO GiulioCocconi/SISL
    REF v0.1.1
    SHA512 d9eb894fa1ad460e0716f410babe129d7a8f62e23c9af860167bad3e4cd67a6559347b18624db42e3650d9e4ab068f96f5f260f76199be82e1a70a15002e2d57
    HEAD_REF main
)

if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    set(SISL_SHARED_LIBS ON)
else()
    set(SISL_SHARED_LIBS OFF)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DSISL_BUILD_TESTS=OFF
        -DSISL_FETCH_LEXY=OFF
        -DSISL_BUILD_SHARED_LIBS=${SISL_SHARED_LIBS}
        -DSISL_INSTALL=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME Sisl CONFIG_PATH lib/cmake/Sisl)
vcpkg_copy_pdbs()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
