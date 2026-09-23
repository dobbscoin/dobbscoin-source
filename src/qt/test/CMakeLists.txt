# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# test_dobbscoin-qt, as src/Makefile.qttest.include builds it.

set(test_moc_headers uritests.h)
if(ENABLE_WALLET)
  list(APPEND test_moc_headers paymentservertests.h)
endif()
qt5_wrap_cpp(test_moc_sources ${test_moc_headers} TARGET test_dobbscoin-qt)

add_executable(test_dobbscoin-qt
  test_main.cpp
  uritests.cpp
  $<$<BOOL:${ENABLE_WALLET}>:paymentservertests.cpp>
  ${test_moc_sources}
)
# Link order follows qt_test_test_dobbscoin_qt_LDADD.
target_link_libraries(test_dobbscoin-qt PRIVATE
  dobbscoin_includes
  dobbscoinqt
  dobbscoin_server
  $<$<BOOL:${ENABLE_WALLET}>:dobbscoin_wallet>
  dobbscoin_cli
  dobbscoin_common
  dobbscoin_util
  dobbscoin_crypto
  dobbscoin_univalue
  leveldb
  memenv
  ${boost_libs}
  $<$<BOOL:${WITH_DBUS}>:Qt5::DBus>
  Qt5::Test
  Qt5::Network Qt5::Widgets Qt5::Gui Qt5::Core
  ${libqrencode_LINK_LIBRARIES}
  protobuf::libprotobuf
  $<$<BOOL:${ENABLE_WALLET}>:SQLite::SQLite3>
  OpenSSL::SSL
  OpenSSL::Crypto
  ${portmap_libs}
  secp256k1
)

add_test(NAME test_dobbscoin-qt COMMAND test_dobbscoin-qt)
# Runs headless, like `make check` does on a box without a display.
set_tests_properties(test_dobbscoin-qt PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
