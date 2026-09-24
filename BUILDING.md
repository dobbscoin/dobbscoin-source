# How to build (BOB)

**Most people don't need to build anything.** If you just want the wallet, download it:

→ **<https://dobbscoin.info/downloads>**

Windows: run the `setup.exe`. Linux: use the `.AppImage` (make it executable, double-click it).

Build it yourself only if you want to check the code, change it, or make the release files.

---

## What you need

- A computer running **Ubuntu 22.04 or newer**, or **Debian 12 or newer**.
  On Windows you can use **WSL** (Windows Subsystem for Linux) with Ubuntu. See the last section.
- About **10 GB** of free disk space.
- An internet connection.

Every command below goes in a terminal. Copy each grey box, paste it, press Enter, and wait until it finishes before the next one.

---

## A. Build the wallet for Linux

**1. Install the tools.** It will ask for your password.

```bash
sudo apt-get update
sudo apt-get install -y git build-essential cmake pkg-config python3 \
    libboost-system-dev libboost-filesystem-dev libboost-program-options-dev \
    libboost-thread-dev libboost-chrono-dev libboost-test-dev \
    libssl-dev libsqlite3-dev libminiupnpc-dev libnatpmp-dev \
    qtbase5-dev qttools5-dev qttools5-dev-tools \
    libprotobuf-dev protobuf-compiler libqrencode-dev
```

**2. Download the code.**

```bash
git clone https://git.subgenius.finance/SubGeniusFinance/dobbscoin-source.git
cd dobbscoin-source
```

**3. Build it.** This takes 10 to 30 minutes.

```bash
cmake -B build -DBUILD_GUI=ON
cmake --build build -j$(nproc)
```

**4. Run it.**

```bash
./build/src/qt/dobbscoin-qt
```

That's the wallet with its window. The first start downloads the whole blockchain, which takes a while.

Want the node without a window (for a server)? Leave out `-DBUILD_GUI=ON` in step 3. The programs are then `build/src/dobbscoind` (the node) and `build/src/dobbscoin-cli` (to talk to it).

---

## B. Build the Windows installer

You do this **on Linux** (or WSL). The result is a `setup.exe` you copy to a Windows PC.

**1. Install the tools.**

```bash
sudo apt-get update
sudo apt-get install -y git build-essential cmake pkg-config python3 curl \
    autoconf automake libtool patch bsdmainutils nsis \
    g++-mingw-w64-x86-64-posix
sudo update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
sudo update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix
```

If the last two lines say "no alternatives", that's fine. Keep going.

**2. Download the code** (skip if you already did it in part A).

```bash
git clone https://git.subgenius.finance/SubGeniusFinance/dobbscoin-source.git
cd dobbscoin-source
```

**3. Build the Windows libraries.** This is the slow part: **about 45 minutes** the first time. You only do it once.

```bash
make -C depends HOST=x86_64-w64-mingw32 -j$(nproc)
```

**4. Build the programs and the installer.** About 10 minutes.

```bash
cmake -B build-win --toolchain depends/x86_64-w64-mingw32/toolchain.cmake
cmake --build build-win -j$(nproc)
cmake --build build-win --target deploy
```

**5. Your installer** is the file `build-win/dobbscoin-<version>-win64-setup.exe`. Copy it to Windows and run it.

The loose programs are also in `build-win/src/` (`dobbscoind.exe`, `dobbscoin-cli.exe`, and `qt/dobbscoin-qt.exe`) if you'd rather skip the installer.

---

## If something goes wrong

- **"Could NOT find ..." or "... not found"**: a tool from step 1 is missing. Run step 1 again and look for a red error line.
- **It stopped halfway and you want a clean start**: delete the build folder and repeat the build step.
  ```bash
  rm -rf build build-win
  ```
  (Don't delete `depends/` after part B step 3 unless you want to wait 45 minutes again.)
- **"left by an in-tree Autotools build"**: someone ran the old `./configure` in this folder. The simplest fix is a fresh copy: go up one folder, `git clone` again into a new folder, and start over there.
- **The computer runs out of memory and the build dies**: use fewer parallel jobs. Replace `-j$(nproc)` with `-j2`.
- Still stuck? Open an issue at <https://git.subgenius.finance/SubGeniusFinance/dobbscoin-source/issues> (free account) and paste the last 20 lines the terminal printed.

---

## Checking your build (optional)

After part A, this runs every test. It takes about 7 minutes.

```bash
sudo apt-get install -y python3-ecdsa
qa/run-all.sh build/src
```

The last line should say `failed 0`.

---

## Building on Windows with WSL

1. Open **PowerShell as Administrator** and run: `wsl --install -d Ubuntu`
2. Restart when it asks. Open **Ubuntu** from the Start menu and pick a username and password.
3. In that Ubuntu window, follow **part B** above.
4. Your installer is then in your Linux home folder. In Windows Explorer, type `\\wsl$\Ubuntu\home\` in the address bar to find it.

---

More detail (every build option, older build methods, macOS): [`doc/build-cmake.md`](doc/build-cmake.md), [`doc/build-windows.md`](doc/build-windows.md), [`doc/build-osx.md`](doc/build-osx.md).
