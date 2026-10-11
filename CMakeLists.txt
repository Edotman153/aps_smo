cmake_minimum_required(VERSION 3.16)
project(smo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_executable(smo main.cpp)
target_link_libraries(smo PRIVATE Threads::Threads)
if(MSVC)
  target_compile_options(smo PRIVATE /W4)
else()
  target_compile_options(smo PRIVATE -Wall -Wextra -Wpedantic)
endif()
