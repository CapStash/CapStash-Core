// Copyright (c) 2023 The CapStash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CAPSTASH_TEST_UTIL_JSON_H
#define CAPSTASH_TEST_UTIL_JSON_H

#include <string>

#include <univalue.h>

UniValue read_json(const std::string& jsondata);

#endif // CAPSTASH_TEST_UTIL_JSON_H
