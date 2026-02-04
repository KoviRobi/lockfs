cmake_minimum_required(VERSION 3.21)

set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug/Release/MinSizeRel")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "compile_commands.json")

project(lockfs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test test/main.cpp)
target_include_directories(test PRIVATE .)

add_library(python SHARED test/python.cpp)
target_include_directories(python PRIVATE .)
