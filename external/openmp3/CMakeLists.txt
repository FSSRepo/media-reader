set(TARGET openmp3)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(${TARGET}
    huffman.cpp
    requantize.cpp
    stereo.cpp
    synthesis.cpp
    tables.cpp)