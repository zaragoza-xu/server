cmake_minimum_required(VERSION 4.2)
project(MYSVR)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER "clang++-20")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SRC_LIST "src/main.cpp" "src/server.cpp" "src/channel.cpp")
add_executable(mysvr "${SRC_LIST}")
target_include_directories(mysvr PUBLIC "${PROJECT_SOURCE_DIR}/include")
