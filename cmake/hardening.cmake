# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Hardening and reduced exports, matching what configure adds by default
# (--enable-hardening and --enable-reduce-exports are both on by default).
# configure appends these to CXXFLAGS/CPPFLAGS/LDFLAGS; CFLAGS never gets the
# CXX-only ones, and neither does the bundled secp256k1.

include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)

# Add a flag to dobbscoin_interface only if the toolchain accepts it, the same
# test-then-add that configure's AX_CHECK_*_FLAG macros do.
function(dobbscoin_try_compile_flag flag)
  string(MAKE_C_IDENTIFIER "CXX_HAS${flag}" var)
  # Compile only, like AX_CHECK_COMPILE_FLAG: linking -fstack-protector-all on
  # mingw needs libssp, which is only added to the link line further down.
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  check_cxx_compiler_flag("${flag}" ${var})
  if(${var})
    target_compile_options(dobbscoin_interface INTERFACE $<$<COMPILE_LANGUAGE:CXX>:${flag}>)
  endif()
endfunction()
function(dobbscoin_try_link_flag flag)
  string(MAKE_C_IDENTIFIER "LD_HAS${flag}" var)
  check_linker_flag(CXX "${flag}" ${var})
  if(${var})
    target_link_options(dobbscoin_interface INTERFACE ${flag})
  endif()
endfunction()

if(ENABLE_HARDENING)
  dobbscoin_try_compile_flag(-Wstack-protector)
  dobbscoin_try_compile_flag(-fstack-protector-all)
  if(NOT WIN32)
    # C and C++, like configure's HARDENED_CPPFLAGS. -U first so a toolchain
    # that already defines _FORTIFY_SOURCE does not warn about a redefinition.
    target_compile_options(dobbscoin_interface INTERFACE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2)
  endif()
  dobbscoin_try_link_flag(-Wl,--dynamicbase)
  dobbscoin_try_link_flag(-Wl,--nxcompat)
  dobbscoin_try_link_flag(-Wl,-z,relro)
  dobbscoin_try_link_flag(-Wl,-z,now)
  if(NOT WIN32)
    # All Windows code is PIC already.
    dobbscoin_try_compile_flag(-fPIC)
  endif()
  if(WIN32)
    # MinGW's stack protector lives in libssp; configure puts it first in LIBS.
    list(PREPEND windows_system_libs -lssp)
  endif()
endif()

# Not a hardening flag in configure, but tested and added the same way, to
# every link. Only Windows linkers know it (a 32-bit executable may then use
# more than 2 GB); elsewhere the test fails and nothing is added.
dobbscoin_try_link_flag(-Wl,--large-address-aware)

if(REDUCE_EXPORTS)
  dobbscoin_try_compile_flag(-fvisibility=hidden)
  # configure puts this in RELDFLAGS, which every executable and
  # libdobbscoinconsensus link with: it keeps symbols of the static libraries
  # we link in (secp256k1 above all) out of the exported symbol table.
  check_linker_flag(CXX "-Wl,--exclude-libs,ALL" LD_HAS_EXCLUDE_LIBS)
  if(LD_HAS_EXCLUDE_LIBS)
    target_link_options(dobbscoin_interface INTERFACE -Wl,--exclude-libs,ALL)
  endif()
endif()
