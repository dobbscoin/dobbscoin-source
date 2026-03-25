// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DOBBSCOIN_TEST_BIGNUM_H
#define DOBBSCOIN_TEST_BIGNUM_H

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <vector>

#include <openssl/bn.h>

class bignum_error : public std::runtime_error
{
public:
    explicit bignum_error(const std::string& str) : std::runtime_error(str) {}
};

class CBigNum
{
private:
    struct BNDeleter {
        void operator()(BIGNUM* bn) const
        {
            BN_clear_free(bn);
        }
    };

    std::unique_ptr<BIGNUM, BNDeleter> bn_;

    static BIGNUM* RequireNew()
    {
        BIGNUM* bn = BN_new();
        if (bn == NULL)
            throw bignum_error("CBigNum: BN_new failed");
        return bn;
    }

    BIGNUM* get()
    {
        return bn_.get();
    }

    const BIGNUM* get() const
    {
        return bn_.get();
    }

public:
    CBigNum() : bn_(RequireNew()) {}

    CBigNum(const CBigNum& b) : bn_(RequireNew())
    {
        if (BN_copy(get(), b.get()) == NULL)
            throw bignum_error("CBigNum::CBigNum(const CBigNum&) : BN_copy failed");
    }

    CBigNum& operator=(const CBigNum& b)
    {
        if (this != &b && BN_copy(get(), b.get()) == NULL)
            throw bignum_error("CBigNum::operator= : BN_copy failed");
        return (*this);
    }

    CBigNum(long long n) : bn_(RequireNew()) { setint64(n); }

    explicit CBigNum(const std::vector<unsigned char>& vch) : bn_(RequireNew())
    {
        setvch(vch);
    }

    int getint() const
    {
        BN_ULONG n = BN_get_word(get());
        if (!BN_is_negative(get()))
            return (n > (BN_ULONG)std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : (int)n);
        return (n > (BN_ULONG)std::numeric_limits<int>::max() ? std::numeric_limits<int>::min() : -(int)n);
    }

    void setint64(int64_t sn)
    {
        unsigned char pch[sizeof(sn) + 6];
        unsigned char* p = pch + 4;
        bool fNegative;
        uint64_t n;

        if (sn < (int64_t)0)
        {
            n = -(sn + 1);
            ++n;
            fNegative = true;
        }
        else
        {
            n = sn;
            fNegative = false;
        }

        bool fLeadingZeroes = true;
        for (int i = 0; i < 8; i++)
        {
            unsigned char c = (n >> 56) & 0xff;
            n <<= 8;
            if (fLeadingZeroes)
            {
                if (c == 0)
                    continue;
                if (c & 0x80)
                    *p++ = (fNegative ? 0x80 : 0);
                else if (fNegative)
                    c |= 0x80;
                fLeadingZeroes = false;
            }
            *p++ = c;
        }
        unsigned int nSize = p - (pch + 4);
        pch[0] = (nSize >> 24) & 0xff;
        pch[1] = (nSize >> 16) & 0xff;
        pch[2] = (nSize >> 8) & 0xff;
        pch[3] = (nSize) & 0xff;
        if (BN_mpi2bn(pch, p - pch, get()) == NULL)
            throw bignum_error("CBigNum::setint64 : BN_mpi2bn failed");
    }

    void setvch(const std::vector<unsigned char>& vch)
    {
        std::vector<unsigned char> vch2(vch.size() + 4);
        unsigned int nSize = vch.size();
        vch2[0] = (nSize >> 24) & 0xff;
        vch2[1] = (nSize >> 16) & 0xff;
        vch2[2] = (nSize >> 8) & 0xff;
        vch2[3] = (nSize >> 0) & 0xff;
        reverse_copy(vch.begin(), vch.end(), vch2.begin() + 4);
        if (BN_mpi2bn(&vch2[0], vch2.size(), get()) == NULL)
            throw bignum_error("CBigNum::setvch : BN_mpi2bn failed");
    }

    std::vector<unsigned char> getvch() const
    {
        unsigned int nSize = BN_bn2mpi(get(), NULL);
        if (nSize <= 4)
            return std::vector<unsigned char>();
        std::vector<unsigned char> vch(nSize);
        BN_bn2mpi(get(), &vch[0]);
        vch.erase(vch.begin(), vch.begin() + 4);
        reverse(vch.begin(), vch.end());
        return vch;
    }

    friend inline const CBigNum operator+(const CBigNum& a, const CBigNum& b);
    friend inline const CBigNum operator-(const CBigNum& a, const CBigNum& b);
    friend inline const CBigNum operator-(const CBigNum& a);
    friend inline bool operator==(const CBigNum& a, const CBigNum& b);
    friend inline bool operator!=(const CBigNum& a, const CBigNum& b);
    friend inline bool operator<=(const CBigNum& a, const CBigNum& b);
    friend inline bool operator>=(const CBigNum& a, const CBigNum& b);
    friend inline bool operator<(const CBigNum& a, const CBigNum& b);
    friend inline bool operator>(const CBigNum& a, const CBigNum& b);
};

inline const CBigNum operator+(const CBigNum& a, const CBigNum& b)
{
    CBigNum r;
    if (!BN_add(r.get(), a.get(), b.get()))
        throw bignum_error("CBigNum::operator+ : BN_add failed");
    return r;
}

inline const CBigNum operator-(const CBigNum& a, const CBigNum& b)
{
    CBigNum r;
    if (!BN_sub(r.get(), a.get(), b.get()))
        throw bignum_error("CBigNum::operator- : BN_sub failed");
    return r;
}

inline const CBigNum operator-(const CBigNum& a)
{
    CBigNum r(a);
    BN_set_negative(r.get(), !BN_is_negative(r.get()));
    return r;
}

inline bool operator==(const CBigNum& a, const CBigNum& b) { return (BN_cmp(a.get(), b.get()) == 0); }
inline bool operator!=(const CBigNum& a, const CBigNum& b) { return (BN_cmp(a.get(), b.get()) != 0); }
inline bool operator<=(const CBigNum& a, const CBigNum& b) { return (BN_cmp(a.get(), b.get()) <= 0); }
inline bool operator>=(const CBigNum& a, const CBigNum& b) { return (BN_cmp(a.get(), b.get()) >= 0); }
inline bool operator<(const CBigNum& a, const CBigNum& b)  { return (BN_cmp(a.get(), b.get()) < 0); }
inline bool operator>(const CBigNum& a, const CBigNum& b)  { return (BN_cmp(a.get(), b.get()) > 0); }

#endif // DOBBSCOIN_TEST_BIGNUM_H
