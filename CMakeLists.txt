# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# CMake build for Dobbscoin Core. See doc/build-cmake.md.
#
# Modelled on Bitcoin Core v29's CMake build, cut down to what this tree has.
# During the transition (v0.14.0) the Autotools build is still here and both
# must produce equivalent binaries: the compile flags, config header defines and
# link lists below deliberately mirror what configure.ac produces. When you
# change one build system, change the other, until Autotools is removed.

cmake_minimum_required(VERSION 3.22)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW) # find_package(Boost) uses Boost's own config
endif()

# The version numbers have ONE home while both build systems exist: the
# define(_CLIENT_VERSION_*) lines at the top of configure.ac. This reads them.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ClientVersion.cmake)

project(DobbscoinCore
  VERSION ${CLIENT_VERSION_MAJOR}.${CLIENT_VERSION_MINOR}.${CLIENT_VERSION_REVISION}.${CLIENT_VERSION_BUILD}
  DESCRIPTION "Dobbscoin client software"
  HOMEPAGE_URL "https://dobbscoin.info"
  # ASM: src/crypto/scrypt-x64.S, the miner's SIMD scrypt (preprocessed by the C compiler).
  LANGUAGES C CXX ASM
)
if(WIN32)
  # The version resources (dobbscoind-res.rc and friends), compiled by windres
  # with the target's defines and includes, as src/Makefile.am's .rc.o rule
  # does. WINDRES_PREPROC keeps C++ out of clientversion.h.
  enable_language(RC)
  string(APPEND CMAKE_RC_FLAGS " -DWINDRES_PREPROC")
endif()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/module)

# An in-tree Autotools build leaves generated headers in src/ that a quoted
# #include finds before anything in the build directory, so CMake would
# silently compile against Autotools' config. Refuse instead.
foreach(stale IN ITEMS src/config/dobbscoin-config.h src/obj/build.h)
  if(EXISTS ${PROJECT_SOURCE_DIR}/${stale})
    message(FATAL_ERROR
      "${stale} exists in the source tree (left by an in-tree Autotools build).\n"
      "Run 'make distclean' in the source tree, or build from a clean checkout.")
  endif()
endforeach()
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not supported. Use: cmake -B build")
endif()

#=============================
# Options
#=============================
# Names follow Bitcoin Core v29 where Core has the same thing. The Autotools
# equivalent of each is in doc/build-cmake.md.
option(BUILD_DAEMON "Build dobbscoind" ON)
option(BUILD_CLI "Build dobbscoin-cli" ON)
option(BUILD_TX "Build dobbscoin-tx" ON)
option(BUILD_CONSENSUS_LIB "Build the shared libdobbscoinconsensus" ON)
option(BUILD_TESTS "Build test_dobbscoin" ON)
option(BUILD_GUI "Build dobbscoin-qt (Qt 5)" OFF)
include(CMakeDependentOption)
cmake_dependent_option(BUILD_GUI_TESTS "Build test_dobbscoin-qt" ON "BUILD_GUI;BUILD_TESTS" OFF)

option(ENABLE_WALLET "Enable the wallet (needs SQLite)" ON)
option(WITH_MINIUPNPC "Enable UPnP port mapping (needs libminiupnpc)" ON)
option(WITH_NATPMP "Enable NAT-PMP port mapping (needs libnatpmp)" ON)
option(ENABLE_UPNP_DEFAULT "Turn UPnP on at startup by default" OFF)
option(ENABLE_NATPMP_DEFAULT "Turn NAT-PMP on at startup by default" OFF)
option(ENABLE_STATIC_PORTMAP "Link libminiupnpc and libnatpmp statically, so a published binary does not need them installed (#45)" OFF)
cmake_dependent_option(WITH_QRENCODE "Enable QR codes in the GUI (needs libqrencode)" ON "BUILD_GUI" OFF)
cmake_dependent_option(WITH_DBUS "Enable D-Bus desktop notifications in the GUI" ON "BUILD_GUI;NOT WIN32;NOT APPLE" OFF)

option(ENABLE_HARDENING "Harden the executables (stack protector, fortify, relro/now)" ON)
option(REDUCE_EXPORTS "Hide symbols that do not need exporting" ON)
option(WITH_CCACHE "Use ccache when it is found" ON)
option(BUILD_FUZZ "Build the libFuzzer harnesses in src/test/fuzz (clang only; see doc/fuzzing.md)" OFF)

if(NOT (BUILD_DAEMON OR BUILD_CLI OR BUILD_TX OR BUILD_CONSENSUS_LIB OR BUILD_TESTS OR BUILD_GUI OR BUILD_FUZZ))
  message(FATAL_ERROR "Nothing to build. Enable at least one BUILD_* option.")
endif()

