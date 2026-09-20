cmake_minimum_required(VERSION 3.22.1)
project(nfsmw LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

if(NOT DEFINED REXGLUE_SDK_DIR)
    if(DEFINED ENV{REXGLUE_SDK_DIR})
        set(REXGLUE_SDK_DIR "$ENV{REXGLUE_SDK_DIR}" CACHE PATH "ReXGlue SDK directory")
    else()
        set(REXGLUE_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rexglue-sdk" CACHE PATH "ReXGlue SDK directory")
    endif()
endif()
set(rexglue_DIR "${REXGLUE_SDK_DIR}/lib/cmake/rexglue" CACHE PATH "ReXGlue CMake directory")
list(PREPEND CMAKE_FIND_ROOT_PATH "${REXGLUE_SDK_DIR}")
list(PREPEND CMAKE_PREFIX_PATH "${REXGLUE_SDK_DIR}")

add_compile_definitions(
    __ANDROID__
    SPDLOG_COMPILED_LIB
    SPDLOG_FMT_EXTERNAL
    REXGLUE_BUILD_CONFIG=\"Release\"
)

find_package(rexglue 0.10.0 REQUIRED CONFIG)

# Android Native App Glue
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)
target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue
)

# Generated sources from ReXGlue codegen (if generated)
set(GENERATED_SOURCES "")
if(EXISTS "${ROOT_DIR}/app/generated/default/sources.cmake")
    include("${ROOT_DIR}/app/generated/default/sources.cmake")
endif()

set(NFSMW_ANDROID_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/cpp/android_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/cpp/rex_app.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/cpp/ui/android_app_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/cpp/ui/android_window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/cpp/audio/aaudio_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/cpp/input/android_input_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/cpp/input/touch_overlay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/cpp/vfs/android_storage.cpp
    ${ROOT_DIR}/app/src/nfsmw_menu.cpp
    ${GENERATED_SOURCES}
)

add_library(nfsmw SHARED ${NFSMW_ANDROID_SOURCES})

target_link_options(nfsmw PRIVATE
    "-u" "ANativeActivity_onCreate"
    "-Wl,--export-dynamic"
)

target_include_directories(nfsmw PRIVATE
    ${ROOT_DIR}
    ${ROOT_DIR}/app
    ${ROOT_DIR}/app/src
    ${ROOT_DIR}/app/generated/default
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/cpp
    ${REXGLUE_SDK_DIR}/include
    ${REXGLUE_SDK_DIR}/include/renderdoc
    ${REXGLUE_SDK_DIR}/share/rexglue
    ${ANDROID_NDK}/sources/android/native_app_glue
)

target_compile_options(nfsmw PRIVATE
    -march=armv8-a
    -O3
    -fexceptions
    -frtti
    -funroll-loops
    -DNDEBUG
    -Wno-error=format-security
)

if(EXISTS "${ROOT_DIR}/app/generated/default/nfsmw_pch.h")
    target_precompile_headers(nfsmw PRIVATE
        "${ROOT_DIR}/app/generated/default/nfsmw_pch.h"
    )
endif()

target_link_libraries(nfsmw PRIVATE
    -Wl,--whole-archive native_app_glue -Wl,--no-whole-archive
    rex::runtime
    android
    log
    aaudio
    vulkan
    OpenSLES
)
