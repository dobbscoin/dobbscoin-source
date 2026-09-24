# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# `cmake --build build --target deploy`: the Windows NSIS installer, as the
# Autotools `make deploy` builds it (Makefile.am, $(DOBBSCOIN_WIN_INSTALLER)):
# strip dobbscoind, dobbscoin-cli and dobbscoin-qt into release/, then run
# makensis on share/setup.nsi. The result is
# build/dobbscoin-<major>.<minor>.<revision>-win64-setup.exe.

if(NOT (WIN32 AND BUILD_GUI AND BUILD_DAEMON AND BUILD_CLI))
  return()
endif()

find_program(MAKENSIS_EXECUTABLE makensis)
if(NOT MAKENSIS_EXECUTABLE)
  message(WARNING "makensis not found. Cannot create installer.") # configure's wording
  return()
endif()

# The variables configure substitutes into share/setup.nsi.in.
set(PACKAGE_NAME "Dobbscoin Core")
math(EXPR WINDOWS_BITS "${CMAKE_SIZEOF_VOID_P} * 8")
set(abs_top_srcdir ${PROJECT_SOURCE_DIR})
set(abs_top_builddir ${PROJECT_BINARY_DIR})
configure_file(${PROJECT_SOURCE_DIR}/share/setup.nsi.in ${PROJECT_BINARY_DIR}/share/setup.nsi @ONLY)

set(_installer ${PROJECT_BINARY_DIR}/dobbscoin-${CLIENT_VERSION_MAJOR}.${CLIENT_VERSION_MINOR}.${CLIENT_VERSION_REVISION}-win${WINDOWS_BITS}-setup.exe)
set(_release ${PROJECT_BINARY_DIR}/release)
add_custom_command(
  OUTPUT ${_installer}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${_release}
  COMMAND ${CMAKE_STRIP} -o ${_release}/$<TARGET_FILE_NAME:dobbscoind> $<TARGET_FILE:dobbscoind>
  COMMAND ${CMAKE_STRIP} -o ${_release}/$<TARGET_FILE_NAME:dobbscoin-qt> $<TARGET_FILE:dobbscoin-qt>
  COMMAND ${CMAKE_STRIP} -o ${_release}/$<TARGET_FILE_NAME:dobbscoin-cli> $<TARGET_FILE:dobbscoin-cli>
  COMMAND ${MAKENSIS_EXECUTABLE} ${PROJECT_BINARY_DIR}/share/setup.nsi
  DEPENDS dobbscoind dobbscoin-qt dobbscoin-cli ${PROJECT_BINARY_DIR}/share/setup.nsi
  COMMENT "Building the NSIS installer"
  VERBATIM
)
add_custom_target(deploy DEPENDS ${_installer})

unset(_installer)
unset(_release)
