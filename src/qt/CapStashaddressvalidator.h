// Copyright (c) 2011-2020 The CapStash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CAPSTASH_QT_CAPSTASHADDRESSVALIDATOR_H
#define CAPSTASH_QT_CAPSTASHADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class CapStashAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit CapStashAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const override;
};

/** CapStash address widget validator, checks for a valid CapStash address.
 */
class CapStashAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit CapStashAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const override;
};

#endif // CAPSTASH_QT_CAPSTASHADDRESSVALIDATOR_H
