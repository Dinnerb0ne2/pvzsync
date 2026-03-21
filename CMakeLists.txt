cmake_minimum_required(VERSION 3.18)
project(PVZSync LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()


if(MINGW)
    add_compile_options(-O2 -Wall -Wno-unused-variable -Wno-missing-field-initializers)
    add_link_options(-static -mwindows)
endif()

include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/imgui
    ${PROJECT_SOURCE_DIR}/imgui/backends
)

set(IMGUI_SOURCES
    ${PROJECT_SOURCE_DIR}/imgui/imgui.cpp
    ${PROJECT_SOURCE_DIR}/imgui/imgui_draw.cpp
    ${PROJECT_SOURCE_DIR}/imgui/imgui_tables.cpp
    ${PROJECT_SOURCE_DIR}/imgui/imgui_widgets.cpp
    ${PROJECT_SOURCE_DIR}/imgui/backends/imgui_impl_win32.cpp
    ${PROJECT_SOURCE_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)


set(SOURCES
    main.cpp
    ${PROJECT_SOURCE_DIR}/src/core.cpp
    ${PROJECT_SOURCE_DIR}/src/network.cpp
    ${PROJECT_SOURCE_DIR}/src/ui.cpp
    ${PROJECT_SOURCE_DIR}/src/remote.cpp
    ${IMGUI_SOURCES}
)

add_executable(pvz_tool ${SOURCES})

target_link_libraries(pvz_tool PRIVATE
    ws2_32
    gdi32
    opengl32
    user32
    kernel32
    shlwapi
    gdiplus
    psapi
    dwmapi
)

# 复制配置文件到build目录（如果存在）
if(EXISTS ${PROJECT_SOURCE_DIR}/config.ini)
    configure_file(
        ${PROJECT_SOURCE_DIR}/config.ini
        ${CMAKE_BINARY_DIR}/config.ini
        COPYONLY
    )
else()
    message(WARNING "config.ini not found in project source directory")
endif()