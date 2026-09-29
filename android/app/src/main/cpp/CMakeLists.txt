# =============================================================================
#  NFSMW Recompiled - parte nativa del APK de Android (arm64-v8a)
#
#  Lo lanza Gradle (android/app/build.gradle.kts), no se usa a mano. Compila:
#
#    librexruntime.so      el SDK parcheado (tools/android/preparar_sdk.py)
#    librexgpu-xenos.so    el plugin de GPU, que el SDK carga con dlopen
#    lib*_hook.so          los hooks de libadrenotools, para Turnip
#    libmain.so            la app: el codigo generado del juego + la entrada
#
#  DOS MODOS, segun haya o no codigo generado:
#
#    con juego  app/generated-android/default/sources.cmake existe (hace falta
#               tu propio default.xex y tools/android/generar_codigo.py)
#    sonda      no hay codigo generado. libmain.so solo lleva la sonda de
#               Vulkan: carga el driver (del sistema o Turnip), crea el
#               dispositivo como lo haria el juego y apunta que soporta. Sirve
#               para probar un movil y un driver SIN tener el juego.
#
#  EL APK "CON JUEGO" LLEVA DENTRO EL CODIGO DEL JUEGO TRADUCIDO. Igual que
#  nfsmw.exe: se instala en tus dispositivos y NO SE PUBLICA. Ver README.
# =============================================================================

cmake_minimum_required(VERSION 3.25)
project(nfsmw_android LANGUAGES C CXX ASM)

set(NFSMW_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." CACHE PATH
    "Raiz del repositorio NFSMW Recompiled")
set(NFSMW_SDK "${NFSMW_REPO}/../rexglue-sdk-android" CACHE PATH
    "SDK ReXGlue preparado para Android (tools/android/preparar_sdk.py)")
set(NFSMW_GEN_DIR "${NFSMW_REPO}/app/generated-android/default" CACHE PATH
    "Codigo generado para Android (tools/android/generar_codigo.py)")
# Cuantos ficheros del codigo GENERADO se compilan a la vez. Cada uno es un
# bloque enorme de C++ y clang puede pedir mas de 2 GB para cada uno: con 16 GB
# de RAM y ocho en paralelo, el equipo se queda sin memoria. El resto del SDK
# no lleva limite, que son ficheros normales.
set(NFSMW_JOBS_RECOMP "4" CACHE STRING "Ficheros del codigo generado a la vez")

get_filename_component(NFSMW_REPO "${NFSMW_REPO}" ABSOLUTE)
get_filename_component(NFSMW_SDK "${NFSMW_SDK}" ABSOLUTE)

if(NOT EXISTS "${NFSMW_SDK}/.nfsmw-android/rexglue-sdk-v0.10.0-android.patch.aplicado")
    message(FATAL_ERROR
        "No hay un SDK preparado para Android en:\n  ${NFSMW_SDK}\n"
        "Lanza antes:  python tools/android/preparar_sdk.py")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Tracy es una .so aparte que no pinta nada en un APK; los tests, tampoco.
