#  Copyright (C) 2025 Giulio Cocconi

#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.

#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.

#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.


if (SILICON_USE_VCPKG AND NOT USING_NIX)
    message(STATUS "SILICON_USE_VCPKG is ON. Bootstrapping bundled vcpkg...")

    set(ENV{VCPKG_USE_SYSTEM_BINARIES} "1")

    # Get the correct commit hash from vcpkg.json
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json" VCPKG_JSON_CONTENT)

    # 2. Extract the "builtin-baseline" field
    string(JSON VCPKG_BASELINE_SHA GET "${VCPKG_JSON_CONTENT}" "builtin-baseline")

    # Download Vcpkg source
    FetchContent_Declare(
            vcpkg
            GIT_REPOSITORY https://github.com/microsoft/vcpkg.git
            GIT_TAG ${VCPKG_BASELINE_SHA}
    )
    FetchContent_MakeAvailable(vcpkg)

    # Determine script path
    if (WIN32)
        set(VCPKG_BOOTSTRAP_SCRIPT "${vcpkg_SOURCE_DIR}/bootstrap-vcpkg.bat")
        set(VCPKG_EXECUTABLE "${vcpkg_SOURCE_DIR}/vcpkg.exe")

        if (CMAKE_GENERATOR MATCHES "MinGW" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            set(VCPKG_TARGET_TRIPLET "x64-mingw-dynamic" CACHE STRING "" FORCE)
            set(VCPKG_HOST_TRIPLET "x64-mingw-dynamic" CACHE STRING "" FORCE)
        endif ()
    else ()
        set(VCPKG_BOOTSTRAP_SCRIPT "${vcpkg_SOURCE_DIR}/bootstrap-vcpkg.sh")
        set(VCPKG_EXECUTABLE "${vcpkg_SOURCE_DIR}/vcpkg")
    endif ()



    # TODO: Differenciate between build-types in order to download release or debug dependencies but not both
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(VCPKG_BUILD_TYPE "debug")
    else ()
        set(VCPKG_BUILD_TYPE "release")
    endif ()

    # Run Bootstrap if executable is missing
    if (NOT EXISTS "${VCPKG_EXECUTABLE}")
        message(STATUS "Running vcpkg bootstrap script...")
        execute_process(
                COMMAND "${VCPKG_BOOTSTRAP_SCRIPT}" -disableMetrics
                WORKING_DIRECTORY "${vcpkg_SOURCE_DIR}"
                RESULT_VARIABLE VCPKG_BOOTSTRAP_RESULT
        )
        if (NOT VCPKG_BOOTSTRAP_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to bootstrap vcpkg.")
        endif ()
    endif ()

    if (EMSCRIPTEN)
        set(VCPKG_TARGET_TRIPLET "wasm32-emscripten" CACHE STRING "" FORCE)

        # Locate the Emscripten toolchain for vcpkg to chainload
        if (DEFINED ENV{EMSDK})
            set(_EMSCRIPTEN_TOOLCHAIN "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
        else ()
            get_filename_component(_EMCC_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
            set(_EMSCRIPTEN_TOOLCHAIN "${_EMCC_DIR}/cmake/Modules/Platform/Emscripten.cmake")
        endif ()

        if (NOT EXISTS "${_EMSCRIPTEN_TOOLCHAIN}")
            message(FATAL_ERROR "Cannot find Emscripten CMake toolchain at: ${_EMSCRIPTEN_TOOLCHAIN}")
        endif ()

        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${_EMSCRIPTEN_TOOLCHAIN}" CACHE FILEPATH "" FORCE)

        # boost-log is marked unsupported for emscripten (because of threading), ignore the mark
        list(FIND VCPKG_INSTALL_OPTIONS "--allow-unsupported" _SILICON_ALLOW_UNSUPPORTED_INDEX)
        if (_SILICON_ALLOW_UNSUPPORTED_INDEX EQUAL -1)
            list(APPEND VCPKG_INSTALL_OPTIONS "--allow-unsupported")
        endif ()

        set(VCPKG_INSTALL_OPTIONS "${VCPKG_INSTALL_OPTIONS}" CACHE STRING "Additional options passed to vcpkg install" FORCE)

        message(STATUS "VCPKG_INSTALL_OPTIONS=${VCPKG_INSTALL_OPTIONS}")

        unset(_SILICON_ALLOW_UNSUPPORTED_INDEX)
    endif ()

    # Enable Manifest Mode (uses vcpkg.json)
    set(VCPKG_MANIFEST_INSTALL ON)
    set(VCPKG_MANIFEST_DIR "${CMAKE_SOURCE_DIR}")

    # Inject toolchain so find_package works
    include("${vcpkg_SOURCE_DIR}/scripts/buildsystems/vcpkg.cmake")
    set(X_VCPKG_APPLOCAL_COPY_DLLS ON CACHE INTERNAL "")
endif ()
