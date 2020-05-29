Dobbscoin Core integration/staging tree
=====================================
The Official CryptoCurrency of the Church of the SubGenius. http://www.subgenius.com

The ONLY crypto-currency accepted on the Pleasure Saucers. https://www.dobbscoin.info

Maybe you've come into a large inheritance, or your income just suddenly popped.
Maybe you gave birth to quintuplets or been recently divorced. Or maybe you just
feel uneasy about your money; where it's going or how far it will take you in the
future. Whatever your problem is, Dobbscoin can help you.

Will you join TheConspiracy’s mindless atheistic unknowing servitude to the Elder Bankers of the Universe and their MINIONS in some hideous World Government, or will you GET SLACK and FIGHT FOR FREEDOM as a zeal-crazed Priest-Warrior for ODIN?

Give yourself to (Bob) freely, joyously, without an atom of restraint --NOW -- and your worries are over.

Get in on the ground floor of this lucrative alt., while difficulty is low.

Eternal Salvation or Triple your Money Back!!

What is Dobbscoin?
----------------

Dobbscoin is an excrimental new digital currency that enables the sending of
instant Slack to anyone, anywhere in the omniverse. Dobbscoin uses peer-to-peer
technology to operate with no central authority: managing transactions and
issuing Slack is carried out collectively by the network.

For more information, as well as an immediately useable binary version of
the Dobbscoin Core software, see https://github.com/dobbscoin/dobbscoin-source/releases

License
-------

Dobbscoin Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see http://opensource.org/licenses/MIT.

Development process
-------------------

Developers work in their own trees, then submit pull requests when they think
their feature or bug fix is ready.

If it is a simple/trivial/non-controversial change, then one of the Dobbscoin
development team members simply pulls it.

If it is a *more complicated or potentially controversial* change, then the patch
submitter will be asked to start a discussion (if they haven't already) on the
[mailing list](http://sourceforge.net/mailarchive/forum.php?forum_name=dobbscoin-development).

The patch will be accepted if there is broad consensus that it is a good thing.
Developers should expect to rework and resubmit patches if the code doesn't
match the project's coding conventions (see [doc/coding.md](doc/coding.md)) or are
controversial.

The `master` branch is regularly built and tested, but is not guaranteed to be
completely stable. [Tags](https://github.com/dobbscoin/dobbscoin-source/tags) are created
regularly to indicate new official, stable release versions of Dobbscoin.

Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

### Automated Testing

Developers are strongly encouraged to write unit tests for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run (assuming they weren't disabled in configure) with: `make check`

Every pull request is built for both Windows and Linux on a dedicated server,
and unit and sanity tests are automatically run. The binaries produced may be
used for manual QA testing — a link to them will appear in a comment on the
pull request posted by [DobbscoinPullTester](https://github.com/DobbscoinPullTester). See https://github.com/TheBlueMatt/test-scripts
for the build/test scripts.

### Manual Quality Assurance (QA) Testing

Large changes should have a test plan, and should be tested by somebody other
than the developer who wrote the code.
See https://github.com/dobbscoin/QA/ for how to create a test plan.

Translations
------------

Changes to translations as well as new translations can be submitted to
[Dobbscoin Core's Transifex page](https://www.transifex.com/projects/p/dobbscoin/).

Translations are periodically pulled from Transifex and merged into the git repository. See the
[translation process](doc/translation_process.md) for details on how this works.

**Important**: We do not accept translation changes as GitHub pull requests because the next
pull from Transifex would automatically overwrite them again.

Translators should also subscribe to the [mailing list](https://groups.google.com/forum/#!forum/dobbscoin-translators).

Development tips and tricks
---------------------------

**compiling for debugging**

Run configure with the --enable-debug option, then make. Or run configure with
CXXFLAGS="-g -ggdb -O0" or whatever debug flags you need.

**debug.log**

If the code is behaving strangely, take a look in the debug.log file in the data directory;
error and debugging messages are written there.

The -debug=... command-line option controls debugging; running with just -debug will turn
on all categories (and give you a very large debug.log file).

The Qt code routes qDebug() output to debug.log under category "qt": run with -debug=qt
to see it.

**testnet and regtest modes**

Run with the -testnet option to run with "play dobbscoins" on the test network, if you
are testing multi-machine code that needs to operate across the internet.

If you are testing something that can run on one machine, run with the -regtest option.
In regression test mode, blocks can be created on-demand; see qa/rpc-tests/ for tests
that run in -regtest mode.

**DEBUG_LOCKORDER**

Dobbscoin Core is a multithreaded application, and deadlocks or other multithreading bugs
can be very difficult to track down. Compiling with -DDEBUG_LOCKORDER (configure
CXXFLAGS="-DDEBUG_LOCKORDER -g") inserts run-time checks to keep track of which locks
are held, and adds warnings to the debug.log file if inconsistencies are detected.

Killroy was here.
