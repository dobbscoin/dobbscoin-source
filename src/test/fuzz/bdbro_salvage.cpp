// Copyright (c) 2026 The Dobbscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// bdbro_salvage: arbitrary bytes as a damaged Berkeley DB wallet.dat, read
// with BerkeleyRO::Salvage (the page-scan reader behind -salvagewallet).
//
// Invariants:
//  - Salvage never throws and never crashes.
//  - It returns false only with no records, and a non-empty report either way.
//  - No key appears twice in what it returns ("first one wins").
//  - Where the strict ReadAll succeeds, Salvage returns exactly ReadAll's
//    records, in key order.
//  - Where it falls back to the page scan, the outer database's pointer to
//    the "main" subdatabase is never returned as a wallet record.
//  - The input file is never written to.

#include "fuzz_util.h"

#include "bdbro.h"

#include <set>

#include <boost/filesystem.hpp>

namespace
{
boost::filesystem::path g_file;
}

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    g_file = fuzz::Setup("bdbro_salvage") / "wallet.dat";
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    fuzz::WriteFile(g_file, data, size);
    const fuzz::Bytes input(data, data + size);

    BerkeleyRO::RecordMap strict;
    bool fStrict = false;
    try {
        BerkeleyRO::ReadAll(g_file, strict);
        fStrict = true;
    } catch (const std::runtime_error&) {
    }

    BerkeleyRO::RecordList records;
    std::string strReport;
    bool fRead = false;
    try {
        fRead = BerkeleyRO::Salvage(g_file, records, strReport);
    } catch (const std::exception& e) {
        fuzz::Fail(__FILE__, __LINE__, "Salvage threw", e.what());
    }

    FUZZ_CHECK(!strReport.empty(), "Salvage returned no report");
    if (!fRead)
        FUZZ_CHECK(records.empty(), "Salvage returned false but handed back records: " + strReport);

    std::set<fuzz::Bytes> keys;
    for (size_t i = 0; i < records.size(); i++)
        FUZZ_CHECK(keys.insert(records[i].first).second, "Salvage returned the same key twice: " + strReport);

    if (fStrict) {
        FUZZ_CHECK(fRead, "ReadAll succeeded but Salvage returned false: " + strReport);
        FUZZ_CHECK(records == BerkeleyRO::RecordList(strict.begin(), strict.end()),
                   "Salvage differs from ReadAll on a file ReadAll accepts: " + strReport);
    } else {
        static const unsigned char name[] = {'m', 'a', 'i', 'n'};
        for (size_t i = 0; i < records.size(); i++)
            FUZZ_CHECK(!(records[i].first == fuzz::Bytes(name, name + 4) && records[i].second.size() == 4),
                       "the page scan returned the outer \"main\" subdatabase pointer as a record");
    }
    FUZZ_CHECK(fuzz::ReadFile(g_file) == input, "Salvage modified the file it read");
    return 0;
}
