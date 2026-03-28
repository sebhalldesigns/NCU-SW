cmake_minimum_required(VERSION 3.20)

project(NCU)

enable_language(C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

add_subdirectory(m7)
add_subdirectory(m4)
add_subdirectory(extern)