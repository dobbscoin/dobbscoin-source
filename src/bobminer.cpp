// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2014-2026 The Dobbscoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bobminer.h>

#include <arith_uint256.h>
#include <chainparams.h>
#include <crypto/common.h>
#include <crypto/scrypt.h>
#include <interfaces/wallet.h>
#include <outputtype.h>
#include <script/standard.h>
#include <miner.h>
#include <net.h>
#include <node/context.h>
#include <primitives/block.h>
#include <streams.h>
#include <txmempool.h>
#include <util/moneystr.h>
#include <util/strencodings.h>
#include <util/threadnames.h>
#include <util/time.h>
#include <util/translation.h>
#include <validation.h>
#include <version.h>
#include <warnings.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#ifdef WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace bobminer {

// ---------------------------------------------------------------------------
// Scrypt for the miners: the solo miner below and the stratum pool workers.
// They may hash with a SIMD multi-way scrypt (crypto/scrypt_nway.h). Nothing
// else does: GetPoWHash(), CheckProofOfWork() and all block validation use the
// generic scrypt_1024_1_1_256(). Every solution the fast path claims is
// recomputed with the generic function before it is used (ScryptCheck), and
// so is one hash per 4096-nonce slice. Any disagreement drops both miners to
// generic for the rest of the session.
// ---------------------------------------------------------------------------
static std::atomic<int> g_scrypt_impl{(int)ScryptImpl::GENERIC};
static std::atomic<bool> g_scrypt_mismatch{false};

void ScryptSelect(const std::string& requested)
{
    ScryptImpl chosen = ScryptImplBest();
    bool forced = false;
    if (!requested.empty() && requested != "auto") {
        ScryptImpl r;
        if (!ScryptImplFromName(requested, r)) {
            LogPrintf("Scrypt miner: unknown -minerscrypt=%s (expected auto, generic, sse2, avx or avx2), ignoring it\n", requested);
        } else if (!ScryptImplSupported(r)) {
            LogPrintf("Scrypt miner: -minerscrypt=%s is not supported by this CPU and OS, ignoring it\n", requested);
        } else {
            chosen = r;
            forced = true;
        }
    }
    if (g_scrypt_mismatch) chosen = ScryptImpl::GENERIC;
    g_scrypt_impl = (int)chosen;
    LogPrintf("Scrypt miner: CPU features: %s\n", ScryptCpuFeatures());
    LogPrintf("Scrypt miner: using %s%s\n", ScryptImplDescription(chosen), forced ? " (forced by -minerscrypt)" : "");
}

ScryptImpl Impl()
{
    return (ScryptImpl)g_scrypt_impl.load(std::memory_order_relaxed);
}

std::string ScryptName()
{
    return ScryptImplName(Impl());
}

bool ScryptCheck(ScryptImpl impl, const unsigned char* header80, const unsigned char* hash32)
{
    if (impl == ScryptImpl::GENERIC) return true;
    uint256 fast, generic;
    memcpy(fast.begin(), hash32, 32);
    scrypt_1024_1_1_256((const char*)header80, (char*)generic.begin());
    if (generic == fast) return true;
    // Only the first thread to notice logs; all of them see GENERIC afterwards.
    g_scrypt_impl = (int)ScryptImpl::GENERIC;
    if (!g_scrypt_mismatch.exchange(true)) {
        const std::string msg = strprintf("Scrypt miner: %s scrypt DISAGREES with the generic scrypt (fast %s, generic %s, header %s). "
                                          "Not submitting; mining with the generic implementation for the rest of this session.",
                                          ScryptImplDescription(impl), fast.GetHex(), generic.GetHex(), HexStr(Span<const unsigned char>(header80, 80)));
        LogPrintf("*** %s\n", msg);
        tfm::format(std::cerr, "*** %s\n", msg);
        SetMiscWarning(Untranslated(msg));
    }
    return false;
}

