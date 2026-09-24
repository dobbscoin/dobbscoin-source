// Copyright (c) 2026 The Dobbscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DOBBSCOIN_TEST_FUZZ_FUZZ_UTIL_H
#define DOBBSCOIN_TEST_FUZZ_FUZZ_UTIL_H

// Shared plumbing for the wallet-file fuzz harnesses (see doc/fuzzing.md).

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include <boost/filesystem/path.hpp>

namespace fuzz
{
typedef std::vector<unsigned char> Bytes;
typedef std::vector<std::pair<Bytes, Bytes> > Records;

/** The fixed clock every harness runs on, so backup names (".bdb-<t>") are reproducible. */
const int64_t MOCK_TIME = 1700000000;

/**
 * Once per process: make a private scratch directory (under $BOB_FUZZ_TMPDIR,
 * else /dev/shm, else the system temp dir), point -datadir at it, switch off
 * debug.log, select mainnet params and freeze the clock at MOCK_TIME.
 * Returns the directory, which is also GetDataDir(). It is removed at exit;
 * one left behind by a crashed process is removed by the next Setup.
 */
const boost::filesystem::path& Setup(const std::string& strName);

/** A second private directory beside the first, for scratch copies. */
const boost::filesystem::path& SideDir();

void WriteFile(const boost::filesystem::path& p, const uint8_t* data, size_t size);
Bytes ReadFile(const boost::filesystem::path& p);
/** Sorted names of everything in dir. */
std::vector<std::string> List(const boost::filesystem::path& dir);
/** Remove everything in dir, keep dir. */
void Clear(const boost::filesystem::path& dir);
/** The names, joined, for failure messages. */
std::string Join(const std::vector<std::string>& v);

/** An invariant failed: print it and abort, which libFuzzer records as a crash. */
void Fail(const char* file, int line, const char* cond, const std::string& msg);
} // namespace fuzz

#define FUZZ_CHECK(cond, msg)                                    \
    do {                                                         \
        if (!(cond))                                             \
            fuzz::Fail(__FILE__, __LINE__, #cond, (msg));        \
    } while (0)

#endif // DOBBSCOIN_TEST_FUZZ_FUZZ_UTIL_H
