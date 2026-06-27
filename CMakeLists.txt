cmake_minimum_required(VERSION 3.16)
project(bronx CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the parent directory containing the stoatpp library
add_subdirectory(.. stoatpp_build)

add_executable(bronx main.cpp)

# Link against the stoatpp library
target_link_libraries(bronx PRIVATE stoatpp)
