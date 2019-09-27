cmake_minimum_required(VERSION 2.8)
project(true_conf)

set(CMAKE_CXX_STANDARD 17)

if ("${CMAKE_BUILD_TYPE}" STREQUAL Debug)
  add_compile_options("-DDEBUG_")
else()
  add_compile_options("-O3")
endif()

include_directories(${PROJECT_SOURCE_DIR})

add_executable(${PROJECT_NAME} true_conf.cpp)
