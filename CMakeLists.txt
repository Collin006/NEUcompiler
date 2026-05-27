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
