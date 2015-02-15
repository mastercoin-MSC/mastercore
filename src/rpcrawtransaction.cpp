// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "core.h"
#include "init.h"
#include "keystore.h"
#include "main.h"
#include "net.h"
#include "rpcserver.h"
#include "uint256.h"
#ifdef ENABLE_WALLET
#include "wallet.h"
#endif

#include <stdint.h>

#include <boost/assign/list_of.hpp>
#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;

void ScriptPubKeyToJSON(const CScript& scriptPubKey, Object& out, bool fIncludeHex)
{
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;

    out.push_back(Pair("asm", scriptPubKey.ToString()));
    if (fIncludeHex)
        out.push_back(Pair("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end())));

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired))
    {
        out.push_back(Pair("type", GetTxnOutputType(type)));
        return;
    }

    out.push_back(Pair("reqSigs", nRequired));
    out.push_back(Pair("type", GetTxnOutputType(type)));

    Array a;
    BOOST_FOREACH(const CTxDestination& addr, addresses)
        a.push_back(CBitcoinAddress(addr).ToString());
    out.push_back(Pair("addresses", a));
}

void TxToJSON(const CTransaction& tx, const uint256 hashBlock, Object& entry)
{
    entry.push_back(Pair("txid", tx.GetHash().GetHex()));
    entry.push_back(Pair("version", tx.nVersion));
    entry.push_back(Pair("locktime", (int64_t)tx.nLockTime));
    Array vin;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        Object in;
        if (tx.IsCoinBase())
            in.push_back(Pair("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
        else
        {
            in.push_back(Pair("txid", txin.prevout.hash.GetHex()));
            in.push_back(Pair("vout", (int64_t)txin.prevout.n));
            Object o;
            o.push_back(Pair("asm", txin.scriptSig.ToString()));
            o.push_back(Pair("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
            in.push_back(Pair("scriptSig", o));
        }
        in.push_back(Pair("sequence", (int64_t)txin.nSequence));
        vin.push_back(in);
    }
    entry.push_back(Pair("vin", vin));
    Array vout;
    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];
        Object out;
        out.push_back(Pair("value", ValueFromAmount(txout.nValue)));
        out.push_back(Pair("n", (int64_t)i));
        Object o;
        ScriptPubKeyToJSON(txout.scriptPubKey, o, true);
        out.push_back(Pair("scriptPubKey", o));
        vout.push_back(out);
    }
    entry.push_back(Pair("vout", vout));

    if (hashBlock != 0)
    {
        entry.push_back(Pair("blockhash", hashBlock.GetHex()));
        map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second)
        {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex))
            {
                entry.push_back(Pair("confirmations", 1 + chainActive.Height() - pindex->nHeight));
                entry.push_back(Pair("time", (int64_t)pindex->nTime));
                entry.push_back(Pair("blocktime", (int64_t)pindex->nTime));
            }
            else
                entry.push_back(Pair("confirmations", 0));
        }
    }
}

Value getrawtransaction(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getrawtransaction \"txid\" ( verbose )\n"
            "\nReturn the raw transaction data.\n"
            "\nIf verbose=0, returns a string that is serialized, hex-encoded data for 'txid'.\n"
            "If verbose is non-zero, returns an Object with information about 'txid'.\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose       (numeric, optional, default=0) If 0, return a string, other return a json object\n"

            "\nResult (if verbose is not set or set to 0):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"

            "\nResult (if verbose > 0):\n"
            "{\n"
            "  \"hex\" : \"data\",       (string) The serialized, hex-encoded data for 'txid'\n"
            "  \"txid\" : \"id\",        (string) The transaction id (same as provided)\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) \n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in btc\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"bitcoinaddress\"        (string) bitcoin address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
            "  \"confirmations\" : n,      (numeric) The confirmations\n"
            "  \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getrawtransaction", "\"mytxid\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" 1")
            + HelpExampleRpc("getrawtransaction", "\"mytxid\", 1")
        );

    uint256 hash = ParseHashV(params[0], "parameter 1");

    bool fVerbose = false;
    if (params.size() > 1)
        fVerbose = (params[1].get_int() != 0);

    CTransaction tx;
    uint256 hashBlock = 0;
    if (!GetTransaction(hash, tx, hashBlock, true))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << tx;
    string strHex = HexStr(ssTx.begin(), ssTx.end());

    if (!fVerbose)
        return strHex;

    Object result;
    result.push_back(Pair("hex", strHex));
    TxToJSON(tx, hashBlock, result);
    return result;
}

