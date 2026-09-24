#!/usr/bin/env python3
# Copyright (c) 2026 The Dobbscoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Regenerate src/qt/test/paymentrequestdata.h, the BIP70 fixtures used by
test_dobbscoin-qt (PaymentServerTests).

Why this exists: the original fixtures (inherited from Bitcoin Core, made by
hand with openssl in 2012-2013) had a test CA valid until 2022-12-08 and
intermediates valid until 2023-02-21. After those dates every "valid" case
failed the GUI's own date check in src/qt/paymentrequestplus.cpp, and
PaymentServerTests failed in every build. Bitcoin Core never regenerated them;
it removed BIP70 in 0.20/0.21 before they expired, so there is no upstream
generator to copy. This script rebuilds the same fixtures with the same
structure, and far-future expiry for everything that is meant to be valid.

What it produces (same variable names and meaning as before):

  caCert_BASE64          self-signed "PaymentRequest Test CA", the trusted root
  paymentrequest1_BASE64 signed by testmerchant.org, cert issued by the root
  paymentrequest2_BASE64 signed by expiredmerchant.org, cert EXPIRED (2013)
  paymentrequest3_BASE64 10-long chain: root -> testca1..testca8 -> testmerchant8.org
  paymentrequest4_BASE64 same chain, but testca5 is an EXPIRED (2013) cert
                         issued directly by the root, for the same key
  paymentrequest5_BASE64 testmerchant.org, validly signed, but by a different
                         CA that is also called "PaymentRequest Test CA" and is
                         NOT in the test's root store

Every key is fresh on each run, so the output differs byte-for-byte between
runs; the test only depends on the structure above.

Usage (from the repository root):

    contrib/devtools/gen-paymentrequest-testdata.py > src/qt/test/paymentrequestdata.h

