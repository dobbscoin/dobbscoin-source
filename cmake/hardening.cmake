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
    # MinGW's stack protector lives in libssp.
    target_link_libraries(dobbscoin_interface INTERFACE ssp)
  endif()
endif()

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
