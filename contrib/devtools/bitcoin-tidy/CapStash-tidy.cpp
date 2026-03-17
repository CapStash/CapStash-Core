// Copyright (c) 2023 CapStash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "logprintf.h"

#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>

class CapStashModule final : public clang::tidy::ClangTidyModule
{
public:
    void addCheckFactories(clang::tidy::ClangTidyCheckFactories& CheckFactories) override
    {
        CheckFactories.registerCheck<CapStash::LogPrintfCheck>("CapStash-unterminated-logprintf");
    }
};

static clang::tidy::ClangTidyModuleRegistry::Add<CapStashModule>
    X("CapStash-module", "Adds CapStash checks.");

volatile int CapStashModuleAnchorSource = 0;
