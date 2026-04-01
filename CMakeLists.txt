cmake_minimum_required(VERSION 3.21)
project(server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(FetchContent)

option(SERVER_FETCH_DEPS "Automatically download missing third-party dependencies" ON)
option(SERVER_BUILD_TESTS "Build unit tests target" ${BUILD_TESTING})

file(GLOB SRC_LIST CONFIGURE_DEPENDS "src/*.cpp")

find_package(Threads REQUIRED)

find_package(asio CONFIG QUIET)
if(NOT asio_FOUND)
	if(NOT SERVER_FETCH_DEPS)
		message(FATAL_ERROR "asio not found. Re-run with -DSERVER_FETCH_DEPS=ON or install asio.")
	endif()

	message(STATUS "asio not found, fetching standalone asio from upstream...")
	FetchContent_Declare(
		asio_src
		URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.tar.gz
	)
	FetchContent_GetProperties(asio_src)
	if(NOT asio_src_POPULATED)
		FetchContent_Populate(asio_src)
	endif()

	add_library(asio INTERFACE)
	add_library(asio::asio ALIAS asio)
	target_include_directories(asio SYSTEM INTERFACE ${asio_src_SOURCE_DIR}/asio/include)
	target_compile_definitions(asio INTERFACE ASIO_STANDALONE)
endif()

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
	if(NOT SERVER_FETCH_DEPS)
		message(FATAL_ERROR "nlohmann_json not found. Re-run with -DSERVER_FETCH_DEPS=ON or install nlohmann_json.")
	endif()

	message(STATUS "nlohmann_json not found, fetching from upstream...")
	FetchContent_Declare(
		nlohmann_json
		URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
	)
	FetchContent_MakeAvailable(nlohmann_json)
endif()


add_executable(server "${SRC_LIST}")
target_include_directories(server PUBLIC "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/third_party")
target_link_libraries(server PRIVATE asio::asio nlohmann_json::nlohmann_json Threads::Threads)

if(SERVER_BUILD_TESTS)
	find_package(GTest CONFIG QUIET)
	if(NOT GTest_FOUND)
		if(NOT SERVER_FETCH_DEPS)
			message(FATAL_ERROR "GTest not found. Re-run with -DSERVER_FETCH_DEPS=ON or install GTest.")
		endif()

		message(STATUS "GTest not found, fetching from upstream...")
		FetchContent_Declare(
			googletest
			URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
		)
		set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
		FetchContent_MakeAvailable(googletest)
	endif()

	add_executable(unit_tests
		tests/unit_tests.cpp
		src/room.cpp
		src/server.cpp
		src/channel.cpp
	)
	target_include_directories(unit_tests PUBLIC "${PROJECT_SOURCE_DIR}/include")
	target_link_libraries(unit_tests PRIVATE
		asio::asio
		nlohmann_json::nlohmann_json
		GTest::gtest
		GTest::gtest_main
		Threads::Threads
	)

	include(GoogleTest)
	gtest_discover_tests(unit_tests)
endif()
