# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# The platform checks behind src/config/dobbscoin-config.h. Only the macros the
# sources actually test are here (audit: git grep for HAVE_/USE_/ENABLE_ plus
# STRERROR_R_CHAR_P, FDELT_TYPE, QT_*), each checked the way configure.ac does.

include(CheckIncludeFileCXX)
include(CheckSymbolExists)
include(CheckFunctionExists)
include(CheckLibraryExists)
include(CheckCXXSourceCompiles)

check_include_file_cxx(endian.h HAVE_ENDIAN_H)
check_include_file_cxx(sys/prctl.h HAVE_SYS_PRCTL_H)
check_include_file_cxx(sys/select.h HAVE_SYS_SELECT_H)

# AC_CHECK_DECLS: always defined, to 1 or 0. crypto/common.h tests "== 1".
set(_endian_header "")
if(HAVE_ENDIAN_H)
  set(_endian_header endian.h)
endif()
foreach(fn IN ITEMS le32toh le64toh htole32 htole64 be32toh be64toh htobe32 htobe64)
  string(TOUPPER ${fn} FN)
  if(_endian_header)
    check_symbol_exists(${fn} ${_endian_header} HAVE_DECL_${FN})
  endif()
  if(HAVE_DECL_${FN})
    set(HAVE_DECL_${FN} 1)
  else()
    set(HAVE_DECL_${FN} 0)
  endif()
endforeach()

# AC_SEARCH_LIBS: a link test, first without any library, then with -lanl.
# (glibc >= 2.34 has getaddrinfo_a in libc itself.)
check_function_exists(getaddrinfo_a HAVE_GETADDRINFO_A_IN_LIBC)
if(HAVE_GETADDRINFO_A_IN_LIBC)
  set(HAVE_GETADDRINFO_A 1)
else()
  check_library_exists(anl getaddrinfo_a "" HAVE_GETADDRINFO_A_IN_ANL)
  if(HAVE_GETADDRINFO_A_IN_ANL)
    set(HAVE_GETADDRINFO_A 1)
    target_link_libraries(dobbscoin_interface INTERFACE anl)
  endif()
endif()
# On Windows inet_pton is in ws2_32, which configure's LIBS already carries
# when AC_SEARCH_LIBS tries it.
if(WIN32)
  set(CMAKE_REQUIRED_LIBRARIES ws2_32)
endif()
check_function_exists(inet_pton HAVE_INET_PTON)
unset(CMAKE_REQUIRED_LIBRARIES)

check_cxx_source_compiles("
  #include <sys/socket.h>
  int main() { int f = MSG_NOSIGNAL; (void)f; return 0; }
" HAVE_MSG_NOSIGNAL)

# AC_FUNC_STRERROR_R: is strerror_r the GNU variant (returns char*)?
check_cxx_source_compiles("
  #include <string.h>
  int main() { char buf[100]; char* p = strerror_r(0, buf, sizeof buf); return !p; }
" STRERROR_R_CHAR_P)

# AX_GCC_FUNC_ATTRIBUTE: an attribute counts only if it compiles without a
# warning, since GCC merely warns about attributes it ignores.
set(CMAKE_REQUIRED_FLAGS -Werror)
check_cxx_source_compiles("
  int foo(void) __attribute__((visibility(\"default\")));
  int foo(void) { return 0; }
  int main() { return foo(); }
" HAVE_FUNC_ATTRIBUTE_VISIBILITY)
check_cxx_source_compiles("
  __declspec(dllexport) int foo(void);
  int foo(void) { return 0; }
  int main() { return foo(); }
" HAVE_FUNC_ATTRIBUTE_DLLEXPORT)
unset(CMAKE_REQUIRED_FLAGS)

# UPnP / NAT-PMP: left undefined when not compiled in; otherwise the value (0
# or 1) is the startup default. A 0 is not "false" here, so #cmakedefine cannot
# express it and the whole line is built instead.
set(USE_UPNP_LINE "/* #undef USE_UPNP */")
if(WITH_MINIUPNPC)
  if(ENABLE_UPNP_DEFAULT)
    set(USE_UPNP_LINE "#define USE_UPNP 1")
  else()
    set(USE_UPNP_LINE "#define USE_UPNP 0")
  endif()
endif()
set(USE_NATPMP_LINE "/* #undef USE_NATPMP */")
if(WITH_NATPMP)
  if(ENABLE_NATPMP_DEFAULT)
    set(USE_NATPMP_LINE "#define USE_NATPMP 1")
  else()
    set(USE_NATPMP_LINE "#define USE_NATPMP 0")
  endif()
endif()

set(HAVE_CONSENSUS_LIB ${BUILD_CONSENSUS_LIB})

configure_file(${PROJECT_SOURCE_DIR}/cmake/dobbscoin-config.h.in
               ${PROJECT_BINARY_DIR}/src/config/dobbscoin-config.h @ONLY)

unset(_endian_header)
