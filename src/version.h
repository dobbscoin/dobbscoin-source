// Copyright (c) 2012-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DOBBSCOIN_VERSION_H
#define DOBBSCOIN_VERSION_H

/**
 * network protocol versioning
 */

//! When X-Day arrives, don't allow pinks on the pleasure saucers.
static const int PROTOCOL_VERSION = 70005;

//! initial proto version, to be increased after version/verack negotiation
static const int INIT_PROTO_VERSION = 209;

//! In this version, 'getheaders' was introduced.
static const int GETHEADERS_VERSION = 31800;

//! disconnect from peers older than this proto version.
//!
//! This is deliberately a literal and not PROTOCOL_VERSION. Tying the floor to
//! our own version means the next bump, for any reason, makes the new release
//! refuse every node already on the network: they accept us, because our
//! version clears their floor, and we reject them. The break is one directional
//! and the only trace is "peer=N using obsolete version" in a log nobody reads.
//!
//! 70005 is where the network has been since the v10.2.0 hardfork at block
//! 951,753. Anything still announcing 70002 or 70003 is on the chain that
//! forked off there, so nothing is let in by pinning the floor here. Raise this
//! only when there is a reason to stop talking to a version, and never as a
//! side effect of raising PROTOCOL_VERSION.
static const int MIN_PEER_PROTO_VERSION = 70005;

//! nTime field added to CAddress, starting with this version;
//! if possible, avoid requesting addresses nodes older than this
static const int CADDR_TIME_VERSION = 31402;

//! only request blocks from nodes outside this range of versions
static const int NOBLKS_VERSION_START = 32000;
static const int NOBLKS_VERSION_END = 32400;

//! BIP 0031, pong message, is enabled for all versions AFTER this one
static const int BIP0031_VERSION = 60000;

//! "mempool" command, enhanced "getdata" behavior starts with this version
static const int MEMPOOL_GD_VERSION = 60002;

#endif // DOBBSCOIN_VERSION_H
