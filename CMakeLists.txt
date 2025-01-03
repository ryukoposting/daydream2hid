cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(daydream2hid)


FILE(GLOB app_sources src/*.c)
target_include_directories(app PRIVATE src)
target_sources(app PRIVATE ${app_sources})
