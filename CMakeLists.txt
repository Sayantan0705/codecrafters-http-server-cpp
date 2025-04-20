cmake_minimum_required(VERSION 3.13)
project(http-server-starter-cpp)

set(CMAKE_CXX_STANDARD 20)

set(SOURCE_FILES src/server.cpp)

# Find zlib
find_package(ZLIB REQUIRED)

# Define the executable first
add_executable(server ${SOURCE_FILES})

# Then link libraries to it
target_link_libraries(server PRIVATE ZLIB::ZLIB pthread)
