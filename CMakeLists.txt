cmake_minimum_required(VERSION 3.16)

project(week5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(client client.cpp)
add_executable(server server.cpp)