Requires python3 and python3-cryptography (Debian/Ubuntu package
python3-cryptography, or `pip install cryptography`). Protobuf encoding is
done by hand below, so no protobuf Python module is needed.
"""

import base64
import datetime
import sys

from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.hazmat.primitives.serialization import Encoding
from cryptography.x509.oid import NameOID

KEY_BITS = 2048

# Validity windows. VALID_* must bracket any date the test will ever run on.
# EXPIRED_* is deliberately in the past: those cases must stay expired.
VALID_FROM = datetime.datetime(2013, 1, 1, 0, 0, 0)
VALID_UNTIL = datetime.datetime(2099, 12, 31, 23, 59, 59)
EXPIRED_FROM = datetime.datetime(2013, 2, 23, 21, 26, 43)
EXPIRED_UNTIL = datetime.datetime(2013, 2, 24, 21, 26, 43)

# PaymentDetails content, identical to the original fixtures.
# 110000 satoshis to a P2PKH script.
OUTPUT_AMOUNT = 110000
OUTPUT_SCRIPT = bytes.fromhex("76a91495608a03c8fba5e9131b4d2cefe2d56155f199a888ac")
DETAILS_TIME = 1366388914
DETAILS_TIME_5 = 1366392248


# --- minimal protobuf (proto2) encoding -------------------------------------

def _varint(n):
    out = bytearray()
    while True:
        b = n & 0x7f
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def _pb_varint(field, value):
    return _varint(field << 3 | 0) + _varint(value)


def _pb_bytes(field, value):
    if isinstance(value, str):
        value = value.encode()
    return _varint(field << 3 | 2) + _varint(len(value)) + value


def payment_details(memo, time):
    # message Output { amount = 1; script = 2 }
    output = _pb_varint(1, OUTPUT_AMOUNT) + _pb_bytes(2, OUTPUT_SCRIPT)
    # message PaymentDetails { outputs = 2; time = 3; memo = 5 }
    return _pb_bytes(2, output) + _pb_varint(3, time) + _pb_bytes(5, memo)


def payment_request(chain, signing_key, details):
    """chain: list of x509.Certificate, signing cert first."""
    # message X509Certificates { repeated bytes certificate = 1 }
    pki_data = b"".join(_pb_bytes(1, c.public_bytes(Encoding.DER)) for c in chain)
    # message PaymentRequest { pki_type = 2; pki_data = 3;
    #                          serialized_payment_details = 4; signature = 5 }
    body = (_pb_bytes(2, "x509+sha256") + _pb_bytes(3, pki_data) +
            _pb_bytes(4, details))
    # PaymentRequestPlus::getMerchant verifies the request serialized with
    # signature set to "" (present, empty), which serializes after field 4.
    to_sign = body + _pb_bytes(5, b"")
    sig = signing_key.sign(to_sign, padding.PKCS1v15(), hashes.SHA256())
    return body + _pb_bytes(5, sig)


# --- certificates --------------------------------------------------------------

def new_key():
    return rsa.generate_private_key(public_exponent=65537, key_size=KEY_BITS)


def name(cn, org=None):
    attrs = [x509.NameAttribute(NameOID.COMMON_NAME, cn)]
    if org:
        attrs.append(x509.NameAttribute(NameOID.ORGANIZATION_NAME, org))
    return x509.Name(attrs)


def make_cert(subject, subject_key, issuer, issuer_key, serial, not_before, not_after):
    # Every cert carries basicConstraints CA:TRUE, as the originals did
    # (they were all made with `openssl req -x509` style v3_ca defaults).
    return (x509.CertificateBuilder()
            .subject_name(subject)
            .issuer_name(issuer)
            .public_key(subject_key.public_key())
            .serial_number(serial)
            .not_valid_before(not_before)
            .not_valid_after(not_after)
            .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=False)
            .sign(issuer_key, hashes.SHA256()))


def b64_c_string(varname, data):
    b64 = base64.b64encode(data).decode()
    lines = [b64[i:i + 64] for i in range(0, len(b64), 64)]
    return ('const char* %s =\n"\\\n' % varname +
            "".join(line + "\\\n" for line in lines) + '";\n')


def fmt_date(d):
    return d.strftime("%b %d %H:%M:%S %Y GMT").replace(" 0", "  ", 1)


def main():
    ca_name = name("PaymentRequest Test CA")

    # Trusted root.
    ca_key = new_key()
    ca_serial = x509.random_serial_number()
    ca = make_cert(ca_name, ca_key, ca_name, ca_key, ca_serial, VALID_FROM, VALID_UNTIL)

    # 1: merchant cert issued directly by the root.
    m_key = new_key()
    m_cert = make_cert(name("testmerchant.org", "Payment Request Test Merchant"), m_key,
                       ca_name, ca_key, 1, VALID_FROM, VALID_UNTIL)
    pr1 = payment_request([m_cert], m_key, payment_details("UnitTestOne", DETAILS_TIME))

    # 2: expired merchant cert issued by the root.
    e_key = new_key()
    e_cert = make_cert(name("expiredmerchant.org", "Expired Test Merchant"), e_key,
                       ca_name, ca_key, 3, EXPIRED_FROM, EXPIRED_UNTIL)
    pr2 = payment_request([e_cert], e_key, payment_details("UnitTestTwo", DETAILS_TIME))

    # 3: root -> testca1 -> ... -> testca8 -> testmerchant8.org
    ca_keys = {}
    ca_certs = {}
    issuer_name, issuer_key, serial = ca_name, ca_key, 5
    for i in range(1, 9):
        k = new_key()
        subj = name("testca%d.org" % i, "Payment Request Intermediate %d" % i)
        ca_keys[i] = k
        ca_certs[i] = make_cert(subj, k, issuer_name, issuer_key, serial,
                                VALID_FROM, VALID_UNTIL)
        issuer_name, issuer_key, serial = subj, k, 2
    m8_key = new_key()
    m8_cert = make_cert(name("testmerchant8.org", "Test Merchant 8"), m8_key,
                        issuer_name, issuer_key, 1, VALID_FROM, VALID_UNTIL)
    chain3 = [m8_cert] + [ca_certs[i] for i in range(8, 0, -1)]
    pr3 = payment_request(chain3, m8_key, payment_details("UnitTestThree", DETAILS_TIME))

    # 4: same chain, but testca5 replaced by an expired cert for the same key,
    #    issued directly by the root.
    ca5_expired = make_cert(ca_certs[5].subject, ca_keys[5], ca_name, ca_key, 6,
                            EXPIRED_FROM, EXPIRED_UNTIL)
    chain4 = list(chain3)
    chain4[chain4.index(ca_certs[5])] = ca5_expired
    pr4 = payment_request(chain4, m8_key, payment_details("UnitTestFour", DETAILS_TIME))

    # 5: valid merchant cert, but issued by an untrusted CA with the same name.
    rogue_key = new_key()
    r_key = new_key()
    r_cert = make_cert(name("testmerchant.org", "Payment Request Test Merchant"), r_key,
                       ca_name, rogue_key, 1, VALID_FROM, VALID_UNTIL)
    pr5 = payment_request([r_cert], r_key, payment_details("UnitTestFive", DETAILS_TIME_5))

    out = sys.stdout
    out.write("""// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2026 The Dobbscoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Data for paymentservertests.cpp
//
// GENERATED FILE, do not edit by hand. Regenerate with:
//   contrib/devtools/gen-paymentrequest-testdata.py > src/qt/test/paymentrequestdata.h
//
// Valid certificates run until %(until)s; the deliberately expired
// ones (paymentrequest2 merchant, paymentrequest4 testca5) ended %(exp)s.
//

// Base64/DER-encoded fake certificate authority certificate.
// Convert pem to base64/der with:
// cat file.pem | openssl x509 -inform PEM -outform DER | openssl enc -base64
//
// Serial Number: %(serial)d (0x%(serialx)x)
// Issuer: CN=PaymentRequest Test CA
// Subject: CN=PaymentRequest Test CA
// Not Valid After : %(until)s
//
""" % {"until": fmt_date(VALID_UNTIL), "exp": fmt_date(EXPIRED_UNTIL),
       "serial": ca_serial, "serialx": ca_serial})
    out.write(b64_c_string("caCert_BASE64", ca.public_bytes(Encoding.DER)))
    sections = [
        ("This payment request validates directly against the\n"
         "// above certificate authority.", "paymentrequest1_BASE64", pr1),
        ("Signed, but expired, merchant cert in the request", "paymentrequest2_BASE64", pr2),
        ("10-long chain, all intermediates valid", "paymentrequest3_BASE64", pr3),
        ("Long chain, with an invalid (expired) cert in the middle",
         "paymentrequest4_BASE64", pr4),
        ("Validly signed, but by a CA not in our root CA list", "paymentrequest5_BASE64", pr5),
    ]
    for comment, var, data in sections:
        out.write("\n//\n// %s\n//\n" % comment)
        out.write(b64_c_string(var, data))


if __name__ == "__main__":
    main()
