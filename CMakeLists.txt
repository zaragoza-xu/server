cmake_minimum_required(VERSION 3.21)
project(server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

option(SERVER_BUILD_TESTS "Build unit tests target" OFF)

include(ProcessorCount)
ProcessorCount(N)

if(NOT N EQUAL 0)
	message(STATUS "Detected ${N} CPU cores for parallel build")
endif()

include(FetchContent)

option(SERVER_FETCH_DEPS "Automatically download missing third-party dependencies" ON)

file(GLOB SRC_LIST CONFIGURE_DEPENDS "src/*.cpp")
set(SERVER_CORE_SRC_LIST ${SRC_LIST})
list(FILTER SERVER_CORE_SRC_LIST EXCLUDE REGEX "src/main\\.cpp$")

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
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	)
	FetchContent_GetProperties(asio_src)

	if(NOT asio_src_POPULATED)
		FetchContent_MakeAvailable(asio_src)
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
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	)
	FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(server_core STATIC ${SERVER_CORE_SRC_LIST})
target_include_directories(server_core PUBLIC "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/third_party")
target_link_libraries(server_core PUBLIC asio::asio nlohmann_json::nlohmann_json Threads::Threads)

add_library(collision
	demos/collision.cpp
)
target_include_directories(collision PUBLIC "${PROJECT_SOURCE_DIR}/demos")

add_executable(server src/main.cpp)
target_link_libraries(server PRIVATE server_core)

add_executable(collision_demo
	demos/collision_demo.cpp
)
target_link_libraries(collision_demo PRIVATE collision)
target_precompile_headers(server_core PRIVATE
	<nlohmann/json.hpp>
	<asio.hpp>
	<algorithm>
	<memory>
	<mutex>
	<string>
	<unordered_map>
	<vector>
)
target_precompile_headers(server REUSE_FROM server_core)

if(SERVER_BUILD_TESTS)
	include(CTest)
	file(GLOB UNIT_TEST_FILES CONFIGURE_DEPENDS "tests/unit_tests/*_tests.cpp")

	find_package(GTest CONFIG QUIET)

	if(NOT GTest_FOUND)
		if(NOT SERVER_FETCH_DEPS)
			message(FATAL_ERROR "GTest not found. Re-run with -DSERVER_FETCH_DEPS=ON or install GTest.")
		endif()

		message(STATUS "GTest not found, fetching from upstream...")
		FetchContent_Declare(
			googletest
			URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
			DOWNLOAD_EXTRACT_TIMESTAMP TRUE
		)
		set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
		FetchContent_MakeAvailable(googletest)
	endif()

	include(GoogleTest)

	add_custom_target(unit_tests)

	foreach(TEST_FILE IN LISTS UNIT_TEST_FILES)
		get_filename_component(TEST_NAME "${TEST_FILE}" NAME_WE)

		add_executable("${TEST_NAME}" "${TEST_FILE}")
		target_include_directories("${TEST_NAME}" PUBLIC
			"${PROJECT_SOURCE_DIR}/include"
			"${PROJECT_SOURCE_DIR}/demos"
		)
		target_link_libraries("${TEST_NAME}" PRIVATE
			server_core
			collision
			GTest::gtest
			GTest::gtest_main
		)
		target_precompile_headers("${TEST_NAME}" REUSE_FROM server_core)

		add_dependencies(unit_tests "${TEST_NAME}")
		gtest_discover_tests("${TEST_NAME}" TEST_PREFIX "${TEST_NAME}::")
	endforeach()
endif()
