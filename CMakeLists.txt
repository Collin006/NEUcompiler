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
        active_mark.cpp
        active_mark.h
        global.h
        global.cpp
        target_code.cpp
        target_code.h
        synbl.cpp
        synbl.h
        parser.h
        youhua.CPP
        youhua.h)
