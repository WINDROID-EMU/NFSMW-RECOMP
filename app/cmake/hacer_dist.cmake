# =============================================================================
#  hacer_dist.cmake - arma la carpeta portable  build\
#
#  Lo lanza CMake en modo script (-P) el target "dist" de app/CMakeLists.txt,
#  que le pasa cada ruta como -D D_...  Ver ahi que papel tiene cada variable.
#
#  LA ISO NO SE COPIA: pesa varios GB y el juego la lee al vuelo desde junto
#  al .exe. La pone el usuario en build\ cuando quiera usar la carpeta.
#
#  El juego se copia como ${D_NOMBRE}.exe, que ahora es NFS_Most_Wanted.exe.
#  Despues CONSTRUIR_LANZADOR.bat lo renombra a nfsmw.exe y deja el lanzador
#  con el nombre del juego, que es como se ve la carpeta terminada.
# =============================================================================

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED D_DIST OR NOT D_DIST)
    message(FATAL_ERROR "hacer_dist.cmake: falta D_DIST")
endif()
if(NOT DEFINED D_NOMBRE)
    set(D_NOMBRE "NFS_Most_Wanted")
endif()
if(NOT DEFINED D_BUILD OR NOT D_BUILD)
    get_filename_component(D_BUILD "${D_EXE}" DIRECTORY)
endif()

# ---- Empezar de cero --------------------------------------------------------
#  Ademas de evitar restos, borra de corridas anteriores logs\, matriz\,
#  shaders\ y cache\, que son de esta maquina y no deben viajar.
file(REMOVE_RECURSE "${D_DIST}")
file(MAKE_DIRECTORY "${D_DIST}")

# ---- El juego ---------------------------------------------------------------
if(NOT DEFINED D_EXE OR NOT EXISTS "${D_EXE}")
    message(FATAL_ERROR "hacer_dist.cmake: no encuentro el ejecutable ${D_EXE}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${D_EXE}" "${D_DIST}/${D_NOMBRE}.exe")

# ---- Las DLL del proyecto ---------------------------------------------------
#  En el directorio de compilacion estan rexruntime.dll y los plugins de GPU
#  (rexgpu-xenos.dll), que el juego carga con LoadLibrary y por eso no figuran
#  en las dependencias del enlazador: hay que copiarlas a mano.
set(DLLS_PROYECTO)
file(GLOB DLLS_PROYECTO "${D_BUILD}/*.dll")
foreach(dll IN LISTS DLLS_PROYECTO)
    file(COPY "${dll}" DESTINATION "${D_DIST}")
endforeach()

# ---- Runtime de Visual C++ --------------------------------------------------
#  Las DLL de la familia VC143 que el juego importa, resueltas desde el propio
#  toolchain. D_VCRUNTIME puede venir vacia si el compilador no es MSVC puro
#  (clang, por ejemplo): entonces se buscan en D_REDIST, el directorio de
#  redist del mismo Visual Studio, que no depende de rutas fijas.
set(VC_NOMBRES
    msvcp140.dll
    msvcp140_1.dll
    msvcp140_2.dll
    msvcp140_atomic_wait.dll
    msvcp140_codecvt_ids.dll
    vcruntime140.dll
    vcruntime140_1.dll
    concrt140.dll
)

set(VC_COPIADAS)
if(DEFINED D_VCRUNTIME AND D_VCRUNTIME)
    foreach(ruta IN LISTS D_VCRUNTIME)
        get_filename_component(nombre "${ruta}" NAME)
        if(nombre IN_LIST VC_NOMBRES AND EXISTS "${ruta}")
            file(COPY "${ruta}" DESTINATION "${D_DIST}")
            list(APPEND VC_COPIADAS "${nombre}")
        endif()
    endforeach()
endif()
if(DEFINED D_REDIST AND D_REDIST)
    string(REGEX REPLACE "[\\/]+$" "" D_REDIST "${D_REDIST}")
    foreach(nombre IN LISTS VC_NOMBRES)
        if(NOT nombre IN_LIST VC_COPIADAS)
            set(ruta "${D_REDIST}/x64/Microsoft.VC143.CRT/${nombre}")
            if(EXISTS "${ruta}")
                file(COPY "${ruta}" DESTINATION "${D_DIST}")
                list(APPEND VC_COPIADAS "${nombre}")
            endif()
        endif()
    endforeach()
endif()

# ---- Ajustes y ayuda --------------------------------------------------------
#  nfsmw.toml va al lado del exe, como lo busca el juego.
if(DEFINED D_CONFIG AND EXISTS "${D_CONFIG}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${D_CONFIG}" "${D_DIST}/nfsmw.toml")
endif()

#  LEEME.txt sale de la plantilla dist_leeme.txt.
if(DEFINED D_PLANTILLA AND EXISTS "${D_PLANTILLA}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${D_PLANTILLA}" "${D_DIST}/LEEME.txt")
endif()

#  Los siguientes cambian de nombre al pasar a build\, que es como los citan
#  el LEEME y los demas ficheros: LANZADOR.bat llama a lanzador.ps1, PROBAR.bat
#  a matriz.ps1, y COMPARAR_VIDEO.bat es el nombre viejo de lo que en el arbol
#  del proyecto es dist_comparar_edram.bat.
if(DEFINED D_LANZADOR_BAT AND EXISTS "${D_LANZADOR_BAT}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${D_LANZADOR_BAT}" "${D_DIST}/LANZADOR.bat")
endif()
if(DEFINED D_LANZADOR AND EXISTS "${D_LANZADOR}")
    file(COPY "${D_LANZADOR}" DESTINATION "${D_DIST}")
endif()
if(DEFINED D_PROBAR AND EXISTS "${D_PROBAR}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${D_PROBAR}" "${D_DIST}/PROBAR.bat")
endif()
if(DEFINED D_MATRIZ AND EXISTS "${D_MATRIZ}")
    file(COPY "${D_MATRIZ}" DESTINATION "${D_DIST}")
endif()
if(DEFINED D_EDRAM AND EXISTS "${D_EDRAM}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${D_EDRAM}" "${D_DIST}/COMPARAR_VIDEO.bat")
endif()

message(STATUS "Carpeta portable armada en ${D_DIST}")