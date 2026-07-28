#  Copyright (c) 2026. Giulio Cocconi

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

cmake_minimum_required(VERSION 3.31)

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
set(yosys_resources_dir "${CMAKE_SOURCE_DIR}/resources/yosys")

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

option(SILICON_USE_VCPKG "Bootstrap and use bundled vcpkg for dependencies (if using Nix, this option has no effect)" ON)

# Include our custom CMake modules
include(cmake/vcpkg.cmake)
include(cmake/windows.cmake)
include(cmake/resources.cmake)


set(COMMON_SOURCE_FILES
        ${src_dir}/utils/num_formatting.cpp
        ${src_dir}/core/wire.cpp
        ${src_dir}/core/io.cpp
        ${src_dir}/core/gates.cpp
        ${src_dir}/core/flipflops.cpp
        ${src_dir}/core/register.cpp
        ${src_dir}/core/subcircuit.cpp
        ${src_dir}/core/subcircuitDefinition.cpp
        ${src_dir}/core/elaboration.cpp
        ${src_dir}/core/simulationSession.cpp
        ${src_dir}/core/component.cpp
        ${src_dir}/core/circuit.cpp
        ${src_dir}/core/projectDocument.cpp
        ${src_dir}/core/simulator.cpp
        ${src_dir}/core/serialization/component_registry.cpp
        ${src_dir}/core/serialization/component_registration.cpp
        ${src_dir}/core/serialization/yosys.cpp
        ${src_dir}/core/serialization/yosys_deserialization.cpp
        ${src_dir}/core/serialization/yosys_components.cpp
        ${src_dir}/core/serialization/yosys_tool.cpp
        ${src_dir}/core/projectDependencyGraph.cpp
        ${src_dir}/core/fstTraceWriter.cpp
        ${src_dir}/core/siliconWaveform.cpp
        ${src_dir}/core/siliconFst.cpp
        ${src_dir}/core/serialization/projectFile.cpp
        ${src_dir}/core/fstWrapper.cpp)

set(EXTRA_COMPONENTS_SOURCE_FILES
        ${src_dir}/extraComponents/arithmetic.cpp
        ${src_dir}/extraComponents/multiplexer.cpp
        ${src_dir}/extraComponents/utils.cpp)

set(UI_SOURCE_FILES
        ${src_dir}/ui/common/graphicalLogStream.cpp
        ${src_dir}/ui/common/logSideView.cpp
        ${src_dir}/ui/common/theme.cpp
        ${src_dir}/ui/common/fileDialogUtils.cpp
        ${src_dir}/ui/common/inputDialogUtils.cpp
        ${src_dir}/ui/common/componentSearchMatcher.cpp
        ${src_dir}/ui/common/componentSearchBox.cpp
        ${src_dir}/ui/common/diagramView.cpp
        ${src_dir}/ui/common/diagramScene/diagramScene.cpp
        ${src_dir}/ui/common/diagramScene/diagramSceneSerializer.cpp
        ${src_dir}/ui/common/diagramScene/diagramSceneSimulationController.cpp
        ${src_dir}/ui/common/graphicalWire.cpp
        ${src_dir}/ui/common/wireManager.cpp
        ${src_dir}/ui/common/wireRouting.cpp
        ${src_dir}/ui/common/waveformViewer.cpp
        ${src_dir}/ui/common/graphicalComponent.cpp
        ${src_dir}/ui/common/graphicalItem.cpp
        ${src_dir}/ui/common/undoCommands.cpp
        ${src_dir}/ui/common/icons.cpp
        ${src_dir}/ui/common/aboutDialog.cpp
        ${src_dir}/ui/common/siliconSettings.cpp
        ${src_dir}/ui/common/settingsWindow.cpp
        ${src_dir}/ui/logiFlow/components/graphicalLogicComponent.cpp
        ${src_dir}/ui/logiFlow/components/graphicalIO.cpp
        ${src_dir}/ui/logiFlow/components/graphicalGates.cpp
        ${src_dir}/ui/logiFlow/components/graphicalFlipFlops.cpp
        ${src_dir}/ui/logiFlow/components/graphicalRegister.cpp
        ${src_dir}/ui/logiFlow/components/subcircuit/componentShapeEditor.cpp
        ${src_dir}/ui/logiFlow/components/subcircuit/graphicalSubcircuit.cpp
        ${src_dir}/ui/logiFlow/components/subcircuit/metadata.cpp
        ${src_dir}/ui/logiFlow/components/subcircuit/utils.cpp
        ${src_dir}/ui/logiFlow/components/graphicalArithmetic.cpp
        ${src_dir}/ui/logiFlow/components/graphicalMultiplexer.cpp
        ${src_dir}/ui/logiFlow/components/graphicalUtils.cpp
        ${src_dir}/ui/logiFlow/componentCatalogOverlay.cpp
        ${src_dir}/ui/logiFlow/hdlCodeEditor.cpp
        ${src_dir}/ui/logiFlow/metadataDescriptionEdit.cpp
        ${src_dir}/ui/logiFlow/projectTree.cpp
        ${src_dir}/ui/logiFlow/logiFlowWindowActions.cpp
        ${src_dir}/ui/logiFlow/logiFlowWindowFile.cpp
        ${src_dir}/ui/logiFlow/logiFlowWindowProject.cpp
        ${src_dir}/ui/logiFlow/logiFlowWindowProperties.cpp
        ${src_dir}/ui/logiFlow/logiFlowWindow.cpp
        ${src_dir}/ui/serialization/gui_component_factory.cpp
        ${src_dir}/ui/serialization/gui_component_registration.cpp)