#=============================
# Build type and language standard
#=============================
# configure's default CXXFLAGS are "-g -O2" and it never defines NDEBUG: the
# code relies on assert() being live. CMake's stock RelWithDebInfo/Release add
# -DNDEBUG, so the build types are redefined here rather than inherited.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()
foreach(lang IN ITEMS C CXX)
  if(DEPENDS_PREFIX)
    # A depends toolchain (depends/toolchain.cmake.in) has already put
    # config.site's flags ("-pipe -O2") in CMAKE_<LANG>_FLAGS, and configure
    # adds no "-g -O2" of its own when CXXFLAGS is set.
    set(CMAKE_${lang}_FLAGS_RELWITHDEBINFO "")
    set(CMAKE_${lang}_FLAGS_RELEASE "")
    set(CMAKE_${lang}_FLAGS_MINSIZEREL "")
  else()
    set(CMAKE_${lang}_FLAGS_RELWITHDEBINFO "-g -O2")
    set(CMAKE_${lang}_FLAGS_RELEASE "-O2")
    set(CMAKE_${lang}_FLAGS_MINSIZEREL "-Os")
  endif()
  set(CMAKE_${lang}_FLAGS_DEBUG "-g3 -O0 -DDEBUG") # = configure --enable-debug
endforeach()

# configure adds -std=c++14 (configure.ac, "legacy buildsystem" block).
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WITH_CCACHE)
  find_program(CCACHE_PROGRAM ccache)
  if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
  endif()
endif()

if(CMAKE_CXX_BYTE_ORDER STREQUAL "BIG_ENDIAN")
  message(FATAL_ERROR "Big endian is not supported.")
endif()

#=============================
# Dependencies
#=============================
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
include(cmake/boost.cmake)
find_package(OpenSSL REQUIRED)

if(ENABLE_WALLET)
  find_package(SQLite3 3.7.17)
  if(NOT SQLite3_FOUND)
    message(FATAL_ERROR "SQLite 3 not found. The wallet needs it: install libsqlite3-dev, or pass -DENABLE_WALLET=OFF.")
  endif()
endif()

if(WITH_MINIUPNPC)
  find_package(MiniUPnPc)
  if(NOT MiniUPnPc_FOUND)
    message(FATAL_ERROR "libminiupnpc not found: install libminiupnpc-dev, or pass -DWITH_MINIUPNPC=OFF.")
  endif()
endif()
if(WITH_NATPMP)
  find_package(NATPMP)
  if(NOT NATPMP_FOUND)
    message(FATAL_ERROR "libnatpmp not found: install libnatpmp-dev, or pass -DWITH_NATPMP=OFF.")
  endif()
endif()

if(BUILD_GUI)
  include(cmake/qt.cmake)
endif()

#=============================
# Flags shared by every target of ours
#=============================
# Everything we compile links this INTERFACE target, the way Core v29 uses
# core_interface. Keeping the flags here (and not in global CMAKE_CXX_FLAGS)
# stops them leaking into the bundled secp256k1, which Autotools also shields
# from them: it configures secp256k1 with its own flags.
add_library(dobbscoin_interface INTERFACE)
target_compile_definitions(dobbscoin_interface INTERFACE
  HAVE_CONFIG_H
  BOOST_SPIRIT_THREADSAFE
  BOOST_BIND_GLOBAL_PLACEHOLDERS
  HAVE_BUILD_INFO
  __STDC_FORMAT_MACROS
)
# configure adds these warnings to CXXFLAGS only, never CFLAGS, and only when
# CXXFLAGS was not given, which depends' config.site always gives.
if(NOT DEPENDS_PREFIX)
  target_compile_options(dobbscoin_interface INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wformat -Wformat-security -Wno-unused-parameter>
  )
endif()
target_link_libraries(dobbscoin_interface INTERFACE Threads::Threads)

if(WIN32)
  # configure.ac, case *mingw*. Everything is linked statically (libtool's
  # -all-static), so the executables need no DLL beyond Windows' own. configure
  # leaves out its -w when CXXFLAGS is given, which a depends build always does.
  # _FILE_OFFSET_BITS=64 is what AC_SYS_LARGEFILE finds for mingw (and nothing
  # for 64-bit Linux).
  target_compile_definitions(dobbscoin_interface INTERFACE
    _MT WIN32 _WINDOWS BOOST_THREAD_USE_LIB _FILE_OFFSET_BITS=64
  )
  target_link_options(dobbscoin_interface INTERFACE -static)
  # configure's AC_CHECK_LIB list, which ends up in LIBS at the end of every
  # link line (libssp is added in cmake/hardening.cmake). CMake's own default
  # list for MinGW is a subset of it.
  set(windows_system_libs
    -lcrypt32 -liphlpapi -lshlwapi -lmswsock -lws2_32 -ladvapi32 -lrpcrt4
    -luuid -loleaut32 -lole32 -lcomctl32 -lshell32 -lwinmm -lwinspool
    -lcomdlg32 -lgdi32 -luser32 -lkernel32 -lmingwthrd
  )
