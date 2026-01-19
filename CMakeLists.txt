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

cmake_minimum_required(VERSION 3.25)

project("SILICON"
        VERSION 0.1.0 # As defined at semver.org
        LANGUAGES CXX)

include(GNUInstallDirs)
include(InstallRequiredSystemLibraries)
include(FetchContent)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(src_dir "${CMAKE_SOURCE_DIR}/src")
set(resources_dir "${CMAKE_SOURCE_DIR}/resources/app")

include_directories(${src_dir})

set(USING_NIX FALSE CACHE INTERNAL "Is the Nix package manager present?")

if (APPLE)
    message(WARNING "Building on Apple devices is untested, things might break")
elseif (UNIX)
    find_program(NIX_EXECUTABLE nix)

    if (NIX_EXECUTABLE)
        set(USING_NIX TRUE CACHE INTERNAL "Is the Nix package manager present?" FORCE)
    endif ()

    if (USING_NIX AND NOT DEFINED ENV{IN_NIX_SHELL})
        message(WARNING "Nix detected, but not running inside a nix-shell/nix develop environment.")
    endif ()
elseif (EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
elseif (MSVC)
    message(FATAL_ERROR "MSVC is not currently supported!")
endif ()

option(SILICON_USE_VCPKG "Bootstrap and use bundled vcpkg for dependencies (if using Nix or WASM, this option has no effect)" ON)

# Include our custom CMake modules
include(cmake/vcpkg.cmake)
include(cmake/windows.cmake)
include(cmake/resources.cmake)


set(COMMON_SOURCE_FILES
        ${src_dir}/core/wire.cpp
        ${src_dir}/core/gates.cpp
        ${src_dir}/core/component.cpp)

set(EXTRA_COMPONENTS_SOURCE_FILES
        ${src_dir}/extraComponents/arithmetic.cpp
        ${src_dir}/extraComponents/utils.cpp)

set(UI_SOURCE_FILES
        ${src_dir}/ui/common/componentSearchBox.cpp
        ${src_dir}/ui/common/diagramView.cpp
        ${src_dir}/ui/common/diagramScene.cpp
        ${src_dir}/ui/common/graphicalWire.cpp
        ${src_dir}/ui/common/graphicalComponent.cpp
        ${src_dir}/ui/common/icons.cpp
        ${src_dir}/ui/common/aboutDialog.cpp
        ${src_dir}/ui/logiFlow/components/graphicalLogicComponent.cpp
        ${src_dir}/ui/logiFlow/components/graphicalIO.cpp
        ${src_dir}/ui/logiFlow/components/graphicalGates.cpp
        ${src_dir}/ui/logiFlow/components/graphicalUtils.cpp
        ${src_dir}/ui/logiFlow/logiFlowWindow.cpp)

set(CMAKE_COLOR_DIAGNOSTICS ON)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SILICON_BUILD_TESTS "Build silicon tests" ON)

add_compile_definitions(
        SILICON_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        SILICON_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        SILICON_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        SILICON_VERSION="${PROJECT_VERSION}"
        SILICON_DEBUG=$<CONFIG:Debug>
)

# Export compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json")

if (CMAKE_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
            ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
endif ()

if (MSVC)
    add_compile_options(/W4)
elseif (UNIX)
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-return-type)
elseif (WIN32 AND MINGW)
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
elseif (EMSCRIPTEN)
    add_link_options(--emrun)
endif ()

if (UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    add_compile_options(-fsanitize=address -fsanitize=undefined)
    add_link_options(-fsanitize=address -fsanitize=undefined)
endif ()



find_package(Qt6 REQUIRED COMPONENTS Widgets SvgWidgets)
qt_standard_project_setup()

qt_add_executable(SILICON
        ${src_dir}/main.cpp)

target_sources(SILICON
        PRIVATE
        ${COMMON_SOURCE_FILES}
        ${EXTRA_COMPONENTS_SOURCE_FILES}
        ${UI_SOURCE_FILES})

target_compile_options(SILICON PRIVATE -Werror)

if (EMSCRIPTEN)
    target_link_libraries(SILICON PRIVATE Qt6::Widgets Qt6::SvgWidgets)
else ()
    add_subdirectory(libfst)
    target_link_libraries(SILICON PRIVATE Qt6::Widgets Qt6::SvgWidgets fst)
endif ()

if ((CMAKE_CXX_COMPILER_ID MATCHES "Clang") OR EMSCRIPTEN)
    # Some view/ranges features are not yet implemented in clang libc++
    # https://en.cppreference.com/w/cpp/compiler_support/23.html

    if (NOT USING_NIX)
        FetchContent_Declare(
                Range-v3
                GIT_REPOSITORY "https://github.com/ericniebler/range-v3"
        )

        # Ignore deprecation warnings
        # see https://github.com/ericniebler/range-v3/pull/1859
        add_compile_definitions(RANGES_DISABLE_DEPRECATED_WARNINGS)

        fetchcontent_makeavailable(Range-v3)
    else ()
        find_package(range-v3 REQUIRED)
    endif()

    target_link_libraries(SILICON PRIVATE range-v3)
endif ()

win_deploy_qt(SILICON)

# Add resources after Qt setup
add_silicon_resources(SILICON)

if (SILICON_BUILD_TESTS AND NOT EMSCRIPTEN)
    include(GoogleTest)
    find_package(GTest REQUIRED)

    if (WIN32)
        set(GTEST_LINKED_AS_SHARED_LIBRARY ON)
    endif ()

    add_subdirectory("${CMAKE_SOURCE_DIR}/tests")
    enable_testing()
endif ()

install(TARGETS SILICON
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
