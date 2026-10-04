# =============================================================================
#  NFSMW Recompiled - parte nativa del APK con el MOTOR NATIVO
#
#  Lo incluye ../CMakeLists.txt cuando Gradle pasa -DNFSMW_MOTOR=nativo
#  (gradlew assembleRelease -Pnfsmw.motor=nativo).
#
#  El motor nativo es el de nfsmw-android (codepdbh), que a su vez es el port a
#  Android de nfsmw-nx (StevensND): el juego no pasa por la GPU de Xbox 360
#  imitada del SDK, sino por un renderizador propio que lee la lista de
#  comandos del juego y dibuja con Vulkan. Ver docs/motor-nativo.md.
#
#  Aqui NO se compila nada de nuestro SDK ni de nuestro codigo generado: el
#  arbol de nfsmw-android trae su SDK (un ReXGlue v0.10.0 modificado), su app y
#  su codigo generado (con su generador). De este repositorio solo entra
#  nativo_android.cpp, los puentes JNI de nuestro lanzador.
#
#  Las opciones de compilacion son las de su android/app/src/main/cpp/CMakeLists.txt
#  (GPL-3.0), sin su libreria de diagnostico ni sus puentes JNI, que son de su
#  lanzador.
# =============================================================================

set(NFSMW_NATIVO "${NFSMW_REPO}/../nfsmw-android" CACHE PATH
    "Arbol de nfsmw-android preparado (tools/android/preparar_nativo.py)")
get_filename_component(NFSMW_NATIVO "${NFSMW_NATIVO}" ABSOLUTE)
# La app de la edicion del juego: app (PAL Espana, para la que esta escrita) o
# app_<edicion>, la copia con las direcciones de otra edicion
# (gradlew -Pnfsmw.edicion=usa). Esta al lado de app/: sus rutas relativas al
# SDK y a las herramientas son las mismas.
set(NFSMW_NATIVO_APP "app" CACHE STRING "Carpeta de la app de la edicion, dentro del arbol nativo")

if(NOT EXISTS "${NFSMW_NATIVO}/${NFSMW_NATIVO_APP}/generated/default/sources.cmake")
    message(FATAL_ERROR
        "No hay motor nativo preparado en:\n  ${NFSMW_NATIVO}/${NFSMW_NATIVO_APP}\n"
        "Lanza antes:  python tools/android/preparar_nativo.py")
endif()

set(REXSDK_DIR "${NFSMW_NATIVO}/sdk" CACHE PATH "SDK del motor nativo" FORCE)
set(REXGLUE_USE_VULKAN ON CACHE BOOL "" FORCE)
set(REXGLUE_USE_D3D12 OFF CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "" FORCE)
set(REXGLUE_VERSION_OVERRIDE "0.10.0-dev.android" CACHE STRING "")
# Sin tablas de depuracion en el codigo generado: son enormes y no se usan.
set(REXGLUE_RECOMP_DEBUG_INFO "none" CACHE STRING "")
# SDLActivity carga libSDL3.so y despues libmain.so.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
# El lanzador de Qt de su app es de escritorio.
set(NFSMW_BUILD_LAUNCHER OFF CACHE BOOL "" FORCE)

# FSR 1.0 y CAS del presentador del SDK (present_effect = fsr | cas). Sus shaders
# vienen precompilados en src/ui/shaders/vulkan_spirv, pero el SDK solo los
# activa si se descarga el FidelityFX SDK de AMD, que es para escritorio (y solo
# hace falta para FSR 2/3, los temporales). Sin el runtime, el codigo ya cae en
# FSR 1.0 y CAS.
#
# En TODO el arbol, porque cambia la forma de las clases del presentador, que
# incluyen la interfaz, los graficos, la entrada, el kernel y la app: una
# definicion distinta entre dos ficheros seria un fallo de los que no avisan. En
# todo menos en el codigo generado del juego, que no lo usa: asi no se
# recompila entero.
add_compile_definitions(
    $<$<NOT:$<STREQUAL:$<TARGET_PROPERTY:NAME>,nfsmw_recomp>>:REX_HAS_FIDELITYFX_SDK=1>)

add_subdirectory("${NFSMW_NATIVO}/${NFSMW_NATIVO_APP}" nfsmw-app)

