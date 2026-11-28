# Copyright (c) 2026. Giulio Cocconi
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(SILICON_BUN_EXECUTABLE bun REQUIRED)

add_library(silicon_documentation STATIC EXCLUDE_FROM_ALL
        "${CMAKE_SOURCE_DIR}/tools/component_docs/reference.cpp")
target_link_libraries(silicon_documentation PUBLIC silicon_ui)
target_include_directories(silicon_documentation PUBLIC
        "${CMAKE_SOURCE_DIR}/tools/component_docs")
target_compile_options(silicon_documentation PRIVATE -Werror)

add_executable(silicon_component_docs EXCLUDE_FROM_ALL
        "${CMAKE_SOURCE_DIR}/tools/component_docs/main.cpp")
target_link_libraries(silicon_component_docs PRIVATE silicon_documentation)
target_compile_options(silicon_component_docs PRIVATE -Werror)
set_target_properties(silicon_component_docs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tools")

add_custom_target(component_docs
        COMMAND "$<TARGET_FILE:silicon_component_docs>" --output
                "${CMAKE_SOURCE_DIR}/webpage/userdocs/components"
        DEPENDS silicon_component_docs
        COMMENT "Generating component reference pages and HD PNG previews"
        VERBATIM)

add_custom_target(user_docs
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/ci/theme/generate.py"
        COMMAND "${SILICON_BUN_EXECUTABLE}" install --frozen-lockfile
        COMMAND "${SILICON_BUN_EXECUTABLE}" run build:docs:render
        DEPENDS component_docs
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/webpage"
        COMMENT "Building SILICON user documentation"
        USES_TERMINAL
        VERBATIM)
