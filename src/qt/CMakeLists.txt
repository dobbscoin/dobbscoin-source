# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# dobbscoin-qt, as src/Makefile.qt.include builds it: a static libdobbscoinqt
# plus a small executable, with the generated sources handled like this:
#   headers with Q_OBJECT   -> moc_*.cpp, one per header (qt5_wrap_cpp)
#   foo.cpp with Q_OBJECT   -> foo.moc, #included by foo.cpp (qt5_generate_moc)
#   forms/*.ui              -> ui_*.h (qt5_wrap_ui)
#   *.qrc                   -> qrc_*.cpp (qt5_add_resources)
#   locale/*.ts             -> .qm (lrelease), compiled in via dobbscoin_locale.qrc
#   paymentrequest.proto    -> protoc
#
# No AUTOMOC/AUTOUIC, on purpose. AUTOUIC treats every "ui_*.h" include as a
# form to generate, and the tree has a real header called ui_interface.h.
# AUTOMOC compiles all moc output as ONE file, so linking any class pulls in
# every class; with explicit per-header moc files, as Autotools has them, the
# executables contain the same objects under both build systems. New Q_OBJECT
# headers go in moc_headers below (and in QT_MOC_CPP in Makefile.qt.include).

qt5_wrap_ui(ui_headers
  forms/addressbookpage.ui
  forms/askpassphrasedialog.ui
  forms/coincontroldialog.ui
  forms/editaddressdialog.ui
  forms/helpmessagedialog.ui
  forms/intro.ui
  forms/miningpage.ui
  forms/openuridialog.ui
  forms/optionsdialog.ui
  forms/overviewpage.ui
  forms/receivecoinsdialog.ui
  forms/receiverequestdialog.ui
  forms/rpcconsole.ui
  forms/sendcoinsdialog.ui
  forms/sendcoinsentry.ui
  forms/signverifymessagedialog.ui
  forms/transactiondescdialog.ui
)

protobuf_generate_cpp(proto_sources proto_headers paymentrequest.proto)

set(moc_headers
  addressbookpage.h
  addresstablemodel.h
  askpassphrasedialog.h
  dobbscoinaddressvalidator.h
  dobbscoinamountfield.h
  dobbscoingui.h
  dobbscoinunits.h
  clientmodel.h
  coincontroldialog.h
  coincontroltreewidget.h
  csvmodelwriter.h
  editaddressdialog.h
  guiutil.h
  intro.h
  macdockiconhandler.h
  macnotificationhandler.h
  miningpage.h
  notificator.h
  openuridialog.h
  optionsdialog.h
  optionsmodel.h
  overviewpage.h
  peertablemodel.h
  paymentserver.h
  qvalidatedlineedit.h
  qvaluecombobox.h
  receivecoinsdialog.h
  receiverequestdialog.h
  recentrequeststablemodel.h
  rpcconsole.h
  sendcoinsdialog.h
  sendcoinsentry.h
  signverifymessagedialog.h
  splashscreen.h
  trafficgraphwidget.h
  transactiondesc.h
  transactiondescdialog.h
  transactionfilterproxy.h
  transactiontablemodel.h
  transactionview.h
  utilitydialog.h
  walletframe.h
  walletmodel.h
  walletview.h
)

