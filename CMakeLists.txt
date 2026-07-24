cmake_minimum_required(VERSION 3.21)
project(svg_renderer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SHARED_SOURCES
    svg_parser.cpp
    tessellator.cpp
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
    main.cpp
    renderer.cpp
    ${SHARED_SOURCES}
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
    "${CMAKE_CURRENT_SOURCE_DIR}/shader.vert" 
    "${CMAKE_CURRENT_SOURCE_DIR}/shader.frag"
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