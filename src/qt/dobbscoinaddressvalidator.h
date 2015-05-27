// Copyright (c) 2011-2014 The Dobbscoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_BITCOINADDRESSVALIDATOR_H
#define BITCOIN_QT_BITCOINADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class DobbscoinAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit DobbscoinAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

/** Dobbscoin address widget validator, checks for a valid dobbscoin address.
 */
class DobbscoinAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit DobbscoinAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

#endif // BITCOIN_QT_BITCOINADDRESSVALIDATOR_H
