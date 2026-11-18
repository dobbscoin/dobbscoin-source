// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2014-2026 The Dobbscoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BOBMINER_H
#define BITCOIN_BOBMINER_H

// (BOB) The built-in CPU miner of 0.14 (solo, "setgenerate"), ported to 0.21.
// Bitcoin Core removed its internal miner in 0.13; Dobbscoin kept one, with the
// multi-way SIMD scrypt (crypto/scrypt_nway.h). It lives on the node side and
// knows nothing about wallets: whoever starts it hands it a function that
// returns the coinbase script, called once per thread and again after every
// block it finds (0.14 took a fresh key per block the same way).

#include <crypto/scrypt_nway.h>
#include <script/script.h>

#include <cstdint>
#include <functional>
#include <string>

struct NodeContext;

namespace bobminer {

/** Fills script with the coinbase output script, or sets error and returns false. */
using CoinbaseScriptFn = std::function<bool(CScript& script, std::string& error)>;

/**
 * Pick the scrypt implementation the miners (solo and stratum) hash with, and log
 * it: "auto" or "" for the fastest this CPU supports, else generic, sse2, avx or
 * avx2 (-minerscrypt). A name the CPU cannot run falls back to the fastest one.
 * Only mining is affected: block validation always uses the generic scrypt.
 */
void ScryptSelect(const std::string& requested);
/** The implementation the miners use now, by name ("avx2" ...). */
std::string ScryptName();
ScryptImpl Impl();
/**
 * Recompute hash32, which impl claimed for header80, with the generic scrypt.
 * True if they agree (always, for GENERIC). If not: logs it loudly, sets the
 * GUI/getnetworkinfo warning, switches the miners to GENERIC for the rest of the
 * session, and returns false: the caller must then not submit.
 */
bool ScryptCheck(ScryptImpl impl, const unsigned char* header80, const unsigned char* hash32);

/**
 * Start (generate=true) or stop solo mining. threads < 0 means one per core
 * (regtest: 1). Restarts the threads if already running. On regtest each thread
 * stops after the block it finds, as in 0.14.
 */
bool SetGenerate(NodeContext& node, bool generate, int threads, CoinbaseScriptFn coinbase, std::string& error);
bool IsGenerating();
/** Threads asked for at the last start (0 when stopped). */
int Threads();
double HashesPerSec();
int64_t BlocksFound();
/** Stop and join the miner threads (shutdown). */
void Stop();

} // namespace bobminer

#endif // BITCOIN_BOBMINER_H
