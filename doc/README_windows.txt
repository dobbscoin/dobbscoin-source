(BOB) Dobbscoin wallet for Windows
==================================

Dobbscoin (BOB) is the coin of the Church of the SubGenius. This is the
full wallet: it keeps your keys on this computer, sends and receives
(BOB), and runs a node that checks every block itself.

Start it from the Dobbscoin Core entry in the Start menu. The first start
downloads the whole block chain, about 1.5 GB. That takes a while; the
wallet is usable once it has caught up.

Where your data lives
---------------------
Everything is in %APPDATA%\Dobbscoin (paste that into the Explorer
address bar). The file that matters is wallet.dat. Back it up, and keep
the copy somewhere safe: whoever has it can spend your (BOB).
Uninstalling the program does not touch this folder.

Upgrading to v0.14
------------------
v0.14 converts wallet.dat to a new format the first time it opens it.
The original file is kept beside it as wallet.dat.bdb-<number>, so
nothing is lost. After the conversion, older versions of the wallet
cannot open the new wallet.dat; keep the .bdb file if you might go back.

The programs
------------
dobbscoin-qt.exe          the wallet
daemon\dobbscoind.exe     the node without a window, for servers
daemon\dobbscoin-cli.exe  talks to a running dobbscoind
doc\files.md              what each file in the data folder is
doc\tor.md                running over Tor

More
----
Website:        https://dobbscoin.info
Report a bug:   https://git.subgenius.finance/SubGeniusFinance/dobbscoin-source/issues
License:        COPYING.txt
