# Linux Build

These steps are intended to work from a fresh clone on a modern Linux distribution with Qt 5, Boost, OpenSSL 3, protobuf, and the standard autotools toolchain installed.

## Clean Build

```bash
make clean
./configure
make -j"$(nproc)"
```

## Notes

- The current tree has been updated to compile with modern Boost bind placeholders used by Qt signal hookups.
- Qt payment request verification was adjusted to use OpenSSL 3 compatible `EVP_MD_CTX` allocation APIs.
- Wallet build compatibility fixes preserve existing crypted-keystore behavior and do not change consensus, networking rules, or block validation.

## Typical Ubuntu/Debian Dependencies

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential libtool autotools-dev automake pkg-config bsdmainutils \
  libssl-dev libevent-dev libboost-system-dev libboost-filesystem-dev \
  libboost-chrono-dev libboost-program-options-dev libboost-test-dev \
  libboost-thread-dev libminiupnpc-dev libprotobuf-dev protobuf-compiler \
  libqrencode-dev qtbase5-dev qttools5-dev-tools libqt5svg5-dev
```