Value gettxposition(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "gettxposition \"txid\"\n"

            "\nReturns information related to the position of transaction.\n"

            "\nArguments:\n"
            "1. txid          (string, required) The transaction id\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"hash\",      (string) The transaction id (same as provided)\n"
            "  \"blockhash\" : \"hash\", (string) The block hash\n"
            "  \"blockheight\" : n,      (numeric) The block height (if orphaned: -1, unconfirmed: 0)\n"
            "  \"position\" : n          (numeric) The position of transaction within block (if unconfirmed: -1)\n"
            "}\n"

            "\nExamples\n"
            + HelpExampleCli("gettxposition", "546a406a131089e7c2f27d34a93a4d27441d98d096404d6737c5ad5b5e61a09b")
            + HelpExampleRpc("gettxposition", "546a406a131089e7c2f27d34a93a4d27441d98d096404d6737c5ad5b5e61a09b")
        );

    uint256 hash = ParseHashV(params[0], "parameter 1");

    CTransaction tx;
    uint256 hashBlock = 0;
    if (!GetTransaction(hash, tx, hashBlock, true))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << tx;

    Object result;
    result.push_back(Pair("txid", hash.GetHex()));

    int nHeight = 0;
    int nPosition = -1;

    if (hashBlock != 0) {
        result.push_back(Pair("blockhash", hashBlock.GetHex()));

        map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex))
                nHeight = pindex->nHeight;
            else
                nHeight = -1;

            CBlock block;
            if (ReadBlockFromDisk(block, pindex))
                nPosition = std::find(block.vtx.begin(), block.vtx.end(), tx) - block.vtx.begin();
        }
    }

    result.push_back(Pair("blockheight", (int64_t)nHeight));
    result.push_back(Pair("position", (int64_t)nPosition));

    return result;
}

Value listalltransactions(const Array &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 5)
        throw runtime_error(
            "listalltransactions \"address\" ( verbose skip count includeorphans )\n"

            "\nReturns array of all transactions associated with address.\n"

            "\nArguments:\n"
            "1. address          (string, required) The Bitcoin address\n"
            "2. verbose          (numeric, optional, default=0) If 0, return only transaction hex\n"
            "3. skip             (numeric, optional, default=0) The number of transactions to skip\n"
            "4. count            (numeric, optional, default=100) The number of transactions to return\n"
            "5. includeorphans   (numeric, optional, default=0) If 1, include orphaned transactions\n"

            "\nExamples\n"
            + HelpExampleCli("listalltransactions", "1EXoDusjGwvnjZUyKkxZ4UHEf77z6A5S4P")
            + HelpExampleCli("listalltransactions", "1EXoDusjGwvnjZUyKkxZ4UHEf77z6A5S4P 1 500 5")
            + HelpExampleRpc("listalltransactions", "1EXoDusjGwvnjZUyKkxZ4UHEf77z6A5S4P, 1, 500, 5")
        );

    RPCTypeCheck(params, list_of(str_type)(int_type)(int_type)(int_type)(int_type));
    
    if (!fAddrIndex)
        throw JSONRPCError(RPC_MISC_ERROR, "Address index not enabled");

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Bitcoin address");
    CTxDestination dest = address.Get();

    std::set<CExtDiskTxPos> setpos;
    if (!FindTransactionsByDestination(dest, setpos))
        throw JSONRPCError(RPC_DATABASE_ERROR, "Cannot search for address");

    bool fVerbose = false;
    if (params.size() > 1)
        fVerbose = (params[1].get_int() != 0);

    int nSkip = 0;
    if (params.size() > 2)
        nSkip = params[2].get_int();
    if (nSkip < 0)
        nSkip += setpos.size();

    int nCount = 100;
    if (params.size() > 3)
        nCount = params[3].get_int();

    bool fIncludeOrphans = false;
    if (params.size() > 4)
        fIncludeOrphans = (params[4].get_int() != 0);

    std::set<CExtDiskTxPos>::const_iterator it = setpos.begin();
    while (it != setpos.end() && nSkip--)
        it++;

    Array result;
    while (it != setpos.end() && nCount > 0) {
        CTransaction tx;
        uint256 hashBlock;
        if (!ReadTransaction(tx, *it, hashBlock))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Cannot read transaction from disk");
            
        if (!fIncludeOrphans && hashBlock != 0) {
            CBlockIndex* pindex = mapBlockIndex[hashBlock];
            if (!chainActive.Contains(pindex)) {
                it++;
                continue;
            }
        }

        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << tx;
        string strHex = HexStr(ssTx.begin(), ssTx.end());
        
        if (fVerbose) {
            Object entry;
            entry.push_back(Pair("hex", strHex));
            TxToJSON(tx, hashBlock, entry);
            result.push_back(entry);
        } else {
            result.push_back(strHex);
        }

        nCount--;
        it++;
    }

    return result;
}

