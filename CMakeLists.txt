cmake_minimum_required(VERSION 3.21)

project(wb VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(wb STATIC
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

add_library(wb::wb ALIAS wb)

target_compile_options(wb PRIVATE -w)

target_include_directories(wb
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>

        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/old
)

find_package(OpenGL REQUIRED)

include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WOLFSSL_CRYPT_TESTS OFF CACHE BOOL "" FORCE)
set(WOLFSSL_EXAMPLES OFF CACHE BOOL "" FORCE)

message(STATUS "Building WolfSSL")

FetchContent_Declare(
        wolfssl
        GIT_REPOSITORY https://github.com/wolfSSL/wolfssl.git
        GIT_TAG v5.9.2-stable
)

FetchContent_MakeAvailable(wolfssl)

set(WOLFSSL_TARGET wolfssl)

target_link_libraries(wb PUBLIC ${WOLFSSL_TARGET})

message(STATUS "WolfSSL built")

if (WIN32)
    include(FetchContent)

    FetchContent_Declare(
            glew
            GIT_REPOSITORY https://github.com/Perlmint/glew-cmake.git
            GIT_TAG master
    )

    set(glew-cmake_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(glew-cmake_BUILD_STATIC ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(glew)

    target_compile_definitions(wb PRIVATE GLEW_STATIC)

    target_link_libraries(wb
            PRIVATE
            libglew_static
            opengl32
            gdi32
    )

elseif (UNIX AND NOT APPLE)
    find_library(EGL_LIBRARY NAMES EGL REQUIRED)
    find_package(GLEW REQUIRED)

    target_link_libraries(wb
            PRIVATE
            OpenGL::GL
            ${EGL_LIBRARY}
            GLEW::GLEW
    )

elseif (APPLE)
    find_package(GLEW REQUIRED)

    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)

    target_link_libraries(wb
            PRIVATE
            OpenGL::GL
            GLEW::GLEW
            ${COREFOUNDATION_FRAMEWORK}
            ${IOKIT_FRAMEWORK}
    )
endif()

install(
        TARGETS wb
        EXPORT wbTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
        DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
        EXPORT wbTargets
        FILE wbTargets.cmake
        NAMESPACE wb::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wb
)

configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/wbConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/wbConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wb
)

write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/wbConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

install(
        FILES
        ${CMAKE_CURRENT_BINARY_DIR}/wbConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/wbConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wb
)

## command line utility

add_executable(wbr
        src/cli/Main.cpp
)

target_link_options(wbr PRIVATE
        -static-libgcc
        -static-libstdc++
        -static
)

target_link_libraries(wbr
        PRIVATE
        wb::wb
)

install(
        TARGETS wbr
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
