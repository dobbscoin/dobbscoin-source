# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Turn a test data file into a C++ header holding it as a byte array, the job
# the hexdump/sed rules in src/Makefile.test.include do for Autotools:
#
#   namespace <NAMESPACE>{
#   static unsigned const char <basename>[] = {
#   0x7b, 0x0a, ...
#   };};
#
# Usage: cmake -DINPUT=<file> -DOUTPUT=<header> -DNAMESPACE=<ns> -P this-script

file(READ "${INPUT}" hex HEX)
get_filename_component(name "${INPUT}" NAME_WE)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " bytes "${hex}")
# Eight bytes per line, like the hexdump format.
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f], ){8})" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}"
  "namespace ${NAMESPACE}{\n"
  "static unsigned const char ${name}[] = {\n"
  "${bytes}\n"
  "};};\n"
)
