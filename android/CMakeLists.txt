cmake_minimum_required(VERSION 3.22.1)
project(nfsmw LANGUAGES CXX C)

# ── libadrenotools (Turnip/Mesa custom Vulkan driver loader) ──────────────────
# Downloaded at configure time; compiled as a shared library so that
# adrenotools_open_libvulkan() can be dlopen()'d by our vulkan_driver_loader.h.
# The ADRENOTOOLS_BUILD_SHARED option is set before add_subdirectory so the
# upstream CMakeLists picks it up.
include(FetchContent)
FetchContent_Declare(
    adrenotools
    GIT_REPOSITORY https://github.com/bylaws/libadrenotools.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
set(GEN_INSTALL_TARGET OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(adrenotools)

# Restrict parallel jobs for Ninja to prevent Out-Of-Memory on CI runners
set_property(GLOBAL PROPERTY JOB_POOLS compile_jobs=1 link_jobs=1)
string(REGEX REPLACE "(^| )-g([0-9]?| )" " " CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g0")
set(CMAKE_JOB_POOL_COMPILE compile_jobs)
set(CMAKE_JOB_POOL_LINK link_jobs)

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
    "-Wl,--gc-sections"
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
    -fomit-frame-pointer
    -fvisibility=hidden
    -fvisibility-inlines-hidden
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
    adrenotools
    android
    log
    aaudio
    vulkan
    OpenSLES
)

# ── libadrenotools shared output ──────────────────────────────────────────────
# adrenotools must also be available as a stand-alone .so so our dlopen() call
# in vulkan_driver_loader.h can locate it via nativeLibraryDir at runtime.
# We build a thin shared wrapper that re-exports the adrenotools symbols.
add_library(adrenotools_shared SHARED
    ${adrenotools_SOURCE_DIR}/src/driver.cpp
    ${adrenotools_SOURCE_DIR}/src/bcenabler.cpp
)
target_include_directories(adrenotools_shared PUBLIC
    ${adrenotools_SOURCE_DIR}/include
    ${adrenotools_SOURCE_DIR}/lib/linkernsbypass/include
)
target_include_directories(adrenotools_shared PRIVATE
    ${adrenotools_SOURCE_DIR}
    ${adrenotools_SOURCE_DIR}/src
)
target_link_libraries(adrenotools_shared PRIVATE
    linkernsbypass
    android
    log
)
set_target_properties(adrenotools_shared PROPERTIES
    OUTPUT_NAME "adrenotools"
)
