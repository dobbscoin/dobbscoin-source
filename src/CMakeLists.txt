# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# The libraries and executables of src/Makefile.am, target for target:
#
#   dobbscoin_crypto     crypto/libdobbscoin_crypto.a
#   dobbscoin_univalue   univalue/libdobbscoin_univalue.a
#   dobbscoin_util       libdobbscoin_util.a      (all executables)
#   dobbscoin_common     libdobbscoin_common.a    (daemon, GUI, tx)
#   dobbscoin_server     libdobbscoin_server.a    (daemon, GUI)
#   dobbscoin_wallet     libdobbscoin_wallet.a    (daemon, GUI; ENABLE_WALLET)
#   dobbscoin_cli        libdobbscoin_cli.a       (cli, GUI)
#   dobbscoinconsensus   libdobbscoinconsensus.so (shared, like libtool makes it)
#
# As in src/Makefile.am, the libraries only COMPILE against their dependencies
# (dobbscoin_includes below is Makefile.am's DOBBSCOIN_INCLUDES); the
# executables LINK them, each with an explicit list in Makefile.am's LDADD
# order. Letting static libraries carry link dependencies would make CMake
# reorder the shared libraries on the link line, and with them the NEEDED
# entries and the layout of every binary.
#
# Executables land where Autotools puts them (build/src/dobbscoind,
# build/src/test/test_dobbscoin, build/src/qt/dobbscoin-qt), so tools written
# against an Autotools tree, qa/run-all.sh among them, work on build/src too.

include(GNUInstallDirs)

# Generated headers go in the build tree: config/dobbscoin-config.h,
# obj/build.h, and (per subdirectory) Qt, protobuf and test data headers.
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/config   # crypto/common.h includes "dobbscoin-config.h"
  ${CMAKE_CURRENT_BINARY_DIR}/obj      # clientversion.cpp includes "build.h"
)

# __FILE__ relative to src/, as Autotools has it (see HAVE_MACRO_PREFIX_MAP).
# Generated sources (moc, uic, protoc) sit in the build tree; map that too, so
# no absolute path of the build machine ends up in a binary.
if(HAVE_MACRO_PREFIX_MAP)
  add_compile_options(
    -fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=
    -fmacro-prefix-map=${CMAKE_CURRENT_BINARY_DIR}/=
  )
endif()