Value listallunspent(const Array &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 5)
        throw runtime_error(
            "listallunspent \"address\" ( verbose minconf maxconf maxreqsigs )\n"

            "\nReturns array of unspent transaction outputs\n"
            "with between minconf and maxconf (inclusive) confirmations\n"
            "spendable by the provided address whereby maximal maxreqsigs\n"
            "signatures are required to redeem the output.\n"

            "\nArguments:\n"
            "1. address          (string, required) The Bitcoin address\n"
            "2. verbose          (numeric, optional, default=0) If 0, exclude reqSigs, addresses, scriptPubKey (asm, hex), blockhash, blocktime, blockheight\n"
            "3. minconf          (numeric, optional, default=1) The minimum confirmations to filter. If 0, include mempool.\n"
            "4. maxconf          (numeric, optional, default=9999999) The maximum confirmations to filter\n"
            "5. maxreqsigs       (numeric, optional, default=1) The number of signatures required to spend the output\n"

            "\nExamples\n"
            + HelpExampleCli("listallunspent", "1BxtgEa8UcrMzVZaW32zVyJh4Sg4KGFzxA")
            + HelpExampleCli("listallunspent", "1BxtgEa8UcrMzVZaW32zVyJh4Sg4KGFzxA 1 0 100 1")
            + HelpExampleRpc("listallunspent", "1BxtgEa8UcrMzVZaW32zVyJh4Sg4KGFzxA, 1, 0, 100, 1")
        );
        
    RPCTypeCheck(params, list_of(str_type)(int_type)(int_type)(int_type)(int_type));

    if (!fAddrIndex)
        throw JSONRPCError(RPC_MISC_ERROR, "Address index not enabled");

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Bitcoin address");
    CTxDestination dest = address.Get();

    std::set<CExtDiskTxPos> setpos;
    if (!FindTransactionsByDestination(dest, setpos))
        throw JSONRPCError(RPC_DATABASE_ERROR, "Cannot search for address");

    bool fVerbose = false;
    if (params.size() > 1)
        fVerbose = (params[1].get_int() != 0);

    int nMinDepth = 1;
    if (params.size() > 2)
        nMinDepth = params[2].get_int();

    int nMaxDepth = 9999999;
    if (params.size() > 3)
        nMaxDepth = params[3].get_int();

    int nMaxReqSigs = 1;
    if (params.size() > 4)
        nMaxReqSigs = params[4].get_int();

    Array results;
    std::set<CExtDiskTxPos>::const_iterator it = setpos.begin();
    while (it != setpos.end()) {
        CTransaction tx;
        uint256 hashBlock;
        if (!ReadTransaction(tx, *it, hashBlock))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Cannot read transaction from disk");
        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << tx;

        CCoins coins;
        pcoinsTip->GetCoins(tx.GetHash(), coins);
        for (unsigned int i = 0; i < tx.vout.size(); i++) {
            const CTxOut& txout = tx.vout[i];
            if (!(coins.IsAvailable(i) && txout.nValue > 0))
                continue;

            txnouttype type;
            vector<CTxDestination> addresses;
            int nRequired;
            if (!ExtractDestinations(txout.scriptPubKey, type, addresses, nRequired))
                continue;

            if (type == TX_MULTISIG && nRequired > nMaxReqSigs)
                continue;

            if (std::find(addresses.begin(), addresses.end(), dest) == addresses.end())
                continue;

            int nDepth = 0;
            int nHeight = 0;
            int64_t nTime = 0;

            if (hashBlock != 0) {
                CBlockIndex* pindex = mapBlockIndex[hashBlock];
                if (chainActive.Contains(pindex)) {
                    nHeight = pindex->nHeight;
                    nDepth = chainActive.Height() - nHeight + 1;
                    nTime = pindex->GetBlockTime();
                }
            }
            
            if (nDepth < nMinDepth || nDepth > nMaxDepth)
                continue;

            Object entry;
            entry.push_back(Pair("txid", tx.GetHash().GetHex()));
            entry.push_back(Pair("vout", (int64_t)i));
            entry.push_back(Pair("amount", ValueFromAmount(txout.nValue)));
            entry.push_back(Pair("type", GetTxnOutputType(type)));

            if (fVerbose) {
                entry.push_back(Pair("reqSigs", nRequired));
                Array a;
                BOOST_FOREACH(const CTxDestination& addrinner, addresses)
                    a.push_back(CBitcoinAddress(addrinner).ToString());
                entry.push_back(Pair("addresses", a));

                Object pkobj;
                const CScript& pk = txout.scriptPubKey;
                pkobj.push_back(Pair("asm", pk.ToString()));
                pkobj.push_back(Pair("hex", HexStr(pk.begin(), pk.end())));
                entry.push_back(Pair("scriptPubKey", pkobj));

                entry.push_back(Pair("blockhash", hashBlock.GetHex()));
                entry.push_back(Pair("blocktime", nTime));
                entry.push_back(Pair("blockheight", nHeight));
            }

            entry.push_back(Pair("confirmations", nDepth));
            results.push_back(entry);
        }
        it++;
    }

    if (nMinDepth < 1) {
        LOCK(mempool.cs);
        BOOST_FOREACH(const PAIRTYPE(uint256, CTxMemPoolEntry)& entry, mempool.mapTx) {
            const CTransaction& tx = entry.second.GetTx();
            for (unsigned int i = 0; i < tx.vout.size(); i++) {
                const CTxOut& txout = tx.vout[i];
                txnouttype type;
                vector<CTxDestination> addresses;
                int nRequired;            
                if (!ExtractDestinations(txout.scriptPubKey, type, addresses, nRequired))
                    continue;

                if (type == TX_MULTISIG && nRequired > nMaxReqSigs)
                    continue;

                if (std::find(addresses.begin(), addresses.end(), dest) == addresses.end())
                    continue;

                Object entry;
                entry.push_back(Pair("txid", tx.GetHash().GetHex()));
                entry.push_back(Pair("vout", (int64_t)i));
                entry.push_back(Pair("amount", ValueFromAmount(txout.nValue)));
                entry.push_back(Pair("type", GetTxnOutputType(type)));

                if (fVerbose) {
                    entry.push_back(Pair("reqSigs", nRequired));
                    Array a;
                    BOOST_FOREACH(const CTxDestination& addrinner, addresses)
                        a.push_back(CBitcoinAddress(addrinner).ToString());
                    entry.push_back(Pair("addresses", a));

                    Object pkobj;
                    const CScript& pk = txout.scriptPubKey;
                    pkobj.push_back(Pair("asm", pk.ToString()));
                    pkobj.push_back(Pair("hex", HexStr(pk.begin(), pk.end())));
                    entry.push_back(Pair("scriptPubKey", pkobj));
                }

                entry.push_back(Pair("confirmations", 0));
                results.push_back(entry);
            }
        }
    }
    
    return results;
}

