cmake_minimum_required(VERSION 3.31)
project(NEUcompiler)

set(CMAKE_CXX_STANDARD 20)

add_executable(NEUcompiler main.cpp
        lexicalAnalyze.cpp
        global.h
        global.cpp
        synbl.cpp
        synbl.h)
