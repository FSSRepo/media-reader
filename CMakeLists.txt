cmake_minimum_required(VERSION 3.12)
project("media-reader")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT XCODE AND NOT MSVC AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(MR_STANDALONE ON)
else()
    set(MR_STANDALONE OFF)
endif()

#
# Option list
#

# general
option(MR_BUILD_EXAMPLES             "mr: build examples" ${MR_STANDALONE})
option(MR_BUILD_SHARED_LIBS          "mr: build shared libs" OFF)

set(MR_LIB media-reader)

include_directories(include)

file(GLOB MR_LIB_SOURCES
    "include/*.h"
    "src/*.cpp"
)

# we can get only one share lib
if(MR_BUILD_SHARED_LIBS)
    message("Build shared library")
    message(${MR_LIB_SOURCES})
    set(BUILD_SHARED_LIBS OFF)
    add_library(${MR_LIB} SHARED ${MR_LIB_SOURCES})
    add_definitions(-DMR_BUILD_SHARED_LIB)
    target_compile_definitions(${MR_LIB} PRIVATE -DMR_BUILD_DLL)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
else()
    message("Build static library")
    set(BUILD_SHARED_LIBS OFF)
    add_library(${MR_LIB} STATIC ${MR_LIB_SOURCES})
endif()

add_subdirectory(external/ffmpeg)

target_link_libraries(${MR_LIB} PUBLIC ffmpeg-codec)
target_include_directories(${MR_LIB} PUBLIC . external/ffmpeg)
target_compile_features(${MR_LIB} PUBLIC cxx_std_11)

if (MR_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