Value getallbalance(const Array &params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
            "getallbalance \"address\" ( minconf maxreqsigs )\n"

            "\nReturns the sum of spendable transaction outputs by address\n"
            "with at least minconf confirmations whereby maximal maxreqsigs\n"
            "signatures are allowed to be required to redeem an output.\n"

            "\nArguments:\n"
            "1. address          (string, required) The Bitcoin address\n"
            "2. minconf          (numeric, optional, default=1) The minimum confirmations to filter. If 0, include mempool.\n"
            "3. maxreqsigs       (numeric, optional, default=1) The number of signatures required to spend an output\n"

            "\nExamples\n"
            + HelpExampleCli("getallbalance", "1BxtgEa8UcrMzVZaW32zVyJh4Sg4KGFzxA")
            + HelpExampleCli("getallbalance", "1BxtgEa8UcrMzVZaW32zVyJh4Sg4KGFzxA 0 1")
            + HelpExampleRpc("getallbalance", "1BxtgEa8UcrMzVZaW32zVyJh4Sg4KGFzxA, 0, 1")
        );

    RPCTypeCheck(params, list_of(str_type)(int_type)(int_type));

    if (!fAddrIndex)
        throw JSONRPCError(RPC_MISC_ERROR, "Address index not enabled");

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Bitcoin address");
    CTxDestination dest = address.Get();

    std::set<CExtDiskTxPos> setpos;
    if (!FindTransactionsByDestination(dest, setpos))
        throw JSONRPCError(RPC_DATABASE_ERROR, "Cannot search for address");

    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();
        
    int nMaxReqSigs = 1;
    if (params.size() > 2)
        nMaxReqSigs = params[2].get_int();

    Array results;
    int64_t nBalance = 0;
    std::set<CExtDiskTxPos>::const_iterator it = setpos.begin();
    while (it != setpos.end()) {
        CTransaction tx;
        uint256 hashBlock;
        if (!ReadTransaction(tx, *it, hashBlock))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Cannot read transaction from disk");
        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << tx;

        CCoins coins;
        pcoinsTip->GetCoins(tx.GetHash(), coins);
        for (unsigned int i = 0; i < tx.vout.size(); i++) {
            const CTxOut& txout = tx.vout[i];
            if (!(coins.IsAvailable(i) && txout.nValue > 0))
                continue;

            txnouttype type;
            vector<CTxDestination> addresses;
            int nRequired;
            if (!ExtractDestinations(txout.scriptPubKey, type, addresses, nRequired))
                continue;

            if (type == TX_MULTISIG && nRequired > nMaxReqSigs)
                continue;
                
            if (std::find(addresses.begin(), addresses.end(), dest) == addresses.end())
                continue;

            int nDepth = 0;
            if (hashBlock != 0) {
                CBlockIndex* pindex = mapBlockIndex[hashBlock];
                if (chainActive.Contains(pindex))
                    nDepth = chainActive.Height() - pindex->nHeight + 1;
            }

            if (nDepth < nMinDepth)
                continue;

            nBalance += txout.nValue;
        }
        it++;
    }

    if (nMinDepth < 1) {
        LOCK(mempool.cs);
        BOOST_FOREACH(const PAIRTYPE(uint256, CTxMemPoolEntry)& entry, mempool.mapTx) {
            const CTransaction& tx = entry.second.GetTx();
            for (unsigned int i = 0; i < tx.vout.size(); i++) {
                const CTxOut& txout = tx.vout[i];
                txnouttype type;
                vector<CTxDestination> addresses;
                int nRequired;
                if (!ExtractDestinations(txout.scriptPubKey, type, addresses, nRequired))
                    continue;

                if (type == TX_MULTISIG && nRequired > nMaxReqSigs)
                    continue;

                if (std::find(addresses.begin(), addresses.end(), dest) == addresses.end())
                    continue;

                nBalance += txout.nValue;
            }
        }
    }

    return ValueFromAmount(nBalance);
}

