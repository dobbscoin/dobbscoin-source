// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pubkey.h"

#include "eccryptoverify.h"

#include <secp256k1.h>
#include "secp256k1/contrib/lax_der_parsing.h"

#ifndef USE_SECP256K1
#include "ecwrapper.h"
#endif

namespace {

/**
 * Verification context for consensus signature checking.
 *
 * Built once and never mutated, which is what makes it safe to share across
 * the nScriptCheckThreads workers that call CPubKey::Verify concurrently.
 * Since libsecp256k1 0.2 no precomputed table is needed for verification, so
 * SECP256K1_CONTEXT_NONE is the correct flag.
 */
class CSecp256k1VerifyContext
{
public:
    CSecp256k1VerifyContext() : ctx(secp256k1_context_create(SECP256K1_CONTEXT_NONE)) {}
    ~CSecp256k1VerifyContext() { secp256k1_context_destroy(ctx); ctx = NULL; }
    secp256k1_context* ctx;
};

static CSecp256k1VerifyContext verifyContext;

} // namespace

bool CPubKey::Verify(const uint256 &hash, const std::vector<unsigned char>& vchSig) const {
    if (!IsValid())
        return false;
    if (vchSig.empty())
        return false;

    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_parse(verifyContext.ctx, &pubkey, begin(), size()))
        return false;

    // Parse leniently on purpose. Signatures mined before BIP66 was enforced
    // may carry non-canonical DER that OpenSSL accepted, and rejecting them
    // here would be a consensus change rather than a cleanup. Strictness is
    // BIP66's job, applied separately via SCRIPT_VERIFY_DERSIG.
    secp256k1_ecdsa_signature sig;
    if (!ecdsa_signature_parse_der_lax(verifyContext.ctx, &sig, &vchSig[0], vchSig.size()))
        return false;

    // libsecp256k1 accepts only low-S signatures; OpenSSL accepted both, and
    // high-S signatures are valid under this chain's consensus rules. Without
    // this normalisation a libsecp256k1 build would reject blocks an OpenSSL
    // build accepts -- a chain split, not a stricter node.
    secp256k1_ecdsa_signature_normalize(verifyContext.ctx, &sig, &sig);

    return secp256k1_ecdsa_verify(verifyContext.ctx, &sig, hash.begin(), &pubkey) == 1;
}

bool CPubKey::RecoverCompact(const uint256 &hash, const std::vector<unsigned char>& vchSig) {
    if (vchSig.size() != 65)
        return false;
    int recid = (vchSig[0] - 27) & 3;
    bool fComp = ((vchSig[0] - 27) & 4) != 0;
    CECKey key;
    if (!key.Recover(hash, &vchSig[1], recid))
        return false;
    std::vector<unsigned char> pubkey;
    key.GetPubKey(pubkey, fComp);
    Set(pubkey.begin(), pubkey.end());
    return true;
}

bool CPubKey::IsFullyValid() const {
    if (!IsValid())
        return false;
    CECKey key;
    if (!key.SetPubKey(begin(), size()))
        return false;
    return true;
}

bool CPubKey::Decompress() {
    if (!IsValid())
        return false;
    CECKey key;
    if (!key.SetPubKey(begin(), size()))
        return false;
    std::vector<unsigned char> pubkey;
    key.GetPubKey(pubkey, false);
    Set(pubkey.begin(), pubkey.end());
    return true;
}

bool CPubKey::Derive(CPubKey& pubkeyChild, unsigned char ccChild[32], unsigned int nChild, const unsigned char cc[32]) const {
    assert(IsValid());
    assert((nChild >> 31) == 0);
    assert(begin() + 33 == end());
    unsigned char out[64];
    BIP32Hash(cc, nChild, *begin(), begin()+1, out);
    memcpy(ccChild, out+32, 32);
    CECKey key;
    bool ret = key.SetPubKey(begin(), size());
    ret &= key.TweakPublic(out);
    std::vector<unsigned char> pubkey;
    key.GetPubKey(pubkey, true);
    pubkeyChild.Set(pubkey.begin(), pubkey.end());
    return ret;
}

void CExtPubKey::Encode(unsigned char code[74]) const {
    code[0] = nDepth;
    memcpy(code+1, vchFingerprint, 4);
    code[5] = (nChild >> 24) & 0xFF; code[6] = (nChild >> 16) & 0xFF;
    code[7] = (nChild >>  8) & 0xFF; code[8] = (nChild >>  0) & 0xFF;
    memcpy(code+9, vchChainCode, 32);
    assert(pubkey.size() == 33);
    memcpy(code+41, pubkey.begin(), 33);
}

void CExtPubKey::Decode(const unsigned char code[74]) {
    nDepth = code[0];
    memcpy(vchFingerprint, code+1, 4);
    nChild = (code[5] << 24) | (code[6] << 16) | (code[7] << 8) | code[8];
    memcpy(vchChainCode, code+9, 32);
    pubkey.Set(code+41, code+74);
}

bool CExtPubKey::Derive(CExtPubKey &out, unsigned int nChild) const {
    out.nDepth = nDepth + 1;
    CKeyID id = pubkey.GetID();
    memcpy(&out.vchFingerprint[0], &id, 4);
    out.nChild = nChild;
    return pubkey.Derive(out.pubkey, out.vchChainCode, nChild, vchChainCode);
}