// ---------------------------------------------------------------------------
// Solo miner
// ---------------------------------------------------------------------------
static std::mutex g_mutex;                  // guards g_threads, g_requested_threads (held while joining)
static std::vector<std::thread> g_threads;
static std::mutex g_coinbase_mutex;         // guards g_coinbase; never held while joining
static CoinbaseScriptFn g_coinbase;
static int g_requested_threads = 0;
static std::atomic<bool> g_stop{false};
static std::atomic<int> g_active{0};
static std::atomic<double> g_hashes_per_sec{0.0};
static std::atomic<int64_t> g_hps_timer_start{0};
static std::atomic<int64_t> g_hash_counter{0};
static std::atomic<int64_t> g_blocks_found{0};
static std::atomic<uint64_t> g_start_count{0};

static void LowerThreadPriority()
{
#ifdef WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#elif defined(__linux__)
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 19);
#endif
}

static bool GetCoinbase(CScript& script)
{
    std::string error;
    CoinbaseScriptFn fn;
    {
        std::lock_guard<std::mutex> lock(g_coinbase_mutex);
        fn = g_coinbase;
    }
    if (!fn || !fn(script, error)) {
        LogPrintf("BobMiner: no coinbase script (%s); this miner thread stops\n", error);
        return false;
    }
    return true;
}

/**
 * Scan nonces for a hash that meets target, ScryptImplLanes() consecutive nonces
 * per step (1 on the generic path). nonce is the last nonce hashed (or, on
 * success, the winning one) and carries over between calls; every 4096 nonces
 * control goes back to the caller so it can refresh nTime, notice a new tip and
 * honour a stop. scratchpad holds ScryptNwayScratchpadSize() for the widest path.
 */
static bool ScanHash(const CBlockHeader& header, uint32_t& nonce, uint256& hash_out, const arith_uint256& target, unsigned char* scratchpad)
{
    // Serialize the header once. Only the nonce -- the last 4 of the 80 bytes --
    // changes per iteration, so it is patched in place rather than reserialized.
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << static_cast<const CPureBlockHeader&>(header);
    if (ss.size() != 80) return false;
    unsigned char headers[SCRYPT_NWAY_MAX_LANES * 80];
    unsigned char hashes[SCRYPT_NWAY_MAX_LANES * 32];
    for (int l = 0; l < SCRYPT_NWAY_MAX_LANES; l++) memcpy(headers + 80 * l, ss.data(), 80);

    while (true) {
        const ScryptImpl impl = Impl();
        const int lanes = ScryptImplLanes(impl);
        const uint32_t prev = nonce;
        for (int l = 0; l < lanes; l++) WriteLE32(headers + 80 * l + 76, prev + 1 + l);
        scrypt_1024_1_1_256_nway(impl, headers, hashes, scratchpad);
        nonce = prev + lanes;
        g_hash_counter += lanes;

        // Test the target here: scrypt is far too slow to afford upstream's
        // leading-zero prefilter, and at the (BOB) floor the target is high
        // enough that such a filter would discard real solutions.
        for (int l = 0; l < lanes; l++) {
            uint256 hash;
            memcpy(hash.begin(), hashes + 32 * l, 32);
            if (UintToArith256(hash) > target) continue;
            if (!ScryptCheck(impl, headers + 80 * l, hashes + 32 * l)) return false; // fast path wrong: now on generic
            nonce = prev + 1 + l;
            hash_out = hash;
            return true;
        }

        if ((prev >> 12) != (nonce >> 12)) {
            // Spot check: a fast path gone wrong would otherwise only ever show
            // up as missing blocks. One generic hash per slice.
            ScryptCheck(impl, headers, hashes);
            return false;
        }
    }
}