target_sources(nfsmw PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/nativo_android.cpp"
    # Los mismos que con el motor de Xenos: la sonda de Vulkan y los hilos
    # fijados a nucleos. Su android_rendimiento.cpp no entra: nativo_android.cpp
    # pone en su sitio nuestro vigilante de afinidad.
    "${CMAKE_CURRENT_LIST_DIR}/../sonda_vulkan.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../afinidad.cpp"
    # Nuestra salida de audio por AAudio, la que arreglo los cortes con el motor
    # de Xenos. Con NFSMW_MOTOR_NATIVO usa ademas el volumen, el limitador y el
    # silencio durante las peliculas de su SDK.
    "${CMAKE_CURRENT_LIST_DIR}/../audio/aaudio_driver.cpp")
target_include_directories(nfsmw PRIVATE "${CMAKE_CURRENT_LIST_DIR}/..")
target_compile_definitions(nfsmw PRIVATE NFSMW_MOTOR_NATIVO=1)
target_link_libraries(nfsmw PRIVATE aaudio)

if(TARGET SDL3-shared)
    # SDLActivity hace System.loadLibrary("SDL3") siempre: sin sufijos.
    set_target_properties(SDL3-shared PROPERTIES
        OUTPUT_NAME "SDL3"
        DEBUG_POSTFIX ""
        RELEASE_POSTFIX ""
        RELWITHDEBINFO_POSTFIX ""
        MINSIZEREL_POSTFIX "")
endif()
# nativo_android.cpp le pide a SDL el JNIEnv y la actividad.
target_link_libraries(nfsmw PRIVATE SDL3::SDL3)

# Paginas de 16 KB (Android 15+).
target_link_options(nfsmw PRIVATE "-Wl,-z,max-page-size=16384")

# libmain y rexruntime llevan FFmpeg los dos. Sus simbolos de C van ocultos,
# pero los de ensamblador no: sin esto libmain inicializa sus tablas de FFT y
# llama a la FFT de rexruntime, que tiene las suyas vacias.
foreach(objetivo nfsmw rexruntime)
    target_link_options(${objetivo} PRIVATE
        "LINKER:--exclude-libs,liblibavcodec.a:liblibavutil.a")
endforeach()

# Los ficheros generados son enormes: de pocos en pocos, que con 16 GB de RAM
# y uno por nucleo el equipo se queda sin memoria.
set_property(GLOBAL APPEND PROPERTY JOB_POOLS recomp=${NFSMW_JOBS_RECOMP})
if(TARGET nfsmw_recomp)
    set_target_properties(nfsmw_recomp PROPERTIES JOB_POOL_COMPILE recomp)
endif()

# ThinLTO sobre el codigo del juego y la app. Su generador ya llama directo a
# __imp__sub_X (tools/llamadas_directas.py), asi que clang puede integrar
# funciones del juego entre los 265 ficheros.
option(NFSMW_ANDROID_LTO "ThinLTO en el codigo del juego y de la app" ON)
if(NFSMW_ANDROID_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    foreach(objetivo nfsmw_recomp nfsmw)
        if(TARGET ${objetivo})
            target_compile_options(${objetivo} PRIVATE -flto=thin)
        endif()
    endforeach()
    target_link_options(nfsmw PRIVATE -flto=thin "-Wl,--thinlto-jobs=${NFSMW_JOBS_RECOMP}"
                        "-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/thinlto-cache")
    target_link_options(nfsmw PRIVATE -Wl,--gc-sections -Wl,--icf=safe)
endif()

# Drivers propios (Turnip) con libadrenotools: los hooks son .so aparte que
# adrenotools busca en nativeLibraryDir. Basta con que se compilen; AGP empaqueta
# toda .so del proyecto. Sin libadrenotools en su thirdparty (preparar_nativo.py
# lo pone) no existen y se va con el driver del sistema.
foreach(_hook main_hook file_redirect_hook gsl_alloc_hook hook_impl)
    if(TARGET ${_hook})
        add_dependencies(nfsmw ${_hook})
    endif()
endforeach()

message(STATUS "NFSMW: motor nativo (${NFSMW_NATIVO}/${NFSMW_NATIVO_APP})")
