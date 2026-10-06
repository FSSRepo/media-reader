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

if(MR_OAL_PLAYER)
    set(OAL_INC openal/include)
    if(MSVC)
        set(OAL_LIB ${CMAKE_CURRENT_SOURCE_DIR}/openal/OpenAL32.lib)
    else()
        # import library generada con gendef+dlltool a partir de OpenAL32.dll
        set(OAL_LIB ${CMAKE_CURRENT_SOURCE_DIR}/openal/libOpenAL32.a)
    endif()
    add_compile_definitions(MR_USE_OAL_PLAYER)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/glad/include ${OAL_INC} ${OGL_INC})
add_executable(${TARGET} main.cpp ${OGL_FILES})

# Copiar OpenAL32.dll junto al ejecutable despues del build
if(MR_OAL_PLAYER AND WIN32)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/openal/OpenAL32.dll
            $<TARGET_FILE_DIR:${TARGET}>)
endif()
target_link_libraries(${TARGET} PRIVATE media-reader h264 de265 aac ${OGL_LIBS} ${OAL_LIB})
target_compile_features(${TARGET} PUBLIC cxx_std_11)