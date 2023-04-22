cmake_minimum_required(VERSION 3.25)
project(testmgba)

set(CMAKE_CXX_STANDARD 20)

set(LIBMGBA_ONLY ON)
add_subdirectory(3rd_party/mgba-0.10.1)

add_executable(testmgba main.cpp)
target_link_libraries(testmgba mgba)
target_include_directories(testmgba PRIVATE 3rd_party/sokol)
