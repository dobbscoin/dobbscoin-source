# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# The bundled LevelDB (src/leveldb) as two static libraries, leveldb and memenv.
#
# Autotools builds these with LevelDB's own Makefile and build_detect_platform,
# passing OPT="$(CXXFLAGS) $(CPPFLAGS)", i.e. our full flag set (and so, no
# NDEBUG). The source list is what build_detect_platform finds: every .cc under
# db/ util/ table/ except tests, benchmarks and leveldb_main.cc, plus one port file.

set(_ldb ${PROJECT_SOURCE_DIR}/src/leveldb)

add_library(leveldb STATIC EXCLUDE_FROM_ALL
  ${_ldb}/db/builder.cc
  ${_ldb}/db/c.cc
  ${_ldb}/db/db_impl.cc
  ${_ldb}/db/db_iter.cc
  ${_ldb}/db/dbformat.cc
  ${_ldb}/db/dumpfile.cc
  ${_ldb}/db/filename.cc
  ${_ldb}/db/log_reader.cc
  ${_ldb}/db/log_writer.cc
  ${_ldb}/db/memtable.cc
  ${_ldb}/db/repair.cc
  ${_ldb}/db/table_cache.cc
  ${_ldb}/db/version_edit.cc
  ${_ldb}/db/version_set.cc
  ${_ldb}/db/write_batch.cc
  ${_ldb}/table/block.cc
  ${_ldb}/table/block_builder.cc
  ${_ldb}/table/filter_block.cc
  ${_ldb}/table/format.cc
  ${_ldb}/table/iterator.cc
  ${_ldb}/table/merger.cc
  ${_ldb}/table/table.cc
  ${_ldb}/table/table_builder.cc
  ${_ldb}/table/two_level_iterator.cc
  ${_ldb}/util/arena.cc
  ${_ldb}/util/bloom.cc
  ${_ldb}/util/cache.cc
  ${_ldb}/util/coding.cc
  ${_ldb}/util/comparator.cc
  ${_ldb}/util/crc32c.cc
  ${_ldb}/util/env.cc
  ${_ldb}/util/env_posix.cc
  ${_ldb}/util/env_win.cc # whole file is #if defined(LEVELDB_PLATFORM_WINDOWS)
  ${_ldb}/util/filter_policy.cc
  ${_ldb}/util/hash.cc
  ${_ldb}/util/histogram.cc
  ${_ldb}/util/logging.cc
  ${_ldb}/util/options.cc
  ${_ldb}/util/status.cc
)
if(WIN32)
  target_sources(leveldb PRIVATE ${_ldb}/port/port_win.cc)
  target_compile_definitions(leveldb PRIVATE OS_WINDOWS LEVELDB_PLATFORM_WINDOWS WINVER=0x0500 __USE_MINGW_ANSI_STDIO=1 _REENTRANT)
else()
  target_sources(leveldb PRIVATE ${_ldb}/port/port_posix.cc)
  # build_detect_platform on Linux: -DOS_LINUX -DLEVELDB_PLATFORM_POSIX
  # -DLEVELDB_ATOMIC_PRESENT (and -std=c++0x, which our -std=c++14 overrides).
  target_compile_definitions(leveldb PRIVATE LEVELDB_PLATFORM_POSIX LEVELDB_ATOMIC_PRESENT)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(leveldb PRIVATE OS_LINUX)
  elseif(APPLE)
    target_compile_definitions(leveldb PRIVATE OS_MACOSX)
  endif()
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # build_detect_platform: use libc's memcmp instead of GCC's builtin.
  target_compile_options(leveldb PRIVATE -fno-builtin-memcmp)
endif()
target_include_directories(leveldb
  PRIVATE ${_ldb}
  PUBLIC ${_ldb}/include
)
target_link_libraries(leveldb PRIVATE dobbscoin_interface)
if(HAVE_MACRO_PREFIX_MAP)
  # LevelDB's Makefile compiles from src/leveldb, so its __FILE__ is "db/..."
  target_compile_options(leveldb PRIVATE -fmacro-prefix-map=${_ldb}/=)
endif()

add_library(memenv STATIC EXCLUDE_FROM_ALL ${_ldb}/helpers/memenv/memenv.cc)
target_include_directories(memenv PRIVATE ${_ldb} ${_ldb}/include PUBLIC ${_ldb}/helpers/memenv)
target_link_libraries(memenv PRIVATE dobbscoin_interface)
if(HAVE_MACRO_PREFIX_MAP)
  target_compile_options(memenv PRIVATE -fmacro-prefix-map=${_ldb}/=)
endif()
# LevelDB's Makefile builds memenv with the same platform flags as the library.
if(WIN32)
  target_compile_definitions(memenv PRIVATE OS_WINDOWS LEVELDB_PLATFORM_WINDOWS WINVER=0x0500 __USE_MINGW_ANSI_STDIO=1 _REENTRANT)
else()
  target_compile_definitions(memenv PRIVATE LEVELDB_PLATFORM_POSIX LEVELDB_ATOMIC_PRESENT)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(memenv PRIVATE OS_LINUX)
  endif()
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(memenv PRIVATE -fno-builtin-memcmp)
endif()

unset(_ldb)
