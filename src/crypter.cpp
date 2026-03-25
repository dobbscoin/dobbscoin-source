// Copyright (c) 2009-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license.

#include "crypter.h"

#include "script/script.h"
#include "script/standard.h"
#include "util.h"

#include <string>
#include <vector>
#include <boost/foreach.hpp>
#include <openssl/aes.h>
#include <openssl/evp.h>

bool CCrypter::SetKeyFromPassphrase(
    const SecureString& strKeyData,
    const std::vector<unsigned char>& chSalt,
    const unsigned int nRounds,
    const unsigned int nDerivationMethod)
{
    if (nRounds < 1 || chSalt.size() != WALLET_CRYPTO_SALT_SIZE)
        return false;

    int i = 0;

    if (nDerivationMethod == 0)
        i = EVP_BytesToKey(
            EVP_aes_256_cbc(),
            EVP_sha512(),
            &chSalt[0],
            (unsigned char*)&strKeyData[0],
            strKeyData.size(),
            nRounds,
            chKey,
            chIV);

    if (i != (int)WALLET_CRYPTO_KEY_SIZE)
    {
        OPENSSL_cleanse(chKey, sizeof(chKey));
        OPENSSL_cleanse(chIV, sizeof(chIV));
        return false;
    }

    fKeySet = true;
    return true;
}

bool CCrypter::SetKey(
    const CKeyingMaterial& chNewKey,
    const std::vector<unsigned char>& chNewIV)
{
    if (chNewKey.size() != WALLET_CRYPTO_KEY_SIZE ||
        chNewIV.size() != WALLET_CRYPTO_KEY_SIZE)
        return false;

    memcpy(&chKey[0], &chNewKey[0], sizeof chKey);
    memcpy(&chIV[0], &chNewIV[0], sizeof chIV);

    fKeySet = true;
    return true;
}

bool CCrypter::Encrypt(
    const CKeyingMaterial& vchPlaintext,
    std::vector<unsigned char>& vchCiphertext)
{
    if (!fKeySet)
        return false;

    int nLen = vchPlaintext.size();
    int nCLen = nLen + AES_BLOCK_SIZE;
    int nFLen = 0;

    vchCiphertext = std::vector<unsigned char>(nCLen);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;

    bool fOk = true;

    if (fOk)
        fOk = EVP_EncryptInit_ex(
                  ctx,
                  EVP_aes_256_cbc(),
                  NULL,
                  chKey,
                  chIV) != 0;

    if (fOk)
        fOk = EVP_EncryptUpdate(
                  ctx,
                  &vchCiphertext[0],
                  &nCLen,
                  &vchPlaintext[0],
                  nLen) != 0;

    if (fOk)
        fOk = EVP_EncryptFinal_ex(
                  ctx,
                  (&vchCiphertext[0]) + nCLen,
                  &nFLen) != 0;

    EVP_CIPHER_CTX_free(ctx);

    if (!fOk)
        return false;

    vchCiphertext.resize(nCLen + nFLen);
    return true;
}

bool CCrypter::Decrypt(
    const std::vector<unsigned char>& vchCiphertext,
    CKeyingMaterial& vchPlaintext)
{
    if (!fKeySet)
        return false;

    int nLen = vchCiphertext.size();
    int nPLen = nLen;
    int nFLen = 0;

    vchPlaintext = CKeyingMaterial(nPLen);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;

    bool fOk = true;

    if (fOk)
        fOk = EVP_DecryptInit_ex(
                  ctx,
                  EVP_aes_256_cbc(),
                  NULL,
                  chKey,
                  chIV) != 0;

    if (fOk)
        fOk = EVP_DecryptUpdate(
                  ctx,
                  &vchPlaintext[0],
                  &nPLen,
                  &vchCiphertext[0],
                  nLen) != 0;

    if (fOk)
        fOk = EVP_DecryptFinal_ex(
                  ctx,
                  (&vchPlaintext[0]) + nPLen,
                  &nFLen) != 0;

    EVP_CIPHER_CTX_free(ctx);

    if (!fOk)
        return false;

    vchPlaintext.resize(nPLen + nFLen);
    return true;
}

bool EncryptSecret(
    const CKeyingMaterial& vMasterKey,
    const CKeyingMaterial& vchPlaintext,
    const uint256& nIV,
    std::vector<unsigned char>& vchCiphertext)
{
    CCrypter cKeyCrypter;

    std::vector<unsigned char> chIV(WALLET_CRYPTO_KEY_SIZE);

    memcpy(&chIV[0], &nIV, WALLET_CRYPTO_KEY_SIZE);

    if (!cKeyCrypter.SetKey(vMasterKey, chIV))
        return false;

    return cKeyCrypter.Encrypt(
        *((const CKeyingMaterial*)&vchPlaintext),
        vchCiphertext);
}