endif()

# CMake hands the compiler absolute source paths, so __FILE__ (in assert()
# messages, log lines and Boost.Test) would carry the builder's directory, e.g.
# /home/someone/dobbscoin/src/main.cpp. Autotools compiles from src/ with
# relative names ("main.cpp"). HAVE_MACRO_PREFIX_MAP lets src/ and LevelDB map
# the prefix away, so the strings, and the machine code that embeds their
# lengths, match Autotools and the build path stays out of release binaries.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-fmacro-prefix-map=A=B" HAVE_MACRO_PREFIX_MAP)

#=============================
# Fuzzing (BUILD_FUZZ)
#=============================
# Clang only. Everything compiled from here on -- our libraries, the bundled
# secp256k1 and LevelDB -- gets coverage instrumentation (fuzzer-no-link) and
# ASan/UBSan; only the harnesses link libFuzzer's main (src/test/fuzz). Use a
# build directory of its own: every executable in it is sanitized.
set(DOBBSCOIN_FUZZ OFF)
if(BUILD_FUZZ)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(DOBBSCOIN_FUZZ ON)
    # -Wl,--exclude-libs,ALL (REDUCE_EXPORTS) would hide the sanitizer
    # runtime's malloc/operator new from libstdc++ and Boost, so memory they
    # allocate would be freed by ASan as "not malloc()-ed". _FORTIFY_SOURCE
    # (ENABLE_HARDENING) only gets in ASan's way. Neither matters here.
    set(REDUCE_EXPORTS OFF)
    set(ENABLE_HARDENING OFF)
    # UBSan reports are recoverable so that src/test/fuzz/ubsan.supp can
    # suppress known ones; run.sh sets UBSAN_OPTIONS=halt_on_error=1.
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  else()
    message(WARNING "BUILD_FUZZ needs clang (CC=clang CXX=clang++); "
                    "the compiler is ${CMAKE_CXX_COMPILER_ID}, so no fuzz harnesses are built.")
  endif()
endif()

include(cmake/hardening.cmake)
if(WIN32)
  # configure's LIBS: last on every link line, which is where CMake puts these.
  list(JOIN windows_system_libs " " CMAKE_C_STANDARD_LIBRARIES)
  set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_C_STANDARD_LIBRARIES}")
endif()

#=============================
# Platform introspection -> src/config/dobbscoin-config.h
#=============================
include(cmake/introspection.cmake)

#=============================
# Bundled libraries
#=============================
include(cmake/secp256k1.cmake)
add_secp256k1(src/secp256k1)
include(cmake/leveldb.cmake)

if(BUILD_TESTS)
  enable_testing()
endif()
add_subdirectory(src)
include(cmake/deploy.cmake) # the `deploy` target: Windows installer

#=============================
# Summary
#=============================
message("")
message("Configure summary")
message("=================")
message("Version ................................ ${CLIENT_VERSION_STRING} (release: ${CLIENT_VERSION_IS_RELEASE})")
message("Executables:")
message("  dobbscoind ........................... ${BUILD_DAEMON}")
message("  dobbscoin-qt (GUI) ................... ${BUILD_GUI}")
message("  dobbscoin-cli ........................ ${BUILD_CLI}")
message("  dobbscoin-tx ......................... ${BUILD_TX}")
message("  libdobbscoinconsensus ................ ${BUILD_CONSENSUS_LIB}")
message("Optional features:")
message("  wallet (SQLite) ...................... ${ENABLE_WALLET}")
message("  UPnP (miniupnpc) ..................... ${WITH_MINIUPNPC}")
message("  NAT-PMP .............................. ${WITH_NATPMP}")
message("  static port-mapping libs ............. ${ENABLE_STATIC_PORTMAP}")
if(BUILD_GUI)
  message("  QR codes ............................. ${WITH_QRENCODE}")
  message("  D-Bus notifications .................. ${WITH_DBUS}")
endif()
message("Tests:")
message("  test_dobbscoin ....................... ${BUILD_TESTS}")
message("  test_dobbscoin-qt .................... ${BUILD_GUI_TESTS}")
message("  fuzz harnesses (clang) ............... ${DOBBSCOIN_FUZZ}")
message("Hardening .............................. ${ENABLE_HARDENING}")
message("Reduce exports ......................... ${REDUCE_EXPORTS}")
message("Build type ............................. ${CMAKE_BUILD_TYPE}")
if(DEPENDS_PREFIX)
  message("depends ................................ ${DEPENDS_PREFIX}")
endif()
message("C++ compiler ........................... ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message("")
