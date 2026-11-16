cmake_minimum_required(VERSION 3.22.1)
project(tinybox CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(tinybox SHARED native-lib.cpp engine.cpp)
target_compile_options(tinybox PRIVATE -O3 -ffast-math -Wall)
target_link_libraries(tinybox aaudio log)
