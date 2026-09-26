Release Process
===============

(BOB) releases are built from `main` with CMake, the way [BUILDING.md](../BUILDING.md)
describes it (part B makes the Windows installer), and published as releases on Forgejo:

<https://git.subgenius.finance/SubGeniusFinance/dobbscoin-source/releases>

Before tagging a release:

* set the version in `configure.ac` (`_CLIENT_VERSION_*`, `_CLIENT_VERSION_IS_RELEASE`
  and `_COPYRIGHT_YEAR`); CMake reads it from there too;
* write the release notes in `doc/release-notes.md`;
* run every test: `qa/run-all.sh build/src` must end with `failed 0`.

Bitcoin Core's gitian process, which this file used to describe, was never
used for (BOB) and has been removed.
