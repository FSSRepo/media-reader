set(TARGET main)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${TARGET} main.cpp)
target_link_libraries(${TARGET} PRIVATE media-reader ffmpeg-codec)