# Compile against a library's headers, defines and compile options without
# linking it. (CMake 3.27 spells this $<COMPILE_ONLY:...>; we support 3.22.)
function(target_compile_against target)
  foreach(dep IN LISTS ARGN)
    target_include_directories(${target} PRIVATE $<TARGET_PROPERTY:${dep},INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(${target} PRIVATE $<TARGET_PROPERTY:${dep},INTERFACE_COMPILE_DEFINITIONS>)
    target_compile_options(${target} PRIVATE $<TARGET_PROPERTY:${dep},INTERFACE_COMPILE_OPTIONS>)
  endforeach()
endfunction()

# DOBBSCOIN_INCLUDES: what every library of ours except crypto and univalue
# compiles with. src/secp256k1/include is the bundled secp256k1 and comes
# before any system directory, so /usr/include/secp256k1.h (libsecp256k1-dev)
# is never used by mistake.
add_library(dobbscoin_includes INTERFACE)
target_include_directories(dobbscoin_includes INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/leveldb/include
  ${CMAKE_CURRENT_SOURCE_DIR}/leveldb/helpers/memenv
  ${CMAKE_CURRENT_SOURCE_DIR}/secp256k1/include
  ${OPENSSL_INCLUDE_DIR}
  $<$<BOOL:${ENABLE_WALLET}>:${SQLite3_INCLUDE_DIRS}>
)
target_compile_definitions(dobbscoin_includes INTERFACE SECP256K1_STATIC)
# configure's AX_BOOST_THREAD puts -pthread (-mthreads for mingw) into
# BOOST_CPPFLAGS, and so into DOBBSCOIN_INCLUDES (it defines _REENTRANT, _MT).
# Linking needs no flag on glibc >= 2.34, and Threads::Threads in
# dobbscoin_interface covers older ones; mingw links -lmingwthrd from LIBS.
if(WIN32)
  target_compile_options(dobbscoin_includes INTERFACE -mthreads)
else()
  target_compile_options(dobbscoin_includes INTERFACE -pthread)
endif()
target_link_libraries(dobbscoin_includes INTERFACE dobbscoin_interface Boost::headers) # headers only

#=============================
# obj/build.h: the git suffix in the version string (v0.13.8.0-7c315584)
#=============================
# The same script Autotools runs, run on every build like its FORCE rule. It
# rewrites build.h only when the commit or dirty state changed, so an unchanged
# tree does not recompile anything.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/obj)
add_custom_target(generate_build_info
  COMMAND sh ${PROJECT_SOURCE_DIR}/share/genbuild.sh ${CMAKE_CURRENT_BINARY_DIR}/obj/build.h ${PROJECT_SOURCE_DIR}
  BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/obj/build.h
  COMMENT "Generating obj/build.h"
  VERBATIM
)

#=============================
# Internal static libraries
#=============================
add_library(dobbscoin_crypto STATIC EXCLUDE_FROM_ALL
  crypto/sha1.cpp
  crypto/sha256.cpp
  crypto/scrypt.cpp
  crypto/sha512.cpp
  crypto/hmac_sha256.cpp
  crypto/rfc6979_hmac_sha256.cpp
  crypto/hmac_sha512.cpp
  crypto/ripemd160.cpp
)
target_link_libraries(dobbscoin_crypto PRIVATE dobbscoin_interface)

add_library(dobbscoin_univalue STATIC EXCLUDE_FROM_ALL
  univalue/univalue.cpp
  univalue/univalue_read.cpp
  univalue/univalue_write.cpp
)
target_link_libraries(dobbscoin_univalue PRIVATE dobbscoin_interface)

add_library(dobbscoin_util STATIC EXCLUDE_FROM_ALL
  compat/strnlen.cpp
  compat/glibc_sanity.cpp
  compat/glibcxx_sanity.cpp
  chainparamsbase.cpp
  clientversion.cpp
  random.cpp
  rpcprotocol.cpp
  sync.cpp
  uint256.cpp
  util.cpp
  utilstrencodings.cpp
  utilmoneystr.cpp
  utiltime.cpp
)
add_dependencies(dobbscoin_util generate_build_info)
target_link_libraries(dobbscoin_util PRIVATE dobbscoin_includes)

add_library(dobbscoin_common STATIC EXCLUDE_FROM_ALL
  allocators.cpp
  amount.cpp
  auxpow.cpp
  base58.cpp
  chainparams.cpp
  coins.cpp
  compressor.cpp
  primitives/block.cpp
  primitives/pureheader.cpp
  primitives/transaction.cpp
  core_read.cpp
  core_write.cpp
  eccryptoverify.cpp
  ecwrapper.cpp
  hash.cpp
  key.cpp
  keystore.cpp
  netbase.cpp
  protocol.cpp
  pubkey.cpp
  secp256k1/contrib/lax_der_parsing.c
  secp256k1/contrib/lax_der_privatekey_parsing.c
  script/interpreter.cpp
  script/script.cpp
  script/sign.cpp
  script/standard.cpp
  script/script_error.cpp
)
target_link_libraries(dobbscoin_common PRIVATE dobbscoin_includes)

add_library(dobbscoin_server STATIC EXCLUDE_FROM_ALL
  addrman.cpp
  alert.cpp
  bloom.cpp
  chain.cpp
  checkpoints.cpp
  init.cpp
  leveldbwrapper.cpp
  main.cpp
  merkleblock.cpp
  miner.cpp
  net.cpp
  noui.cpp
  pow.cpp
  rest.cpp
  rpc_auxpow.cpp
  rpcblockchain.cpp
  rpcmining.cpp
  rpcmisc.cpp
  rpcnet.cpp
  rpcrawtransaction.cpp
  rpcserver.cpp
  stratum.cpp
  script/sigcache.cpp
  timedata.cpp
  txdb.cpp
  txmempool.cpp
)
target_link_libraries(dobbscoin_server PRIVATE dobbscoin_includes)
# Only net.cpp uses these; as with configure's MINIUPNPC_CPPFLAGS, only this
# library compiles with their defines (STATICLIB for a static miniupnpc).
foreach(portmap IN ITEMS MiniUPnPc::MiniUPnPc NATPMP::NATPMP)
  if(TARGET ${portmap})
    target_compile_against(dobbscoin_server ${portmap})
  endif()
endforeach()
if(ENABLE_WALLET)
  add_library(dobbscoin_wallet STATIC EXCLUDE_FROM_ALL
    bdbro.cpp
    db.cpp
    crypter.cpp
    rpcdump.cpp
    rpcwallet.cpp
    wallet.cpp
    wallet_ismine.cpp
    walletdb.cpp
  )
  target_link_libraries(dobbscoin_wallet PRIVATE dobbscoin_includes)
endif()

add_library(dobbscoin_cli STATIC EXCLUDE_FROM_ALL rpcclient.cpp)
target_link_libraries(dobbscoin_cli PRIVATE dobbscoin_includes)

#=============================
# libdobbscoinconsensus
#=============================
# Built once as position-independent objects, then used twice: as the shared
# library that gets installed, and as a static archive for test_dobbscoin
# (Autotools links the test with libtool's -static, i.e. against the .a).
#
# It links the bundled secp256k1, which the Autotools build did not do until
# 82d7c9cd: pubkey.cpp calls into it unconditionally.
if(BUILD_CONSENSUS_LIB)
  add_library(dobbscoinconsensus_objects OBJECT
    primitives/transaction.cpp
    crypto/hmac_sha512.cpp
    crypto/sha1.cpp
    crypto/sha256.cpp
    crypto/scrypt.cpp
    crypto/sha512.cpp
    crypto/ripemd160.cpp
    eccryptoverify.cpp
    ecwrapper.cpp
    hash.cpp
    pubkey.cpp
    secp256k1/contrib/lax_der_parsing.c
    script/script.cpp
    script/interpreter.cpp
    script/dobbscoinconsensus.cpp
    uint256.cpp
    utilstrencodings.cpp
  )
  set_target_properties(dobbscoinconsensus_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(dobbscoinconsensus_objects PRIVATE
    BUILD_DOBBSCOIN_INTERNAL
    $<$<BOOL:${WIN32}>:DLL_EXPORT> # what libtool defines when it builds a DLL
  )
  # Its own flags in Makefile.am: the bundled secp256k1 and OpenSSL headers, no Boost.
  target_include_directories(dobbscoinconsensus_objects PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/secp256k1/include
    ${OPENSSL_INCLUDE_DIR}
  )
  target_compile_definitions(dobbscoinconsensus_objects PRIVATE SECP256K1_STATIC)
  target_link_libraries(dobbscoinconsensus_objects PRIVATE dobbscoin_interface)

  add_library(dobbscoinconsensus SHARED $<TARGET_OBJECTS:dobbscoinconsensus_objects>)
  # libtool names it libdobbscoinconsensus.so.0.0.0 with soname .so.0, and
  # libdobbscoinconsensus-0.dll (import library libdobbscoinconsensus.dll.a) on Windows.
  set_target_properties(dobbscoinconsensus PROPERTIES VERSION 0.0.0 SOVERSION 0)
  target_link_libraries(dobbscoinconsensus PRIVATE dobbscoin_interface secp256k1 OpenSSL::Crypto)
  if(WIN32)
    set_target_properties(dobbscoinconsensus PROPERTIES RUNTIME_OUTPUT_NAME dobbscoinconsensus-0)
    # configure's archive_cmds_CXX for mingw: a DLL that needs neither
    # libgcc_s nor libstdc++ DLLs at run time.
    target_link_options(dobbscoinconsensus PRIVATE -static-libgcc -static-libstdc++ -Wl,--enable-auto-image-base)
  endif()

  add_library(dobbscoinconsensus_static STATIC EXCLUDE_FROM_ALL $<TARGET_OBJECTS:dobbscoinconsensus_objects>)

  install(TARGETS dobbscoinconsensus
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(FILES script/dobbscoinconsensus.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

#=============================
# Executables
#=============================

# The port-mapping libraries, when compiled in (the targets exist only then).
set(portmap_libs
  $<TARGET_NAME_IF_EXISTS:MiniUPnPc::MiniUPnPc>
  $<TARGET_NAME_IF_EXISTS:NATPMP::NATPMP>
)
# Link order follows src/Makefile.am (dobbscoind_LDADD and friends).
set(node_libs
  dobbscoin_server
  dobbscoin_common
  dobbscoin_univalue
  dobbscoin_util
  dobbscoin_crypto
  leveldb
  memenv
  secp256k1
  $<$<BOOL:${ENABLE_WALLET}>:dobbscoin_wallet>
  ${boost_libs}
  $<$<BOOL:${ENABLE_WALLET}>:SQLite::SQLite3>
  OpenSSL::SSL
  OpenSSL::Crypto
  ${portmap_libs}
)

if(BUILD_DAEMON)
  add_executable(dobbscoind dobbscoind.cpp $<$<BOOL:${WIN32}>:dobbscoind-res.rc>)
  target_link_libraries(dobbscoind PRIVATE dobbscoin_includes ${node_libs})
  install(TARGETS dobbscoind RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(BUILD_CLI)
  add_executable(dobbscoin-cli dobbscoin-cli.cpp $<$<BOOL:${WIN32}>:dobbscoin-cli-res.rc>)
  target_link_libraries(dobbscoin-cli PRIVATE
    dobbscoin_includes
    dobbscoin_cli
    dobbscoin_util
    ${boost_libs}
    OpenSSL::SSL
    OpenSSL::Crypto
  )
  install(TARGETS dobbscoin-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(BUILD_TX)
  add_executable(dobbscoin-tx dobbscoin-tx.cpp)
  target_link_libraries(dobbscoin-tx PRIVATE
    dobbscoin_includes
    dobbscoin_univalue
    dobbscoin_common
    dobbscoin_util
    dobbscoin_crypto
    secp256k1
    ${boost_libs}
    OpenSSL::Crypto
  )
  install(TARGETS dobbscoin-tx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(BUILD_GUI)
  add_subdirectory(qt)
endif()

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

if(DOBBSCOIN_FUZZ)
  if(ENABLE_WALLET)
    add_subdirectory(test/fuzz)
  else()
    message(WARNING "BUILD_FUZZ: the harnesses fuzz the wallet files and need ENABLE_WALLET=ON; none are built.")
  endif()
endif()
