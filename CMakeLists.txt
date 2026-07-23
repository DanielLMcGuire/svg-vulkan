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
    set(VULKAN_LIB_DIR "$ENV{VULKAN_SDK}/Lib")
    set(VULKAN_BIN_DIR "$ENV{VULKAN_SDK}/Bin")

    add_library(shaderc_shared SHARED IMPORTED)
    set_target_properties(shaderc_shared PROPERTIES
        IMPORTED_IMPLIB   "${VULKAN_LIB_DIR}/shaderc_shared.lib"
        IMPORTED_LOCATION "${VULKAN_BIN_DIR}/shaderc_shared.dll"
        INTERFACE_INCLUDE_DIRECTORIES "$ENV{VULKAN_SDK}/Include"
    )

    target_link_libraries(vulkan_svg PRIVATE
        Vulkan::Vulkan
        shaderc_shared
        user32
        gdi32
    )

    add_custom_command(TARGET vulkan_svg POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VULKAN_BIN_DIR}/shaderc_shared.dll"
            "$<TARGET_FILE_DIR:vulkan_svg>"
        COMMENT "Copying shaderc_shared.dll to output directory"
    )

    target_compile_definitions(vulkan_svg PRIVATE SHADERC_SHAREDLIB)
else()
    set(VULKAN_LIB_DIR "$ENV{VULKAN_SDK}/lib")
    set(VULKAN_BIN_DIR "$ENV{VULKAN_SDK}/bin")

    add_library(shaderc_shared SHARED IMPORTED)
    set_target_properties(shaderc_shared PROPERTIES
        IMPORTED_LOCATION "${VULKAN_LIB_DIR}/libshaderc_shared.so"
        INTERFACE_INCLUDE_DIRECTORIES "$ENV{VULKAN_SDK}/include"
    )

    find_package(X11 REQUIRED)

    target_link_libraries(vulkan_svg PRIVATE
        Vulkan::Vulkan
        shaderc_shared
        X11::X11
    )

    add_custom_command(TARGET vulkan_svg POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VULKAN_LIB_DIR}/libshaderc_shared.so"
            "$<TARGET_FILE_DIR:vulkan_svg>"
        COMMENT "Copying libshaderc_shared.so to output directory"
    )

    target_compile_definitions(vulkan_svg PRIVATE SHADERC_SHAREDLIB)
endif()

apply_common_options(vulkan_svg)