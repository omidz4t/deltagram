cmake_minimum_required(VERSION 3.16)
project(delta_shell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_executable(delta_shell
    main.cpp
    delta_rpc.cpp
    delta_rpc.h
    shell.cpp
    shell.h
)

target_link_libraries(delta_shell PRIVATE Qt6::Core Qt6::Widgets)

if (NOT MSVC)
    target_compile_options(delta_shell PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
