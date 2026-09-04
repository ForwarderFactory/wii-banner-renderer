cmake_minimum_required(VERSION 3.21)
project(wii-banner-renderer)

set(CMAKE_CXX_STANDARD 23)

add_compile_options(-w)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/old)

add_executable(wii-banner-renderer
    src/old/Blob.cpp
    src/old/CDUtils.cpp
    src/old/FileBlob.cpp
    src/old/FileHandlerARC.cpp
    src/old/TextureDecoder.cpp

    src/Animator.cpp
    src/Banner.cpp
    src/Endian.cpp
    src/Font.cpp
    src/Layout.cpp
    src/Main.cpp
    src/Material.cpp
    src/Pane.cpp
    src/Picture.cpp
    src/Sound.cpp
    src/Textbox.cpp
    src/Texture.cpp
    src/Window.cpp
    src/WrapGx.cpp
    src/Renderer.cpp
    src/Wad.cpp
)

find_package(OpenGL REQUIRED)
find_package(OpenSSL REQUIRED)

if (WIN32)
    if (MINGW)
        target_link_options(wii-banner-renderer PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()

    include(FetchContent)

    FetchContent_Declare(
        glew
        GIT_REPOSITORY https://github.com/Perlmint/glew-cmake.git
        GIT_TAG master
    )
    
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(glew-cmake_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(glew-cmake_BUILD_STATIC ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(glew)

    target_compile_definitions(wii-banner-renderer PRIVATE GLEW_STATIC)

    target_link_libraries(wii-banner-renderer
        PRIVATE
            libglew_static
            opengl32
            gdi32
	    OpenSSL::SSL
            OpenSSL::Crypto
    )

    file(GLOB OPENSSL_DLLS
        "${CMAKE_PREFIX_PATH}/bin/libssl-*.dll"
        "${CMAKE_PREFIX_PATH}/bin/libcrypto-*.dll"
    )

    execute_process(
            COMMAND bash -lc "cygpath -w $MINGW_PREFIX"
            OUTPUT_VARIABLE MINGW_PREFIX_WIN
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT MINGW_PREFIX_WIN OR MINGW_PREFIX_WIN STREQUAL "")
        message(STATUS "MINGW_PREFIX not found, using fallback path")
        set(MINGW_PREFIX_WIN "C:/msys64/mingw64")
    endif()

    if(EXISTS "${MINGW_PREFIX_WIN}/bin/libssl-3-x64.dll")
        add_custom_command(TARGET wii-banner-renderer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_PREFIX_WIN}/bin/libssl-3-x64.dll"
                "${MINGW_PREFIX_WIN}/bin/libcrypto-3-x64.dll"
                $<TARGET_FILE_DIR:wii-banner-renderer>
        )
    else()
        message(WARNING "OpenSSL DLLs not found in ${MINGW_PREFIX_WIN}/bin")
    endif()
endif()
if (UNIX AND NOT APPLE)
    find_library(EGL_LIBRARY NAMES EGL REQUIRED)

    find_package(GLEW REQUIRED)

    target_link_libraries(wii-banner-renderer
        PRIVATE
            OpenGL::GL
            ${EGL_LIBRARY}
            OpenSSL::SSL
            OpenSSL::Crypto
	    GLEW::GLEW
    )
endif()
if (UNIX AND APPLE)
    find_package(GLEW REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)

    target_link_libraries(wii-banner-renderer
        PRIVATE
            OpenGL::GL
            OpenSSL::SSL
            OpenSSL::Crypto
            GLEW::GLEW
            ${COREFOUNDATION_FRAMEWORK}
            ${IOKIT_FRAMEWORK}
    )
endif()

install(TARGETS wii-banner-renderer DESTINATION bin)
