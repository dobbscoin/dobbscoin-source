# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Qt 5 and the GUI's other dependencies (protobuf for BIP70 payment requests,
# libqrencode). What configure does in build-aux/m4/dobbscoin_qt.m4.

set(_qt_components Core Gui Network Widgets LinguistTools)
if(WITH_DBUS)
  list(APPEND _qt_components DBus)
endif()
if(BUILD_GUI_TESTS)
  list(APPEND _qt_components Test)
endif()
# A static Qt's CMake package reads the .prl files for what each Qt library
# needs linked after it, and looks each system library up with find_library.
# In a cross build those (libws2_32.a and co.) are in the compiler's own lib
# directory, outside the depends prefix the toolchain file confines searches to.
set(_saved_mode ${CMAKE_FIND_ROOT_PATH_MODE_LIBRARY})
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
find_package(Qt5 5.5 REQUIRED COMPONENTS ${_qt_components})
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ${_saved_mode})
message(STATUS "Found Qt ${Qt5_VERSION}")
set(USE_DBUS ${WITH_DBUS})

# Qt's CMake package adds QT_NO_DEBUG to every non-Debug build, which turns
# Q_ASSERT off. configure (pkg-config) never passes it; keep the two builds alike.
set_property(TARGET Qt5::Core PROPERTY INTERFACE_COMPILE_DEFINITIONS QT_CORE_LIB)

# A static Qt (the depends build) needs its platform plugin linked in and
# imported by name; dobbscoin.cpp does the Q_IMPORT_PLUGIN when these are set.
# src/qt/CMakeLists.txt links the plugin (qt_platform_plugin) into dobbscoin-qt
# and keeps Qt's automatic plugin selection out of every executable.
get_target_property(_qt_core_type Qt5::Core TYPE)
if(_qt_core_type STREQUAL "STATIC_LIBRARY")
  set(QT_STATICPLUGIN 1)
  if(WIN32)
    set(QT_QPA_PLATFORM_WINDOWS 1)
    set(qt_platform_plugin Qt5::QWindowsIntegrationPlugin)
  elseif(APPLE)
    set(QT_QPA_PLATFORM_COCOA 1)
    set(qt_platform_plugin Qt5::QCocoaIntegrationPlugin)
  else()
    set(QT_QPA_PLATFORM_XCB 1)
    set(qt_platform_plugin Qt5::QXcbIntegrationPlugin)
  endif()
  # Qt's package would also compile in a Q_IMPORT_PLUGIN of its own for the
  # plugin (Qt5Gui_*_Import.cpp); dobbscoin.cpp already has one.
  set_property(TARGET ${qt_platform_plugin} PROPERTY INTERFACE_SOURCES "")
endif()

find_package(Protobuf REQUIRED)

if(WITH_QRENCODE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(libqrencode REQUIRED IMPORTED_TARGET libqrencode)
  set(USE_QRCODE 1)
endif()

unset(_qt_components)
unset(_qt_core_type)
unset(_saved_mode)