set(CMAKE_COLOR_DIAGNOSTICS ON)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(SILICON_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" ON)
option(SILICON_BUILD_TESTS "Build silicon tests" ON)
option(SILICON_BUILD_BENCHMARKS "Build standalone silicon performance benchmarks" ON)

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

if (UNIX AND NOT APPLE AND NOT EMSCRIPTEN AND SILICON_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address -fsanitize=undefined)
    add_link_options(-fsanitize=address -fsanitize=undefined)
endif ()


set(SILICON_BOOST_COMPONENTS graph log log_setup thread)
if (NOT EMSCRIPTEN)
    list(APPEND SILICON_BOOST_COMPONENTS process)
endif ()
find_package(Boost REQUIRED COMPONENTS ${SILICON_BOOST_COMPONENTS})
find_package(nlohmann_json REQUIRED)
find_package(tomlplusplus REQUIRED)
find_package(libzip CONFIG REQUIRED)

if (TARGET libzip::zip)
    set(SILICON_LIBZIP_TARGET libzip::zip)
elseif (TARGET zip)
    set(SILICON_LIBZIP_TARGET zip)
else ()
    message(FATAL_ERROR "libzip was found, but no supported CMake target is available")
endif ()

add_subdirectory(vendor/libfst)
add_subdirectory(vendor/libavoid)

add_library(silicon_logging STATIC
        ${src_dir}/logging/logger.cpp)

target_compile_options(silicon_logging PRIVATE -Werror)

target_link_libraries(silicon_logging
        PUBLIC
        Boost::log
        Boost::log_setup
        Boost::thread)

add_library(silicon_core STATIC
        ${COMMON_SOURCE_FILES}
        ${EXTRA_COMPONENTS_SOURCE_FILES})

target_compile_options(silicon_core PRIVATE -Werror)

if (NOT EMSCRIPTEN)
    find_program(SILICON_YOSYS_CONFIG_EXECUTABLE yosys-config)
    if (SILICON_YOSYS_CONFIG_EXECUTABLE)
        set(SILICON_YOSYS_PLUGIN_OUTPUT
                "${CMAKE_CURRENT_BINARY_DIR}/yosys/silicon_bmux_case${CMAKE_SHARED_LIBRARY_SUFFIX}")
        add_custom_command(
                OUTPUT "${SILICON_YOSYS_PLUGIN_OUTPUT}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory
                        "${CMAKE_CURRENT_BINARY_DIR}/yosys"
                COMMAND "${SILICON_YOSYS_CONFIG_EXECUTABLE}" --build
                        "${SILICON_YOSYS_PLUGIN_OUTPUT}"
                        "${CMAKE_SOURCE_DIR}/tools/yosys/silicon_bmux_case.cpp"
                DEPENDS "${CMAKE_SOURCE_DIR}/tools/yosys/silicon_bmux_case.cpp"
                VERBATIM)
        add_custom_target(silicon_yosys_plugin ALL
                DEPENDS "${SILICON_YOSYS_PLUGIN_OUTPUT}")
        add_dependencies(silicon_core silicon_yosys_plugin)
    else ()
        message(WARNING
                "yosys-config was not found; the SILICON mux-case plugin will not be built")
    endif ()
endif ()

file(TO_CMAKE_PATH "${yosys_resources_dir}" SILICON_YOSYS_BUILD_RESOURCE_DIR)
file(TO_CMAKE_PATH "${CMAKE_INSTALL_FULL_DATADIR}/silicon/yosys"
        SILICON_YOSYS_INSTALL_RESOURCE_DIR)
file(TO_CMAKE_PATH "${CMAKE_INSTALL_DATADIR}/silicon/yosys"
        SILICON_YOSYS_INSTALL_RELATIVE_RESOURCE_DIR)
target_compile_definitions(silicon_core PRIVATE
        SILICON_YOSYS_BUILD_RESOURCE_DIR="${SILICON_YOSYS_BUILD_RESOURCE_DIR}"
        SILICON_YOSYS_INSTALL_RESOURCE_DIR="${SILICON_YOSYS_INSTALL_RESOURCE_DIR}"
        SILICON_YOSYS_INSTALL_RELATIVE_RESOURCE_DIR="${SILICON_YOSYS_INSTALL_RELATIVE_RESOURCE_DIR}")

if (NOT EMSCRIPTEN AND SILICON_YOSYS_CONFIG_EXECUTABLE)
    file(TO_CMAKE_PATH "${SILICON_YOSYS_PLUGIN_OUTPUT}"
            SILICON_YOSYS_BUILD_PLUGIN)
    file(TO_CMAKE_PATH
            "${CMAKE_INSTALL_FULL_LIBDIR}/silicon/yosys/silicon_bmux_case${CMAKE_SHARED_LIBRARY_SUFFIX}"
            SILICON_YOSYS_INSTALL_PLUGIN)
    file(TO_CMAKE_PATH
            "${CMAKE_INSTALL_LIBDIR}/silicon/yosys/silicon_bmux_case${CMAKE_SHARED_LIBRARY_SUFFIX}"
            SILICON_YOSYS_INSTALL_RELATIVE_PLUGIN)
    target_compile_definitions(silicon_core PRIVATE
            SILICON_YOSYS_BUILD_PLUGIN="${SILICON_YOSYS_BUILD_PLUGIN}"
            SILICON_YOSYS_INSTALL_PLUGIN="${SILICON_YOSYS_INSTALL_PLUGIN}"
            SILICON_YOSYS_INSTALL_RELATIVE_PLUGIN="${SILICON_YOSYS_INSTALL_RELATIVE_PLUGIN}")
endif ()

target_link_libraries(silicon_core
        PUBLIC
        fst
        Boost::graph
        silicon_logging
        nlohmann_json
        ${SILICON_LIBZIP_TARGET})

if (NOT EMSCRIPTEN)
    target_link_libraries(silicon_core PRIVATE Boost::process)
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

    target_link_libraries(silicon_core PUBLIC range-v3)
endif ()

find_package(Qt6 REQUIRED COMPONENTS Widgets SvgWidgets)
if (UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    find_package(Fontconfig QUIET)
endif ()
qt_standard_project_setup()

qt_add_executable(SILICON
        ${src_dir}/main.cpp)

target_sources(SILICON
        PRIVATE
        ${UI_SOURCE_FILES})

target_compile_options(SILICON PRIVATE -Werror)

target_link_libraries(SILICON PRIVATE
        Qt6::Widgets
        Qt6::SvgWidgets
        avoid
        silicon_core)

if (TARGET Fontconfig::Fontconfig)
    target_link_libraries(SILICON PRIVATE Fontconfig::Fontconfig)
    target_compile_definitions(SILICON PRIVATE SILICON_HAS_FONTCONFIG=1)
endif ()

set_target_properties(SILICON PROPERTIES
        # Spawn a console windows (on Windows) only when in debug mode
        WIN32_EXECUTABLE $<CONFIG:Release>)

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

if (SILICON_BUILD_BENCHMARKS AND NOT EMSCRIPTEN)
    add_subdirectory("${CMAKE_SOURCE_DIR}/benchmarks")
endif ()

if (NOT EMSCRIPTEN)
    find_program(SILICON_YOSYS_EXECUTABLE yosys)
    if (SILICON_YOSYS_EXECUTABLE AND EXISTS "${CMAKE_SOURCE_DIR}/examples/yosys_roundtrip.cpp")
        add_executable(yosys_roundtrip examples/yosys_roundtrip.cpp)
        target_link_libraries(yosys_roundtrip PRIVATE silicon_core)
        target_compile_definitions(yosys_roundtrip PRIVATE
                SILICON_YOSYS_EXECUTABLE="${SILICON_YOSYS_EXECUTABLE}"
                SILICON_YOSYS_RESOURCE_DIR="${SILICON_YOSYS_BUILD_RESOURCE_DIR}")
        set_target_properties(yosys_roundtrip PROPERTIES
                AUTOMOC OFF
                AUTOUIC OFF
                AUTORCC OFF)
    endif ()
endif ()

install(TARGETS SILICON
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY "${yosys_resources_dir}/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/silicon/yosys")

if (NOT EMSCRIPTEN AND SILICON_YOSYS_CONFIG_EXECUTABLE)
    install(FILES "${SILICON_YOSYS_PLUGIN_OUTPUT}"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/silicon/yosys")
endif ()
