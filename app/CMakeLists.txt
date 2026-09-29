# nfsmw - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(nfsmw LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(NFSMW_SOURCES
    src/main.cpp
    src/nfsmw_menu.cpp
)

if(WIN32)
    add_executable(nfsmw WIN32 ${NFSMW_SOURCES})
else()
    add_executable(nfsmw ${NFSMW_SOURCES})
endif()

# GPU_PLUGINS deja el DLL del backend grafico junto al .exe
# (rexgpu-xenosd.dll en Debug, rexgpu-xenos.dll en Release).
#
# Hacen falta LAS DOS COSAS para ver imagen:
#   1. esta linea, que coloca el DLL donde el cargador lo busca
#   2. que gpu_plugin valga "xenos" al ejecutar
#
# El CVar gpu_plugin viene vacio de fabrica, y vacio significa "sin emulacion
# de GPU": el juego inicializa su ring buffer y sus callbacks graficos, el
# runtime los descarta con "no GPU emulation loaded", y la pantalla se queda
# en negro aunque el juego este corriendo perfectamente.
#
# Desde que src/nfsmw_app.h pone gpu_plugin=xenos por defecto, el argumento
# --gpu_plugin ya no hace falta; se puede seguir pasando para cambiarlo.
rexglue_setup_target(nfsmw GPU_PLUGINS xenos)

# Native Qt launcher (Linux). Used by packaging/appimage/.
option(NFSMW_BUILD_LAUNCHER "Build the native Qt launcher" ON)
option(NFSMW_LAUNCHER_BUILD_TESTS "Build Qt launcher tests" OFF)
if(NFSMW_BUILD_LAUNCHER AND UNIX AND NOT APPLE)
    find_package(Qt6 6.4 COMPONENTS Core Gui Widgets QUIET)
    if(Qt6_FOUND)
        if(NFSMW_LAUNCHER_BUILD_TESTS)
            enable_testing()
        endif()
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../launcher-linux"
                         "${CMAKE_CURRENT_BINARY_DIR}/launcher-linux")
    else()
        message(WARNING
            "Qt 6.4+ was not found; nfsmw_launcher will not be built")
    endif()
endif()


# =============================================================================
#  TARGET "dist" - la carpeta portable, que se llama  build\
#
#  Junta en una sola carpeta el ejecutable, las DLL del proyecto y las del
#  runtime de Visual C++, para poder llevarsela a un equipo sin herramientas
#  de desarrollo.
#
#      cmake --build --preset win-amd64-release --target dist
#
#  EL TARGET SE LLAMA "dist" Y LA CARPETA "build". No es un descuido: build/
#  ya estaba en el .gitignore y dist/ no, asi que dejando ahi la carpeta -que
#  lleva dentro el ejecutable con el codigo del juego- queda fuera de git
#  sola. Para ponerla en otro sitio:  -D NFSMW_DIST_DIR=...
#
#  NO CONFUNDIR con app/out/build/win-amd64-release, que es donde compila
#  CMake. Esta es la carpeta ya montada, lista para llevarsela.
#
#  LO QUE NO HACE, Y ES IMPORTANTE:
#  no mete la ISO dentro de nada. Sigue siendo un archivo suelto que el juego
#  lee al vuelo, y src/nfsmw_app.h la busca junto al .exe. Asi la carpeta
#  funciona este donde este, sin ninguna ruta fija.
#
#  Nada de esto cambia como se construye el proyecto: es un paso mas, aparte.
# =============================================================================
if(WIN32)
    # Resuelve las DLL del runtime de VC desde el toolchain que se este usando.
    # Con _SKIP a TRUE no instala nada: solo rellena la lista, que es lo unico
    # que queremos. Sin esto habria que escribir a mano una ruta tipo
    # "C:\Program Files\Microsoft Visual Studio\...\Microsoft.VC143.CRT", que es
    # exactamente lo que no debe haber aqui.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_NO_WARNINGS TRUE)
    include(InstallRequiredSystemLibraries)

    # SIN cache a proposito. Si fueran variables de cache, cambiar el valor
    # por defecto no serviria de nada en un arbol ya configurado: seguiria
    # usando el viejo sin decir ni pio, el target diria "hecho" y la carpeta
    # apareceria donde antes. Asi manda siempre lo de aqui, y un -D de la
    # linea de comandos lo sigue pisando.
    if(NOT DEFINED NFSMW_DIST_NOMBRE)
        set(NFSMW_DIST_NOMBRE "NFS_Most_Wanted")
    endif()
    if(NOT DEFINED NFSMW_DIST_DIR)
        set(NFSMW_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build")
    endif()

    add_custom_target(dist
        DEPENDS nfsmw
        COMMAND ${CMAKE_COMMAND}
                -D "D_EXE=$<TARGET_FILE:nfsmw>"
                -D "D_BUILD=$<TARGET_FILE_DIR:nfsmw>"
                -D "D_DIST=${NFSMW_DIST_DIR}"
                -D "D_NOMBRE=${NFSMW_DIST_NOMBRE}"
                -D "D_VCRUNTIME=${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}"
                -D "D_REDIST=$ENV{VCToolsRedistDir}"
                -D "D_PLANTILLA=${CMAKE_CURRENT_SOURCE_DIR}/dist_leeme.txt"
                -D "D_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/nfsmw.toml"
                -D "D_EDRAM=${CMAKE_CURRENT_SOURCE_DIR}/dist_comparar_edram.bat"
                -D "D_LANZADOR=${CMAKE_CURRENT_SOURCE_DIR}/../tools/lanzador/lanzador.ps1"
                -D "D_LANZADOR_BAT=${CMAKE_CURRENT_SOURCE_DIR}/dist_lanzador.bat"
                -D "D_MATRIZ=${CMAKE_CURRENT_SOURCE_DIR}/../tools/matriz.ps1"
                -D "D_PROBAR=${CMAKE_CURRENT_SOURCE_DIR}/dist_probar.bat"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/hacer_dist.cmake"
        COMMENT "Armando la carpeta portable en ${NFSMW_DIST_DIR}"
        VERBATIM
    )
endif()
