# Resource management

# This function should be called AFTER qt_standard_project_setup()
function(add_silicon_resources target)
    file(GLOB image_resources CONFIGURE_DEPENDS "${resources_dir}/*.jpg" "${resources_dir}/*.png")
    file(GLOB font_resources CONFIGURE_DEPENDS "${resources_dir}/fonts/*.ttf" "${resources_dir}/fonts/*.otf")
    file(GLOB licenses_file CONFIGURE_DEPENDS "${resources_dir}/licenses/*.txt")

    file(GLOB app_icons CONFIGURE_DEPENDS "${resources_dir}/*.svg")
    file(GLOB ui_icons CONFIGURE_DEPENDS "${resources_dir}/icons/*.svg")
    file(GLOB gate_icons CONFIGURE_DEPENDS "${resources_dir}/gates/*.svg")
    file(GLOB component_icons CONFIGURE_DEPENDS "${resources_dir}/other_components/*.svg")

    set(all_icons ${ui_icons} ${gate_icons} ${component_icons})

    # Add resources to the target
    qt_add_resources(${target} "images" BASE ${resources_dir}
            FILES ${image_resources})

    qt_add_resources(${target} "fonts" BASE ${resources_dir}
            FILES ${font_resources})

    qt_add_resources(${target} "icons" BASE ${resources_dir}
            FILES ${all_icons})

    qt_add_resources(${target} "licenses" BASE ${resources_dir}
            FILES ${licenses_file})

    qt_add_resources(${target} "theme" BASE ${resources_dir}
            FILES ${resources_dir}/theme.qss)
endfunction()