static void MeterHashes()
{
    if (GetTimeMillis() - g_hps_timer_start <= 4000) return;
    static std::mutex cs;
    std::lock_guard<std::mutex> lock(cs);
    const int64_t now = GetTimeMillis();
    if (g_hps_timer_start == 0) {
        g_hps_timer_start = now;
        g_hash_counter = 0;
    } else if (now - g_hps_timer_start > 4000) {
        g_hashes_per_sec = 1000.0 * g_hash_counter.exchange(0) / (now - g_hps_timer_start);
        g_hps_timer_start = now;
        static int64_t log_time = 0;
        if (GetTime() - log_time > 30 * 60) {
            log_time = GetTime();
            LogPrintf("hashmeter %6.0f khash/s (%s scrypt)\n", g_hashes_per_sec / 1000.0, ScryptName());
        }
    }
}

static void MinerThread(NodeContext* node)
{
    util::ThreadRename("bobminer");
    LowerThreadPriority();
    LogPrintf("BobMiner started (%s scrypt)\n", ScryptName());

    unsigned int extra_nonce = 0;
    // scratchpad big enough for the widest implementation (768 KiB)
    std::vector<unsigned char> scratchpad(ScryptNwayScratchpadSize(ScryptImpl::AVX2));
    const CChainParams& chainparams = Params();
    CScript coinbase;

    try {
        if (!GetCoinbase(coinbase)) throw std::runtime_error("no coinbase script");
        while (!g_stop) {
            // Wait for peers before mining, so we build on the real chain rather than
            // a private fork that gets reorged away on reconnect. Not IsInitialBlockDownload():
            // it stays true whenever the tip is a day old, and (BOB) routinely sits idle
            // for days -- mining could then never make the tip fresh again (as 0.14).
            if (!chainparams.MineBlocksOnDemand() && node->connman && node->connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0) {
                UninterruptibleSleep(std::chrono::seconds{1});
                continue;
            }

            const CTxMemPool& mempool = *node->mempool;
            const unsigned int tx_updated_last = mempool.GetTransactionsUpdated();
            std::unique_ptr<CBlockTemplate> tmpl = BlockAssembler(mempool, chainparams).CreateNewBlock(coinbase);
            if (!tmpl) {
                UninterruptibleSleep(std::chrono::seconds{1});
                continue;
            }
            CBlock* pblock = &tmpl->block;
            const CBlockIndex* pindex_prev;
            {
                LOCK(cs_main);
                pindex_prev = LookupBlockIndex(pblock->hashPrevBlock);
                if (!pindex_prev) continue;
                IncrementExtraNonce(pblock, pindex_prev, extra_nonce);
            }

            const int64_t start = GetTime();
            arith_uint256 target;
            target.SetCompact(pblock->nBits);
            uint256 hash;
            uint32_t nonce = 0;
            while (!g_stop) {
                if (ScanHash(*pblock, nonce, hash, target, scratchpad.data())) {
                    pblock->nNonce = nonce;
                    if (pblock->GetPoWHash() != hash) {
                        LogPrintf("BobMiner: scan hash %s is not the block's PoW hash %s; not submitting\n", hash.GetHex(), pblock->GetPoWHash().GetHex());
                        break;
                    }
                    LogPrintf("BobMiner: proof-of-work found\n  hash: %s\ntarget: %s\n", hash.GetHex(), ArithToUint256(target).GetHex());
                    bool stale;
                    {
                        LOCK(cs_main);
                        stale = pblock->hashPrevBlock != ::ChainActive().Tip()->GetBlockHash();
                    }
                    if (stale) {
                        LogPrintf("BobMiner: generated block is stale\n");
                    } else if (!node->chainman->ProcessNewBlock(chainparams, std::make_shared<const CBlock>(*pblock), true, nullptr)) {
                        LogPrintf("BobMiner: ProcessNewBlock, block not accepted\n");
                    } else {
                        ++g_blocks_found;
                        LogPrintf("BobMiner: generated %s, block %s\n", FormatMoney(pblock->vtx[0]->vout[0].nValue), pblock->GetHash().GetHex());
                        if (!GetCoinbase(coinbase)) throw std::runtime_error("no coinbase script");
                        // In regression test mode, stop mining after a block is accepted (a stale
                        // one, lost to another thread on the same tip, does not count).
                        if (chainparams.MineBlocksOnDemand()) throw std::runtime_error("regtest: one block per thread");
                    }
                    break;
                }
                MeterHashes();

                // Rebuild the block when it goes stale
                if (!chainparams.MineBlocksOnDemand() && node->connman && node->connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0) break;
                if (nonce >= 0xffff0000) break;
                if (mempool.GetTransactionsUpdated() != tx_updated_last && GetTime() - start > 60) break;
                {
                    LOCK(cs_main);
                    if (pindex_prev != ::ChainActive().Tip()) break;
                }
                // Update nTime every few seconds. Changing nTime can change the work
                // required on testnet and, on mainnet, opens the emergency-difficulty
                // valve once the tip gap passes 6h mid-search.
                UpdateTime(pblock, chainparams.GetConsensus(), pindex_prev);
                target.SetCompact(pblock->nBits);
            }
        }
    } catch (const std::exception& e) {
        LogPrintf("BobMiner thread stops: %s\n", e.what());
    }
    if (--g_active == 0) g_hashes_per_sec = 0.0;
    LogPrintf("BobMiner terminated\n");
}

