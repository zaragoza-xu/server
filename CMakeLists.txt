cmake_minimum_required(VERSION 4.2)
project(server)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_COMPILER "clang++-20")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB SRC_LIST "src/*.cpp")

find_package(asio CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(server "${SRC_LIST}")
target_include_directories(server PUBLIC "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/third_party")
target_link_libraries(server PRIVATE asio::asio nlohmann_json::nlohmann_json)
