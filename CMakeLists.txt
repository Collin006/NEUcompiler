cmake_minimum_required(VERSION 3.31)
project(NEUcompiler)

set(CMAKE_CXX_STANDARD 20)

# MSVC: 强制 UTF-8 编码，避免中文字符串编译错误
if(MSVC)
    add_compile_options(/utf-8)
endif()

set(NEUCOMPILER_CORE_SOURCES
        lexicalAnalyze.cpp
        parser.cpp
        active_mark.cpp
        code_builders.cpp
        global.cpp
        synbl.cpp
        youhua.CPP
        compiler_core.cpp)

add_library(NEUcompiler_core ${NEUCOMPILER_CORE_SOURCES})
target_include_directories(NEUcompiler_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(NEUcompiler main.cpp)
target_link_libraries(NEUcompiler PRIVATE NEUcompiler_core)

option(NEUCOMPILER_BUILD_GUI "Build NEUcompiler GUI (Qt Widgets)" ON)

if(NEUCOMPILER_BUILD_GUI)
    find_package(Qt6 COMPONENTS Widgets QUIET)
    if(Qt6Widgets_FOUND)
        add_executable(NEUcompilerGUI gui_main.cpp)
        target_link_libraries(NEUcompilerGUI PRIVATE NEUcompiler_core Qt6::Widgets)
    else()
        find_package(Qt5 COMPONENTS Widgets QUIET)
        if(Qt5Widgets_FOUND)
            add_executable(NEUcompilerGUI gui_main.cpp)
            target_link_libraries(NEUcompilerGUI PRIVATE NEUcompiler_core Qt5::Widgets)
        else()
            message(STATUS "Qt Widgets not found. NEUcompilerGUI will not be built.")
        endif()
    endif()
endif()