static void StopLocked()
{
    g_stop = true;
    for (std::thread& t : g_threads) {
        if (t.joinable()) t.join();
    }
    g_threads.clear();
    g_requested_threads = 0;
    {
        // Drop the coinbase source, and with it any wallet it holds (unloadwallet waits for that).
        std::lock_guard<std::mutex> lock(g_coinbase_mutex);
        g_coinbase = nullptr;
    }
    g_hashes_per_sec = 0.0;
    g_hps_timer_start = 0;
}

bool SetGenerate(NodeContext& node, bool generate, int threads, CoinbaseScriptFn coinbase, std::string& error)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    StopLocked();
    if (!generate || threads == 0) return true;
    if (!coinbase) {
        error = "no coinbase script source";
        return false;
    }
    if (!node.chainman || !node.mempool) {
        error = "the node is not ready";
        return false;
    }
    if (threads < 0) {
        threads = Params().MineBlocksOnDemand() ? 1 : (int)std::max(1u, std::thread::hardware_concurrency());
    }
    if (threads > MAX_THREADS) {
        error = strprintf("at most %d miner threads", MAX_THREADS);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_coinbase_mutex);
        g_coinbase = std::move(coinbase);
    }
    g_stop = false;
    g_requested_threads = threads;
    g_active = threads;
    try {
        for (int i = 0; i < threads; i++) g_threads.emplace_back(MinerThread, &node);
    } catch (const std::system_error& e) {
        g_active -= threads - (int)g_threads.size();
        StopLocked();
        error = strprintf("could not start the miner threads: %s", e.what());
        return false;
    }
    ++g_start_count;
    return true;
}

bool IsGenerating() { return g_active > 0; }
int Threads()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_active > 0 ? g_requested_threads : 0;
}
double HashesPerSec() { return g_hashes_per_sec; }
uint64_t StartCount() { return g_start_count; }

CoinbaseScriptFn WalletCoinbase(interfaces::WalletClient& client, const std::string& name)
{
    interfaces::WalletClient* c = &client;
    return [c, name](CScript& script, std::string& err) {
        for (auto& w : c->getWallets()) {
            if (w->getWalletName() != name) continue;
            CTxDestination dest;
            if (!w->getNewDestination(OutputType::LEGACY, "", dest)) {
                err = "the wallet could not give a new address (keypool empty or wallet locked?)";
                return false;
            }
            script = GetScriptForDestination(dest);
            return true;
        }
        err = "wallet \"" + name + "\" is no longer loaded";
        return false;
    };
}
int64_t BlocksFound() { return g_blocks_found; }

void Stop()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    StopLocked();
}

} // namespace bobminer
