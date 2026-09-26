#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "rex::runtime" for configuration "Debug"
set_property(TARGET rex::runtime APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::runtime PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/librexruntimed.so"
  IMPORTED_SONAME_DEBUG "librexruntimed.so"
  )

list(APPEND _cmake_import_check_targets rex::runtime )
list(APPEND _cmake_import_check_files_for_rex::runtime "${_IMPORT_PREFIX}/lib/librexruntimed.so" )

# Import target "rex::gpu-xenos" for configuration "Debug"
set_property(TARGET rex::gpu-xenos APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::gpu-xenos PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "rex::runtime"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/librexgpu-xenosd.so"
  IMPORTED_SONAME_DEBUG "librexgpu-xenosd.so"
  )

list(APPEND _cmake_import_check_targets rex::gpu-xenos )
list(APPEND _cmake_import_check_files_for_rex::gpu-xenos "${_IMPORT_PREFIX}/lib/librexgpu-xenosd.so" )

# Import target "rex::gpu-plume" for configuration "Debug"
set_property(TARGET rex::gpu-plume APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::gpu-plume PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "rex::runtime"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/librexgpu-plumed.so"
  IMPORTED_SONAME_DEBUG "librexgpu-plumed.so"
  )

list(APPEND _cmake_import_check_targets rex::gpu-plume )
list(APPEND _cmake_import_check_files_for_rex::gpu-plume "${_IMPORT_PREFIX}/lib/librexgpu-plumed.so" )

# Import target "rex::aes128" for configuration "Debug"
set_property(TARGET rex::aes128 APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::aes128 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libaes128d.a"
  )

list(APPEND _cmake_import_check_targets rex::aes128 )
list(APPEND _cmake_import_check_files_for_rex::aes128 "${_IMPORT_PREFIX}/lib/libaes128d.a" )

# Import target "rex::mspack" for configuration "Debug"
set_property(TARGET rex::mspack APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::mspack PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libmspackd.a"
  )

list(APPEND _cmake_import_check_targets rex::mspack )
list(APPEND _cmake_import_check_files_for_rex::mspack "${_IMPORT_PREFIX}/lib/libmspackd.a" )

# Import target "rex::o1heap" for configuration "Debug"
set_property(TARGET rex::o1heap APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::o1heap PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libo1heapd.a"
  )

list(APPEND _cmake_import_check_targets rex::o1heap )
list(APPEND _cmake_import_check_files_for_rex::o1heap "${_IMPORT_PREFIX}/lib/libo1heapd.a" )

# Import target "rex::disasm" for configuration "Debug"
set_property(TARGET rex::disasm APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::disasm PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libdisasmd.a"
  )

list(APPEND _cmake_import_check_targets rex::disasm )
list(APPEND _cmake_import_check_files_for_rex::disasm "${_IMPORT_PREFIX}/lib/libdisasmd.a" )

# Import target "rex::xxhash" for configuration "Debug"
set_property(TARGET rex::xxhash APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::xxhash PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libxxhashd.a"
  )

list(APPEND _cmake_import_check_targets rex::xxhash )
list(APPEND _cmake_import_check_files_for_rex::xxhash "${_IMPORT_PREFIX}/lib/libxxhashd.a" )

# Import target "rex::libavcodec" for configuration "Debug"
set_property(TARGET rex::libavcodec APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::libavcodec PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "ASM;C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/liblibavcodecd.a"
  )

list(APPEND _cmake_import_check_targets rex::libavcodec )
list(APPEND _cmake_import_check_files_for_rex::libavcodec "${_IMPORT_PREFIX}/lib/liblibavcodecd.a" )

# Import target "rex::libavutil" for configuration "Debug"
set_property(TARGET rex::libavutil APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::libavutil PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "ASM;C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/liblibavutild.a"
  )

list(APPEND _cmake_import_check_targets rex::libavutil )
list(APPEND _cmake_import_check_files_for_rex::libavutil "${_IMPORT_PREFIX}/lib/liblibavutild.a" )

# Import target "rex::SPIRV" for configuration "Debug"
set_property(TARGET rex::SPIRV APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::SPIRV PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libSPIRVd.a"
  )

list(APPEND _cmake_import_check_targets rex::SPIRV )
list(APPEND _cmake_import_check_files_for_rex::SPIRV "${_IMPORT_PREFIX}/lib/libSPIRVd.a" )

# Import target "rex::glslang" for configuration "Debug"
set_property(TARGET rex::glslang APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::glslang PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libglslangd.a"
  )

list(APPEND _cmake_import_check_targets rex::glslang )
list(APPEND _cmake_import_check_files_for_rex::glslang "${_IMPORT_PREFIX}/lib/libglslangd.a" )

# Import target "rex::MachineIndependent" for configuration "Debug"
set_property(TARGET rex::MachineIndependent APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::MachineIndependent PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libMachineIndependentd.a"
  )

list(APPEND _cmake_import_check_targets rex::MachineIndependent )
list(APPEND _cmake_import_check_files_for_rex::MachineIndependent "${_IMPORT_PREFIX}/lib/libMachineIndependentd.a" )

# Import target "rex::GenericCodeGen" for configuration "Debug"
set_property(TARGET rex::GenericCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::GenericCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libGenericCodeGend.a"
  )

list(APPEND _cmake_import_check_targets rex::GenericCodeGen )
list(APPEND _cmake_import_check_files_for_rex::GenericCodeGen "${_IMPORT_PREFIX}/lib/libGenericCodeGend.a" )

# Import target "rex::OSDependent" for configuration "Debug"
set_property(TARGET rex::OSDependent APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::OSDependent PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libOSDependentd.a"
  )

list(APPEND _cmake_import_check_targets rex::OSDependent )
list(APPEND _cmake_import_check_files_for_rex::OSDependent "${_IMPORT_PREFIX}/lib/libOSDependentd.a" )

# Import target "rex::OGLCompiler" for configuration "Debug"
set_property(TARGET rex::OGLCompiler APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rex::OGLCompiler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libOGLCompilerd.a"
  )

list(APPEND _cmake_import_check_targets rex::OGLCompiler )
list(APPEND _cmake_import_check_files_for_rex::OGLCompiler "${_IMPORT_PREFIX}/lib/libOGLCompilerd.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
