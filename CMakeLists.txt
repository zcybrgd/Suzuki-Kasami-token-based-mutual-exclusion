cmake_minimum_required(VERSION 3.10)
project(ricart_agrawala)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

include_directories(include common)

add_executable(ricart_agrawala
    src/main.cpp
    src/process.cpp
    common/network.hpp
)
