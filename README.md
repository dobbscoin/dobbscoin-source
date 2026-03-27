Dobbscoin Core integration/staging tree
=====================================
Dobbscoin (BOB) is the Official CryptoCurrency of 
The Church of the SubGenius. http://www.subgenius.com

The ONLY crypto-currency accepted on the Pleasure Saucers.

Backed By Nothing, Powered By Everything


Dobbscoin (BOB) Homepage: https://www.dobbscoin.info


•  Maybe you've come into a large inheritance, or your income just suddenly popped.  
•  Maybe you gave birth to quintuplets or been recently divorced. Or maybe you just feel uneasy about your money; where it's going or how far it will take you in the future.

Whatever your problem is, Dobbscoin can help you.


Will you join The Conspiracy’s mindless atheistic unknowing servitude to the Elder Bankers of the Universe and their MINIONS in some hideous World Government, or will you GET SLACK and FIGHT FOR FREEDOM as a zeal-crazed Priest-Warrior for ODIN?


•  SubGenius Finance : https://SubGenius.Finance  
Where Culture Becomes Capital

Eternal Salvation or Triple your Money Back!!


What is Dobbscoin?
------------------

Dobbscoin is an excremental digital currency that enables the sending of
instant Slack to anyone, anywhere in the omniverse. Dobbscoin uses peer-to-peer
technology to operate with no central authority: managing transactions and
issuing Slack is carried out collectively by the network.


For more information, as well as an immediately useable binary version of
the Dobbscoin Core software, see:

https://github.com/dobbscoin/dobbscoin-source/releases


License
-------

Dobbscoin Core is released under the terms of the MIT license.
See COPYING for more information or see:

http://opensource.org/licenses/MIT.


Development process
-------------------

Developers work in their own trees, then submit pull requests when they think
their feature or bug fix is ready. If it is a simple/trivial/non-controversial
change, then one of the Dobbscoin development team members simply pulls it.
If it is a more complicated or potentially controversial change, then the patch
submitter will be asked to start a discussion (if they haven't already) on the
FORUMS:

https://subgenius.finance/smf/


The patch will be accepted if there is broad consensus that it is a good thing.
Developers should expect to rework and resubmit patches if the code doesn't
match the project's coding conventions (see doc/coding.md) or are controversial.


The `master` branch is regularly built and tested, but is not guaranteed to be
completely stable. Tags are created regularly to indicate new official,
stable release versions of Dobbscoin.


Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and
help out by testing other people's pull requests, and remember this is a
security-critical project where any mistake might cost people lots of time.


Automated Testing
-----------------

Developers are strongly encouraged to write unit tests for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run
(assuming they weren't disabled in configure) with:

    make check

Every pull request is built for both Windows and Linux on a dedicated server,
and unit and sanity tests are automatically run.

See:

https://github.com/TheBlueMatt/test-scripts

for the build/test scripts.


Manual Quality Assurance (QA) Testing
-------------------------------------

Large changes should have a test plan, and should be tested by somebody other
than the developer who wrote the code.



Building Dobbscoin on Linux
---------------------------

Dobbscoin can be built on modern Linux distributions such as Ubuntu 22/24
and Debian using standard system libraries.

Most users can build Dobbscoin with the following commands:

    git clone https://github.com/dobbscoin/dobbscoin-source.git
    cd dobbscoin-source

    ./autogen.sh
    ./configure
    make

After building, verify the version:

    ./src/dobbscoind --version



Compatible Wallet Builds (Berkeley DB 4.8)
------------------------------------------

For maximum wallet portability and compatibility across systems and releases,
maintainers, exchanges, and production deployments may prefer to build using
Berkeley DB 4.8.

Dobbscoin provides a helper script to install a local compatible copy safely:

    ./contrib/install-db4.sh

This installs Berkeley DB 4.8 into:

    $HOME/db4

Then build Dobbscoin using:

    ./autogen.sh
    ./configure --with-bdb=$HOME/db4
    make



When is Berkeley DB 4.8 recommended?

- Building official release binaries
- Operating production nodes
- Exchanges and custodial services
- Long-term wallet compatibility requirements

Most users and developers can safely use the default system libraries.



Notes
-----

Dobbscoin supports building with modern Berkeley DB versions provided by the
operating system.

The compatible Berkeley DB 4.8 option is provided for environments that require
strict wallet portability guarantees.
