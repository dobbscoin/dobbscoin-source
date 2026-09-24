// Copyright (c) 2026 The Dobbscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// bdbro_parse: arbitrary bytes as a Berkeley DB wallet.dat, read with
// BerkeleyRO::ReadAll (the strict reader the v0.14.0 migration uses).
//
// Invariants:
//  - ReadAll either returns or throws std::runtime_error. Anything else
//    (another exception type, a crash, a sanitizer report) is a bug.
//  - A successful read implies IsBerkeleyBtreeFile() and a file of at least
//    one 512-byte page.
//  - The input file is never written to.
//  - Reading the same file twice gives the same records.

#include "fuzz_util.h"

#include "bdbro.h"

#include <boost/filesystem.hpp>

namespace
{
boost::filesystem::path g_file;
}

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    g_file = fuzz::Setup("bdbro_parse") / "wallet.dat";
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    fuzz::WriteFile(g_file, data, size);
    const fuzz::Bytes input(data, data + size);
    const bool fMagic = BerkeleyRO::IsBerkeleyBtreeFile(g_file);

    BerkeleyRO::RecordMap records;
    bool fOk = false;
    try {
        BerkeleyRO::ReadAll(g_file, records);
        fOk = true;
    } catch (const std::runtime_error& e) {
        FUZZ_CHECK(e.what()[0] != '\0', "ReadAll threw an empty message");
    }

    if (fOk) {
        FUZZ_CHECK(fMagic, "ReadAll accepted a file without the btree magic");
        FUZZ_CHECK(size >= 512, "ReadAll accepted a file shorter than one page");
        BerkeleyRO::RecordMap again;
        BerkeleyRO::ReadAll(g_file, again); // must not throw the second time either
        FUZZ_CHECK(again == records, "a second ReadAll of the same file returned different records");
    }
    FUZZ_CHECK(fuzz::ReadFile(g_file) == input, "ReadAll modified the file it read");
    return 0;
}
