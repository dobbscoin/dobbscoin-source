// Copyright (c) 2026 The Dobbscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// wallet_load: arbitrary wallet *records* loaded into a CWallet with
// CWallet::LoadWallet, i.e. CWalletDB::LoadWallet and ReadKeyValue. After a
// migration every record of the old wallet.dat reaches this code unchanged,
// so the record contents are as untrusted as the file was.
//
// The records live in the in-memory (mock) wallet store, so this harness is
// about the deserialization, not the container. Input format: a sequence of
// key, value, key, value... each read with FuzzedDataProvider's
// ConsumeRandomLengthString (a backslash followed by anything but a
// backslash ends a string; "\\" is a literal backslash). fuzz_seedgen writes
// the records of the checked-in wallets in this format.
//
// Invariants: no crash, no sanitizer report, no exception out of
// LoadWallet, and a result that is one of the DBErrors values.

#include "fuzz_util.h"

#include "db.h"
#include "serialize.h"
#include "sync.h"
#include "wallet.h"
#include "walletdb.h"

#include <fuzzer/FuzzedDataProvider.h>

namespace
{
const char* const WALLET = "wallet.dat";

/** Writes raw key/value bytes, without CDB's serialization framing. */
class RawWriter : public CDB
{
public:
    RawWriter() : CDB(WALLET, "cr+") {}
    bool Put(std::string k, std::string v)
    {
        std::vector<unsigned char> vk(k.begin(), k.end()), vv(v.begin(), v.end());
        return Write(CFlatData(vk), CFlatData(vv), true);
    }
};

void Reset()
{
    LOCK(bitdb.cs_db);
    bitdb.RemoveDb(WALLET); // in mock mode this drops the in-memory database
    bitdb.mapFileUseCount.erase(WALLET);
}
} // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    fuzz::Setup("wallet_load");
    bitdb.MakeMock();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Reset();
    FuzzedDataProvider fdp(data, size);
    {
        RawWriter db;
        while (fdp.remaining_bytes() > 0) {
            std::string k = fdp.ConsumeRandomLengthString();
            std::string v = fdp.ConsumeRandomLengthString();
            db.Put(k, v);
        }
    }

    {
        CWallet wallet(WALLET);
        bool fFirstRun = false;
        DBErrors ret = DB_LOAD_OK;
        try {
            ret = wallet.LoadWallet(fFirstRun);
        } catch (const std::exception& e) {
            fuzz::Fail(__FILE__, __LINE__, "LoadWallet threw", e.what());
        }
        FUZZ_CHECK(ret == DB_LOAD_OK || ret == DB_CORRUPT || ret == DB_NONCRITICAL_ERROR || ret == DB_TOO_NEW ||
                       ret == DB_LOAD_FAIL || ret == DB_NEED_REWRITE,
                   "LoadWallet returned an unknown DBErrors value");
        if (ret == DB_LOAD_OK) {
            // What the node does right after a load: balances and the keypool.
            wallet.GetBalance();
            wallet.GetUnconfirmedBalance();
            wallet.GetKeyPoolSize();
        }
    }
    Reset();
    return 0;
}
