# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Finds libnatpmp and defines the imported target NATPMP::NATPMP.
#
# With ENABLE_STATIC_PORTMAP it insists on libnatpmp.a, as configure's
# --enable-static-portmap does (#45). On Windows natpmp_declspec.h resolves to
# __declspec(dllimport) unless told the library is static.

find_path(NATPMP_INCLUDE_DIR NAMES natpmp.h)

# Separate cache entries, so flipping ENABLE_STATIC_PORTMAP in an existing
# build directory really switches libraries.
if(ENABLE_STATIC_PORTMAP AND NOT WIN32)
  find_library(NATPMP_STATIC_LIBRARY NAMES libnatpmp.a)
  set(NATPMP_LIBRARY ${NATPMP_STATIC_LIBRARY})
else()
  find_library(NATPMP_LIBRARY NAMES natpmp)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NATPMP
  REQUIRED_VARS NATPMP_LIBRARY NATPMP_INCLUDE_DIR
)

if(NATPMP_FOUND AND NOT TARGET NATPMP::NATPMP)
  add_library(NATPMP::NATPMP UNKNOWN IMPORTED)
  set_target_properties(NATPMP::NATPMP PROPERTIES
    IMPORTED_LOCATION "${NATPMP_LIBRARY}"
  )
  set_property(TARGET NATPMP::NATPMP PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${NATPMP_INCLUDE_DIR}")
  if(WIN32)
    set_property(TARGET NATPMP::NATPMP PROPERTY INTERFACE_COMPILE_DEFINITIONS STATICLIB NATPMP_STATICLIB)
    set_property(TARGET NATPMP::NATPMP PROPERTY INTERFACE_LINK_LIBRARIES iphlpapi ws2_32)
  endif()
endif()

mark_as_advanced(NATPMP_INCLUDE_DIR NATPMP_LIBRARY NATPMP_STATIC_LIBRARY)
