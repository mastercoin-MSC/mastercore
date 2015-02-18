#ifndef MASTERCORE_RPC_H
#define MASTERCORE_RPC_H

#include "json/json_spirit_value.h"

#include <string>

class uint256;

int populateRPCTransactionObject(const uint256& txid, json_spirit::Object *txobj, const std::string& filterAddress);

#endif // MASTERCORE_RPC_H
