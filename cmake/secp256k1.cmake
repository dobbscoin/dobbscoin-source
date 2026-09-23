# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# The bundled libsecp256k1 (src/secp256k1, v0.5.1) through its own CMake build,
# configured like configure.ac configures it:
#   --disable-shared --with-pic --enable-module-recovery
#   --disable-benchmark --disable-tests --disable-exhaustive-tests
#
# Linking the `secp256k1` target is the ONLY way our code gets at secp256k1: it
# carries the bundled include/ directory, and every target that includes
# <secp256k1.h> links it (see src/CMakeLists.txt). That is what keeps a system
# /usr/include/secp256k1.h (libsecp256k1-dev) from being picked up instead,
# which is exactly how libdobbscoinconsensus came to compile against the
# system header under Autotools (fixed in 82d7c9cd).

function(add_secp256k1 subdir)
  message("")
  message("Configuring secp256k1 subtree...")
  # Scoped to this function, so none of it leaks back into our build.
  set(BUILD_SHARED_LIBS OFF)
  set(CMAKE_EXPORT_COMPILE_COMMANDS OFF)
  set(SECP256K1_DISABLE_SHARED ON CACHE BOOL "" FORCE)
  set(SECP256K1_ENABLE_MODULE_RECOVERY ON CACHE BOOL "" FORCE) # CKey::SignCompact
  set(SECP256K1_ENABLE_MODULE_ECDH ON CACHE BOOL "" FORCE)       # configure's defaults,
  set(SECP256K1_ENABLE_MODULE_EXTRAKEYS ON CACHE BOOL "" FORCE)  # kept identical so the
  set(SECP256K1_ENABLE_MODULE_SCHNORRSIG ON CACHE BOOL "" FORCE) # library is the same
  set(SECP256K1_ENABLE_MODULE_ELLSWIFT ON CACHE BOOL "" FORCE)
  set(SECP256K1_ECMULT_WINDOW_SIZE 15 CACHE STRING "" FORCE)
  set(SECP256K1_ECMULT_GEN_KB 86 CACHE STRING "" FORCE)
  set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_CTIME_TESTS OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SECP256K1_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory(${subdir})
  # --with-pic: it is linked into the shared libdobbscoinconsensus.
  set_target_properties(secp256k1 PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    EXCLUDE_FROM_ALL TRUE
  )
  # We always link it statically. Since v0.5 the header marks every function
  # __declspec(dllimport) on Windows unless SECP256K1_STATIC is set; secp256k1's
  # CMake only adds it on WIN32, and configure adds it everywhere, so do the same.
  target_compile_definitions(secp256k1 INTERFACE SECP256K1_STATIC)
endfunction()
