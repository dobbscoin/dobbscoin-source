// Copyright (c) 2026 The Dobbscoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/bobconsolidate.h>

#include <interfaces/chain.h>
#include <logging.h>
#include <outputtype.h>
#include <script/standard.h>
#include <util/moneystr.h>
#include <util/system.h>
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <wallet/fees.h>
#include <wallet/wallet.h>

#include <algorithm>
#include <map>

const std::string BOB_CONSOLIDATION_KEY{"bobconsolidation"};

// Bytes an input adds with an uncompressed-key P2PKH signature, the worst case 0.14
// wallets hold. Used only to skip coins worth less than the fee of spending them.
static constexpr int64_t INPUT_BYTES_WORST{180};

void BobAddConsolidateOptions(ArgsManager& argsman)
{
    argsman.AddArg("-consolidate", strprintf("Automatically merge many small coins (such as mining payouts) into one, in the background (default: %u)", DEFAULT_BOB_CONSOLIDATE), ArgsManager::ALLOW_ANY, OptionsCategory::WALLET);
    argsman.AddArg("-consolidateall", strprintf("Let automatic merging combine coins from different addresses, which shows publicly that they belong together. Otherwise only coins on the same address are merged (default: %u)", DEFAULT_BOB_CONSOLIDATE_ALL), ArgsManager::ALLOW_ANY, OptionsCategory::WALLET);
    argsman.AddArg("-consolidatemin=<n>", strprintf("Merge once more than this many small coins have collected (default: %d)", DEFAULT_BOB_CONSOLIDATE_MIN), ArgsManager::ALLOW_ANY, OptionsCategory::WALLET);
    argsman.AddArg("-consolidategap=<minutes>", strprintf("At most one automatic merge in this many minutes, 0 for no limit (default: %d)", DEFAULT_BOB_CONSOLIDATE_GAP), ArgsManager::ALLOW_ANY, OptionsCategory::WALLET);
    argsman.AddArg("-consolidatebelow=<amt>", strprintf("Coins below this amount count as small (default: %s)", FormatMoney(DEFAULT_BOB_CONSOLIDATE_BELOW)), ArgsManager::ALLOW_ANY, OptionsCategory::WALLET);
    argsman.AddArg("-keepminedblocks", strprintf("Keep blocks mined by this wallet intact: never merge them, and spend them only when the other coins cannot cover a payment (default: %u)", DEFAULT_BOB_KEEP_MINED_BLOCKS), ArgsManager::ALLOW_ANY, OptionsCategory::WALLET);
    argsman.AddArg("-consolidatekeepbefore=<unix time>", strprintf("Never merge coins confirmed before this time, 0 for no limit (default: %d)", DEFAULT_BOB_CONSOLIDATE_KEEP_BEFORE), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::WALLET);
    argsman.AddArg("-consolidatebatch=<n>", strprintf("Coins merged per transaction, at most %d (default: %d)", MAX_BOB_CONSOLIDATE_BATCH, DEFAULT_BOB_CONSOLIDATE_BATCH), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::WALLET);
}

bool BobKeepMinedBlocks()
{
    return gArgs.GetBoolArg("-keepminedblocks", DEFAULT_BOB_KEEP_MINED_BLOCKS);
}

// Wait until the wallet has caught up with the chain: merging while it is still adding
// blocks from a long time offline could pick coins already spent elsewhere.
static constexpr int64_t MAX_TIP_AGE_FOR_MERGE{60 * 60};

static bool PendingConsolidation(const CWallet& wallet) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    // Only a merge that is still in the mempool counts as pending. One that the network
    // refused or that lost to a conflicting spend must not stop merging for good.
    for (const auto& entry : wallet.mapWallet) {
        const CWalletTx& wtx = entry.second;
        if (wtx.mapValue.count(BOB_CONSOLIDATION_KEY) && !wtx.isAbandoned() && wtx.GetDepthInMainChain() == 0 && wtx.InMempool()) return true;
    }
    return false;
}

