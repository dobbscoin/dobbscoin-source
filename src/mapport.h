// Copyright (c) 2011-2020 The Bitcoin Core developers
// Copyright (c) 2014-2026 The Dobbscoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MAPPORT_H
#define BITCOIN_MAPPORT_H

#include <string>

// (BOB) Port mapping: NAT-PMP (libnatpmp) and UPnP (miniupnpc), backported from
// Bitcoin Core 22.0 with 0.14's behaviour on top: NAT-PMP is tried first with a
// short probe, UPnP is the fallback, and GetMapPortStatus() says what happened.
// DEFAULT_UPNP stays in net.h.

#ifdef USE_NATPMP
static constexpr bool DEFAULT_NATPMP = USE_NATPMP;
#else
static constexpr bool DEFAULT_NATPMP = false;
#endif // USE_NATPMP

enum MapPortProtoFlag : unsigned int {
    NONE = 0x00,
    UPNP = 0x01,
    NAT_PMP = 0x02,
};

void StartMapPort(bool use_upnp, bool use_natpmp);
void InterruptMapPort();
void StopMapPort();
/** What the port mapper last did, for the GUI ("" when off). */
std::string GetMapPortStatus();

#endif // BITCOIN_MAPPORT_H
