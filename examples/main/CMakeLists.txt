set(TARGET main)

if(MR_OGL_PLAYER)
    find_package(glfw3 REQUIRED)
    find_package(OpenGL REQUIRED)
    set(OGL_FILES glad/src/gl.c glad/src/egl.c)
    set(OGL_LIBS OpenGL::GL glfw)
    add_compile_definitions(MR_USE_OGL_PLAYER)
endif()

if(MR_OAL_PLAYER)
    link_directories(openal)
    set(OAL_LIB OpenAL32)
    set(OAL_INC openal/include)
    add_compile_definitions(MR_USE_OAL_PLAYER)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/glad/include ${OAL_INC})
add_executable(${TARGET} main.cpp ${OGL_FILES})
target_link_libraries(${TARGET} PRIVATE media-reader h264 de265 aac ${OGL_LIBS} ${OAL_LIB})
target_compile_features(${TARGET} PUBLIC cxx_std_11)