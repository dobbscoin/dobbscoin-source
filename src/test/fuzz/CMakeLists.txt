# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# libFuzzer harnesses for the wallet file readers. Only added when the
# top-level BUILD_FUZZ is on and the compiler is clang; see doc/fuzzing.md.

set(fuzz_targets
  bdbro_parse
  bdbro_salvage
  wallet_migrate
  wallet_sqlite
  wallet_recover
  wallet_load
)

foreach(target IN LISTS fuzz_targets)
  add_executable(fuzz_${target} ${target}.cpp fuzz_util.cpp)
  target_include_directories(fuzz_${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  # dobbscoin_wallet first: the harnesses call into it, and it needs server/common.
  target_link_libraries(fuzz_${target} PRIVATE dobbscoin_includes dobbscoin_wallet ${node_libs})
  target_link_options(fuzz_${target} PRIVATE -fsanitize=fuzzer)
  set_target_properties(fuzz_${target} PROPERTIES OUTPUT_NAME ${target})
endforeach()

# Writes the SQLite seeds (and the wallet_load seeds) from the checked-in
# Berkeley DB fixtures. A plain program, not a fuzzer: see doc/fuzzing.md.
add_executable(fuzz_seedgen seedgen.cpp fuzz_util.cpp)
target_include_directories(fuzz_seedgen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fuzz_seedgen PRIVATE dobbscoin_includes dobbscoin_wallet ${node_libs})