# Translations. dobbscoin_locale.qrc names locale/<lang>.qm relative to itself,
# so the .qm files and a copy of the .qrc sit side by side in the build tree.
file(GLOB ts_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/locale/*.ts)
set_source_files_properties(${ts_files} PROPERTIES OUTPUT_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/locale)
qt5_add_translation(qm_files ${ts_files})
configure_file(dobbscoin_locale.qrc ${CMAKE_CURRENT_BINARY_DIR}/dobbscoin_locale.qrc COPYONLY)
qt5_add_resources(qrc_sources
  dobbscoin.qrc
  ${CMAKE_CURRENT_BINARY_DIR}/dobbscoin_locale.qrc
)

add_library(dobbscoinqt STATIC EXCLUDE_FROM_ALL
  ${ui_headers}
  ${CMAKE_CURRENT_BINARY_DIR}/dobbscoinamountfield.moc
  ${CMAKE_CURRENT_BINARY_DIR}/intro.moc
  ${CMAKE_CURRENT_BINARY_DIR}/overviewpage.moc
  ${CMAKE_CURRENT_BINARY_DIR}/rpcconsole.moc
  ${proto_sources}
  ${proto_headers}
  ${qm_files}
  ${qrc_sources}
  dobbscoinaddressvalidator.cpp
  dobbscoinamountfield.cpp
  dobbscoingui.cpp
  dobbscoinunits.cpp
  clientmodel.cpp
  csvmodelwriter.cpp
  guiutil.cpp
  intro.cpp
  networkstyle.cpp
  notificator.cpp
  optionsdialog.cpp
  optionsmodel.cpp
  peertablemodel.cpp
  qvalidatedlineedit.cpp
  qvaluecombobox.cpp
  rpcconsole.cpp
  splashscreen.cpp
  trafficgraphwidget.cpp
  utilitydialog.cpp
  winshutdownmonitor.cpp
)
if(ENABLE_WALLET)
  target_sources(dobbscoinqt PRIVATE
    addressbookpage.cpp
    addresstablemodel.cpp
    askpassphrasedialog.cpp
    coincontroldialog.cpp
    coincontroltreewidget.cpp
    editaddressdialog.cpp
    miningpage.cpp
    openuridialog.cpp
    overviewpage.cpp
    paymentrequestplus.cpp
    paymentserver.cpp
    receivecoinsdialog.cpp
    receiverequestdialog.cpp
    recentrequeststablemodel.cpp
    sendcoinsdialog.cpp
    sendcoinsentry.cpp
    signverifymessagedialog.cpp
    transactiondesc.cpp
    transactiondescdialog.cpp
    transactionfilterproxy.cpp
    transactionrecord.cpp
    transactiontablemodel.cpp
    transactionview.cpp
    walletframe.cpp
    walletmodel.cpp
    walletmodeltransaction.cpp
    walletview.cpp
  )
endif()

target_include_directories(dobbscoinqt PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/forms
  ${CMAKE_CURRENT_BINARY_DIR}   # ui_*.h, *.moc, paymentrequest.pb.h
)
# Qt, protobuf and qrencode headers, not their libraries: see src/CMakeLists.txt.
set(qt_compile_deps Qt5::Widgets Qt5::Network protobuf::libprotobuf)
if(WITH_DBUS)
  list(APPEND qt_compile_deps Qt5::DBus)
endif()
if(WITH_QRENCODE)
  list(APPEND qt_compile_deps PkgConfig::libqrencode)
endif()
target_link_libraries(dobbscoinqt PRIVATE dobbscoin_includes)
target_compile_against(dobbscoinqt ${qt_compile_deps})

qt5_wrap_cpp(moc_sources ${moc_headers} TARGET dobbscoinqt)
target_sources(dobbscoinqt PRIVATE ${moc_sources})
foreach(f IN ITEMS dobbscoinamountfield intro overviewpage rpcconsole)
  qt5_generate_moc(${f}.cpp ${CMAKE_CURRENT_BINARY_DIR}/${f}.moc TARGET dobbscoinqt)
endforeach()

# Link order follows qt_dobbscoin_qt_LDADD.
set(qt_node_libs
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
  Qt5::Network Qt5::Widgets Qt5::Gui
  $<$<BOOL:${WITH_DBUS}>:Qt5::DBus>
  Qt5::Core
  ${libqrencode_LINK_LIBRARIES}
  protobuf::libprotobuf
  $<$<BOOL:${ENABLE_WALLET}>:SQLite::SQLite3>
  OpenSSL::SSL
  OpenSSL::Crypto
  ${portmap_libs}
  secp256k1
)

qt5_generate_moc(dobbscoin.cpp ${CMAKE_CURRENT_BINARY_DIR}/dobbscoin.moc TARGET dobbscoin-qt)
add_executable(dobbscoin-qt dobbscoin.cpp ${CMAKE_CURRENT_BINARY_DIR}/dobbscoin.moc)
if(WIN32)
  target_sources(dobbscoin-qt PRIVATE res/dobbscoin-qt-res.rc)
  set_target_properties(dobbscoin-qt PROPERTIES WIN32_EXECUTABLE TRUE) # -mwindows
elseif(APPLE)
  target_sources(dobbscoin-qt PRIVATE macdockiconhandler.mm macnotificationhandler.mm)
endif()
target_link_libraries(dobbscoin-qt PRIVATE dobbscoin_includes ${qt_node_libs})
if(QT_STATICPLUGIN)
  # What configure links: the platform plugin (and what its .prl lists), none
  # of the other plugins a static Qt would pick by default (qminimal,
  # qdirect2d, the Vista style).
  qt5_import_plugins(dobbscoin-qt
    INCLUDE ${qt_platform_plugin}
    EXCLUDE_BY_TYPE platforms styles imageformats iconengines bearer
  )
endif()
install(TARGETS dobbscoin-qt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_GUI_TESTS)
  add_subdirectory(test)
  if(QT_STATICPLUGIN)
    # test_main.cpp only needs a QCoreApplication and imports no plugin, and
    # configure links none into it.
    qt5_import_plugins(test_dobbscoin-qt EXCLUDE_BY_TYPE platforms styles imageformats iconengines bearer)
  endif()
endif()
