cmake_minimum_required(VERSION 3.21)
project(svg_renderer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Sources, grouped by component ----------------------------------------

set(APP_SOURCES
    src/app/main.cpp
    src/app/cli_options.cpp
)

set(RENDERER_SOURCES
    src/renderer/renderer_init.cpp
    src/renderer/renderer_swapchain.cpp
    src/renderer/renderer_pipeline.cpp
    src/renderer/renderer_resources.cpp
    src/renderer/renderer_draw.cpp
)

set(SVG_SOURCES
    src/svg/xml_mini.cpp
    src/svg/svg_color.cpp
    src/svg/svg_style.cpp
    src/svg/svg_transform.cpp
    src/svg/svg_path.cpp
    src/svg/svg_text.cpp
    src/svg/svg_document.cpp
)

set(TESSELLATOR_SOURCES
    src/tessellator/earclip.cpp
    src/tessellator/path_flatten.cpp
    src/tessellator/paint_eval.cpp
    src/tessellator/stroke.cpp
    src/tessellator/fill.cpp
    src/tessellator/tessellator.cpp
)

set(FONT_SOURCES
    src/font/font.cpp
)

find_package(Vulkan REQUIRED)

function(apply_common_options TARGET)
    if(MSVC)
        target_compile_options(${TARGET} PRIVATE
            /W3
            /WX-
            /EHsc
            /MP
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:RelWithDebInfo>:/O2>
        )
        target_compile_definitions(${TARGET} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
    endif()

    target_compile_definitions(${TARGET} PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
    )
endfunction()

add_executable(vulkan_svg WIN32
    ${APP_SOURCES}
    ${RENDERER_SOURCES}
    ${SVG_SOURCES}
    ${TESSELLATOR_SOURCES}
    ${FONT_SOURCES}
)

# Every component directory is on the include path so headers can be
# referenced by filename alone (e.g. #include "renderer.h") regardless of
# which component is including them.
target_include_directories(vulkan_svg PRIVATE
    src/app
    src/renderer
    src/svg
    src/tessellator
    src/font
    third_party
)

if(WIN32)
    target_link_libraries(vulkan_svg PRIVATE
        Vulkan::Vulkan
        user32
        gdi32
    )
else()
    find_package(X11 REQUIRED)

    target_link_libraries(vulkan_svg PRIVATE
        Vulkan::Vulkan
        X11::X11
    )
endif()

apply_common_options(vulkan_svg)


find_program(GLSLC_EXE glslc REQUIRED HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")

set(SHADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag"
)

set(SPV_FILES "")

foreach(SHADER IN LISTS SHADERS)
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV_FILE "${CMAKE_CURRENT_BINARY_DIR}/${SHADER_NAME}.spv")

    add_custom_command(
        OUTPUT "${SPV_FILE}"
        COMMAND ${GLSLC_EXE} "${SHADER}" -o "${SPV_FILE}"
        DEPENDS "${SHADER}"
        COMMENT "Compiling ${SHADER_NAME} to SPIR-V"
    )
    list(APPEND SPV_FILES "${SPV_FILE}")
endforeach()

add_custom_target(compile_shaders DEPENDS ${SPV_FILES})

add_dependencies(vulkan_svg compile_shaders)

add_custom_command(
    TARGET vulkan_svg POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SPV_FILES} "$<TARGET_FILE_DIR:vulkan_svg>"
    COMMENT "Copying compiled shaders to executable directory"
)