set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "" FORCE)
set(REXGLUE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# En Android solo hay Vulkan.
set(REXGLUE_USE_VULKAN ON CACHE BOOL "" FORCE)
set(REXGLUE_USE_D3D12 OFF CACHE BOOL "" FORCE)

add_subdirectory("${NFSMW_SDK}" rexglue-sdk)

# Cabeceras que necesitan la app y el codigo generado ademas de las de
# rex::runtime: imgui (rex_app.h -> rex/ui/style.h), y RenderDoc y Vulkan
# (la sonda, via rex/ui/vulkan/instance.h). Todas son solo cabeceras.
#
# NUNCA enlazar rexui ni rexcore aqui. Son librerias OBJECT: enlazarlas mete sus
# objetos OTRA VEZ en libmain.so, y el estado global -el registro de cvars, sin
# ir mas lejos- quedaria duplicado. El propio SDK lo avisa en
# src/graphics/CMakeLists.txt. Solo se toman sus rutas de include.
add_library(nfsmw_cabeceras INTERFACE)
target_include_directories(nfsmw_cabeceras INTERFACE
    $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(nfsmw_cabeceras INTERFACE
    renderdoc
    Vulkan::Headers
    GPUOpen::VulkanMemoryAllocator
    spirv-tools-headers)

# -----------------------------------------------------------------------------
#  Codigo generado del juego, si lo hay
# -----------------------------------------------------------------------------
set(NFSMW_CON_JUEGO OFF)
if(EXISTS "${NFSMW_GEN_DIR}/sources.cmake")
    set(NFSMW_CON_JUEGO ON)
    include("${NFSMW_GEN_DIR}/sources.cmake")

    add_library(nfsmw_recomp STATIC ${GENERATED_SOURCES})
    target_include_directories(nfsmw_recomp PRIVATE
        "${NFSMW_REPO}/app"
        "${NFSMW_REPO}/app/src"
        "${NFSMW_GEN_DIR}")
    target_link_libraries(nfsmw_recomp PUBLIC rex::runtime nfsmw_cabeceras)
    rexglue_apply_target_settings(nfsmw_recomp)
    # Lo mismo que generated/rexglue.cmake en escritorio: PCH y solo tablas de
    # lineas de depuracion, que con un millon de lineas generadas se nota.
    target_precompile_headers(nfsmw_recomp PRIVATE "${NFSMW_GEN_DIR}/nfsmw_pch.h")
    target_compile_options(nfsmw_recomp PRIVATE -gline-tables-only -Wno-unused-variable)
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS recomp=${NFSMW_JOBS_RECOMP})
    set_target_properties(nfsmw_recomp PROPERTIES JOB_POOL_COMPILE recomp)
    message(STATUS "NFSMW: con juego (${NFSMW_GEN_DIR})")
else()
    message(WARNING
        "NFSMW: no hay codigo generado en ${NFSMW_GEN_DIR}.\n"
        "Se compila SOLO la sonda de Vulkan. Para el juego: "
        "python tools/android/generar_codigo.py")
endif()

# -----------------------------------------------------------------------------
#  libmain.so - la libreria que SDLActivity carga y en la que busca SDL_main
# -----------------------------------------------------------------------------
add_library(main SHARED
    android_main.cpp
    sonda_vulkan.cpp)
target_include_directories(main PRIVATE "${NFSMW_SDK}/include")
target_link_libraries(main PRIVATE rex::runtime nfsmw_cabeceras android log)
target_compile_definitions(main PRIVATE REXGLUE_BUILD_CONFIG="$<CONFIG>")

if(NFSMW_CON_JUEGO)
    # windowed_app_main_sdl.cpp del SDK NO se compila aqui: es la entrada de
    # escritorio y llama a GetWindowedAppCreator(), que en Android no existe.
    # Alli el SDK define XE_UI_WINDOWED_APPS_IN_LIBRARY y REX_DEFINE_APP apunta
    # la app en una tabla; android_main.cpp la pide por su nombre.
    target_sources(main PRIVATE
        juego.cpp
        ganchos.cpp
        render_targets.cpp
        afinidad.cpp
        audio/aaudio_driver.cpp
        # El menu de ajustes del juego (tecla ESC en escritorio): lo usa
        # app/src/nfsmw_app.h, que se comparte con la version de PC.
        "${NFSMW_REPO}/app/src/nfsmw_menu.cpp"
        "${NFSMW_SDK}/src/ui/rex_app.cpp")
    target_include_directories(main PRIVATE
        "${NFSMW_REPO}/app/src"
        "${NFSMW_GEN_DIR}"
        "${NFSMW_SDK}/src/ui")
    target_link_libraries(main PRIVATE nfsmw_recomp aaudio)
    target_compile_definitions(main PRIVATE NFSMW_CON_JUEGO=1)
    # El runtime resuelve PPCImageConfig, la tabla de funciones y el creador de
    # la app desde esta libreria.
    target_link_options(main PRIVATE -Wl,--export-dynamic)
endif()

# Plugins y hooks: no se enlazan, se cargan con dlopen desde nativeLibraryDir.
# Basta con que se compilen; AGP empaqueta toda .so de este proyecto.
add_dependencies(main rexgpu-xenos)
foreach(_hook main_hook file_redirect_hook gsl_alloc_hook hook_impl)
    if(TARGET ${_hook})
        add_dependencies(main ${_hook})
    endif()
endforeach()
