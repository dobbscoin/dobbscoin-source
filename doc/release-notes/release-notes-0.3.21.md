Binaries for Bitcoin version 0.3.21 are available at:
  https://sourceforge.net/projects/dobbscoin/files/Bitcoin/dobbscoin-0.3.21/

Changes and new features from the 0.3.20 release include:

* Universal Plug and Play support.  Enable automatic opening of a port for incoming connections by running dobbscoin or dobbscoind with the - -upnp=1 command line switch or using the Options dialog box.

* Support for full-precision dobbscoin amounts.  You can now send, and dobbscoin will display, dobbscoin amounts smaller than 0.01.  However, sending fewer than 0.01 dobbscoins still requires a 0.01 dobbscoin fee (so you can send 1.0001 dobbscoins without a fee, but you will be asked to pay a fee if you try to send 0.0001).

* A new method of finding dobbscoin nodes to connect with, via DNS A records. Use the -dnsseed option to enable.

For developers, changes to dobbscoin's remote-procedure-call API:

* New rpc command "sendmany" to send dobbscoins to more than one address in a single transaction.

* Several bug fixes, including a serious intermittent bug that would sometimes cause dobbscoind to stop accepting rpc requests. 

* -logtimestamps option, to add a timestamp to each line in debug.log.

* Immature blocks (newly generated, under 120 confirmations) are now shown in listtransactions.