#ifdef ENABLE_WALLET
Value listunspent(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw runtime_error(
            "listunspent ( minconf maxconf  [\"address\",...] )\n"
            "\nReturns array of unspent transaction outputs\n"
            "with between minconf and maxconf (inclusive) confirmations.\n"
            "Optionally filter to only include txouts paid to specified addresses.\n"
            "Results are an array of Objects, each of which has:\n"
            "{txid, vout, scriptPubKey, amount, confirmations}\n"
            "\nArguments:\n"
            "1. minconf          (numeric, optional, default=1) The minimum confirmationsi to filter\n"
            "2. maxconf          (numeric, optional, default=9999999) The maximum confirmations to filter\n"
            "3. \"addresses\"    (string) A json array of bitcoin addresses to filter\n"
            "    [\n"
            "      \"address\"   (string) bitcoin address\n"
            "      ,...\n"
            "    ]\n"
            "\nResult\n"
            "[                   (array of json object)\n"
            "  {\n"
            "    \"txid\" : \"txid\",        (string) the transaction id \n"
            "    \"vout\" : n,               (numeric) the vout value\n"
            "    \"address\" : \"address\",  (string) the bitcoin address\n"
            "    \"account\" : \"account\",  (string) The associated account, or \"\" for the default account\n"
            "    \"scriptPubKey\" : \"key\", (string) the script key\n"
            "    \"amount\" : x.xxx,         (numeric) the transaction amount in btc\n"
            "    \"confirmations\" : n       (numeric) The number of confirmations\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples\n"
            + HelpExampleCli("listunspent", "")
            + HelpExampleCli("listunspent", "6 9999999 \"[\\\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\"")
            + HelpExampleRpc("listunspent", "6, 9999999 \"[\\\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\"")
        );

    RPCTypeCheck(params, list_of(int_type)(int_type)(array_type));

    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    int nMaxDepth = 9999999;
    if (params.size() > 1)
        nMaxDepth = params[1].get_int();

    set<CBitcoinAddress> setAddress;
    if (params.size() > 2)
    {
        Array inputs = params[2].get_array();
        BOOST_FOREACH(Value& input, inputs)
        {
            CBitcoinAddress address(input.get_str());
            if (!address.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid Bitcoin address: ")+input.get_str());
            if (setAddress.count(address))
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ")+input.get_str());
           setAddress.insert(address);
        }
    }

    Array results;
    vector<COutput> vecOutputs;
    assert(pwalletMain != NULL);
    pwalletMain->AvailableCoins(vecOutputs, false);
    BOOST_FOREACH(const COutput& out, vecOutputs)
    {
        if (out.nDepth < nMinDepth || out.nDepth > nMaxDepth)
            continue;

        if (setAddress.size())
        {
            CTxDestination address;
            if (!ExtractDestination(out.tx->vout[out.i].scriptPubKey, address))
                continue;

            if (!setAddress.count(address))
                continue;
        }

        int64_t nValue = out.tx->vout[out.i].nValue;
        const CScript& pk = out.tx->vout[out.i].scriptPubKey;
        Object entry;
        entry.push_back(Pair("txid", out.tx->GetHash().GetHex()));
        entry.push_back(Pair("vout", out.i));
        CTxDestination address;
        if (ExtractDestination(out.tx->vout[out.i].scriptPubKey, address))
        {
            entry.push_back(Pair("address", CBitcoinAddress(address).ToString()));
            if (pwalletMain->mapAddressBook.count(address))
                entry.push_back(Pair("account", pwalletMain->mapAddressBook[address].name));
        }
        entry.push_back(Pair("scriptPubKey", HexStr(pk.begin(), pk.end())));
        if (pk.IsPayToScriptHash())
        {
            CTxDestination address;
            if (ExtractDestination(pk, address))
            {
                const CScriptID& hash = boost::get<const CScriptID&>(address);
                CScript redeemScript;
                if (pwalletMain->GetCScript(hash, redeemScript))
                    entry.push_back(Pair("redeemScript", HexStr(redeemScript.begin(), redeemScript.end())));
            }
        }
        entry.push_back(Pair("amount",ValueFromAmount(nValue)));
        entry.push_back(Pair("confirmations",out.nDepth));
        results.push_back(entry);
    }

    return results;
}
#endif

Value createrawtransaction(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "createrawtransaction [{\"txid\":\"id\",\"vout\":n},...] {\"address\":amount,...}\n"
            "\nCreate a transaction spending the given inputs and sending to the given addresses.\n"
            "Returns hex-encoded raw transaction.\n"
            "Note that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network.\n"

            "\nArguments:\n"
            "1. \"transactions\"        (string, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",  (string, required) The transaction id\n"
            "         \"vout\":n        (numeric, required) The output number\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "2. \"addresses\"           (string, required) a json object with addresses as keys and amounts as values\n"
            "    {\n"
            "      \"address\": x.xxx   (numeric, required) The key is the bitcoin address, the value is the btc amount\n"
            "      ,...\n"
            "    }\n"

            "\nResult:\n"
            "\"transaction\"            (string) hex string of the transaction\n"

            "\nExamples\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"address\\\":0.01}\"")
        );

    RPCTypeCheck(params, list_of(array_type)(obj_type));

    Array inputs = params[0].get_array();
    Object sendTo = params[1].get_obj();

    CTransaction rawTx;

    BOOST_FOREACH(const Value& input, inputs)
    {
        const Object& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const Value& vout_v = find_value(o, "vout");
        if (vout_v.type() != int_type)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        CTxIn in(COutPoint(txid, nOutput));
        rawTx.vin.push_back(in);
    }

    set<CBitcoinAddress> setAddress;
    BOOST_FOREACH(const Pair& s, sendTo)
    {
        CBitcoinAddress address(s.name_);
        if (!address.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid Bitcoin address: ")+s.name_);

        if (setAddress.count(address))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ")+s.name_);
        setAddress.insert(address);

        CScript scriptPubKey;
        scriptPubKey.SetDestination(address.Get());
        int64_t nAmount = AmountFromValue(s.value_);

        CTxOut out(nAmount, scriptPubKey);
        rawTx.vout.push_back(out);
    }

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << rawTx;
    return HexStr(ss.begin(), ss.end());
}

