Files in the data directory
---------------------------

The data directory is `%APPDATA%\Dobbscoin` on Windows, `~/.dobbscoin` on Linux
and `~/Library/Application Support/Dobbscoin` on macOS.

* wallet.dat: personal wallet with keys and transactions; SQLite in releases after 0.13.8, Berkeley DB (BDB) up to 0.13.8
* wallet.dat.bdb-<unixtime>: the Berkeley DB wallet as it was before the one-time conversion to SQLite; after 0.13.8
* peers.dat: peer IP address database (custom format)
* blocks/blk000??.dat: block data (custom, 128 MiB per file)
* blocks/rev000??.dat: block undo data (custom)
* blocks/index/*: block index (LevelDB)
* chainstate/*: block chain state database (LevelDB)
* database/*: BDB database environment; only used for the wallet up to 0.13.8
* dobbscoin.conf: optional settings file, read at startup
* debug.log: the log
