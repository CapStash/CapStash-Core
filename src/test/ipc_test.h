// Copyright (c) 2023 The CapStash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CAPSTASH_TEST_IPC_TEST_H
#define CAPSTASH_TEST_IPC_TEST_H

#include <primitives/transaction.h>
#include <univalue.h>

class FooImplementation
{
public:
    int add(int a, int b) { return a + b; }
    COutPoint passOutPoint(COutPoint o) { return o; }
    UniValue passUniValue(UniValue v) { return v; }
};

void IpcTest();

#endif // CAPSTASH_TEST_IPC_TEST_H
