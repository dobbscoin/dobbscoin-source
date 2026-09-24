Building with CMake
===================

Dobbscoin Core is moving from Autotools to CMake (issue #43). v0.14.0 ships
**both** build systems so their output can be compared; the Autotools files
(`autogen.sh`, `configure.ac`, `Makefile.am`, `src/Makefile*.am`, `build-aux/`)
are removed in the release after that. Nothing about consensus or behaviour
depends on which one you use.

The layout follows Bitcoin Core v29's CMake build, cut down to what this tree has.

Linux build
-----------

Packages (Debian 12 / Ubuntu 22.04 and later):

    sudo apt-get install build-essential cmake pkg-config python3 \
        libboost-system-dev libboost-filesystem-dev libboost-program-options-dev \
        libboost-thread-dev libboost-chrono-dev libboost-test-dev \
        libssl-dev libsqlite3-dev libminiupnpc-dev libnatpmp-dev

For the GUI, add:

    sudo apt-get install qtbase5-dev qttools5-dev qttools5-dev-tools \
        libprotobuf-dev protobuf-compiler libqrencode-dev

Build and test:

    cmake -B build                  # add -DBUILD_GUI=ON for dobbscoin-qt
    cmake --build build -j4
    ctest --test-dir build          # unit tests (test_dobbscoin, test_dobbscoin-qt)
    qa/run-all.sh build/src         # every suite: unit, chain harnesses, functional

Build in a directory of its own (`build/` above). CMake refuses to configure a
source tree that still holds an in-tree Autotools build (`src/config/dobbscoin-config.h`),
because those generated headers would shadow its own; `make distclean` clears them.

The binaries land where Autotools puts them, relative to the build directory:
`build/src/dobbscoind`, `build/src/dobbscoin-cli`, `build/src/dobbscoin-tx`,
`build/src/qt/dobbscoin-qt`, `build/src/test/test_dobbscoin`,
`build/src/libdobbscoinconsensus.so`. Anything written against an Autotools tree,
`qa/run-all.sh` included, works on `build/src`.

`cmake --install build` installs the executables, `libdobbscoinconsensus` and its header.

A release build of the Linux tarballs is:

    cmake -B build -DBUILD_GUI=ON -DENABLE_STATIC_PORTMAP=ON

Options
-------

Pass with `-D<option>=ON|OFF`. The last column is the Autotools spelling.

| Option | Default | What it does | configure |
|---|---|---|---|
| `BUILD_DAEMON` | ON | Build `dobbscoind` | `--with-daemon` |
| `BUILD_CLI` | ON | Build `dobbscoin-cli` | `--with-utils` (cli and tx together) |
| `BUILD_TX` | ON | Build `dobbscoin-tx` | `--with-utils` |
| `BUILD_CONSENSUS_LIB` | ON | Build the shared `libdobbscoinconsensus` | `--with-libs` |
| `BUILD_GUI` | **OFF** | Build `dobbscoin-qt` (Qt 5) | `--with-gui` (on if Qt is found) |
| `BUILD_TESTS` | ON | Build `test_dobbscoin` | `--enable-tests` |
| `BUILD_GUI_TESTS` | ON with GUI and tests | Build `test_dobbscoin-qt` | on when Qt Test is found |
| `ENABLE_WALLET` | ON | The wallet (needs SQLite) | `--enable-wallet` |
| `WITH_MINIUPNPC` | ON | UPnP port mapping | `--with-miniupnpc` (on if found) |
| `WITH_NATPMP` | ON | NAT-PMP port mapping | `--with-natpmp` (on if found) |
| `ENABLE_UPNP_DEFAULT` | OFF | Turn UPnP on at startup | `--enable-upnp-default` |
| `ENABLE_NATPMP_DEFAULT` | OFF | Turn NAT-PMP on at startup | `--enable-natpmp-default` |
| `ENABLE_STATIC_PORTMAP` | OFF | Link `libminiupnpc.a`/`libnatpmp.a` so a published binary needs neither installed (#45) | `--enable-static-portmap` |
| `WITH_QRENCODE` | ON with GUI | QR codes in the GUI | `--with-qrencode` |
| `WITH_DBUS` | ON with GUI (Linux) | D-Bus desktop notifications | `--with-qtdbus` |
| `ENABLE_HARDENING` | ON | Stack protector, `_FORTIFY_SOURCE=2`, `-z relro -z now` | `--enable-hardening` |
| `REDUCE_EXPORTS` | ON | `-fvisibility=hidden`, `--exclude-libs,ALL` | `--enable-reduce-exports` |
| `WITH_CCACHE` | ON | Use ccache if it is installed | `--enable-ccache` |
| `CMAKE_BUILD_TYPE` | RelWithDebInfo | `-g -O2`, as configure's default; `Debug` = `-g3 -O0 -DDEBUG` | `--enable-debug` |

Two deliberate differences from configure: the GUI is opt-in rather than
built whenever Qt happens to be installed, and UPnP/NAT-PMP are on by default and
fail loudly when their library is missing instead of silently dropping out. Both
make a build say what it contains instead of depending on what the machine has.

No build type ever defines `NDEBUG`: the code relies on `assert()`, and
configure never defines it either.

Not ported, because nothing uses them: `--enable-glibc-back-compat`, `--enable-lcov`,
`--with-comparison-tool`, and the macOS DMG packaging. The Windows installer
(`make deploy`) is the `deploy` target; see Cross-compiling below.

How the two builds are kept equivalent
--------------------------------------

During the transition, a change to one build system needs the same change in the other.

* **Version.** `CLIENT_VERSION_*`, `CLIENT_VERSION_IS_RELEASE` and `COPYRIGHT_YEAR`
  have one home: the `define(_CLIENT_VERSION_...)` lines at the top of `configure.ac`.
  `cmake/ClientVersion.cmake` reads them from there, so the two cannot drift.
  Bump the version in `configure.ac` only.
* **Git suffix** (`v0.13.8.0-7c315584`). Both run `share/genbuild.sh` to write `obj/build.h`.
* **Config header.** `cmake/dobbscoin-config.h.in` defines every macro the sources
  test, to the same values configure gives them; it leaves out Autotools
  bookkeeping nothing reads (`PACKAGE_*`, `HAVE_STDIO_H`, `LT_OBJDIR`, ...).
* **Flags.** `-std=c++14`, `-g -O2`, the warning set, hardening and reduced exports
  mirror configure's, including its quirk of adding the CXX-only ones to C++ but not C.
* **Libraries.** The same internal static libraries (`src/CMakeLists.txt` maps them
  to their `src/Makefile.am` names). As in Makefile.am, libraries only compile
  against their dependencies and each executable links an explicit list in LDADD
  order, so the NEEDED entries come out the same and in the same order.
* **__FILE__.** CMake passes absolute paths; `-fmacro-prefix-map` turns them back into
  the src/-relative names Autotools uses, so assert messages match and the builder's
  directory stays out of the binaries.
* **secp256k1.** The bundled `src/secp256k1` is built by its own CMake, with the
  modules configure enables. `src/secp256k1/include` is on the include path of every
  target of ours (`dobbscoin_includes`, Makefile.am's `DOBBSCOIN_INCLUDES`), so a
  system `libsecp256k1-dev` header is never picked up, and `libdobbscoinconsensus`
  links the bundled library.

To check a pair of builds of the same commit (Autotools in-tree, CMake in `build/`):

    contrib/devtools/compare-build-systems.py /path/to/autotools-tree build

It compares the config headers, the code of every object file both builds
compile, the NEEDED lists, libdobbscoinconsensus' exports, and the version string.

Cross-compiling
---------------

As in Bitcoin Core v29: `make -C depends HOST=<host>` builds the dependencies
and writes `depends/<host>/toolchain.cmake` beside the `share/config.site` that
configure uses. The toolchain file names the cross compilers, confines package
searches to the depends prefix, and turns `BUILD_GUI`, `ENABLE_WALLET` and
`WITH_MINIUPNPC`/`WITH_NATPMP` on or off to match what depends built
(`NO_QT`, `NO_WALLET`, `NO_UPNP`; a `-D` on the command line still wins).

Windows (64-bit), from Debian 12 / Ubuntu 22.04 with `g++-mingw-w64-x86-64-posix`
selected as the default `x86_64-w64-mingw32-g++` (see build-windows.md) and `nsis`
for the installer:

    make -C depends HOST=x86_64-w64-mingw32 -j4
    cmake -B build-win --toolchain depends/x86_64-w64-mingw32/toolchain.cmake
    cmake --build build-win -j4
    cmake --build build-win --target deploy    # the NSIS installer

That builds `build-win/src/dobbscoind.exe`, `dobbscoin-cli.exe`, `dobbscoin-tx.exe`,
`libdobbscoinconsensus-0.dll` (import library `libdobbscoinconsensus.dll.a`),
`build-win/src/qt/dobbscoin-qt.exe`, `build-win/src/test/test_dobbscoin.exe` and
`build-win/src/qt/test/test_dobbscoin-qt.exe`, and with `deploy`
`build-win/dobbscoin-<major>.<minor>.<revision>-win64-setup.exe`, as `make deploy`
does (stripped copies of the three programs in `build-win/release/`, then
`makensis` on `build-win/share/setup.nsi`). `ENABLE_STATIC_PORTMAP` makes no
difference on Windows, where everything is linked statically anyway.

What the Windows build does that the native one does not, each mirroring
configure.ac's `*mingw*` case:

* `-D_MT -DWIN32 -D_WINDOWS -DBOOST_THREAD_USE_LIB -D_FILE_OFFSET_BITS=64`, and
  `-mthreads` in place of `-pthread`.
* Every executable is linked `-static` (libtool's `-all-static`), and every
  link ends with configure's `LIBS`: `-lssp -lcrypt32 -liphlpapi -lshlwapi
  -lmswsock -lws2_32 ... -lkernel32 -lmingwthrd` (set as
  `CMAKE_<LANG>_STANDARD_LIBRARIES`). The executables import nothing but
  Windows system DLLs.
* The consensus DLL additionally gets `-static-libgcc -static-libstdc++
  -Wl,--enable-auto-image-base`, libtool's name `libdobbscoinconsensus-0.dll`,
  and `DLL_EXPORT`.
* The `.rc` version resources are compiled by windres with the target's defines
  and `-DWINDRES_PREPROC`, so they carry the real version (they used to say
  0.13.0.0 under Autotools too, which now passes its `DEFS`/`CPPFLAGS` to windres).
* Boost is the static `-mt-s` build and only the five libraries configure links:
  Boost's own CMake package would add libboost_atomic and `synchronization`,
  and with it an import of `api-ms-win-core-synch-l1-2-0.dll`.
* The static Qt comes from Qt's CMake package (depends installs it, with the
  `.prl` files that list what each Qt library needs after it). Only the
  `qwindows` platform plugin is linked, into `dobbscoin-qt.exe`, where
  `dobbscoin.cpp` imports it; Qt's default plugin set is switched off.

With a depends toolchain the compile flags are config.site's (`-pipe -O2`)
without the `-g -O2` of the build type, and without the warning flags, because
configure adds neither when CXXFLAGS is given, which config.site always does.
`-DCMAKE_CXX_FLAGS=-g` puts debug info back.

`contrib/devtools/compare-build-systems.py` checks a Windows pair the same way
as a native one (COFF sections, the PE import and export tables, the version
resources, and `dobbscoind.exe -version` under wine when it is installed).

macOS (`depends` for `x86_64-apple-darwin`) has not been tried with CMake.
