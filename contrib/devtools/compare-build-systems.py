#!/usr/bin/env python3
# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Check that the Autotools and CMake builds of one commit are equivalent.

    contrib/devtools/compare-build-systems.py <autotools-tree> <cmake-build-dir>

<autotools-tree> is a source tree built in place (./autogen.sh && ./configure &&
make); <cmake-build-dir> is a `cmake -B <dir>` build of the same commit, with
the same options (e.g. both with the GUI, or both without). It compares:

  1. config header: every macro the sources test, same value in both
  2. object code: code (and data, up to __FILE__ strings) of every object both compile
  3. NEEDED: the shared libraries each executable and libdobbscoinconsensus load
  4. libdobbscoinconsensus: exported symbols, and that libsecp256k1 is linked in
  5. version: the first line of `dobbscoind -version`

Windows cross builds (depends/x86_64-w64-mingw32, found by the .exe files) are
compared the same way with the mingw binutils: COFF sections for 2, the PE
import table (DLL names) for 3, the PE export table for 4, and for 5 the
version resource plus `dobbscoind.exe -version` run under wine (set WINEPREFIX
as needed; skipped when wine is missing).

Exit status 0 when everything matches. A transition tool for v0.14.0 (#43):
it goes when Autotools does.
"""
import hashlib
import os
import re
import struct
import subprocess
import sys


def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True).stdout


def header_macros(path):
    macros = {}
    for line in open(path):
        m = re.match(r'\s*#\s*define\s+(\w+)(?:\s+(.*?))?\s*(?:/\*.*)?$', line)
        if m:
            macros[m.group(1)] = (m.group(2) or '').strip()
    return macros


def tested_macro_names(template):
    names = set()
    for line in open(template):
        m = re.match(r'#(?:define|cmakedefine)\s+(\w+)', line) or re.match(r'@(\w+)_LINE@', line)
        if m:
            names.add(m.group(1))
    names.discard('DOBBSCOIN_CONFIG_H')
    return sorted(names)


MINGW = 'x86_64-w64-mingw32-'
PE = False  # set in main() for a Windows build


def coff_sections(obj, kinds):
    """Contents of a COFF object's sections, read directly: g++ gives every
    inline function a COMDAT section of its own (.text$name), thousands per
    object, far too many to extract one objcopy call at a time.

    COFF keeps relocation addends in the section bytes, where ELF (RELA) keeps
    them in the relocation entries. They are zeroed here, so that, as with ELF,
    a string moving within .rdata (a shorter __FILE__ before it) does not show
    up as a change in the code that refers to it."""
    data = open(obj, 'rb').read()
    nsec, symtab, nsym = struct.unpack_from('<2xH4xII', data, 0)
    strtab = symtab + 18 * nsym
    width = {0: 0, 1: 8, 0xA: 2}  # IMAGE_REL_AMD64_ABSOLUTE, ADDR64, SECTION; the rest are 4 bytes
    result = {}
    for i in range(nsec):
        raw_name, size, ptr, relptr, nrel = struct.unpack_from('<8s8xIII4xH', data, 20 + 40 * i)
        name = raw_name.rstrip(b'\0').decode()
        if name.startswith('/'):  # long name, in the string table
            start = strtab + int(name[1:])
            name = data[start:data.index(b'\0', start)].decode()
        if re.match(r'\.(?:%s)' % kinds, name):
            key = name
            while key in result:  # the same name can occur more than once
                key += '+'
            contents = bytearray(data[ptr:ptr + size] if ptr else b'')
            for r in range(nrel):
                offset, _, kind = struct.unpack_from('<IIH', data, relptr + 10 * r)
                n = width.get(kind, 4)
                contents[offset:offset + n] = bytes(n)
            result[key] = bytes(contents)
    return result


def sections(obj, kinds):
    if PE:
        # COFF puts read-only data in .rdata, which 'rodata' is taken to mean.
        return coff_sections(obj, kinds.replace('rodata', 'rdata'))
    names = re.findall(r'\]\s+(\.(?:%s)\S*)\s+PROGBITS' % kinds, run('readelf', '-SW', obj))
    return {n: subprocess.run(['objcopy', '-O', 'binary', '--only-section=' + n, obj, '/dev/stdout'],
                              capture_output=True).stdout for n in names}


# A source file name as it appears in __FILE__ strings, with its directories.
# Autotools and CMake find some headers by different paths ("./allocators.h"
# through -I. versus "allocators.h"; "./qt/forms/ui_intro.h" versus
# "qt/ui_intro.h" for generated ones), which changes assert and Qt log strings
# and nothing else.
FILE_PATH = re.compile(rb'[\w./-]*/([\w.-]+\.(?:h|cpp|cc|c))\b')


def without_file_names(data):
    # Shorter names also change the NUL padding between aligned strings.
    return re.sub(rb'\0+', b'\0', FILE_PATH.sub(rb'\1', data)).rstrip(b'\0')


def compare_object(a, c):
    """'same', 'file-names', 'rcc-timestamps' or 'DIFFERENT'.

    Relocated fields are zero in an object file, so this compares what the
    compiler generated, not where the linker will put it. Code must match
    exactly. Data may differ only in source file names (see FILE_PATH), or, in
    the qrc_*.o files rcc generates, in the modification times rcc records for
    each resource file (build- and checkout-time, so two builds with the same
    build system differ there too)."""
    if sections(a, 'text') != sections(c, 'text'):
        return 'DIFFERENT'
    kinds = 'rodata|data|ctors' if PE else 'rodata|data|init_array'
    da, dc = sections(a, kinds), sections(c, kinds)
    if da == dc:
        return 'same'
    if da.keys() == dc.keys() and all(without_file_names(da[k]) == without_file_names(dc[k]) for k in da):
        return 'file-names'
    if os.path.basename(a).split('-')[-1].startswith('qrc_'):
        return 'rcc-timestamps'
    return 'DIFFERENT'


# CMake object dir -> (Autotools object name prefix). The Autotools object for
# CMakeFiles/<target>.dir/<dir>/<name>.cpp.o is <dir>/<prefix><name>.o.
PREFIX = {
    'dobbscoin_crypto': 'libdobbscoin_crypto_a-', 'dobbscoin_univalue': '',
    'dobbscoin_util': 'libdobbscoin_util_a-', 'dobbscoin_common': 'libdobbscoin_common_a-',
    'dobbscoin_server': 'libdobbscoin_server_a-', 'dobbscoin_wallet': 'libdobbscoin_wallet_a-',
    'dobbscoin_cli': 'libdobbscoin_cli_a-', 'dobbscoind': 'dobbscoind-',
    'dobbscoin-cli': 'dobbscoin_cli-', 'dobbscoin-tx': 'dobbscoin_tx-',
    'dobbscoinconsensus_objects': '.libs/libdobbscoinconsensus_la-',
    'test_dobbscoin': 'test_dobbscoin-', 'dobbscoinqt': 'libdobbscoinqt_a-',
    'dobbscoin-qt': 'dobbscoin_qt-', 'test_dobbscoin-qt': 'test_dobbscoin_qt-',
    'leveldb': '', 'memenv': '',
    'secp256k1': 'libsecp256k1_la-', 'secp256k1_precomputed': 'libsecp256k1_precomputed_la-',
}


def autotools_twin(at_src, cm_build, obj):
    rel = os.path.relpath(obj, cm_build)
    m = re.match(r'(?:src/)?(.*?)CMakeFiles/([^/]+)\.dir/(.*)\.(?:cpp|cc|c)\.obj?$', rel)
    if not m or m.group(2) not in PREFIX:
        return None
    subdir, target, source = m.groups()
    if target in ('leveldb', 'memenv'):
        return os.path.join(at_src, source.replace('src/', '', 1) + '.o')
    directory, name = os.path.split(source)
    prefix = PREFIX[target]
    if prefix.startswith('.libs/'):
        return os.path.join(at_src, subdir, directory, '.libs', prefix[6:] + name + '.o')
    return os.path.join(at_src, subdir, directory, prefix + name + '.o')


def needed(path):
    if PE:
        return re.findall(r'DLL Name: (\S+)', run(MINGW + 'objdump', '-p', path))
    return re.findall(r'\(NEEDED\).*\[(.*)\]', run('readelf', '-d', path))


def exports(path):
    if PE:
        # The [Ordinal/Name Pointer] Table of objdump -p: "\t[   0] name".
        table = run(MINGW + 'objdump', '-p', path).split('[Ordinal/Name Pointer] Table')[-1]
        return sorted(re.findall(r'^\s+\[\s*\d+\] (\S+)$', table, re.M))
    return sorted(' '.join(l.split()[1:]) for l in run('nm', '-D', '--defined-only', path).splitlines())


def version_resource(path):
    # FileVersion/ProductVersion/LegalCopyright of a PE VERSIONINFO (UTF-16 strings).
    strings = run('strings', '-el', path).splitlines()
    return {k: strings[i + 1] for i, k in enumerate(strings[:-1])
            if k in ('FileVersion', 'ProductVersion', 'LegalCopyright')}


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    global PE
    at_src = os.path.join(sys.argv[1], 'src')
    cm = sys.argv[2]
    cm_src = os.path.join(cm, 'src')
    PE = os.path.exists(os.path.join(cm_src, 'dobbscoind.exe'))
    exe = '.exe' if PE else ''
    lib_at, lib_cm = (('.libs/libdobbscoinconsensus-0.dll', 'libdobbscoinconsensus-0.dll') if PE else
                      ('.libs/libdobbscoinconsensus.so.0.0.0', 'libdobbscoinconsensus.so.0.0.0'))
    template = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../cmake/dobbscoin-config.h.in')
    failures = 0

    print('== 1. config header (macros the sources test)')
    a = header_macros(os.path.join(at_src, 'config/dobbscoin-config.h'))
    c = header_macros(os.path.join(cm_src, 'config/dobbscoin-config.h'))
    names = tested_macro_names(template)
    for n in names:
        va, vc = a.get(n, '(undefined)'), c.get(n, '(undefined)')
        mark = 'same' if va == vc else 'DIFFERENT'
        failures += va != vc
        print(f'   {n:34} {va:12} {vc:12} {mark}')
    print(f'   {len(names)} macros, {sum(a.get(n) != c.get(n) for n in names)} different')

    print('== 2. object code (code and data of every object both builds compile)')
    result = {'same': [], 'file-names': [], 'rcc-timestamps': [], 'DIFFERENT': []}
    unmatched = []
    for root, _, files in os.walk(cm):
        for f in files:
            if not f.endswith(('.o', '.obj')):
                continue
            obj = os.path.join(root, f)
            twin = autotools_twin(at_src, cm, obj)
            if twin is None:
                continue
            if not os.path.exists(twin):
                unmatched.append(os.path.relpath(obj, cm))
            else:
                result[compare_object(twin, obj)].append(os.path.relpath(obj, cm))
    total = sum(len(v) for v in result.values())
    print(f'   {total} objects compared: {len(result["same"])} identical code and data, '
          f'{len(result["file-names"])} identical code with data differing only in source file names '
          f'(__FILE__), {len(result["rcc-timestamps"])} Qt resource files with identical code whose data '
          f'differs (rcc records file times), {len(result["DIFFERENT"])} different; '
          f'{len(unmatched)} with no Autotools twin')
    for d in sorted(result['DIFFERENT']):
        print('   DIFFERENT', d)
    for u in sorted(unmatched):
        print('   no twin  ', u)
    failures += len(result['DIFFERENT'])

    print('== 3. NEEDED (shared libraries loaded, in order)' if not PE else
          '== 3. DLL imports (PE import table, in order)')
    pairs = [(b + exe, os.path.join(at_src, b + exe), os.path.join(cm_src, b + exe)) for b in (
        'dobbscoind', 'dobbscoin-cli', 'dobbscoin-tx', 'qt/dobbscoin-qt',
        'test/test_dobbscoin', 'qt/test/test_dobbscoin-qt')]
    pairs.append((lib_cm, os.path.join(at_src, lib_at), os.path.join(cm_src, lib_cm)))
    for name, pa, pc in pairs:
        if not (os.path.exists(pa) and os.path.exists(pc)):
            print(f'   {name:32} (not built by both)')
            continue
        na, nc = needed(pa), needed(pc)
        if na == nc:
            verdict = 'identical, same order'
        elif sorted(na) == sorted(nc):
            verdict = 'same libraries, DIFFERENT order'
        else:
            verdict = 'DIFFERENT'
            failures += 1
        print(f'   {name:32} {verdict}: {" ".join(nc)}')

    print('== 4. libdobbscoinconsensus')
    la = os.path.join(at_src, lib_at)
    lc = os.path.join(cm_src, lib_cm)
    if os.path.exists(la) and os.path.exists(lc):
        ea, ec = exports(la), exports(lc)
        failures += ea != ec
        print(f'   exported symbols: {len(ec)}, {"identical" if ea == ec else "DIFFERENT"}'
              + (f': {" ".join(ec)}' if PE else ''))
        for p, label in ((la, 'Autotools'), (lc, 'CMake')):
            has = 'secp256k1_ecdsa_verify' in run((MINGW if PE else '') + 'nm', p)
            failures += not has
            print(f'   {label:9} links libsecp256k1 in: {"yes" if has else "NO"}')

    print('== 5. version')
    if PE:
        for b in ('dobbscoind', 'dobbscoin-cli', 'qt/dobbscoin-qt'):
            pa, pc = os.path.join(at_src, b + exe), os.path.join(cm_src, b + exe)
            if os.path.exists(pa) and os.path.exists(pc):
                va, vc = version_resource(pa), version_resource(pc)
                failures += va != vc
                print(f'   {b + exe:20} version resource {"identical" if va == vc else "DIFFERENT"}: {vc}')
    wine = ['wine'] if PE else []
    if not PE or subprocess.run(['sh', '-c', 'command -v wine'], capture_output=True).returncode == 0:
        env = dict(os.environ, WINEDEBUG='-all')
        for label, d in (('Autotools', at_src), ('CMake', cm_src)):
            out = subprocess.run(wine + [os.path.join(d, 'dobbscoind' + exe), '-version'],
                                 capture_output=True, text=True, env=env).stdout.replace('\r', '')
            print(f'   {label:9} {out.splitlines()[0] if out else "(no output)"}')

    print()
    print('EQUIVALENT' if failures == 0 else f'{failures} DIFFERENCES')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
