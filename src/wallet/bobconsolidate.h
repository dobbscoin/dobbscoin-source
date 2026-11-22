// Copyright (c) 2026 The Dobbscoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_BOBCONSOLIDATE_H
#define BITCOIN_WALLET_BOBCONSOLIDATE_H

#include <amount.h>

#include <string>

class ArgsManager;
class CWallet;

// (BOB) #55 part B: automatic consolidation of small coins. Mining wallets collect
// thousands of small pool payouts; left alone they make sends huge or impossible
// (a 100 kB transaction holds about 550 inputs with uncompressed keys). Every minute,
// for each unlocked wallet with private keys: if more than -consolidatemin confirmed,
// mature coins below -consolidatebelow sit on one address (or on any address, with
// -consolidateall), merge the smallest -consolidatebatch of them into one coin on that same
// address. One batch at a time: the next waits until
// the previous one confirms. A locked wallet is simply skipped until it is unlocked.

static constexpr bool DEFAULT_BOB_CONSOLIDATE{true};
static constexpr int DEFAULT_BOB_CONSOLIDATE_MIN{500};
static constexpr int DEFAULT_BOB_CONSOLIDATE_BATCH{400};
static constexpr int MAX_BOB_CONSOLIDATE_BATCH{500};
static constexpr CAmount DEFAULT_BOB_CONSOLIDATE_BELOW{10 * COIN};
static constexpr bool DEFAULT_BOB_CONSOLIDATE_ALL{false};
//! Coins confirmed before this time (2026-01-28 00:00 UTC) are never merged: moving a coin
//! resets its age, and the (BOB) buyback pays by age. Same boundary as the buyback checker.
static constexpr int64_t DEFAULT_BOB_CONSOLIDATE_KEEP_BEFORE{1769558400};
//! A coinbase output paid to this wallet is a block its owner mined: a coin nobody else has
//! ever held. Spending or merging it ends that for good, so by default merging skips mined
//! blocks and ordinary sends use them only when the other coins cannot cover the payment.
static constexpr bool DEFAULT_BOB_KEEP_MINED_BLOCKS{true};
//! mapValue key marking a consolidation transaction
extern const std::string BOB_CONSOLIDATION_KEY;

void BobAddConsolidateOptions(ArgsManager& argsman);

/** Whether mined blocks are kept intact (-keepminedblocks). */
bool BobKeepMinedBlocks();

/** One consolidation pass over one wallet. Returns the txid of the consolidation it
 *  sent, or "" and sets why_not to the reason nothing was done. */
std::string BobMaybeConsolidate(CWallet& wallet, std::string& why_not);

/** Scheduler entry point: one pass over every loaded wallet. */
void BobMaybeConsolidateWallets();

#endif // BITCOIN_WALLET_BOBCONSOLIDATE_H
