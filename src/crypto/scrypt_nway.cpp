// Copyright (c) 2026 The Dobbscoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Multi-way scrypt for the built-in miner. See scrypt_nway.h: nothing that
// validates a block uses this file.
//
// Each lane is exactly scrypt_1024_1_1_256() of crypto/scrypt.cpp:
//
//   B = PBKDF2-HMAC-SHA256(P = header, S = header, c = 1, 128 bytes)
//   X = ROMix(B)                         (the salsa20/8 core, N = 1024, r = 1)
//   out = PBKDF2-HMAC-SHA256(P = header, S = X, c = 1, 32 bytes)
//
// The core runs in pooler's SIMD assembly (crypto/scrypt-x64.S) for three or
// six lanes at once. The PBKDF2 steps run per lane here, on the same OpenSSL
// SHA-256 the generic code uses; they are a few percent of the work. (pooler's
// multi-lane SHA-256, sha2.c and sha2-x64.S, is GPL and is not used.)

#include "crypto/scrypt_nway.h"

#include "crypto/scrypt.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <openssl/sha.h>

#if defined(__x86_64__) && !defined(__ILP32__) && defined(__GNUC__)
#define SCRYPT_NWAY_X86_64 1
#include <cpuid.h>

// crypto/scrypt-x64.S. X holds the lanes' 32-word states back to back, V is
// the scratchpad (128 * N bytes per lane).
extern "C" void scrypt_core_3way(uint32_t* X, uint32_t* V, int N);      // AVX if CPU+OS allow, else SSE2
extern "C" void scrypt_core_3way_sse2(uint32_t* X, uint32_t* V, int N); // always SSE2
extern "C" void scrypt_core_6way(uint32_t* X, uint32_t* V, int N);      // AVX2 only
#endif