Value decoderawtransaction(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "decoderawtransaction \"hexstring\"\n"
            "\nReturn a JSON object representing the serialized, hex-encoded transaction.\n"

            "\nArguments:\n"
            "1. \"hex\"      (string, required) The transaction hex string\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"id\",        (string) The transaction id\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) The output number\n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n     (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [             (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in btc\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"12tvKAXCxZjSmdNbao16dKXC8tRWfcF5oc\"   (string) bitcoin address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("decoderawtransaction", "\"hexstring\"")
            + HelpExampleRpc("decoderawtransaction", "\"hexstring\"")
        );

    vector<unsigned char> txData(ParseHexV(params[0], "argument"));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    CTransaction tx;
    try {
        ssData >> tx;
    }
    catch (std::exception &e) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    Object result;
    TxToJSON(tx, 0, result);

    return result;
}

Value decodescript(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "decodescript \"hex\"\n"
            "\nDecode a hex-encoded script.\n"
            "\nArguments:\n"
            "1. \"hex\"     (string) the hex encoded script\n"
            "\nResult:\n"
            "{\n"
            "  \"asm\":\"asm\",   (string) Script public key\n"
            "  \"hex\":\"hex\",   (string) hex encoded public key\n"
            "  \"type\":\"type\", (string) The output type\n"
            "  \"reqSigs\": n,    (numeric) The required signatures\n"
            "  \"addresses\": [   (json array of string)\n"
            "     \"address\"     (string) bitcoin address\n"
            "     ,...\n"
            "  ],\n"
            "  \"p2sh\",\"address\" (string) script address\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("decodescript", "\"hexstring\"")
            + HelpExampleRpc("decodescript", "\"hexstring\"")
        );

    RPCTypeCheck(params, list_of(str_type));

    Object r;
    CScript script;
    if (params[0].get_str().size() > 0){
        vector<unsigned char> scriptData(ParseHexV(params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    } else {
        // Empty scripts are valid
    }
    ScriptPubKeyToJSON(script, r, false);

    r.push_back(Pair("p2sh", CBitcoinAddress(script.GetID()).ToString()));
    return r;
}

Value signrawtransaction(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 4)
        throw runtime_error(
            "signrawtransaction \"hexstring\" ( [{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\",\"redeemScript\":\"hex\"},...] [\"privatekey1\",...] sighashtype )\n"
            "\nSign inputs for raw transaction (serialized, hex-encoded).\n"
            "The second optional argument (may be null) is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the block chain.\n"
            "The third optional argument (may be null) is an array of base58-encoded private\n"
            "keys that, if given, will be the only keys used to sign the transaction.\n"
#ifdef ENABLE_WALLET
            + HelpRequiringPassphrase() + "\n"
#endif

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The transaction hex string\n"
            "2. \"prevtxs\"       (string, optional) An json array of previous dependent transaction outputs\n"
            "     [               (json array of json objects, or 'null' if none provided)\n"
            "       {\n"
            "         \"txid\":\"id\",             (string, required) The transaction id\n"
            "         \"vout\":n,                  (numeric, required) The output number\n"
            "         \"scriptPubKey\": \"hex\",   (string, required) script key\n"
            "         \"redeemScript\": \"hex\"    (string, required for P2SH) redeem script\n"
            "       }\n"
            "       ,...\n"
            "    ]\n"
            "3. \"privatekeys\"     (string, optional) A json array of base58-encoded private keys for signing\n"
            "    [                  (json array of strings, or 'null' if none provided)\n"
            "      \"privatekey\"   (string) private key in base58-encoding\n"
            "      ,...\n"
            "    ]\n"
            "4. \"sighashtype\"     (string, optional, default=ALL) The signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"

            "\nResult:\n"
            "{\n"
            "  \"hex\": \"value\",   (string) The raw transaction with signature(s) (hex-encoded string)\n"
            "  \"complete\": n       (numeric) if transaction has a complete set of signature (0 if not)\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"")
            + HelpExampleRpc("signrawtransaction", "\"myhex\"")
        );

    RPCTypeCheck(params, list_of(str_type)(array_type)(array_type)(str_type), true);

    vector<unsigned char> txData(ParseHexV(params[0], "argument 1"));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    vector<CTransaction> txVariants;
    while (!ssData.empty())
    {
        try {
            CTransaction tx;
            ssData >> tx;
            txVariants.push_back(tx);
        }
        catch (std::exception &e) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        }
    }

    if (txVariants.empty())
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transaction");

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the rawtx:
    CTransaction mergedTx(txVariants[0]);
    bool fComplete = true;

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(viewDummy);
    {
        LOCK(mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        BOOST_FOREACH(const CTxIn& txin, mergedTx.vin) {
            const uint256& prevHash = txin.prevout.hash;
            CCoins coins;
            view.GetCoins(prevHash, coins); // this is certainly allowed to fail
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    bool fGivenKeys = false;
    CBasicKeyStore tempKeystore;
    if (params.size() > 2 && params[2].type() != null_type)
    {
        fGivenKeys = true;
        Array keys = params[2].get_array();
        BOOST_FOREACH(Value k, keys)
        {
            CBitcoinSecret vchSecret;
            bool fGood = vchSecret.SetString(k.get_str());
            if (!fGood)
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
            CKey key = vchSecret.GetKey();
            tempKeystore.AddKey(key);
        }
    }
#ifdef ENABLE_WALLET
    else
        EnsureWalletIsUnlocked();
#endif

    // Add previous txouts given in the RPC call:
    if (params.size() > 1 && params[1].type() != null_type)
    {
        Array prevTxs = params[1].get_array();
        BOOST_FOREACH(Value& p, prevTxs)
        {
            if (p.type() != obj_type)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");

            Object prevOut = p.get_obj();

            RPCTypeCheck(prevOut, map_list_of("txid", str_type)("vout", int_type)("scriptPubKey", str_type));

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");

            vector<unsigned char> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            CCoins coins;
            if (view.GetCoins(txid, coins)) {
                if (coins.IsAvailable(nOut) && coins.vout[nOut].scriptPubKey != scriptPubKey) {
                    string err("Previous output scriptPubKey mismatch:\n");
                    err = err + coins.vout[nOut].scriptPubKey.ToString() + "\nvs:\n"+
                        scriptPubKey.ToString();
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
                // what todo if txid is known, but the actual output isn't?
            }
            if ((unsigned int)nOut >= coins.vout.size())
                coins.vout.resize(nOut+1);
            coins.vout[nOut].scriptPubKey = scriptPubKey;
            coins.vout[nOut].nValue = 0; // we don't know the actual output value
            view.SetCoins(txid, coins);

            // if redeemScript given and not using the local wallet (private keys
            // given), add redeemScript to the tempKeystore so it can be signed:
            if (fGivenKeys && scriptPubKey.IsPayToScriptHash())
            {
                RPCTypeCheck(prevOut, map_list_of("txid", str_type)("vout", int_type)("scriptPubKey", str_type)("redeemScript",str_type));
                Value v = find_value(prevOut, "redeemScript");
                if (!(v == Value::null))
                {
                    vector<unsigned char> rsData(ParseHexV(v, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    tempKeystore.AddCScript(redeemScript);
                }
            }
        }
    }

#ifdef ENABLE_WALLET
    const CKeyStore& keystore = ((fGivenKeys || !pwalletMain) ? tempKeystore : *pwalletMain);
#else
    const CKeyStore& keystore = tempKeystore;
#endif

    int nHashType = SIGHASH_ALL;
    if (params.size() > 3 && params[3].type() != null_type)
    {
        static map<string, int> mapSigHashValues =
            boost::assign::map_list_of
            (string("ALL"), int(SIGHASH_ALL))
            (string("ALL|ANYONECANPAY"), int(SIGHASH_ALL|SIGHASH_ANYONECANPAY))
            (string("NONE"), int(SIGHASH_NONE))
            (string("NONE|ANYONECANPAY"), int(SIGHASH_NONE|SIGHASH_ANYONECANPAY))
            (string("SINGLE"), int(SIGHASH_SINGLE))
            (string("SINGLE|ANYONECANPAY"), int(SIGHASH_SINGLE|SIGHASH_ANYONECANPAY))
            ;
        string strHashType = params[3].get_str();
        if (mapSigHashValues.count(strHashType))
            nHashType = mapSigHashValues[strHashType];
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sighash param");
    }

    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++)
    {
        CTxIn& txin = mergedTx.vin[i];
        CCoins coins;
        if (!view.GetCoins(txin.prevout.hash, coins) || !coins.IsAvailable(txin.prevout.n))
        {
            fComplete = false;
            continue;
        }
        const CScript& prevPubKey = coins.vout[txin.prevout.n].scriptPubKey;

        txin.scriptSig.clear();
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mergedTx.vout.size()))
            SignSignature(keystore, prevPubKey, mergedTx, i, nHashType);

        // ... and merge in other signatures:
        BOOST_FOREACH(const CTransaction& txv, txVariants)
        {
            txin.scriptSig = CombineSignatures(prevPubKey, mergedTx, i, txin.scriptSig, txv.vin[i].scriptSig);
        }
        if (!VerifyScript(txin.scriptSig, prevPubKey, mergedTx, i, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC, 0))
            fComplete = false;
    }

    Object result;
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << mergedTx;
    result.push_back(Pair("hex", HexStr(ssTx.begin(), ssTx.end())));
    result.push_back(Pair("complete", fComplete));

    return result;
}

Value sendrawtransaction(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "sendrawtransaction \"hexstring\" ( allowhighfees )\n"
            "\nSubmits raw transaction (serialized, hex-encoded) to local node and network.\n"
            "\nAlso see createrawtransaction and signrawtransaction calls.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw transaction)\n"
            "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction hash in hex\n"
            "\nExamples:\n"
            "\nCreate a transaction\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"") +
            "\nSend the transaction (signed hex)\n"
            + HelpExampleCli("sendrawtransaction", "\"signedhex\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("sendrawtransaction", "\"signedhex\"")
        );


    // parse hex string from parameter
    vector<unsigned char> txData(ParseHexV(params[0], "parameter"));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    CTransaction tx;

    bool fOverrideFees = false;
    if (params.size() > 1)
        fOverrideFees = params[1].get_bool();

    // deserialize binary data stream
    try {
        ssData >> tx;
    }
    catch (std::exception &e) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }
    uint256 hashTx = tx.GetHash();

    CCoinsViewCache &view = *pcoinsTip;
    CCoins existingCoins;
    bool fHaveMempool = mempool.exists(hashTx);
    bool fHaveChain = view.GetCoins(hashTx, existingCoins) && existingCoins.nHeight < 1000000000;
    if (!fHaveMempool && !fHaveChain) {
        // push to local node and sync with wallets
        CValidationState state;
        if (AcceptToMemoryPool(mempool, state, tx, false, NULL, !fOverrideFees))
            SyncWithWallets(hashTx, tx, NULL);
        else {
            if(state.IsInvalid())
                throw JSONRPCError(RPC_TRANSACTION_REJECTED, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
            else
                throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
        }
    } else if (fHaveChain) {
        throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
    }
    RelayTransaction(tx, hashTx);

    return hashTx.GetHex();
}
