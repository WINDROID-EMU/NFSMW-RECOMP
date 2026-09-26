#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "rex::runtime" for configuration "Release"
set_property(TARGET rex::runtime APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::runtime PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/librexruntime.so"
  IMPORTED_SONAME_RELEASE "librexruntime.so"
  )

list(APPEND _cmake_import_check_targets rex::runtime )
list(APPEND _cmake_import_check_files_for_rex::runtime "${_IMPORT_PREFIX}/lib/librexruntime.so" )

# Import target "rex::gpu-xenos" for configuration "Release"
set_property(TARGET rex::gpu-xenos APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::gpu-xenos PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE "rex::runtime"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/librexgpu-xenos.so"
  IMPORTED_SONAME_RELEASE "librexgpu-xenos.so"
  )

list(APPEND _cmake_import_check_targets rex::gpu-xenos )
list(APPEND _cmake_import_check_files_for_rex::gpu-xenos "${_IMPORT_PREFIX}/lib/librexgpu-xenos.so" )

# Import target "rex::gpu-plume" for configuration "Release"
set_property(TARGET rex::gpu-plume APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::gpu-plume PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE "rex::runtime"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/librexgpu-plume.so"
  IMPORTED_SONAME_RELEASE "librexgpu-plume.so"
  )

list(APPEND _cmake_import_check_targets rex::gpu-plume )
list(APPEND _cmake_import_check_files_for_rex::gpu-plume "${_IMPORT_PREFIX}/lib/librexgpu-plume.so" )

# Import target "rex::aes128" for configuration "Release"
set_property(TARGET rex::aes128 APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::aes128 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libaes128.a"
  )

list(APPEND _cmake_import_check_targets rex::aes128 )
list(APPEND _cmake_import_check_files_for_rex::aes128 "${_IMPORT_PREFIX}/lib/libaes128.a" )

# Import target "rex::mspack" for configuration "Release"
set_property(TARGET rex::mspack APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::mspack PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libmspack.a"
  )

list(APPEND _cmake_import_check_targets rex::mspack )
list(APPEND _cmake_import_check_files_for_rex::mspack "${_IMPORT_PREFIX}/lib/libmspack.a" )

# Import target "rex::o1heap" for configuration "Release"
set_property(TARGET rex::o1heap APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::o1heap PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libo1heap.a"
  )

list(APPEND _cmake_import_check_targets rex::o1heap )
list(APPEND _cmake_import_check_files_for_rex::o1heap "${_IMPORT_PREFIX}/lib/libo1heap.a" )

# Import target "rex::disasm" for configuration "Release"
set_property(TARGET rex::disasm APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::disasm PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libdisasm.a"
  )

list(APPEND _cmake_import_check_targets rex::disasm )
list(APPEND _cmake_import_check_files_for_rex::disasm "${_IMPORT_PREFIX}/lib/libdisasm.a" )

# Import target "rex::xxhash" for configuration "Release"
set_property(TARGET rex::xxhash APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::xxhash PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libxxhash.a"
  )

list(APPEND _cmake_import_check_targets rex::xxhash )
list(APPEND _cmake_import_check_files_for_rex::xxhash "${_IMPORT_PREFIX}/lib/libxxhash.a" )

# Import target "rex::libavcodec" for configuration "Release"
set_property(TARGET rex::libavcodec APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::libavcodec PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "ASM;C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/liblibavcodec.a"
  )

list(APPEND _cmake_import_check_targets rex::libavcodec )
list(APPEND _cmake_import_check_files_for_rex::libavcodec "${_IMPORT_PREFIX}/lib/liblibavcodec.a" )

# Import target "rex::libavutil" for configuration "Release"
set_property(TARGET rex::libavutil APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::libavutil PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "ASM;C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/liblibavutil.a"
  )

list(APPEND _cmake_import_check_targets rex::libavutil )
list(APPEND _cmake_import_check_files_for_rex::libavutil "${_IMPORT_PREFIX}/lib/liblibavutil.a" )

# Import target "rex::SPIRV" for configuration "Release"
set_property(TARGET rex::SPIRV APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::SPIRV PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libSPIRV.a"
  )

list(APPEND _cmake_import_check_targets rex::SPIRV )
list(APPEND _cmake_import_check_files_for_rex::SPIRV "${_IMPORT_PREFIX}/lib/libSPIRV.a" )

# Import target "rex::glslang" for configuration "Release"
set_property(TARGET rex::glslang APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::glslang PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libglslang.a"
  )

list(APPEND _cmake_import_check_targets rex::glslang )
list(APPEND _cmake_import_check_files_for_rex::glslang "${_IMPORT_PREFIX}/lib/libglslang.a" )

# Import target "rex::MachineIndependent" for configuration "Release"
set_property(TARGET rex::MachineIndependent APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::MachineIndependent PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libMachineIndependent.a"
  )

list(APPEND _cmake_import_check_targets rex::MachineIndependent )
list(APPEND _cmake_import_check_files_for_rex::MachineIndependent "${_IMPORT_PREFIX}/lib/libMachineIndependent.a" )

# Import target "rex::GenericCodeGen" for configuration "Release"
set_property(TARGET rex::GenericCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::GenericCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libGenericCodeGen.a"
  )

list(APPEND _cmake_import_check_targets rex::GenericCodeGen )
list(APPEND _cmake_import_check_files_for_rex::GenericCodeGen "${_IMPORT_PREFIX}/lib/libGenericCodeGen.a" )

# Import target "rex::OSDependent" for configuration "Release"
set_property(TARGET rex::OSDependent APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::OSDependent PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libOSDependent.a"
  )

list(APPEND _cmake_import_check_targets rex::OSDependent )
list(APPEND _cmake_import_check_files_for_rex::OSDependent "${_IMPORT_PREFIX}/lib/libOSDependent.a" )

# Import target "rex::OGLCompiler" for configuration "Release"
set_property(TARGET rex::OGLCompiler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::OGLCompiler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libOGLCompiler.a"
  )

list(APPEND _cmake_import_check_targets rex::OGLCompiler )
list(APPEND _cmake_import_check_files_for_rex::OGLCompiler "${_IMPORT_PREFIX}/lib/libOGLCompiler.a" )

# Import target "rex::rexglue" for configuration "Release"
set_property(TARGET rex::rexglue APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::rexglue PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/rexglue"
  )

list(APPEND _cmake_import_check_targets rex::rexglue )
list(APPEND _cmake_import_check_files_for_rex::rexglue "${_IMPORT_PREFIX}/bin/rexglue" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
