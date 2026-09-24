# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Boost. The same libraries configure links (BOOST_LIBS in configure.ac), so
# the executables end up with the same NEEDED list either way.

set(_boost_components system filesystem program_options thread chrono)
if(BUILD_TESTS)
  list(APPEND _boost_components unit_test_framework)
endif()
# 1.55 is what depends/ has shipped; older ones have no working sleep_for.
find_package(Boost 1.55 REQUIRED CONFIG COMPONENTS ${_boost_components})
mark_as_advanced(Boost_DIR)

# Boost's CMake config attaches BOOST_ALL_NO_LIB and BOOST_<LIB>_DYN_LINK to its
# targets. Those only matter to MSVC auto-linking and Windows DLL imports, and
# configure never passes them, so drop them to keep the compile lines of the two
# build systems identical.
get_property(_imported DIRECTORY PROPERTY IMPORTED_TARGETS)
foreach(_t IN LISTS _imported)
  if(_t MATCHES "^Boost::")   # includes dependencies pulled in, e.g. Boost::atomic
    set_property(TARGET ${_t} PROPERTY INTERFACE_COMPILE_DEFINITIONS "")
  endif()
endforeach()

# BOOST_LIBS, in configure's order. The executables link these by name; the
# libraries only use Boost::headers.
set(boost_libs Boost::system Boost::filesystem Boost::program_options Boost::thread Boost::chrono)
# configure: "Determine if -DBOOST_TEST_DYN_LINK is needed". It is when the unit
# test framework is a shared library, which a distro Boost is.
if(BUILD_TESTS)
  get_target_property(_utf_type Boost::unit_test_framework TYPE)
  if(_utf_type STREQUAL "SHARED_LIBRARY")
    set(BOOST_TEST_DYN_LINK ON)
  else()
    set(BOOST_TEST_DYN_LINK OFF)
  endif()
endif()

# configure checks for a working boost::this_thread::sleep_for (broken in Boost
# 1.50-1.52). Every Boost we accept has it.
set(HAVE_WORKING_BOOST_SLEEP_FOR 1)

unset(_boost_components)
unset(_utf_type)
unset(_imported)
