// Copyright (c) 2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERCOIN_MASTERCORE_VERSION
#define MASTERCOIN_MASTERCORE_VERSION

#include <string>

const int OMNICORE_VERSION_BASE = 100;           // 82 = 0.0.8.2   91 = 0.0.9.1   103 = 0.0.10.3 etc
const std::string OMNICORE_VERSION_TYPE("-dev"); // switch to -rel for tags, switch back to -dev for development

std::string GetMasterCoreVersion();

#endif // MASTERCOIN_MASTERCORE_VERSION
