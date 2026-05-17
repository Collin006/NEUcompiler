cmake_minimum_required(VERSION 4.0)
project(NEUcompiler)

set(CMAKE_CXX_STANDARD 20)

add_executable(NEUcompiler main.cpp
        lexicalAnalyze.cpp
        global.h
        global.cpp
        code_builders.cpp
        code_builders.h
        four_tuple.h
        synbl.cpp
        synbl.h)
