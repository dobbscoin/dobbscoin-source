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
find_package(Qt5 5.5 REQUIRED COMPONENTS ${_qt_components})
message(STATUS "Found Qt ${Qt5_VERSION}")
set(USE_DBUS ${WITH_DBUS})

# Qt's CMake package adds QT_NO_DEBUG to every non-Debug build, which turns
# Q_ASSERT off. configure (pkg-config) never passes it; keep the two builds alike.
set_property(TARGET Qt5::Core PROPERTY INTERFACE_COMPILE_DEFINITIONS QT_CORE_LIB)

# A static Qt (the depends build) needs its platform plugin linked in and
# imported by name; dobbscoin.cpp does the Q_IMPORT_PLUGIN when these are set.
# Linking the plugins themselves is left to the depends toolchain step.
get_target_property(_qt_core_type Qt5::Core TYPE)
if(_qt_core_type STREQUAL "STATIC_LIBRARY")
  set(QT_STATICPLUGIN 1)
  if(WIN32)
    set(QT_QPA_PLATFORM_WINDOWS 1)
  elseif(APPLE)
    set(QT_QPA_PLATFORM_COCOA 1)
  else()
    set(QT_QPA_PLATFORM_XCB 1)
  endif()
endif()

find_package(Protobuf REQUIRED)

if(WITH_QRENCODE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(libqrencode REQUIRED IMPORTED_TARGET libqrencode)
  set(USE_QRCODE 1)
endif()

unset(_qt_components)
unset(_qt_core_type)