bool DecryptSecret(
    const CKeyingMaterial& vMasterKey,
    const std::vector<unsigned char>& vchCiphertext,
    const uint256& nIV,
    CKeyingMaterial& vchPlaintext)
{
    CCrypter cKeyCrypter;

    std::vector<unsigned char> chIV(WALLET_CRYPTO_KEY_SIZE);

    memcpy(&chIV[0], &nIV, WALLET_CRYPTO_KEY_SIZE);

    if (!cKeyCrypter.SetKey(vMasterKey, chIV))
        return false;

    return cKeyCrypter.Decrypt(
        vchCiphertext,
        *((CKeyingMaterial*)&vchPlaintext));
}

bool CCryptoKeyStore::SetCrypted()
{
    LOCK(cs_KeyStore);

    if (fUseCrypto)
        return true;
    if (!mapKeys.empty())
        return false;

    fUseCrypto = true;
    return true;
}

bool CCryptoKeyStore::Lock()
{
    if (!SetCrypted())
        return false;

    {
        LOCK(cs_KeyStore);
        vMasterKey.clear();
    }

    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::Unlock(const CKeyingMaterial& vMasterKeyIn)
{
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;

        bool keyPass = mapCryptedKeys.empty();
        CryptedKeyMap::const_iterator mi = mapCryptedKeys.begin();
        while (mi != mapCryptedKeys.end()) {
            const CPubKey& vchPubKey = (*mi).second.first;
            const std::vector<unsigned char>& vchCryptedSecret = (*mi).second.second;
            CKeyingMaterial vchSecret;
            if (!DecryptSecret(vMasterKeyIn, vchCryptedSecret, vchPubKey.GetHash(), vchSecret)) {
                keyPass = false;
                break;
            }
            CKey key;
            key.Set(vchSecret.begin(), vchSecret.end(), vchPubKey.IsCompressed());
            if (!key.IsValid() || key.GetPubKey() != vchPubKey) {
                keyPass = false;
                break;
            }
            if (fDecryptionThoroughlyChecked)
                break;
            ++mi;
        }

        if (!keyPass)
            return false;

        vMasterKey = vMasterKeyIn;
        fDecryptionThoroughlyChecked = true;
    }

    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
    if (!SetCrypted())
        return false;

    LOCK(cs_KeyStore);
    mapCryptedKeys[vchPubKey.GetID()] = std::make_pair(vchPubKey, vchCryptedSecret);
    return true;
}

bool CCryptoKeyStore::AddKeyPubKey(const CKey& key, const CPubKey &pubkey)
{
    LOCK(cs_KeyStore);

    if (!IsCrypted())
        return CBasicKeyStore::AddKeyPubKey(key, pubkey);
    if (IsLocked())
        return false;
    if (!key.VerifyPubKey(pubkey))
        return false;

    std::vector<unsigned char> vchCryptedSecret;
    if (!EncryptSecret(vMasterKey, key.GetPrivKey(), pubkey.GetHash(), vchCryptedSecret))
        return false;

    return AddCryptedKey(pubkey, vchCryptedSecret);
}

bool CCryptoKeyStore::GetKey(const CKeyID &address, CKey& keyOut) const
{
    LOCK(cs_KeyStore);

    if (!IsCrypted())
        return CBasicKeyStore::GetKey(address, keyOut);
    if (IsLocked())
        return false;

    CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
    if (mi != mapCryptedKeys.end()) {
        const CPubKey& vchPubKey = (*mi).second.first;
        const std::vector<unsigned char>& vchCryptedSecret = (*mi).second.second;
        CKeyingMaterial vchSecret;
        if (!DecryptSecret(vMasterKey, vchCryptedSecret, vchPubKey.GetHash(), vchSecret))
            return false;
        keyOut.Set(vchSecret.begin(), vchSecret.end(), vchPubKey.IsCompressed());
        return keyOut.IsValid();
    }
    return false;
}

bool CCryptoKeyStore::GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    LOCK(cs_KeyStore);

    if (!IsCrypted())
        return CBasicKeyStore::GetPubKey(address, vchPubKeyOut);

    CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
    if (mi != mapCryptedKeys.end()) {
        vchPubKeyOut = (*mi).second.first;
        return true;
    }
    return false;
}

bool CCryptoKeyStore::EncryptKeys(CKeyingMaterial& vMasterKeyIn)
{
    LOCK(cs_KeyStore);

    if (!mapCryptedKeys.empty() || IsCrypted())
        return false;
    if (!SetCrypted())
        return false;

    BOOST_FOREACH(KeyMap::value_type& mKey, mapKeys)
    {
        const CKey &key = mKey.second;
        CPubKey vchPubKey = key.GetPubKey();
        if (!key.VerifyPubKey(vchPubKey))
            return false;

        std::vector<unsigned char> vchCryptedSecret;
        if (!EncryptSecret(vMasterKeyIn, key.GetPrivKey(), vchPubKey.GetHash(), vchCryptedSecret))
            return false;
        if (!AddCryptedKey(vchPubKey, vchCryptedSecret))
            return false;
    }
    mapKeys.clear();
    return true;
}
