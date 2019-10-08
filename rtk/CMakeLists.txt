cmake_minimum_required(VERSION 2.8)
project(echo_server)

set(CMAKE_CXX_STANDARD 17)

if ("${CMAKE_BUILD_TYPE}" STREQUAL Debug)
  add_compile_options("-DDEBUG_")
else()
  add_compile_options("-O3")
endif()

include_directories(${PROJECT_SOURCE_DIR})

add_executable(${PROJECT_NAME} main.cpp server.h jobs_pool.h)
target_link_libraries(${PROJECT_NAME} pthread)

add_executable(test_client test_client.cpp)
target_link_libraries(test_client pthread)
