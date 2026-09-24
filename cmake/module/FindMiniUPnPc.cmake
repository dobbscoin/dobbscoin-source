# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Finds libminiupnpc and defines the imported target MiniUPnPc::MiniUPnPc.
#
# With ENABLE_STATIC_PORTMAP it insists on libminiupnpc.a and adds the defines a
# static miniupnpc needs, as configure's --enable-static-portmap does. On
# Windows the depends build is static anyway, so the defines are always set.

find_path(MiniUPnPc_INCLUDE_DIR NAMES miniupnpc/miniupnpc.h)

# Separate cache entries, so flipping ENABLE_STATIC_PORTMAP in an existing
# build directory really switches libraries.
if(ENABLE_STATIC_PORTMAP AND NOT WIN32)
  find_library(MiniUPnPc_STATIC_LIBRARY NAMES libminiupnpc.a)
  set(MiniUPnPc_LIBRARY ${MiniUPnPc_STATIC_LIBRARY})
else()
  find_library(MiniUPnPc_LIBRARY NAMES miniupnpc)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MiniUPnPc
  REQUIRED_VARS MiniUPnPc_LIBRARY MiniUPnPc_INCLUDE_DIR
)

if(MiniUPnPc_FOUND AND NOT TARGET MiniUPnPc::MiniUPnPc)
  add_library(MiniUPnPc::MiniUPnPc UNKNOWN IMPORTED)
  set_target_properties(MiniUPnPc::MiniUPnPc PROPERTIES
    IMPORTED_LOCATION "${MiniUPnPc_LIBRARY}"
  )
  # CMake leaves system directories such as /usr/include off the command line.
  set_property(TARGET MiniUPnPc::MiniUPnPc PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${MiniUPnPc_INCLUDE_DIR}")
  if(WIN32 OR ENABLE_STATIC_PORTMAP)
    set_property(TARGET MiniUPnPc::MiniUPnPc PROPERTY
      INTERFACE_COMPILE_DEFINITIONS STATICLIB MINIUPNP_STATICLIB)
  endif()
  if(WIN32)
    set_property(TARGET MiniUPnPc::MiniUPnPc PROPERTY INTERFACE_LINK_LIBRARIES iphlpapi ws2_32)
  endif()
endif()

mark_as_advanced(MiniUPnPc_INCLUDE_DIR MiniUPnPc_LIBRARY MiniUPnPc_STATIC_LIBRARY)
