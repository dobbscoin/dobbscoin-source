# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# test_dobbscoin, as src/Makefile.test.include builds it.

# Test vectors compiled in as byte arrays: data/foo.json -> data/foo.json.h
# (namespace json_tests) and data/foo.raw -> data/foo.raw.h (namespace alert_tests).
set(json_test_files
  script_valid.json
  base58_keys_valid.json
  sig_canonical.json
  sig_noncanonical.json
  base58_encode_decode.json
  base58_keys_invalid.json
  script_invalid.json
  tx_invalid.json
  tx_valid.json
  sighash.json
)
set(raw_test_files alertTests.raw bdb_fixture.raw sqlite_index_mismatch.raw)

set(generated_test_headers)
foreach(f IN LISTS json_test_files raw_test_files)
  if(f MATCHES "\\.json$")
    set(ns json_tests)
  else()
    set(ns alert_tests)
  endif()
  set(out ${CMAKE_CURRENT_BINARY_DIR}/data/${f}.h)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/data/${f} -DOUTPUT=${out} -DNAMESPACE=${ns}
            -P ${PROJECT_SOURCE_DIR}/cmake/script/GenerateHeaderFromBinary.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data/${f} ${PROJECT_SOURCE_DIR}/cmake/script/GenerateHeaderFromBinary.cmake
    COMMENT "Generating test/data/${f}.h"
    VERBATIM
  )
  list(APPEND generated_test_headers ${out})
endforeach()

add_executable(test_dobbscoin
  ${generated_test_headers}
  alert_tests.cpp
  allocator_tests.cpp
  auxpow_tests.cpp
  base32_tests.cpp
  base58_tests.cpp
  base64_tests.cpp
  bloom_tests.cpp
  checkblock_tests.cpp
  Checkpoints_tests.cpp
  coins_tests.cpp
  compress_tests.cpp
  crypto_tests.cpp
  DoS_tests.cpp
  getarg_tests.cpp
  hash_tests.cpp
  key_tests.cpp
  main_tests.cpp
  mempool_tests.cpp
  miner_tests.cpp
  mruset_tests.cpp
  multisig_tests.cpp
  netbase_tests.cpp
  pmt_tests.cpp
  pow_tests.cpp
  lwma3_tests.cpp
  cltv_tests.cpp
  emergency_diff_tests.cpp
  rpc_tests.cpp
  sanity_tests.cpp
  script_P2SH_tests.cpp
  script_tests.cpp
  scriptnum_tests.cpp
  serialize_tests.cpp
  sighash_tests.cpp
  sigopcount_tests.cpp
  skiplist_tests.cpp
  test_dobbscoin.cpp
  timedata_tests.cpp
  transaction_tests.cpp
  uint256_tests.cpp
  univalue_tests.cpp
  util_tests.cpp
)
if(ENABLE_WALLET)
  target_sources(test_dobbscoin PRIVATE
    accounting_tests.cpp
    wallet_sqlite_tests.cpp
    wallet_tests.cpp
    rpc_wallet_tests.cpp
  )
endif()

target_include_directories(test_dobbscoin PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
if(BOOST_TEST_DYN_LINK)
  target_compile_definitions(test_dobbscoin PRIVATE BOOST_TEST_DYN_LINK)
endif()

# Link order follows test_test_dobbscoin_LDADD.
target_link_libraries(test_dobbscoin PRIVATE
  dobbscoin_includes
  dobbscoin_server
  dobbscoin_cli
  dobbscoin_common
  dobbscoin_util
  dobbscoin_crypto
  dobbscoin_univalue
  leveldb
  memenv
  ${boost_libs}
  Boost::unit_test_framework
  secp256k1
  $<$<BOOL:${ENABLE_WALLET}>:dobbscoin_wallet>
  $<$<BOOL:${BUILD_CONSENSUS_LIB}>:dobbscoinconsensus_static>
  $<$<BOOL:${ENABLE_WALLET}>:SQLite::SQLite3>
  OpenSSL::SSL
  OpenSSL::Crypto
  ${portmap_libs}
)

# `ctest` runs the unit tests; qa/run-all.sh runs them plus everything else.
add_test(NAME test_dobbscoin COMMAND test_dobbscoin)
