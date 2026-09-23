# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Read CLIENT_VERSION_* and COPYRIGHT_YEAR from configure.ac, so the version
# lives in exactly one place while both build systems exist. Bump the numbers
# there; CMake picks them up on its next configure run.
#
# When Autotools is removed, replace this file with plain set() calls in the
# top-level CMakeLists.txt (Core v29 keeps its version there).

set(_version_source ${CMAKE_CURRENT_LIST_DIR}/../configure.ac)
# Re-run CMake's configure step whenever configure.ac changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_version_source})
file(STRINGS ${_version_source} _version_lines
  REGEX "^define\\(_(CLIENT_VERSION_[A-Z_]+|COPYRIGHT_YEAR), *[a-z0-9]+\\)")

foreach(name IN ITEMS CLIENT_VERSION_MAJOR CLIENT_VERSION_MINOR CLIENT_VERSION_REVISION
                      CLIENT_VERSION_BUILD CLIENT_VERSION_IS_RELEASE COPYRIGHT_YEAR)
  set(${name} "")
  foreach(line IN LISTS _version_lines)
    if(line MATCHES "^define\\(_${name}, *([a-z0-9]+)\\)")
      set(${name} ${CMAKE_MATCH_1})
    endif()
  endforeach()
  if(${name} STREQUAL "")
    message(FATAL_ERROR "Could not read _${name} from ${_version_source}")
  endif()
endforeach()

if(NOT CLIENT_VERSION_IS_RELEASE MATCHES "^(true|false)$")
  message(FATAL_ERROR "_CLIENT_VERSION_IS_RELEASE in configure.ac must be true or false")
endif()

set(CLIENT_VERSION_STRING
  "${CLIENT_VERSION_MAJOR}.${CLIENT_VERSION_MINOR}.${CLIENT_VERSION_REVISION}.${CLIENT_VERSION_BUILD}")
unset(_version_lines)
unset(_version_source)
