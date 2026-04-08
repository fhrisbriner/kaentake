cmake_minimum_required(VERSION 3.14)
project(MapleNight)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "D:/SkirchFolder")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "D:/SkirchFolder")
foreach(CFG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} "D:/SkirchFolder")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG} "D:/SkirchFolder")
endforeach()

# Build external dependencies
add_subdirectory(external)

# Build injector and laucher
add_subdirectory(src)