set(TARGET main)

if(MR_OGL_PLAYER)
    
    # Intentar encontrar usando el nombre del paquete 'glfw3'
    # Esto buscará glfw3Config.cmake o glfw3-config.cmake
    find_package(OpenGL REQUIRED)
    find_package(glfw3 3.3 REQUIRED)
    
    set(OGL_FILES glad/src/gl.c glad/src/egl.c)
    # La biblioteca importada se llama 'glfw' (definida por glfw3Config.cmake)
    set(OGL_LIBS OpenGL::GL glfw)
    add_compile_definitions(MR_USE_OGL_PLAYER)
endif()

if(MR_AUDIO_PLAYER)
    add_compile_definitions(MR_USE_AUDIO_PLAYER)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/glad/include ${OGL_INC})
add_executable(${TARGET} main.cpp ${OGL_FILES})
target_link_libraries(${TARGET} PRIVATE media-reader h264 de265 aac ${OGL_LIBS})
target_compile_features(${TARGET} PUBLIC cxx_std_11)