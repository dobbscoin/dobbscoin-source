(BOB) Dobbscoin documentation
=============================

Setup
---------------------
Get the wallet from <https://dobbscoin.info>. It downloads and checks the whole (BOB) block chain on first start, about 1.5 GB; how long that takes depends on your computer and connection. You only do it once.

Running
---------------------

### Windows

Run the `setup.exe` installer, then start Dobbscoin Core from the Start menu. Your wallet and the block chain live in `%APPDATA%\Dobbscoin`.

### Linux

Make the `.AppImage` executable and run it, or build from source (below). The data directory is `~/.dobbscoin`.

### Need Help?

* Website: <https://dobbscoin.info>
* Report a bug: <https://git.subgenius.finance/SubGeniusFinance/dobbscoin-source/issues>

Building
---------------------
Start with [BUILDING.md](../BUILDING.md) in the repository root. More detailed notes:

- [CMake Build Notes](build-cmake.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OSX Build Notes](build-osx.md)

Development
---------------------
The source lives at <https://git.subgenius.finance/SubGeniusFinance/dobbscoin-source>; its [root README](../README.md) describes the project.

- [Coding Guidelines](coding.md)
- [Multiwallet Qt Development](multiwallet-qt.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- [Translation Process](translation_process.md)
- [Unit Tests](unit-tests.md)
- [Fuzzing](fuzzing.md)

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)

License
---------------------
Distributed under the [MIT software license](http://www.opensource.org/licenses/mit-license.php); see [COPYING](../COPYING) for the third-party notices.
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
