cmake_minimum_required(VERSION 3.31)
project(NEUcompiler)

set(CMAKE_CXX_STANDARD 20)

# MSVC: 强制 UTF-8 编码，避免中文字符串编译错误
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_executable(NEUcompiler main.cpp
        lexicalAnalyze.cpp
        parser.cpp
        global.h
        global.cpp
        synbl.cpp
        synbl.h
        parser.h)