// When this wallet last sent a merge (0 if never). Kept in the wallet's own records,
// so the gap holds across a restart.
static int64_t LastConsolidationTime(const CWallet& wallet) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    int64_t last = 0;
    for (const auto& entry : wallet.mapWallet) {
        const CWalletTx& wtx = entry.second;
        if (wtx.mapValue.count(BOB_CONSOLIDATION_KEY) && !wtx.isAbandoned()) last = std::max<int64_t>(last, wtx.nTimeReceived);
    }
    return last;
}

std::string BobMaybeConsolidate(CWallet& wallet, std::string& why_not)
{
    why_not.clear();
    if (!gArgs.GetBoolArg("-consolidate", DEFAULT_BOB_CONSOLIDATE)) { why_not = "turned off"; return ""; }
    if (wallet.IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) { why_not = "no private keys"; return ""; }
    if (wallet.IsLocked()) { why_not = "wallet locked"; return ""; }
    if (!wallet.chain().isReadyToBroadcast()) { why_not = "not synced"; return ""; }

    const int64_t min_count = std::max<int64_t>(2, gArgs.GetArg("-consolidatemin", DEFAULT_BOB_CONSOLIDATE_MIN));
    const int64_t batch = std::min<int64_t>(MAX_BOB_CONSOLIDATE_BATCH, std::max<int64_t>(2, gArgs.GetArg("-consolidatebatch", DEFAULT_BOB_CONSOLIDATE_BATCH)));
    CAmount below = DEFAULT_BOB_CONSOLIDATE_BELOW;
    if (gArgs.IsArgSet("-consolidatebelow") && (!ParseMoney(gArgs.GetArg("-consolidatebelow", ""), below) || below <= 0)) {
        why_not = "bad -consolidatebelow";
        return "";
    }
    const bool any_address = gArgs.GetBoolArg("-consolidateall", DEFAULT_BOB_CONSOLIDATE_ALL);
    const bool keep_mined = BobKeepMinedBlocks();
    const int64_t keep_before = gArgs.GetArg("-consolidatekeepbefore", DEFAULT_BOB_CONSOLIDATE_KEEP_BEFORE);

    std::vector<COutput> picked;
    CScript dest_script;
    {
        LOCK(wallet.cs_wallet);
        int64_t tip_time = 0;
        if (!wallet.chain().findBlock(wallet.GetLastBlockHash(), interfaces::FoundBlock().time(tip_time)) ||
            tip_time < wallet.chain().getAdjustedTime() - MAX_TIP_AGE_FOR_MERGE) {
            why_not = "catching up with the chain";
            return "";
        }
        if (PendingConsolidation(wallet)) { why_not = "waiting for the last batch to confirm"; return ""; }
        const int64_t gap_minutes = std::max<int64_t>(0, gArgs.GetArg("-consolidategap", DEFAULT_BOB_CONSOLIDATE_GAP));
        if (gap_minutes > 0 && LastConsolidationTime(wallet) + gap_minutes * 60 > wallet.chain().getAdjustedTime()) {
            why_not = strprintf("at most one merge every %d minutes", gap_minutes);
            return "";
        }

        CCoinControl cc;
        cc.m_min_depth = 1;
        const CAmount skip_below = 3 * GetMinimumFeeRate(wallet, cc, nullptr).GetFee(INPUT_BYTES_WORST);
        std::vector<COutput> coins;
        // confirmed, mature (AvailableCoins leaves out immature coinbase), unlocked, below the size
        wallet.AvailableCoins(coins, /* fOnlySafe */ true, &cc, /* nMinimumAmount */ 1, /* nMaximumAmount */ below - 1);

        std::map<CScript, std::vector<COutput>> by_script;
        std::map<uint256, int64_t> block_times; // many coins share a block
        for (const COutput& out : coins) {
            if (!out.fSpendable || !out.fSolvable) continue;
            if (keep_mined && out.tx->IsCoinBase()) continue; // a mined block stays whole
            if (keep_before > 0) {
                // Old coins keep their age. The age is the time of the block the coin
                // confirmed in; a coin whose block cannot be found is left alone too.
                const uint256& block_hash = out.tx->m_confirm.hashBlock;
                auto it = block_times.find(block_hash);
                if (it == block_times.end()) {
                    int64_t block_time = 0;
                    if (!wallet.chain().findBlock(block_hash, interfaces::FoundBlock().time(block_time))) block_time = 0;
                    it = block_times.emplace(block_hash, block_time).first;
                }
                if (it->second < keep_before) continue;
            }
            const CTxOut& txout = out.tx->tx->vout[out.i];
            if (txout.nValue < skip_below) continue; // costs more to spend than it is worth
            by_script[any_address ? CScript() : txout.scriptPubKey].push_back(out);
        }
        const std::vector<COutput>* best = nullptr;
        for (const auto& entry : by_script) {
            if (!best || entry.second.size() > best->size()) best = &entry.second;
        }
        if (!best || (int64_t)best->size() <= min_count) {
            why_not = strprintf("%u small coins %s, merging starts above %d", best ? best->size() : 0,
                                any_address ? "in the wallet" : "on one address", min_count);
            return "";
        }
        picked = *best;
        // Pay the merged coin back to the address the coins sit on (with -consolidateall: the
        // address holding most of them). A fresh address would exist only in this wallet file:
        // anyone running a copy of a non-HD 0.14 wallet (as testers are told to) would see the
        // merged coins vanish from the original. The same address also links nothing new.
        std::map<CScript, size_t> count;
        for (const COutput& out : picked) ++count[out.tx->tx->vout[out.i].scriptPubKey];
        dest_script = std::max_element(count.begin(), count.end(),
                                       [](const std::pair<const CScript, size_t>& a, const std::pair<const CScript, size_t>& b) { return a.second < b.second; })->first;
        std::sort(picked.begin(), picked.end(), [](const COutput& a, const COutput& b) {
            return a.tx->tx->vout[a.i].nValue < b.tx->tx->vout[b.i].nValue;
        });
        if ((int64_t)picked.size() > batch) picked.erase(picked.begin() + batch, picked.end());
    }

    // Build; if the transaction comes out too large (unusual key types), halve the batch.
    while (true) {
        CCoinControl cc;
        cc.fAllowOtherInputs = false;
        cc.m_min_depth = 1;
        CAmount total = 0;
        for (const COutput& out : picked) {
            cc.Select(COutPoint(out.tx->GetHash(), out.i));
            total += out.tx->tx->vout[out.i].nValue;
        }
        std::vector<CRecipient> recipients{{dest_script, total, /* fSubtractFeeFromAmount */ true}};
        CTransactionRef tx;
        CAmount fee = 0;
        int change_pos = -1;
        bilingual_str create_error;
        FeeCalculation fee_calc;
        if (wallet.CreateTransaction(recipients, tx, fee, change_pos, create_error, cc, fee_calc)) {
            mapValue_t value;
            value[BOB_CONSOLIDATION_KEY] = "1";
            value["comment"] = strprintf("Automatic maintenance: merged %u small coins into one. Nothing left your wallet.", picked.size());
            wallet.CommitTransaction(tx, std::move(value), {});
            wallet.WalletLogPrintf("Consolidation: merged %u small coins (%s) into one, fee %s, tx %s\n",
                                   picked.size(), FormatMoney(total), FormatMoney(fee), tx->GetHash().GetHex());
            return tx->GetHash().GetHex();
        }
        if (picked.size() > 20 && create_error.original.find("too large") != std::string::npos) {
            picked.erase(picked.begin() + picked.size() / 2, picked.end());
            continue;
        }
        why_not = "could not build the transaction: " + create_error.original;
        return "";
    }
}

void BobMaybeConsolidateWallets()
{
    static std::map<std::string, std::string> last_reason;
    for (const std::shared_ptr<CWallet>& wallet : GetWallets()) {
        std::string why_not;
        try {
            BobMaybeConsolidate(*wallet, why_not);
        } catch (const std::exception& e) {
            why_not = std::string("error: ") + e.what();
        }
        // say why nothing happened only when the reason changes, so the log stays quiet
        std::string& last = last_reason[wallet->GetName()];
        if (!why_not.empty() && why_not != last) {
            wallet->WalletLogPrintf("Consolidation: nothing to do (%s)\n", why_not);
        }
        last = why_not;
    }
}
