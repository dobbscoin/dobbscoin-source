// Copyright (c) 2026 The Dobbscoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DOBBSCOIN_CRYPTO_SCRYPT_NWAY_H
#define DOBBSCOIN_CRYPTO_SCRYPT_NWAY_H

// Multi-way scrypt(N=1024, r=1, p=1) for the built-in CPU MINER ONLY.
//
// Nothing that validates a block may call these. GetPoWHash(), and with it
// CheckProofOfWork() and every other consensus check, stays on the generic
// scrypt_1024_1_1_256() in crypto/scrypt.cpp. The miner re-checks every
// solution these functions claim with that generic function before it
// submits anything.
//
// On x86_64 the scrypt core comes from pooler's cpuminer (crypto/scrypt-x64.S,
// 2-clause BSD); everywhere else only the generic implementation exists.

#include <stddef.h>
#include <string>

enum class ScryptImpl : int {
    GENERIC = 0, //!< crypto/scrypt.cpp, one hash per call
    SSE2 = 1,    //!< 3-way, SSE2 (every x86_64 CPU)
    AVX = 2,     //!< 3-way, AVX encoding of the same code
    AVX2 = 3,    //!< 6-way, AVX2
};

static const int SCRYPT_IMPL_COUNT = 4;
static const int SCRYPT_NWAY_MAX_LANES = 6;

/** Short name, as -minerscrypt takes it: "generic", "sse2", "avx", "avx2". */
const char* ScryptImplName(ScryptImpl impl);
/** For the log: "AVX2 (6-way)". */
std::string ScryptImplDescription(ScryptImpl impl);
/** Parse a short name. Returns false if it names no implementation. */
bool ScryptImplFromName(const std::string& name, ScryptImpl& impl);
/** Hashes computed per call of scrypt_1024_1_1_256_nway(). */
int ScryptImplLanes(ScryptImpl impl);
/** Compiled in, and this CPU and OS can run it (cpuid, and XGETBV for AVX). */
bool ScryptImplSupported(ScryptImpl impl);
/** The fastest supported implementation. */
ScryptImpl ScryptImplBest();
/** What this CPU reports, for the log: "sse2 avx avx2 (os ymm on)". */
std::string ScryptCpuFeatures();

/** Scratchpad bytes scrypt_1024_1_1_256_nway() needs (alignment slack included). */
size_t ScryptNwayScratchpadSize(ScryptImpl impl);

/**
 * Hash ScryptImplLanes(impl) 80-byte inputs, laid out back to back in input,
 * into as many 32-byte outputs, back to back in output. impl must be
 * supported (asserted). scratchpad must hold ScryptNwayScratchpadSize(impl)
 * bytes and must not be shared between threads.
 */
void scrypt_1024_1_1_256_nway(ScryptImpl impl, const unsigned char* input, unsigned char* output, unsigned char* scratchpad);

#endif // DOBBSCOIN_CRYPTO_SCRYPT_NWAY_H
