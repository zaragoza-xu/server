cmake_minimum_required(VERSION 4.2)
project(server)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB SRC_LIST "src/*.cpp")

find_package(asio CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)


add_executable(server "${SRC_LIST}")
target_include_directories(server PUBLIC "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/third_party")
target_link_libraries(server PRIVATE asio::asio nlohmann_json::nlohmann_json)

enable_testing()

add_executable(unit_tests
	tests/unit_tests.cpp
	src/room.cpp
	src/server.cpp
	src/channel.cpp
)
target_include_directories(unit_tests PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(unit_tests PRIVATE asio::asio nlohmann_json::nlohmann_json GTest::gtest GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(unit_tests)
