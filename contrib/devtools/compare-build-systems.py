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

Exit status 0 when everything matches. A transition tool for v0.14.0 (#43):
it goes when Autotools does.
"""
import hashlib
import os
import re
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


def sections(obj, kinds):
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
    return re.sub(rb'\0+', b'\0', FILE_PATH.sub(rb'\1', data))


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
    da, dc = sections(a, 'rodata|data|init_array'), sections(c, 'rodata|data|init_array')
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
    m = re.match(r'(?:src/)?(.*?)CMakeFiles/([^/]+)\.dir/(.*)\.(?:cpp|cc|c)\.o$', rel)
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
    return re.findall(r'\(NEEDED\).*\[(.*)\]', run('readelf', '-d', path))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    at_src = os.path.join(sys.argv[1], 'src')
    cm = sys.argv[2]
    cm_src = os.path.join(cm, 'src')
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
            if not f.endswith('.o'):
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

    print('== 3. NEEDED (shared libraries loaded, in order)')
    pairs = [(b, os.path.join(at_src, b), os.path.join(cm_src, b)) for b in (
        'dobbscoind', 'dobbscoin-cli', 'dobbscoin-tx', 'qt/dobbscoin-qt',
        'test/test_dobbscoin', 'qt/test/test_dobbscoin-qt')]
    pairs.append(('libdobbscoinconsensus.so.0.0.0',
                  os.path.join(at_src, '.libs/libdobbscoinconsensus.so.0.0.0'),
                  os.path.join(cm_src, 'libdobbscoinconsensus.so.0.0.0')))
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
    la = os.path.join(at_src, '.libs/libdobbscoinconsensus.so.0.0.0')
    lc = os.path.join(cm_src, 'libdobbscoinconsensus.so.0.0.0')
    if os.path.exists(la) and os.path.exists(lc):
        def exports(p):
            return sorted(' '.join(l.split()[1:]) for l in run('nm', '-D', '--defined-only', p).splitlines())
        ea, ec = exports(la), exports(lc)
        failures += ea != ec
        print(f'   exported symbols: {len(ec)}, {"identical" if ea == ec else "DIFFERENT"}')
        for p, label in ((la, 'Autotools'), (lc, 'CMake')):
            has = 'secp256k1_ecdsa_verify' in run('nm', p)
            failures += not has
            print(f'   {label:9} links libsecp256k1 in: {"yes" if has else "NO"}')

    print('== 5. version')
    for label, d in (('Autotools', at_src), ('CMake', cm_src)):
        out = subprocess.run([os.path.join(d, 'dobbscoind'), '-version'], capture_output=True, text=True).stdout
        print(f'   {label:9} {out.splitlines()[0] if out else "(no output)"}')

    print()
    print('EQUIVALENT' if failures == 0 else f'{failures} DIFFERENCES')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