namespace {

static const int SCRYPT_N = 1024;
static const size_t LANE_BYTES = 128 * SCRYPT_N; // V per lane
static const size_t SCRATCH_ALIGN = 128;

// ---------------------------------------------------------------------------
// CPU features
// ---------------------------------------------------------------------------
struct CpuFeatures {
    bool sse2 = false;
    bool avx = false;       // CPU has AVX, OS saves YMM state
    bool avx2 = false;      // and CPU has AVX2
    bool cpu_avx = false;   // CPUID bits alone, for the log
    bool cpu_avx2 = false;
    bool os_ymm = false;
};

CpuFeatures DetectCpu()
{
    CpuFeatures f;
#ifdef SCRYPT_NWAY_X86_64
    f.sse2 = true; // part of the x86_64 baseline
    unsigned int eax, ebx, ecx, edx;
    unsigned int max_leaf = __get_cpuid_max(0, nullptr);
    if (max_leaf >= 1) {
        __cpuid(1, eax, ebx, ecx, edx);
        const bool osxsave = (ecx >> 27) & 1;
        f.cpu_avx = (ecx >> 28) & 1;
        if (osxsave) {
            // XCR0 bits 1 (SSE) and 2 (AVX): the OS saves XMM and YMM state
            // across context switches. Without it AVX code corrupts registers.
            uint32_t xcr0_lo, xcr0_hi;
            __asm__ __volatile__("xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));
            f.os_ymm = (xcr0_lo & 6) == 6;
        }
        if (max_leaf >= 7) {
            __cpuid_count(7, 0, eax, ebx, ecx, edx);
            f.cpu_avx2 = (ebx >> 5) & 1;
        }
        f.avx = f.cpu_avx && osxsave && f.os_ymm;
        f.avx2 = f.avx && f.cpu_avx2;
    }
#endif
    return f;
}

const CpuFeatures& Cpu()
{
    static const CpuFeatures features = DetectCpu(); // thread-safe init (C++11)
    return features;
}

#ifdef SCRYPT_NWAY_X86_64
// ---------------------------------------------------------------------------
// PBKDF2-HMAC-SHA256 with c = 1, keyed by an 80-byte header
// ---------------------------------------------------------------------------
// Same computation as PBKDF2_SHA256(..., c = 1, ...) in crypto/scrypt.cpp. The
// keyed inner and outer states are computed once per lane and used for both
// PBKDF2 calls, since both take the header as the password.
struct HmacKey {
    SHA256_CTX ictx;
    SHA256_CTX octx;
};

void HmacKeyInit(HmacKey& key, const unsigned char* header80)
{
    // An 80-byte key is longer than the 64-byte block: the key is SHA256(K).
    unsigned char khash[32];
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, header80, 80);
    SHA256_Final(khash, &ctx);

    unsigned char pad[64];
    memset(pad, 0x36, 64);
    for (int i = 0; i < 32; i++)
        pad[i] ^= khash[i];
    SHA256_Init(&key.ictx);
    SHA256_Update(&key.ictx, pad, 64);

    memset(pad, 0x5c, 64);
    for (int i = 0; i < 32; i++)
        pad[i] ^= khash[i];
    SHA256_Init(&key.octx);
    SHA256_Update(&key.octx, pad, 64);
}

void Pbkdf2Sha256C1(const HmacKey& key, const unsigned char* salt, size_t saltlen, unsigned char* out, size_t dklen)
{
    SHA256_CTX salted = key.ictx;
    SHA256_Update(&salted, salt, saltlen);
    for (size_t i = 0; i * 32 < dklen; i++) {
        const uint32_t n = (uint32_t)(i + 1);
        const unsigned char ivec[4] = {(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n};
        unsigned char ihash[32], u[32];

        SHA256_CTX ictx = salted;
        SHA256_Update(&ictx, ivec, 4);
        SHA256_Final(ihash, &ictx);

        SHA256_CTX octx = key.octx;
        SHA256_Update(&octx, ihash, 32);
        SHA256_Final(u, &octx);

        const size_t clen = (dklen - i * 32 < 32) ? dklen - i * 32 : 32;
        memcpy(out + i * 32, u, clen);
    }
}

unsigned char* AlignScratch(unsigned char* scratchpad)
{
    return (unsigned char*)(((uintptr_t)scratchpad + SCRATCH_ALIGN - 1) & ~(uintptr_t)(SCRATCH_ALIGN - 1));
}

// X is scrypt's B read as little-endian 32-bit words. x86 is little-endian,
// so the PBKDF2 output bytes ARE the words: no conversion either way.
typedef void (*ScryptCoreFn)(uint32_t* X, uint32_t* V, int N);

template <int LANES>
void ScryptSimd(ScryptCoreFn core, const unsigned char* input, unsigned char* output, unsigned char* scratchpad)
{
    alignas(128) uint32_t X[LANES * 32];
    HmacKey keys[LANES];
    for (int l = 0; l < LANES; l++) {
        HmacKeyInit(keys[l], input + 80 * l);
        Pbkdf2Sha256C1(keys[l], input + 80 * l, 80, (unsigned char*)&X[32 * l], 128);
    }
    core(X, (uint32_t*)AlignScratch(scratchpad), SCRYPT_N);
    for (int l = 0; l < LANES; l++)
        Pbkdf2Sha256C1(keys[l], (const unsigned char*)&X[32 * l], 128, output + 32 * l, 32);
}
#endif

} // namespace

const char* ScryptImplName(ScryptImpl impl)
{
    switch (impl) {
    case ScryptImpl::GENERIC: return "generic";
    case ScryptImpl::SSE2: return "sse2";
    case ScryptImpl::AVX: return "avx";
    case ScryptImpl::AVX2: return "avx2";
    }
    return "unknown";
}

std::string ScryptImplDescription(ScryptImpl impl)
{
    const char* label = "generic";
    switch (impl) {
    case ScryptImpl::GENERIC: label = "generic"; break;
    case ScryptImpl::SSE2: label = "SSE2"; break;
    case ScryptImpl::AVX: label = "AVX"; break;
    case ScryptImpl::AVX2: label = "AVX2"; break;
    }
    return std::string(label) + " (" + std::to_string(ScryptImplLanes(impl)) + "-way)";
}

bool ScryptImplFromName(const std::string& name, ScryptImpl& impl)
{
    for (int i = 0; i < SCRYPT_IMPL_COUNT; i++) {
        if (name == ScryptImplName((ScryptImpl)i)) {
            impl = (ScryptImpl)i;
            return true;
        }
    }
    return false;
}

int ScryptImplLanes(ScryptImpl impl)
{
    switch (impl) {
    case ScryptImpl::GENERIC: return 1;
    case ScryptImpl::SSE2: return 3;
    case ScryptImpl::AVX: return 3;
    case ScryptImpl::AVX2: return 6;
    }
    return 1;
}

bool ScryptImplSupported(ScryptImpl impl)
{
    switch (impl) {
    case ScryptImpl::GENERIC: return true;
    case ScryptImpl::SSE2: return Cpu().sse2;
    case ScryptImpl::AVX: return Cpu().avx;
    case ScryptImpl::AVX2: return Cpu().avx2;
    }
    return false;
}

ScryptImpl ScryptImplBest()
{
    if (ScryptImplSupported(ScryptImpl::AVX2)) return ScryptImpl::AVX2;
    if (ScryptImplSupported(ScryptImpl::AVX)) return ScryptImpl::AVX;
    if (ScryptImplSupported(ScryptImpl::SSE2)) return ScryptImpl::SSE2;
    return ScryptImpl::GENERIC;
}

std::string ScryptCpuFeatures()
{
#ifdef SCRYPT_NWAY_X86_64
    const CpuFeatures& f = Cpu();
    std::string s = "x86_64 sse2";
    if (f.cpu_avx) s += " avx";
    if (f.cpu_avx2) s += " avx2";
    s += f.os_ymm ? " (OS saves YMM)" : " (OS does not save YMM)";
    return s;
#else
    return "no x86_64 SIMD scrypt in this build";
#endif
}

size_t ScryptNwayScratchpadSize(ScryptImpl impl)
{
    return LANE_BYTES * ScryptImplLanes(impl) + SCRATCH_ALIGN - 1;
}

void scrypt_1024_1_1_256_nway(ScryptImpl impl, const unsigned char* input, unsigned char* output, unsigned char* scratchpad)
{
    assert(ScryptImplSupported(impl));
    switch (impl) {
#ifdef SCRYPT_NWAY_X86_64
    case ScryptImpl::SSE2:
        ScryptSimd<3>(scrypt_core_3way_sse2, input, output, scratchpad);
        return;
    case ScryptImpl::AVX:
        // scrypt_core_3way picks its AVX code itself when CPU and OS allow it,
        // which ScryptImplSupported(AVX) has just checked the same way.
        ScryptSimd<3>(scrypt_core_3way, input, output, scratchpad);
        return;
    case ScryptImpl::AVX2:
        ScryptSimd<6>(scrypt_core_6way, input, output, scratchpad);
        return;
#endif
    default:
        // The generic code aligns its own 64-byte window in the scratchpad.
        scrypt_1024_1_1_256_sp((const char*)input, (char*)output, (char*)scratchpad);
        return;
    }
}
