#  Copyright (C) 2026 Giulio Cocconi

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


# Windows-specific configuration

# -- Deployment --

if (WIN32)
    if (NOT TARGET Qt6::qmake)
        find_package(Qt6 REQUIRED COMPONENTS Core)
    endif ()

    if (NOT SILICON_USE_VCPKG)
        # 1. Locate windeployqt.exe
        get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

        if (WINDEPLOYQT_EXECUTABLE)
            message(STATUS "Found windeployqt: ${WINDEPLOYQT_EXECUTABLE}")
        else ()
            message(FATAL_ERROR "windeployqt not found!")
        endif ()
    endif ()

    # 2. Copy other dependencies DLLs
    # TODO: Support for systems not using vcpkg (maybe?)

    if (SILICON_USE_VCPKG)
        if (VCPKG_BUILD_TYPE STREQUAL "debug")
            set(VCPKG_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin")
            set(_qt_plugin_dir_candidates
                    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins"
                    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/plugins")
        else ()
            set(VCPKG_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
            set(_qt_plugin_dir_candidates
                    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins"
                    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/plugins")
        endif ()

        set(OUTPUT_BIN_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

        if (NOT EXISTS "${VCPKG_BIN_DIR}")
            message(FATAL_ERROR "${VCPKG_BIN_DIR} does not exist, cannot proceed!")
        endif ()

        if (NOT EXISTS "${OUTPUT_BIN_DIR}")
            file(MAKE_DIRECTORY "${OUTPUT_BIN_DIR}")
        endif ()

        message(STATUS "Copying DLLs from vcpkg: ${VCPKG_BIN_DIR} -> ${OUTPUT_BIN_DIR}")
        file(GLOB VCPKG_DLLS "${VCPKG_BIN_DIR}/*.dll")
        file(COPY ${VCPKG_DLLS} DESTINATION "${OUTPUT_BIN_DIR}")

        if (MINGW)
            get_filename_component(_mingw_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
            file(GLOB MINGW_RUNTIME_DLLS
                    "${_mingw_bin_dir}/libgcc*.dll"
                    "${_mingw_bin_dir}/libstdc++*.dll"
                    "${_mingw_bin_dir}/libwinpthread*.dll")

            if (MINGW_RUNTIME_DLLS)
                message(STATUS "Copying MinGW runtime DLLs from: ${_mingw_bin_dir}")
                file(COPY ${MINGW_RUNTIME_DLLS} DESTINATION "${OUTPUT_BIN_DIR}")
            endif ()
        endif ()

        foreach (_qt_plugin_dir IN LISTS _qt_plugin_dir_candidates)
            if (EXISTS "${_qt_plugin_dir}/platforms")
                set(VCPKG_QT_PLUGIN_DIR "${_qt_plugin_dir}")
                break()
            endif ()
        endforeach ()

        if (NOT VCPKG_QT_PLUGIN_DIR)
            message(FATAL_ERROR "Could not find Qt platform plugins in vcpkg installation: ${_qt_plugin_dir_candidates}")
        endif ()

        message(STATUS "Copying Qt plugins from vcpkg: ${VCPKG_QT_PLUGIN_DIR} -> ${OUTPUT_BIN_DIR}")
    endif ()
endif ()

function(win_deploy_qt target)
    if (WIN32)
        if (SILICON_USE_VCPKG AND VCPKG_QT_PLUGIN_DIR)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    ${VCPKG_DLLS}
                    ${MINGW_RUNTIME_DLLS}
                    "$<TARGET_FILE_DIR:${target}>"
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${VCPKG_QT_PLUGIN_DIR}"
                    "$<TARGET_FILE_DIR:${target}>"
                COMMENT "Deploying Qt dependencies for ${target}..."
                COMMAND_EXPAND_LISTS
            )
        else ()
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    $<$<CONFIG:Debug>:--debug>
                    $<$<NOT:$<CONFIG:Debug>>:--release>
                    --verbose 1
                    --compiler-runtime
                    --dir "$<TARGET_FILE_DIR:${target}>"
                    "$<TARGET_FILE:${target}>"
                COMMENT "Deploying Qt dependencies for ${target}..."
                COMMAND_EXPAND_LISTS
            )
        endif ()
    endif ()
endfunction()
