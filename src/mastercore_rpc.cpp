// RPC calls

#include "rpcserver.h"
#include "util.h"
#include "init.h"
#include "wallet.h"

#include <stdint.h>
#include <string.h>
#include <map>

#include <fstream>
#include <vector>
#include <string>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>

#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"

#include "leveldb/db.h"
#include "leveldb/write_batch.h"

#include <openssl/sha.h>

using namespace std;
using namespace boost;
using namespace json_spirit;

#include "mastercore.h"

using namespace mastercore;

#include "mastercore_dex.h"
#include "mastercore_parse_string.h"
#include "mastercore_tx.h"
#include "mastercore_sp.h"
#include "mastercore_errors.h"

// display the tally map & the offer/accept list(s)
Value mscrpc(const Array& params, bool fHelp)
{
int extra = 0;
int extra2 = 0, extra3 = 0;

    if (fHelp || params.size() > 3)
        throw runtime_error(
            "mscrpc\n"
            "\nReturns the number of blocks in the longest block chain.\n"
            "\nResult:\n"
            "n    (numeric) The current block count\n"
            "\nExamples:\n"
            + HelpExampleCli("mscrpc", "")
            + HelpExampleRpc("mscrpc", "")
        );

  if (0 < params.size()) extra = atoi(params[0].get_str());
  if (1 < params.size()) extra2 = atoi(params[1].get_str());
  if (2 < params.size()) extra3 = atoi(params[2].get_str());

  printf("%s(extra=%d,extra2=%d,extra3=%d)\n", __FUNCTION__, extra, extra2, extra3);

  bool bDivisible = isPropertyDivisible(extra2);

  // various extra tests
  switch (extra)
  {
    case 0: // the old output
    {
    uint64_t total = 0;

        // display all balances
        for(map<string, CMPTally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it)
        {
          // my_it->first = key
          // my_it->second = value

          printf("%34s => ", (my_it->first).c_str());
          total += (my_it->second).print(extra2, bDivisible);
        }

        printf("total for property %d  = %X is %s\n", extra2, extra2, FormatDivisibleMP(total).c_str());
      }
      break;

    case 1:
      // display the whole CMPTxList (leveldb)
      p_txlistdb->printAll();
      p_txlistdb->printStats();
      break;

    case 2:
        // display smart properties
        _my_sps->printAll();
      break;

    case 3:
        unsigned int id;
        // for each address display all currencies it holds
        for(map<string, CMPTally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it)
        {
          // my_it->first = key
          // my_it->second = value

          printf("%34s => ", (my_it->first).c_str());
          (my_it->second).print(extra2);

          (my_it->second).init();
          while (0 != (id = (my_it->second).next()))
          {
            printf("Id: %u=0x%X ", id, id);
          }
          printf("\n");
        }
      break;

    case 4:
      for(CrowdMap::const_iterator it = my_crowds.begin(); it != my_crowds.end(); ++it)
      {
        (it->second).print(it->first);
      }
      break;

    case 5:
      printf("isMPinBlockRange(%d,%d)=%s\n", extra2, extra3, isMPinBlockRange(extra2, extra3, false) ? "YES":"NO");
      break;

    case 6:
      MetaDEx_debug_print();
      break;

    case 7:
      // display the whole CMPTradeList (leveldb)
      t_tradelistdb->printAll();
      t_tradelistdb->printStats();
      break;

  }

  return GetHeight();
}

// display an MP balance via RPC
Value getbalance_MP(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "getbalance_MP \"address\" propertyid\n"
            "\nReturns the Master Protocol balance for a given address and currency/property.\n"
            "\nResult:\n"
            "n    (numeric) The applicable balance for address:currency/propertyID pair\n"
            "\nExamples:\n"
            ">mastercored getbalance_MP 1EXoDusjGwvnjZUyKkxZ4UHEf77z6A5S4P 1\n"
        );
    std::string address = params[0].get_str();
    int64_t tmpPropertyId = params[1].get_int64();
    if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property identifier");

    unsigned int propertyId = int(tmpPropertyId);
    CMPSPInfo::Entry sp;
    if (false == _my_sps->getSP(propertyId, sp)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Property identifier does not exist");
    }

    bool divisible = false;
    divisible=sp.isDivisible();

    Object balObj;

    int64_t tmpBalAvailable = getUserAvailableMPbalance(address, propertyId);
    int64_t tmpBalReservedSell = getMPbalance(address, propertyId, SELLOFFER_RESERVE);
    int64_t tmpBalReservedAccept = 0;
    if (propertyId<3) tmpBalReservedAccept = getMPbalance(address, propertyId, ACCEPT_RESERVE);

    if (divisible)
    {
        balObj.push_back(Pair("balance", FormatDivisibleMP(tmpBalAvailable)));
        balObj.push_back(Pair("reserved", FormatDivisibleMP(tmpBalReservedSell+tmpBalReservedAccept)));
    }
    else
    {
        balObj.push_back(Pair("balance", FormatIndivisibleMP(tmpBalAvailable)));
        balObj.push_back(Pair("reserved", FormatIndivisibleMP(tmpBalReservedSell+tmpBalReservedAccept)));
    }

    return balObj;
}

// send a MP transaction via RPC - simple send
Value send_MP(const Array& params, bool fHelp)
{
if (fHelp || params.size() < 4 || params.size() > 6)
        throw runtime_error(
            "send_MP \"fromaddress\" \"toaddress\" propertyid \"amount\" ( \"redeemaddress\" \"referenceamount\" )\n"
            "\nCreates and broadcasts a simple send for a given amount and currency/property ID.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send from\n"
            "ToAddress     : the address to send to\n"
            "PropertyID    : the id of the smart property to send\n"
            "Amount        : the amount to send\n"
            "RedeemAddress : (optional) the address that can redeem the bitcoin outputs. Defaults to FromAddress\n"
            "ReferenceAmount:(optional)\n"
            "Result:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">mastercored send_MP 1FromAddress 1ToAddress PropertyID Amount 1RedeemAddress\n"
        );

  std::string FromAddress = (params[0].get_str());
  std::string ToAddress = (params[1].get_str());
  std::string RedeemAddress = (params.size() > 4) ? (params[4].get_str()): "";

  int64_t tmpPropertyId = params[2].get_int64();
  if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
    throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property identifier");
  unsigned int propertyId = int(tmpPropertyId);

  CMPSPInfo::Entry sp;
  if (false == _my_sps->getSP(propertyId, sp)) {
    throw JSONRPCError(RPC_INVALID_PARAMETER, "Property identifier does not exist");
  }

  bool divisible = false;
  divisible=sp.isDivisible();

  string strAmount = params[3].get_str();
  int64_t Amount = 0, additional = 0;
  Amount = StrToInt64(strAmount, divisible);

  if (0 >= Amount)
   throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");

  std::string strAdditional = (params.size() > 5) ? (params[5].get_str()): "0";
  additional = StrToInt64(strAdditional, true);

  int n = params.size();
  printf("#: %d, additional= %ld\n", n, additional);

  if ((0.01 * COIN) < additional)
   throw JSONRPCError(RPC_TYPE_ERROR, "Invalid reference amount");

  //some sanity checking of the data supplied?
  int code = 0;
  uint256 newTX = send_INTERNAL_1packet(FromAddress, ToAddress, RedeemAddress, propertyId, Amount, 0, 0, MSC_TYPE_SIMPLE_SEND, additional, &code);

  if (0 != code) throw JSONRPCError(code, error_str(code));

  //we need to do better than just returning a string of 0000000 here if we can't send the TX
  return newTX.GetHex();
}

// send a MP transaction via RPC - simple send
Value sendtoowners_MP(const Array& params, bool fHelp)
{
if (fHelp || params.size() < 3 || params.size() > 4)
        throw runtime_error(
            "sendtoowners_MP \"fromaddress\" propertyid \"amount\" ( \"redeemaddress\" )\n"
            "\nCreates and broadcasts a send-to-owners transaction for a given amount and currency/property ID.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send from\n"
            "PropertyID    : the id of the smart property to send\n"
            "Amount (string): the amount to send\n"
            "RedeemAddress : (optional) the address that can redeem the bitcoin outputs. Defaults to FromAddress\n"
            "\nResult:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">mastercored send_MP 1FromAddress PropertyID Amount 1RedeemAddress\n"
        );

  std::string FromAddress = (params[0].get_str());
  std::string RedeemAddress = (params.size() > 3) ? (params[3].get_str()): "";

  int64_t tmpPropertyId = params[1].get_int64();
  if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
    throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property identifier");

  unsigned int propertyId = int(tmpPropertyId);

  CMPSPInfo::Entry sp;
  if (false == _my_sps->getSP(propertyId, sp)) {
    throw JSONRPCError(RPC_INVALID_PARAMETER, "Property identifier does not exist");
  }

  bool divisible = false;
  divisible=sp.isDivisible();

//  printf("%s(), params3='%s' line %d, file: %s\n", __FUNCTION__, params[3].get_str().c_str(), __LINE__, __FILE__);

  string strAmount = params[2].get_str();
  int64_t Amount = 0;
  Amount = StrToInt64(strAmount, divisible);

  if (0 >= Amount)
    throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");

  // printf("%s() %40.25lf, %lu, line %d, file: %s\n", __FUNCTION__, tmpAmount, Amount, __LINE__, __FILE__);

  //some sanity checking of the data supplied?
  int code = 0;
  uint256 newTX = send_INTERNAL_1packet(FromAddress, "", RedeemAddress, propertyId, Amount, 0, 0, MSC_TYPE_SEND_TO_OWNERS, 0, &code);

  if (0 != code)
    throw JSONRPCError(code, error_str(code));

  //we need to do better than just returning a string of 0000000 here if we can't send the TX
  return newTX.GetHex();
}

Value sendrawtx_MP(const Array& params, bool fHelp)
{
if (fHelp || params.size() < 2 || params.size() > 5)
        throw runtime_error(
            "sendrawtx_MP \"fromaddress\" \"hexstring\" ( \"toaddress\" \"redeemaddress\" \"referenceamount\" )\n"
            "\nCreates and broadcasts a raw Master protocol transaction.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send from\n"
            "RawTX         : the hex-encoded raw transaction\n"
            "ToAddress     : the address to send to.  This should be empty: (\"\") for transaction\n"
            "                types that do not use a reference/to address\n"
            "RedeemAddress : (optional) the address that can redeem the bitcoin outputs. Defaults to FromAddress\n"
            "ReferenceAmount:(optional)\n"
            "\nResult:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">mastercored sendrawtx_MP 1FromAddress <tx bytes hex> 1ToAddress 1RedeemAddress\n"
        );

  std::string fromAddress = (params[0].get_str());
  std::string hexTransaction = (params[1].get_str());
  std::string toAddress = (params.size() > 2) ? (params[2].get_str()): "";
  std::string redeemAddress = (params.size() > 3) ? (params[3].get_str()): "";
  
  int64_t referenceAmount = 0;

  if (params.size() > 4)
      referenceAmount = StrToInt64(params[4].get_str(), true);

  //some sanity checking of the data supplied?
  uint256 newTX;
  vector<unsigned char> data = ParseHex(hexTransaction);
  int rc = ClassB_send(fromAddress, toAddress, redeemAddress, data, newTX, referenceAmount);

  if (0 != rc)
      throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("error code= %i", rc));

  //we need to do better than just returning a string of 0000000 here if we can't send the TX
  return newTX.GetHex();
}

Value getallbalancesforid_MP(const Array& params, bool fHelp)
{
   if (fHelp || params.size() != 1)
        throw runtime_error(
            "getallbalancesforid_MP propertyid\n"
            "\nGet a list of balances for a given currency or property identifier.\n"
            "\nArguments:\n"
            "1. propertyid    (int, required) The property identifier\n"
            "\nResult:\n"
            "{\n"
            "  \"address\" : \"1Address\",  (string) The address\n"
            "  \"balance\" : \"x.xxx\",     (string) The available balance of the address\n"
            "  \"reserved\" : \"x.xxx\",    (string) The amount reserved by sell offers and accepts\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("getallbalancesforid_MP", "1")
            + HelpExampleRpc("getallbalancesforid_MP", "1")
        );

    int64_t tmpPropertyId = params[0].get_int64();
    if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property identifier");

    unsigned int propertyId = int(tmpPropertyId);
    CMPSPInfo::Entry sp;
    if (false == _my_sps->getSP(propertyId, sp)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Property identifier does not exist");
    }

    bool divisible=false;
    divisible=sp.isDivisible();

    Array response;

    for(map<string, CMPTally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it)
    {
        unsigned int id;
        bool includeAddress=false;
        string address = (my_it->first).c_str();
        (my_it->second).init();
        while (0 != (id = (my_it->second).next()))
        {
           if(id==propertyId) { includeAddress=true; break; }
        }

        if (!includeAddress) continue; //ignore this address, has never transacted in this propertyId

        Object addressbal;

        int64_t tmpBalAvailable = getUserAvailableMPbalance(address, propertyId);
        int64_t tmpBalReservedSell = getMPbalance(address, propertyId, SELLOFFER_RESERVE);
        int64_t tmpBalReservedAccept = 0;
        if (propertyId<3) tmpBalReservedAccept = getMPbalance(address, propertyId, ACCEPT_RESERVE);

        addressbal.push_back(Pair("address", address));
        if(divisible)
        {
        addressbal.push_back(Pair("balance", FormatDivisibleMP(tmpBalAvailable)));
        addressbal.push_back(Pair("reserved", FormatDivisibleMP(tmpBalReservedSell+tmpBalReservedAccept)));
        }
        else
        {
        addressbal.push_back(Pair("balance", FormatIndivisibleMP(tmpBalAvailable)));
        addressbal.push_back(Pair("reserved", FormatIndivisibleMP(tmpBalReservedSell+tmpBalReservedAccept)));
        }
        response.push_back(addressbal);
    }
return response;
}

Value getallbalancesforaddress_MP(const Array& params, bool fHelp)
{
   string address;

   if (fHelp || params.size() != 1)
        throw runtime_error(
            "getallbalancesforaddress_MP \"address\"\n"
            "\nGet a list of all balances for a given address.\n"
            "\nArguments:\n"
            "1. address    (string, required) The address\n"
            "\nResult:\n"
            "{\n"
            "  \"propertyid\" : x,        (numeric) the property id\n"
            "  \"balance\" : \"x.xxx\",     (string) The available balance of the address\n"
            "  \"reserved\" : \"x.xxx\",    (string) The amount reserved by sell offers and accepts\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("getallbalancesforaddress_MP", "address")
            + HelpExampleRpc("getallbalancesforaddress_MP", "address")
        );

    address = params[0].get_str();
    if (address.empty())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");

    Array response;

    CMPTally *addressTally=getTally(address);

    if (NULL == addressTally) // addressTally object does not exist
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Address not found");

    addressTally->init();

    uint64_t propertyId; // avoid issues with json spirit at uint32
    while (0 != (propertyId = addressTally->next()))
    {
            bool divisible=false;
            CMPSPInfo::Entry sp;
            if (_my_sps->getSP(propertyId, sp)) {
              divisible = sp.isDivisible();
            }

            Object propertyBal;

            propertyBal.push_back(Pair("propertyid", propertyId));

            int64_t tmpBalAvailable = getUserAvailableMPbalance(address, propertyId);
            int64_t tmpBalReservedSell = getMPbalance(address, propertyId, SELLOFFER_RESERVE);
            int64_t tmpBalReservedAccept = 0;
            if (propertyId<3) tmpBalReservedAccept = getMPbalance(address, propertyId, ACCEPT_RESERVE);

            if (divisible)
            {
                    propertyBal.push_back(Pair("balance", FormatDivisibleMP(tmpBalAvailable)));
                    propertyBal.push_back(Pair("reserved", FormatDivisibleMP(tmpBalReservedSell+tmpBalReservedAccept)));
            }
            else
            {
                    propertyBal.push_back(Pair("balance", FormatIndivisibleMP(tmpBalAvailable)));
                    propertyBal.push_back(Pair("reserved", FormatIndivisibleMP(tmpBalReservedSell+tmpBalReservedAccept)));
            }

            response.push_back(propertyBal);
    }

    return response;
}

Value getproperty_MP(const Array& params, bool fHelp)
{
   if (fHelp || params.size() != 1)
        throw runtime_error(
            "getproperty_MP propertyid\n"
            "\nGet details for a property identifier.\n"
            "\nArguments:\n"
            "1. propertyid    (int, required) The property identifier\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"PropertyName\",     (string) the property name\n"
            "  \"category\" : \"PropertyCategory\",     (string) the property category\n"
            "  \"subcategory\" : \"PropertySubCategory\",     (string) the property subcategory\n"
            "  \"data\" : \"PropertyData\",     (string) the property data\n"
            "  \"url\" : \"PropertyURL\",     (string) the property URL\n"
            "  \"divisible\" : false,     (boolean) whether the property is divisible\n"
            "  \"issuer\" : \"1Address\",     (string) the property issuer address\n"
            "  \"issueancetype\" : \"Fixed\",     (string) the property method of issuance\n"
            "  \"totaltokens\" : x     (string) the total number of tokens in existence\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("getproperty_MP", "3")
            + HelpExampleRpc("getproperty_MP", "3")
        );

    int64_t tmpPropertyId = params[0].get_int64();
    if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property identifier");

    unsigned int propertyId = int(tmpPropertyId);
    CMPSPInfo::Entry sp;
    if (false == _my_sps->getSP(propertyId, sp)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Property identifier does not exist");
    }

    Object response;
        bool divisible = false;
        divisible=sp.isDivisible();
        string propertyName = sp.name;
        string propertyCategory = sp.category;
        string propertySubCategory = sp.subcategory;
        string propertyData = sp.data;
        string propertyURL = sp.url;
        uint256 creationTXID = sp.txid;
        int64_t totalTokens = getTotalTokens(propertyId);
        string issuer = sp.issuer;
        bool fixedIssuance = sp.fixed;

        uint64_t dispPropertyId = propertyId; //json spirit needs a uint64 as noted elsewhere
        response.push_back(Pair("propertyid", dispPropertyId)); //req by DexX to include propId in this output, no harm :)
        response.push_back(Pair("name", propertyName));
        response.push_back(Pair("category", propertyCategory));
        response.push_back(Pair("subcategory", propertySubCategory));
        response.push_back(Pair("data", propertyData));
        response.push_back(Pair("url", propertyURL));
        response.push_back(Pair("divisible", divisible));
        response.push_back(Pair("issuer", issuer));
        response.push_back(Pair("creationtxid", creationTXID.GetHex()));
        response.push_back(Pair("fixedissuance", fixedIssuance));
        if (divisible)
        {
            response.push_back(Pair("totaltokens", FormatDivisibleMP(totalTokens)));
        }
        else
        {
            response.push_back(Pair("totaltokens", FormatIndivisibleMP(totalTokens)));
        }

return response;
}

Value listproperties_MP(const Array& params, bool fHelp)
{
   if (fHelp)
        throw runtime_error(
            "listproperties_MP\n"
            "\nLists all smart properties.\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"PropertyName\",     (string) the property name\n"
            "  \"category\" : \"PropertyCategory\",     (string) the property category\n"
            "  \"subcategory\" : \"PropertySubCategory\",     (string) the property subcategory\n"
            "  \"data\" : \"PropertyData\",     (string) the property data\n"
            "  \"url\" : \"PropertyURL\",     (string) the property URL\n"
            "  \"divisible\" : false,     (boolean) whether the property is divisible\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("listproperties_MP", "")
            + HelpExampleRpc("listproperties_MP", "")
        );

    Array response;

    int64_t propertyId;
    unsigned int nextSPID = _my_sps->peekNextSPID(1);
    for (propertyId = 1; propertyId<nextSPID; propertyId++)
    {
        CMPSPInfo::Entry sp;
        if (false != _my_sps->getSP(propertyId, sp))
        {
            Object responseItem;

            bool divisible=sp.isDivisible();
            string propertyName = sp.name;
            string propertyCategory = sp.category;
            string propertySubCategory = sp.subcategory;
            string propertyData = sp.data;
            string propertyURL = sp.url;

            responseItem.push_back(Pair("propertyid", propertyId));
            responseItem.push_back(Pair("name", propertyName));
            responseItem.push_back(Pair("category", propertyCategory));
            responseItem.push_back(Pair("subcategory", propertySubCategory));
            responseItem.push_back(Pair("data", propertyData));
            responseItem.push_back(Pair("url", propertyURL));
            responseItem.push_back(Pair("divisible", divisible));

            response.push_back(responseItem);
        }
    }

    unsigned int nextTestSPID = _my_sps->peekNextSPID(2);
    for (propertyId = TEST_ECO_PROPERTY_1; propertyId<nextTestSPID; propertyId++)
    {
        CMPSPInfo::Entry sp;
        if (false != _my_sps->getSP(propertyId, sp))
        {
            Object responseItem;

            bool divisible=sp.isDivisible();
            string propertyName = sp.name;
            string propertyCategory = sp.category;
            string propertySubCategory = sp.subcategory;
            string propertyData = sp.data;
            string propertyURL = sp.url;

            responseItem.push_back(Pair("propertyid", propertyId));
            responseItem.push_back(Pair("name", propertyName));
            responseItem.push_back(Pair("category", propertyCategory));
            responseItem.push_back(Pair("subcategory", propertySubCategory));
            responseItem.push_back(Pair("data", propertyData));
            responseItem.push_back(Pair("url", propertyURL));
            responseItem.push_back(Pair("divisible", divisible));

            response.push_back(responseItem);
        }
    }
return response;
}

Value getcrowdsale_MP(const Array& params, bool fHelp)
{
   if (fHelp || params.size() < 1 )
        throw runtime_error(
            "getcrowdsale_MP propertyid\n"
            "\nGet information about a crowdsale for a property identifier.\n"
            "\nArguments:\n"
            "1. propertyid    (int, required) The property identifier\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"PropertyName\",     (string) the property name\n"
            "  \"active\" : false,     (boolean) whether the crowdsale is active\n"
            "  \"issuer\" : \"1Address\",     (string) the issuer address\n"
            "  \"creationtxid\" : \"txid\",     (string) the transaction that created the crowdsale\n"
            "  \"propertyiddesired\" : x,     (numeric) the property ID desired\n"
            "  \"tokensperunit\" : x,     (numeric) the number of tokens awarded per unit\n"
            "  \"earlybonus\" : x,     (numeric) the percentage per week early bonus applied\n"
            "  \"percenttoissuer\" : x,     (numeric) the percentage awarded to the issuer\n"
            "  \"starttime\" : xxx,     (numeric) the start time of the crowdsale\n"
            "  \"deadline\" : xxx,     (numeric) the time the crowdsale will automatically end\n"
            "  \"closedearly\" : false,     (boolean) whether the crowdsale was ended early\n"
            "  \"maxtokens\" : false,     (boolean) whether the crowdsale was ended early due to maxing the token count\n"
            "  \"endedtime\" : xxx,     (numeric) the time the crowdsale ended\n"
            "  \"closetx\" : \"txid\",     (string) the transaction that closed the crowdsale\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("getcrowdsale_MP", "3")
            + HelpExampleRpc("getcrowdsale_MP", "3")
        );

    int64_t tmpPropertyId = params[0].get_int64();
    if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property identifier");

    unsigned int propertyId = int(tmpPropertyId);
    CMPSPInfo::Entry sp;
    if (false == _my_sps->getSP(propertyId, sp)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Property identifier does not exist");
    }

    bool showVerbose = false;
    if (params.size() > 1) showVerbose = params[1].get_bool();

    bool fixedIssuance = sp.fixed;
    bool manualIssuance = sp.manual;
    if (fixedIssuance || manualIssuance) // property was not a variable issuance
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Property was not created with a crowdsale");

    uint256 creationHash = sp.txid;

    CTransaction tx;
    uint256 hashBlock = 0;
    if (!GetTransaction(creationHash, tx, hashBlock, true))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

    if ((0 == hashBlock) || (NULL == mapBlockIndex[hashBlock]))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unconfirmed transactions are not supported");

    Object response;

    bool active = false;
    active = isCrowdsaleActive(propertyId);
    bool divisible = false;
    divisible=sp.isDivisible();
    string propertyName = sp.name;
    int64_t startTime = mapBlockIndex[hashBlock]->nTime;
    int64_t deadline = sp.deadline;
    uint8_t earlyBonus = sp.early_bird;
    uint8_t percentToIssuer = sp.percentage;
    bool closeEarly = sp.close_early;
    bool maxTokens = sp.max_tokens;
    int64_t timeClosed = sp.timeclosed;
    string txidClosed = sp.txid_close.GetHex();
    string issuer = sp.issuer;
    int64_t amountRaised = 0;
    int64_t tokensIssued = getTotalTokens(propertyId);
    int64_t missedTokens = sp.missedTokens;
    int64_t tokensPerUnit = sp.num_tokens;
    int64_t propertyIdDesired = sp.property_desired;
    std::map<std::string, std::vector<uint64_t> > database;

    if (active)
    {
          bool crowdFound = false;
          for(CrowdMap::const_iterator it = my_crowds.begin(); it != my_crowds.end(); ++it)
          {
              CMPCrowd crowd = it->second;
              int64_t tmpPropertyId = crowd.getPropertyId();
              if (tmpPropertyId == propertyId)
              {
                  crowdFound = true;
                  database = crowd.getDatabase();
              }
          }
          if (!crowdFound)
                  throw JSONRPCError(RPC_INTERNAL_ERROR, "Crowdsale is flagged active but cannot be retrieved");
    }
    else
    {
        database = sp.historicalData;
    }

    fprintf(mp_fp,"\nSIZE OF DB %lu\n", sp.historicalData.size() );
    //bool closedEarly = false; //this needs to wait for dead crowdsale persistence
    //int64_t endedTime = 0; //this needs to wait for dead crowdsale persistence

    bool divisibleDesired = false;
    CMPSPInfo::Entry spDesired;
    if (false == _my_sps->getSP(propertyId, spDesired)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Desired property identifier does not exist");
    }
    divisibleDesired = spDesired.isDivisible();
    divisibleDesired = isPropertyDivisible(propertyIdDesired);

    Array participanttxs;
    std::map<std::string, std::vector<uint64_t> >::const_iterator it;
    for(it = database.begin(); it != database.end(); it++)
    {
        Object participanttx;

        string txid = it->first; //uint256 txid = it->first;
        int64_t userTokens = it->second.at(2);
        int64_t issuerTokens = it->second.at(3);
        int64_t amountSent = it->second.at(0);

        amountRaised += amountSent;
        participanttx.push_back(Pair("txid", txid)); //.GetHex()).c_str();
        if (divisibleDesired)
        {
             participanttx.push_back(Pair("amountsent", FormatDivisibleMP(amountSent)));
        }
        else
        {
             participanttx.push_back(Pair("amountsent", FormatIndivisibleMP(amountSent)));
        }
        if (divisible)
        {
             participanttx.push_back(Pair("participanttokens", FormatDivisibleMP(userTokens)));
        }
        else
        {
             participanttx.push_back(Pair("participanttokens", FormatIndivisibleMP(userTokens)));
        }
        if (divisible)
        {
             participanttx.push_back(Pair("issuertokens", FormatDivisibleMP(issuerTokens)));
        }
        else
        {
             participanttx.push_back(Pair("issuertokens", FormatIndivisibleMP(issuerTokens)));
        }
        participanttxs.push_back(participanttx);
    }

    response.push_back(Pair("name", propertyName));
    response.push_back(Pair("active", active));
    response.push_back(Pair("issuer", issuer));
    response.push_back(Pair("propertyiddesired", propertyIdDesired));
    if (divisible)
    {
        response.push_back(Pair("tokensperunit", FormatDivisibleMP(tokensPerUnit)));
    }
    else
    {
        response.push_back(Pair("tokensperunit", FormatIndivisibleMP(tokensPerUnit)));
    }
    response.push_back(Pair("earlybonus", earlyBonus));
    response.push_back(Pair("percenttoissuer", percentToIssuer));
    response.push_back(Pair("starttime", startTime));
    response.push_back(Pair("deadline", deadline));

    if (divisibleDesired)
    {
        response.push_back(Pair("amountraised", FormatDivisibleMP(amountRaised)));
    }
    else
    {
        response.push_back(Pair("amountraised", FormatIndivisibleMP(amountRaised)));
    }
    if (divisible)
    {
        response.push_back(Pair("tokensissued", FormatDivisibleMP(tokensIssued)));
        response.push_back(Pair("addedissuertokens", FormatDivisibleMP(missedTokens)));
    }
    else
    {
        response.push_back(Pair("tokensissued", FormatIndivisibleMP(tokensIssued)));
        response.push_back(Pair("addedissuertokens", FormatIndivisibleMP(missedTokens)));
    }
    if (!active) response.push_back(Pair("closedearly", closeEarly));
    if (!active) response.push_back(Pair("maxtokens", maxTokens));
    if (closeEarly) response.push_back(Pair("endedtime", timeClosed));
    if (closeEarly && !maxTokens) response.push_back(Pair("closetx", txidClosed));
    
    // array of txids contributing to crowdsale here if needed
    if (showVerbose)
    {
        response.push_back(Pair("participanttransactions", participanttxs));
    }
return response;
}

Value getactivecrowdsales_MP(const Array& params, bool fHelp)
{
   if (fHelp)
        throw runtime_error(
            "getactivecrowdsales_MP\n"
            "\nGet active crowdsales.\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"PropertyName\",     (string) the property name\n"
            "  \"issuer\" : \"1Address\",     (string) the issuer address\n"
            "  \"creationtxid\" : \"txid\",     (string) the transaction that created the crowdsale\n"
            "  \"propertyiddesired\" : x,     (numeric) the property ID desired\n"
            "  \"tokensperunit\" : x,     (numeric) the number of tokens awarded per unit\n"
            "  \"earlybonus\" : x,     (numeric) the percentage per week early bonus applied\n"
            "  \"percenttoissuer\" : x,     (numeric) the percentage awarded to the issuer\n"
            "  \"starttime\" : xxx,     (numeric) the start time of the crowdsale\n"
            "  \"deadline\" : xxx,     (numeric) the time the crowdsale will automatically end\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("getactivecrowdsales_MP", "")
            + HelpExampleRpc("getactivecrowdsales_MP", "")
        );

      Array response;

      for(CrowdMap::const_iterator it = my_crowds.begin(); it != my_crowds.end(); ++it)
      {
          CMPCrowd crowd = it->second;
          CMPSPInfo::Entry sp;
          bool spFound = _my_sps->getSP(crowd.getPropertyId(), sp);
          int64_t propertyId = crowd.getPropertyId();
          if (spFound)
          {
              Object responseObj;

              uint256 creationHash = sp.txid;

              CTransaction tx;
              uint256 hashBlock = 0;
              if (!GetTransaction(creationHash, tx, hashBlock, true))
                  throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

              if ((0 == hashBlock) || (NULL == mapBlockIndex[hashBlock]))
                  throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unconfirmed transactions are not supported");

              bool divisible = false;
              divisible=sp.isDivisible();
              string propertyName = sp.name;
              int64_t startTime = mapBlockIndex[hashBlock]->nTime;
              int64_t deadline = sp.deadline;
              uint8_t earlyBonus = sp.early_bird;
              uint8_t percentToIssuer = sp.percentage;
              string issuer = sp.issuer;
              int64_t tokensPerUnit = sp.num_tokens;
              int64_t propertyIdDesired = sp.property_desired;

              responseObj.push_back(Pair("propertyid", propertyId));
              responseObj.push_back(Pair("name", propertyName));
              responseObj.push_back(Pair("issuer", issuer));
              responseObj.push_back(Pair("propertyiddesired", propertyIdDesired));
              if (divisible)
              {
                  responseObj.push_back(Pair("tokensperunit", FormatDivisibleMP(tokensPerUnit)));
              }
              else
              {
                  responseObj.push_back(Pair("tokensperunit", FormatIndivisibleMP(tokensPerUnit)));
              }
              responseObj.push_back(Pair("earlybonus", earlyBonus));
              responseObj.push_back(Pair("percenttoissuer", percentToIssuer));
              responseObj.push_back(Pair("starttime", startTime));
              responseObj.push_back(Pair("deadline", deadline));

              response.push_back(responseObj);
          }
      }

return response;
}

Value getgrants_MP(const Array& params, bool fHelp)
{
   if (fHelp || params.size() != 1 )
        throw runtime_error(
            "getgrants_MP propertyid\n"
            "\nGet information about grants and revokes for a property identifier.\n"
            "\nArguments:\n"
            "1. propertyid    (int, required) The property identifier\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"PropertyName\",   (string) the property name\n"
            "  \"issuer\" : \"1Address\",     (string) the issuer address\n"
            "  \"creationtxid\" : \"txid\",   (string) the transaction that created the crowdsale\n"
            "  \"totaltokens\" : \"1234\",    (string) the number of tokens in circulation\n"
            "  \"issuances\" : [\n"
            "                    {\n"
            "                      \"txid\" : \"txid\",   (string) the transaction that granted/revoked tokens\n"
            "                      \"grant\" : \"1234\",  (string) the number of tokens granted\n"
            "                    },\n"
            "                    {\n"
            "                      \"txid\" : \"txid\",   (string) the transaction that granted/revoked tokens\n"
            "                      \"revoke\" : \"1234\", (string) the number of tokens revoked\n"
            "                    },\n"
            "                    ...\n"
            "                  ]\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("getgrants_MP", "3")
            + HelpExampleRpc("getgrants_MP", "3")
        );

    int64_t tmpPropertyId = params[0].get_int64();
    if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property identifier");

    unsigned int propertyId = int(tmpPropertyId);
    CMPSPInfo::Entry sp;
    if (false == _my_sps->getSP(propertyId, sp)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Property identifier does not exist");
    }

    bool fixedIssuance = sp.fixed;
    bool manualIssuance = sp.manual;
    if (fixedIssuance || !manualIssuance) // property was not a manual issuance
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Property was not created with a manual issuance");

    uint256 creationHash = sp.txid;

    Object response;

//    bool active = isCrowdsaleActive(propertyId);  // UNUSED WARNING
//    bool divisible = sp.isDivisible(); // UNUSED WARNING
    string propertyName = sp.name;
    string issuer = sp.issuer;
    int64_t totalTokens = getTotalTokens(propertyId);

    Array issuancetxs;
    std::map<std::string, std::vector<uint64_t> >::const_iterator it;
    for(it = sp.historicalData.begin(); it != sp.historicalData.end(); it++)
    {

        string txid = it->first; //uint256 txid = it->first;
        int64_t grantedTokens = it->second.at(0);
        int64_t revokedTokens = it->second.at(1);
        if (grantedTokens > 0){
          Object granttx;
          granttx.push_back(Pair("txid", txid));
          if (sp.isDivisible()) {
            granttx.push_back(Pair("grant", FormatDivisibleMP(grantedTokens)));
          } else {
            granttx.push_back(Pair("grant", FormatIndivisibleMP(grantedTokens)));
          }
          issuancetxs.push_back(granttx);
        }

        if (revokedTokens > 0){
          Object revoketx;
          revoketx.push_back(Pair("txid", txid));
          if (sp.isDivisible()) {
            revoketx.push_back(Pair("revoke", FormatDivisibleMP(revokedTokens)));
          } else {
            revoketx.push_back(Pair("revoke", FormatIndivisibleMP(revokedTokens)));
          }
          issuancetxs.push_back(revoketx);
        }
    }

    response.push_back(Pair("name", propertyName));
    response.push_back(Pair("issuer", issuer));
    response.push_back(Pair("creationtxid", creationHash.GetHex()));
    if (sp.isDivisible()) {
      response.push_back(Pair("totaltokens", FormatDivisibleMP(totalTokens)));
    } else {
      response.push_back(Pair("totaltokens", FormatIndivisibleMP(totalTokens)));
    }
    response.push_back(Pair("issuances", issuancetxs));
    return response;
}

int check_prop_valid(int64_t tmpPropId, string error, string exist_error ) {
  CMPSPInfo::Entry sp;

  if ((1 > tmpPropId) || (4294967295 < tmpPropId)) 
    throw JSONRPCError(RPC_INVALID_PARAMETER, error);
  if (false == _my_sps->getSP(tmpPropId, sp)) 
    throw JSONRPCError(RPC_INVALID_PARAMETER, exist_error);

  return tmpPropId;
}

void sort_lexical(std::vector<Object> *unsorted) {
  std::vector<Object> sorted;
  std::vector<int> sorted_int;

  for(int i = 0; i < (int) unsorted->size(); i++ ) {
    Object current = unsorted->at(i);
    string txid = current[1].value_.get_str().c_str();

    int lexical_value = 0;

    // compute lexical value of txid
    for ( int j = 0; j < (int) txid.size(); j++) { lexical_value += (int) txid[j]; }

    // collect lexical values for sorting
    sorted_int.push_back( lexical_value );
    sorted.push_back( current );

    // sort by lexical value
    for ( int k = 0; k < (int) sorted_int.size(); k++) {
      //printf(" \n lexical value %d < sorted_int[k] %d \n ", lexical_value, sorted_int[k] );
      if ( lexical_value < sorted_int[k]  ) { 
        sorted.insert( sorted.begin() + k, current );
        sorted.pop_back(); 
        break;
      } 
    }
  }

  // sanity check, below check should never be false 
  if ( sorted.size() == unsorted->size() ) *unsorted = sorted;
}

Object find_next_largest(Array *elems) {
  Object largest;

  int largest_index = 0, iter = 0, largest_block = 0;

  //Look for largest blockheight in elems
  for( Array::iterator it = elems->begin(); it != elems->end(); ++it) {
    Object temp_obj = it->get_obj();
    int block_ = temp_obj[10].value_.get_int(); //10th pos is blockheight

    if ( block_ >= largest_block )
    {
      largest_block = block_;
      largest = temp_obj;
      largest_index = it - elems->begin();
    }
    iter += 1;
  }
  //Mark the largest blockheight as already used
  elems->at( largest_index ).get_obj()[10] = Pair("block", -1); 
  return largest;
}

//Sorts metadex objects that contain more than 1 item
void sort_mdex_obj(Array *response) {
 Array res;
 Object current_largest, next_largest;

 int c_block, n_block; // current block, last block
 std::vector<Object> unsorted; //lexical sort

 for(int i = 0; i < (int) response->size(); i+=2) {
   //Retreive largest and next largest object by blockheight
   current_largest = find_next_largest(response);
   c_block = current_largest[10].value_.get_int();

   next_largest = find_next_largest(response);
   n_block = next_largest[10].value_.get_int(); 

   unsorted.push_back(current_largest);

   //Collect any other objects together that have the same block
   while( c_block == n_block ) {
     unsorted.push_back(next_largest);
     next_largest = find_next_largest(response);
     n_block = next_largest[10].value_.get_int(); 
     i++;
   }
   
   //Lexically sort blocks if more than 1 per blockheight
   //Add sorted data to res object
   if( c_block != n_block )
   {
     if ( unsorted.size() > 1 ) sort_lexical(&unsorted);
     for(int j = 0; j < (int) unsorted.size(); j++ ) { res.push_back( unsorted[j] ); }
     unsorted.clear();
     res.push_back(next_largest); 
   }
 }

 // sanity check, below check should never be false 
 if( res.size() == response->size() ) *response = res;
}

void add_mdex_fields(Object *metadex_obj, CMPMetaDEx obj, bool c_own_div, bool c_want_div, string eco) {

  string add_txid; for (int i = 0; i < 64; i++) { add_txid += '0'; }
 
  metadex_obj->push_back(Pair("address", obj.getAddr().c_str()));

  //add detection for ADDs
  if ( obj.getHash().GetHex() == add_txid ) metadex_obj->push_back(Pair("txid", "ADD transaction" ));  
  else  metadex_obj->push_back(Pair("txid", obj.getHash().GetHex() )); 

  metadex_obj->push_back(Pair("ecosystem", eco ));
  metadex_obj->push_back(Pair("property_owned", (uint64_t) obj.getProperty()));
  metadex_obj->push_back(Pair("property_desired", (uint64_t) obj.getDesProperty()));
  metadex_obj->push_back(Pair("property_owned_divisible", c_own_div));
  metadex_obj->push_back(Pair("property_desired_divisible", c_want_div));

  //uint64_t *price = obj.getPrice();
  //uint64_t *invprice = obj.getInversePrice();

  //metadex_obj->push_back(Pair("unit_price", strprintf("%lu.%.8s",  price[0],  boost::lexical_cast<std::string>(price[1]) ).c_str() ) );
  //metadex_obj->push_back(Pair("inverse_unit_price", strprintf("%lu.%.8s", invprice[0], boost::lexical_cast<std::string>(invprice[1]) ).c_str() ) );
  //active?
  metadex_obj->push_back(Pair("amount_original", FormatDivisibleMP(obj.getAmount())));
  metadex_obj->push_back(Pair("amount_desired", FormatDivisibleMP(obj.getAmountDesired())));
  metadex_obj->push_back(Pair("action", (int) obj.getAction()));
  metadex_obj->push_back(Pair("block", obj.getBlock()));
  metadex_obj->push_back(Pair("blockTime", obj.getBlockTime()));
}

void MetaDexObjectToJSON(const CMPMetaDEx& obj, Object& metadex_obj)
{
    CMPSPInfo::Entry spProperty;
    CMPSPInfo::Entry spDesProperty;

    _my_sps->getSP(obj.getProperty(), spProperty);
    _my_sps->getSP(obj.getDesProperty(), spDesProperty);

    // TODO: proper handling of ADD transactions
    std::string strZeroHash; strZeroHash.resize(64, '0');
    std::string strTransactionHash = obj.getHash().GetHex();
    if (strTransactionHash == strZeroHash) strTransactionHash = "ADD transaction";

    std::string strAmountOriginal = FormatMP(obj.getProperty(), obj.getAmount());
    std::string strAmountDesired = FormatMP(obj.getDesProperty(), obj.getAmountDesired());
    std::string strEcosystem = isTestEcosystemProperty(obj.getProperty()) ? "Test" : "Main";

    // add data to JSON object
    metadex_obj.push_back(Pair("address", obj.getAddr()));
    metadex_obj.push_back(Pair("txid", strTransactionHash));
    metadex_obj.push_back(Pair("ecosystem", strEcosystem));
    metadex_obj.push_back(Pair("property_owned", (uint64_t) obj.getProperty()));
    metadex_obj.push_back(Pair("property_desired", (uint64_t) obj.getDesProperty()));
    metadex_obj.push_back(Pair("property_owned_divisible", spProperty.isDivisible()));
    metadex_obj.push_back(Pair("property_desired_divisible", spDesProperty.isDivisible()));
    metadex_obj.push_back(Pair("amount_original", strAmountOriginal));
    metadex_obj.push_back(Pair("amount_desired", strAmountDesired));
    metadex_obj.push_back(Pair("action", (int) obj.getAction()));
    metadex_obj.push_back(Pair("block", obj.getBlock()));
    metadex_obj.push_back(Pair("blocktime", obj.getBlockTime()));
}

void MetaDexObjectsToJSON(std::vector<CMPMetaDEx> vMetaDexObjs, Array& response)
{
    MetaDEx_compare compareByHeight;

    // sorts metadex objects based on block height and position in block
    std::sort (vMetaDexObjs.begin(), vMetaDexObjs.end(), compareByHeight);

    for (std::vector<CMPMetaDEx>::const_iterator it = vMetaDexObjs.begin(); it != vMetaDexObjs.end(); ++it) {
        Object metadex_obj;
        MetaDexObjectToJSON(*it, metadex_obj);

        response.push_back(metadex_obj);
    }
}

Value trade_MP(const Array& params, bool fHelp) {

   if (fHelp || params.size() < 6)
        throw runtime_error(
            "trade_MP\n"
            "\nPlace a trade on the Metadex\n"
            
            "\nArguments:\n"
            "1. address           (string, required) address that is making the sale\n"
            "2. amount            (string, required) amount owned to put up on sale\n"
            "3. property_id1      (int, required) property owned to put up on sale\n"
            "4. amount            (string, required) amount wanted/willing to purchase\n"
            "5. property_id2      (int, required) property wanted/willing to purchase\n"
            "6. action            (int, required) decision to either start a new (1), update(2), or cancel(3) an offer\n"
            "7. RedeemAddress : (optional) the address that can redeem the bitcoin outputs. Defaults to FromAddress\n"
        );

  CMPSPInfo::Entry sp;
  bool divisible_sale = false, divisible_want = false; 
  std::string FromAddress = params[0].get_str();
  std::string RedeemAddress = (params.size() > 6) ? (params[6].get_str()): "";

  const unsigned int propertyIdSale = check_prop_valid( params[2].get_int64() , "Invalid property identifier (Sale)", "Property identifier does not exist (Sale)"); 

  const unsigned int propertyIdWant = check_prop_valid( params[4].get_int64() , "Invalid property identifier (Want)", "Property identifier does not exist (Want)"); 
  
  if (! (isTestEcosystemProperty(propertyIdSale) == isTestEcosystemProperty(propertyIdWant)) )
  {
    throw JSONRPCError(RPC_INVALID_PARAMETER, "Properties must be in the same ecosystem (Main/Test)");
  }

  if (propertyIdSale == propertyIdWant)
  {
    throw JSONRPCError(RPC_INVALID_PARAMETER, "Properties must be different");
  }

  _my_sps->getSP(propertyIdSale, sp);
  divisible_sale=sp.isDivisible();

  _my_sps->getSP(propertyIdWant, sp);
  divisible_want=sp.isDivisible();

  std::string strAmountSale = params[1].get_str();
  int64_t Amount_Sale = 0;
  Amount_Sale = StrToInt64(strAmountSale, divisible_sale);

  std::string strAmountWant = params[3].get_str();
  int64_t Amount_Want = 0;
  Amount_Want = StrToInt64(strAmountWant, divisible_want);

  if (0 >= Amount_Sale)
    throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount (Sale)");

  if (0 >= Amount_Want)
    throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount (Want)");

  int64_t action = params[5].get_int64();
  if ((action > 2) || (0 >= action))
    throw JSONRPCError(RPC_TYPE_ERROR, "Invalid action (1 or 2 only)");

 //printf("\n params: %s %lu %u %lu %u\n", FromAddress.c_str(), Amount_Sale, propertyIdSale, Amount_Want, propertyIdWant);
  int code = 0;
  uint256 newTX = send_INTERNAL_1packet(FromAddress, "", RedeemAddress, propertyIdSale, Amount_Sale, propertyIdWant, Amount_Want, MSC_TYPE_METADEX, action, &code);

  if (0 != code) throw JSONRPCError(code, error_str(code) );
  
  //we need to do better than just returning a string of 0000000 here if we can't send the TX
  return newTX.GetHex();
} 

Value getorderbook_MP(const Array& params, bool fHelp) {

   if (fHelp || params.size() < 1)
        throw runtime_error(
            "getorderbook_MP property_id1 ( property_id2 )\n"
            "\nAllows user to request active order information from the order book\n"
            
            "\nArguments:\n"
            "1. property_id1            (int, required) amount owned to up on sale\n"
            "2. property_id2         (int, optional) property owned to put up on sale\n"
        );

  Array response;

  Object metadex_obj;
  unsigned int propertyIdSaleFilter = 0, propertyIdWantFilter = 0;

  bool filter_by_desired = (params.size() == 2) ? true : false;

  propertyIdSaleFilter = check_prop_valid( params[0].get_int64() , "Invalid property identifier (Sale)", "Property identifier does not exist (Sale)"); 

  if ( filter_by_desired ) {
    propertyIdWantFilter = check_prop_valid( params[1].get_int64() , "Invalid property identifier (Want)", "Property identifier does not exist (Want)"); 
  }

  //for each address
  //get property pair and total order amount at a price

  for (md_PropertiesMap::iterator my_it = metadex.begin(); my_it != metadex.end(); ++my_it)
  {
    md_PricesMap & prices = my_it->second;
    for (md_PricesMap::iterator it = prices.begin(); it != prices.end(); ++it)
    {
      md_Set & indexes = (it->second);
      for (md_Set::iterator it = indexes.begin(); it != indexes.end(); ++it)
      {
        CMPMetaDEx obj = *it;
        CMPSPInfo::Entry sp;

        //this filter, the first part is filtering by two currencies, the second part is filtering by the first only
        bool filter = ( filter_by_desired && ( obj.getProperty() == propertyIdSaleFilter ) && ( obj.getDesProperty() == propertyIdWantFilter ) ) || ( !filter_by_desired && ( obj.getProperty() == propertyIdSaleFilter ) );

        if ( filter  ) {
            //clear obj before reuse
            metadex_obj.clear();
            
            _my_sps->getSP(obj.getProperty(), sp);
            bool c_own_div = sp.isDivisible();

            _my_sps->getSP(obj.getDesProperty(), sp);
            bool c_want_div = sp.isDivisible();

            string eco = (isTestEcosystemProperty(propertyIdSaleFilter) == true) ? "Test" : "Main";

            // add data to obj
            add_mdex_fields( &metadex_obj , obj , c_own_div, c_want_div, eco);

            //add it to response
            response.push_back(metadex_obj);
        }

      }
    }
  }
  if( response.size() > 1 ) sort_mdex_obj( &response );

  return response;
}

Value gettradessince_MP(const Array& params, bool fHelp)
{
    const unsigned int timeTwoWeeksAgo = GetLatestBlockTime() - 1209600;

    if (fHelp || params.size() > 3)
        throw runtime_error(
            "\nReturns the last known orders from the order book with a timestamp equal or \n"
            "greater than provided.\n"

            "\nNote: the order of property ids for the filter has no relevance, and filters such \n"
            "as \"gettradessince_MP "+strprintf("%s",timeTwoWeeksAgo)+" 1 3\" and \"gettradessince_MP "
            +strprintf("%s",timeTwoWeeksAgo)+" 3 1\" can \n"
            "be used equally.\n"

            "\nArguments:\n"
            "1. timestamp       (int, optional, default=["+strprintf("%s",timeTwoWeeksAgo)+"]) earlier orders are excluded\n"
            "2. property_id1    (int, optional) filter orders by property_id1 on either side of the trade\n"
            "3. property_id2    (int, optional) filter orders by property_id1 and property_id2\n"

            "\nExamples:\n"
            + HelpExampleCli("gettradessince_MP", "")
            + HelpExampleRpc("gettradessince_MP", "")
        );

    unsigned int propertyIdSaleFilter = 0;
    unsigned int propertyIdWantFilter = 0;

    uint64_t timestamp = (params.size() > 0) ? params[0].get_int64() : timeTwoWeeksAgo;    

    bool filter_by_one = (params.size() > 1);
    bool filter_by_two = (params.size() > 2);

    if (filter_by_one)
        propertyIdSaleFilter = check_prop_valid(params[1].get_int64(),
                "Invalid property identifier (Sale)",
                "Property identifier does not exist (Sale)");

    if (filter_by_two)
        propertyIdWantFilter = check_prop_valid(params[2].get_int64(),
                "Invalid property identifier (Want)", 
                "Property identifier does not exist (Want)");

    std::vector<CMPMetaDEx> vMetaDexObjects;

    for (md_PropertiesMap::const_iterator it = metadex.begin(); it != metadex.end(); ++it) {
        const md_PricesMap& prices = it->second;
        for (md_PricesMap::const_iterator it = prices.begin(); it != prices.end(); ++it) {
            const md_Set& indexes = it->second;
            for (md_Set::const_iterator it = indexes.begin(); it != indexes.end(); ++it) {
                const CMPMetaDEx& obj = *it;

                bool sale_filter_property_found =
                    propertyIdSaleFilter == obj.getProperty() || propertyIdSaleFilter == obj.getDesProperty();
                bool want_filter_property_found =
                    propertyIdWantFilter == obj.getProperty() || propertyIdWantFilter == obj.getDesProperty();

                if ((filter_by_one && !(sale_filter_property_found || want_filter_property_found)) ||
                    (filter_by_two && !(sale_filter_property_found && want_filter_property_found)))
                    continue;

                if (obj.getBlockTime() >= timestamp)
                    vMetaDexObjects.push_back(obj);
            }
        }
    }

    Array response;
    MetaDexObjectsToJSON(vMetaDexObjects, response);

    return response;
}

Value getopenorders_MP(const Array& params, bool fHelp) {

   if (fHelp)
        throw runtime_error(
            "getorderbook_MP\n"
            "\nAllows user to request active order information from the order book\n"
            
            "\nArguments:\n"
            "1. property_id1            (int, required) amount owned to up on sale\n"
            "2. property_id2         (int, optional) property owned to put up on sale\n"
            
            "\nResult:\n"
            "[                (array of string)\n"
            "  \"hash\"         (string) Transaction id\n"            
            "  ,...\n"
            "]\n"

            "\nbExamples\n"
            //+ HelpExampleCli("trade_MP", "50", "3", "500", "5" )
            //+ HelpExampleRpc("trade_MP", "50", "3", "500", "5" )
        );
  return "\nNot Implemented";
}
Value gettradehistory_MP(const Array& params, bool fHelp) {

   if (fHelp || params.size() < 1)
        throw runtime_error(
            "gettradehistory_MP\n"
            "\nAllows user to retreive trade history for some address on the Metadex\n"
            
            "\nArguments:\n"
            "1. address          (string, required) address to query history on\n"
            "2. number            (int, optional ) number of trades to retreive\n"
            "3. property_id         (int, optional) filter by propertyid on one side\n"
        );
  
  Array response;
  Object metadex_obj;

  string address = params[0].get_str();
  unsigned int number_trades = (params.size() == 2 ? (unsigned int) params[1].get_int64() : 512);
  unsigned int propertyIdFilter = 0;

  bool filter_by_one = (params.size() == 3) ? true : false;

  if( filter_by_one ) {
    propertyIdFilter = check_prop_valid( params[2].get_int64() , "Invalid property identifier (Sale)", "Property identifier does not exist (Sale)"); 
  }

  for (md_PropertiesMap::iterator my_it = metadex.begin(); my_it != metadex.end(); ++my_it)
  {
    md_PricesMap & prices = my_it->second;
    for (md_PricesMap::iterator it = prices.begin(); it != prices.end(); ++it)
    {
      md_Set & indexes = (it->second);
      for (md_Set::iterator it = indexes.begin(); it != indexes.end(); ++it)
      {
          CMPMetaDEx obj = *it;
          CMPSPInfo::Entry sp;

          bool filter = 1;

          //this filter, the first part is filtering by two currencies, the second part is filtering by the first only
          filter = (obj.getAddr() == address) && 
                   ( ( ( filter_by_one && (obj.getProperty() == propertyIdFilter) == 1 ) ) || !filter_by_one ) && 
                   (response.size() < number_trades);
          if ( filter ) {

            //clear obj before reuse
            metadex_obj.clear();
            
            _my_sps->getSP(obj.getProperty(), sp);
            bool c_own_div = sp.isDivisible();

            _my_sps->getSP(obj.getDesProperty(), sp);
            bool c_want_div = sp.isDivisible();

            string eco = (isTestEcosystemProperty(propertyIdFilter) == true) ? "Test" : "Main";
    
            // add data to obj
            add_mdex_fields( &metadex_obj , obj , c_own_div, c_want_div, eco);

            //add it to response
            response.push_back(metadex_obj);
          }
      }
    }
  }
  if( response.size() > 1 ) sort_mdex_obj( &response );
  return response;
}

Value getactivedexsells_MP(const Array& params, bool fHelp)
{
   if (fHelp)
        throw runtime_error(
            "getactivedexsells_MP\n"
            "\nGet currently active offers on the distributed exchange.\n"
            "\nResult:\n"
            "{\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("getactivedexsells_MP", "")
            + HelpExampleRpc("getactivedexsells_MP", "")
        );

      //if 0 params list all sells, otherwise first param is filter address
      bool addressFilter = false;
      string addressParam;

      if (params.size() > 0)
      {
          addressParam = params[0].get_str();
          addressFilter = true;
      }

      Array response;

      for(OfferMap::iterator it = my_offers.begin(); it != my_offers.end(); ++it)
      {
          CMPOffer selloffer = it->second;
          string sellCombo = it->first;
          string seller = sellCombo.substr(0, sellCombo.size()-2);

          //filtering
          if ((addressFilter) && (seller != addressParam)) continue;

          uint256 sellHash = selloffer.getHash();
          string txid = sellHash.GetHex();
          uint64_t propertyId = selloffer.getProperty();
          uint64_t minFee = selloffer.getMinFee();
          unsigned char timeLimit = selloffer.getBlockTimeLimit();
          uint64_t sellOfferAmount = selloffer.getOfferAmountOriginal(); //badly named - "Original" implies off the wire, but is amended amount
          uint64_t sellBitcoinDesired = selloffer.getBTCDesiredOriginal(); //badly named - "Original" implies off the wire, but is amended amount
          uint64_t amountAvailable = getMPbalance(seller, propertyId, SELLOFFER_RESERVE);
          uint64_t amountAccepted = getMPbalance(seller, propertyId, ACCEPT_RESERVE);

          //unit price & updated bitcoin desired calcs
          double unitPriceFloat = 0;
          if ((sellOfferAmount>0) && (sellBitcoinDesired > 0)) unitPriceFloat = (double)sellBitcoinDesired/(double)sellOfferAmount; //divide by zero protection
          uint64_t unitPrice = rounduint64(unitPriceFloat * COIN);
          uint64_t bitcoinDesired = rounduint64(amountAvailable*unitPriceFloat);

          Object responseObj;

          responseObj.push_back(Pair("txid", txid));
          responseObj.push_back(Pair("propertyid", propertyId));
          responseObj.push_back(Pair("seller", seller));
          responseObj.push_back(Pair("amountavailable", FormatDivisibleMP(amountAvailable)));
          responseObj.push_back(Pair("bitcoindesired", FormatDivisibleMP(bitcoinDesired)));
          responseObj.push_back(Pair("unitprice", FormatDivisibleMP(unitPrice)));
          responseObj.push_back(Pair("timelimit", timeLimit));
          responseObj.push_back(Pair("minimumfee", FormatDivisibleMP(minFee)));

          // display info about accepts related to sell
          responseObj.push_back(Pair("amountaccepted", FormatDivisibleMP(amountAccepted)));
          Array acceptsMatched;
          for(AcceptMap::iterator ait = my_accepts.begin(); ait != my_accepts.end(); ++ait)
          {
              Object matchedAccept;

              CMPAccept accept = ait->second;
              string acceptCombo = ait->first;
              uint256 matchedHash = accept.getHash();
              // does this accept match the sell?
              if (matchedHash == sellHash)
              {
                  //split acceptCombo out to get the buyer address
                  string buyer = acceptCombo.substr((acceptCombo.find("+")+1),(acceptCombo.size()-(acceptCombo.find("+")+1)));
                  uint64_t acceptBlock = accept.getAcceptBlock();
                  uint64_t acceptAmount = accept.getAcceptAmountRemaining();
                  matchedAccept.push_back(Pair("buyer", buyer));
                  matchedAccept.push_back(Pair("block", acceptBlock));
                  matchedAccept.push_back(Pair("amount", FormatDivisibleMP(acceptAmount)));
                  acceptsMatched.push_back(matchedAccept);
              }
          }
          responseObj.push_back(Pair("accepts", acceptsMatched));

          // add sell object into response array
          response.push_back(responseObj);
      }

return response;
}

Value listblocktransactions_MP(const Array& params, bool fHelp)
{
   if (fHelp || params.size() != 1)
        throw runtime_error(
            "listblocktransactions_MP index\n"
            "\nReturns all Master Protocol transactions in a block.\n"
            "\nArguments:\n"
            "1. index         (numeric, required) The block height or index\n"
            "\nResult:\n"
            "[                (array of string)\n"
            "  \"hash\"         (string) Transaction id\n"
            "  ,...\n"
            "]\n"

            "\nExamples\n"
            + HelpExampleCli("listblocktransactions_MP", "279007")
            + HelpExampleRpc("listblocktransactions_MP", "279007")
        );

  // firstly let's get the block height given in the param
  int blockHeight = params[0].get_int();
  if (blockHeight < 0 || blockHeight > GetHeight())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

  // next let's obtain the block for this height
  CBlockIndex* mpBlockIndex = chainActive[blockHeight];
  CBlock mpBlock;

  // now let's read this block in from disk so we can loop its transactions
  if(!ReadBlockFromDisk(mpBlock, mpBlockIndex))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to read block from disk");

  // create an array to hold our response
  Array response;

  // now we want to loop through each of the transactions in the block and run against CMPTxList::exists
  // those that return positive add to our response array
  BOOST_FOREACH(const CTransaction&tx, mpBlock.vtx)
  {
       bool mptx = p_txlistdb->exists(tx.GetHash());
       if (mptx)
       {
            // later we can add a verbose flag to decode here, but for now callers can send returned txids into gettransaction_MP
            // add the txid into the response as it's an MP transaction
            response.push_back(tx.GetHash().GetHex());
       }
  }
  return response;
}

// this function standardizes the RPC output for gettransaction_MP and listtransaction_MP into a central function
static int populateRPCTransactionObject(uint256 txid, Object *txobj, string filterAddress = "")
{
    //uint256 hash;
    //hash.SetHex(params[0].get_str());

    CTransaction wtx;
    uint256 blockHash = 0;
    if (!GetTransaction(txid, wtx, blockHash, true)) { return MP_TX_NOT_FOUND; }

    CMPTransaction mp_obj;
    uint256 wtxid = txid;
    bool bIsMine = false;
    bool isMPTx = false;
    uint64_t nFee = 0;
    string MPTxType;
    unsigned int MPTxTypeInt;
    string selectedAddress;
    string senderAddress;
    string refAddress;
    bool valid = false;
    bool showReference = false;
    uint64_t propertyId = 0;  //using 64 instead of 32 here as json::sprint chokes on 32 - research why
    bool divisible = false;
    uint64_t amount = 0;
    string result;
    uint64_t sell_minfee = 0;
    unsigned char sell_timelimit = 0;
    unsigned char sell_subaction = 0;
    uint64_t sell_btcdesired = 0;
    int rc=0;
    bool crowdPurchase = false;
    int64_t crowdPropertyId = 0;
    int64_t crowdTokens = 0;
    int64_t issuerTokens = 0;
    bool crowdDivisible = false;
    string crowdName;
    string propertyName;
    bool mdex_propertyId_Div = false;
    uint64_t mdex_propertyWanted = 0;
    uint64_t mdex_amountWanted = 0;
    bool mdex_propertyWanted_Div = false;
    unsigned int mdex_action = 0;
    
    if ((0 == blockHash) || (NULL == mapBlockIndex[blockHash])) { return MP_TX_UNCONFIRMED; }

    CBlockIndex* pBlockIndex = mapBlockIndex[blockHash];
    if (NULL == pBlockIndex) { return MP_BLOCK_NOT_IN_CHAIN; }

    int blockHeight = pBlockIndex->nHeight;
    int confirmations =  1 + GetHeight() - pBlockIndex->nHeight;
    int64_t blockTime = mapBlockIndex[blockHash]->nTime;

    mp_obj.SetNull();
    CMPOffer temp_offer;
    CMPMetaDEx temp_metadexoffer;

    // replace initial MP detection with levelDB lookup instead of parse, this is much faster especially in calls like list/search
    if (p_txlistdb->exists(wtxid))
    {
        //transaction is in levelDB, so we can attempt to parse it
        int parseRC = parseTransaction(true, wtx, blockHeight, 0, &mp_obj);
        if (0 <= parseRC) //negative RC means no MP content/badly encoded TX, we shouldn't see this if TX in levelDB but check for sanity
        {
            // do we have a non-zero RC, if so it's a payment, handle differently
            if (0 < parseRC)
            {
                // handle as payment TX - this doesn't fit nicely into the kind of output for a MP tx so we'll do this seperately
                // add generic TX data to the output
                //Object txobj;
                txobj->push_back(Pair("txid", wtxid.GetHex()));
                txobj->push_back(Pair("confirmations", confirmations));
                txobj->push_back(Pair("blocktime", blockTime));
                txobj->push_back(Pair("type", "DEx Purchase"));
                // always min one subrecord, grab sender from first sub so we can display in parent as per Adam feedback
                string tmpBuyer;
                string tmpSeller;
                uint64_t tmpVout;
                uint64_t tmpNValue;
                uint64_t tmpPropertyId;
                p_txlistdb->getPurchaseDetails(wtxid,1,&tmpBuyer,&tmpSeller,&tmpVout,&tmpPropertyId,&tmpNValue);
                txobj->push_back(Pair("sendingaddress", tmpBuyer));
                // get the details of sub records for payment(s) in the tx and push into an array
                Array purchases;
                int numberOfPurchases=p_txlistdb->getNumberOfPurchases(wtxid);
                if (0<numberOfPurchases)
                {
                    for(int purchaseNumber = 1; purchaseNumber <= numberOfPurchases; purchaseNumber++)
                    {
                        Object purchaseObj;
                        string buyer;
                        string seller;
                        uint64_t vout;
                        uint64_t nValue;
                        p_txlistdb->getPurchaseDetails(wtxid,purchaseNumber,&buyer,&seller,&vout,&propertyId,&nValue);
                        bIsMine = false;
                        bIsMine = IsMyAddress(buyer);
                        if (!bIsMine)
                        {
                            bIsMine = IsMyAddress(seller);
                        }
                        if (!filterAddress.empty()) if ((buyer != filterAddress) && (seller != filterAddress)) return -1; // return negative rc if filtering & no match
                        uint64_t amountPaid = wtx.vout[vout].nValue;
                        purchaseObj.push_back(Pair("vout", vout));
                        purchaseObj.push_back(Pair("amountpaid", FormatDivisibleMP(amountPaid)));
                        purchaseObj.push_back(Pair("ismine", bIsMine));
                        purchaseObj.push_back(Pair("referenceaddress", seller));
                        purchaseObj.push_back(Pair("propertyid", propertyId));
                        purchaseObj.push_back(Pair("amountbought", FormatDivisibleMP(nValue)));
                        purchaseObj.push_back(Pair("valid", true)); //only valid purchases are stored, anything else is regular BTC tx
                        purchases.push_back(purchaseObj);
                    }
                }
                txobj->push_back(Pair("purchases", purchases));
                // return the object
                return 0;
            }
            else
            {
                // otherwise RC was 0, a valid MP transaction so far
                if (0<=mp_obj.step1())
                {
                    MPTxType = mp_obj.getTypeString();
                    MPTxTypeInt = mp_obj.getType();
                    senderAddress = mp_obj.getSender();
                    refAddress = mp_obj.getReceiver();
                    if (!filterAddress.empty()) if ((senderAddress != filterAddress) && (refAddress != filterAddress)) return -1; // return negative rc if filtering & no match
                    isMPTx = true;
                    nFee = mp_obj.getFeePaid();
                    int tmpblock=0;
                    uint32_t tmptype=0;
                    uint64_t amountNew=0;
                    valid=getValidMPTX(wtxid, &tmpblock, &tmptype, &amountNew);
                    //populate based on type of tx
                    switch (MPTxTypeInt)
                    {
                        case MSC_TYPE_METADEX:
                             if (0 == mp_obj.step2_Value())
                             {
                                 propertyId = mp_obj.getProperty();
                                 amount = mp_obj.getAmount();
                                 mdex_propertyId_Div = isPropertyDivisible(propertyId);
                                 if (0 <= mp_obj.interpretPacket(NULL,&temp_metadexoffer))
                                 {
                                     mdex_propertyWanted = temp_metadexoffer.getDesProperty();
                                     mdex_propertyWanted_Div = isPropertyDivisible(mdex_propertyWanted);
                                     mdex_amountWanted = temp_metadexoffer.getAmountDesired();
                                     mdex_action = temp_metadexoffer.getAction();
                                 }
                             }

                        break;
                        case MSC_TYPE_GRANT_PROPERTY_TOKENS:
                             if (0 == mp_obj.step2_Value())
                             {
                                showReference = true;
                                propertyId = mp_obj.getProperty();
                                amount = mp_obj.getAmount();
                             }
                        break;
                        case MSC_TYPE_REVOKE_PROPERTY_TOKENS:
                             if (0 == mp_obj.step2_Value())
                             {
                                propertyId = mp_obj.getProperty();
                                amount = mp_obj.getAmount();
                             }
                        break;
                        case MSC_TYPE_CREATE_PROPERTY_FIXED:
                            mp_obj.step2_SmartProperty(rc);
                            if (0 == rc)
                            {
                                propertyId = _my_sps->findSPByTX(wtxid); // propertyId of created property (if valid)
                                amount = getTotalTokens(propertyId);
                                propertyName = mp_obj.getSPName();
                            }
                        break;
                        case MSC_TYPE_CREATE_PROPERTY_VARIABLE:
                            mp_obj.step2_SmartProperty(rc);
                            if (0 == rc)
                            {
                                propertyId = _my_sps->findSPByTX(wtxid); // propertyId of created property (if valid)
                                amount = 0; // crowdsale txs always create zero tokens
                                propertyName = mp_obj.getSPName();
                            }
                        break;
                        case MSC_TYPE_CREATE_PROPERTY_MANUAL:
                            //propertyId = _my_sps->findSPByTX(wtxid); // propertyId of created property (if valid)
                            //amount = 0; // issuance of a managed token does not create tokens
                            mp_obj.step2_SmartProperty(rc);
                            if (0 == rc)
                            {
                                propertyId = _my_sps->findSPByTX(wtxid); // propertyId of created property (if valid)
                                amount = 0; // crowdsale txs always create zero tokens
                                propertyName = mp_obj.getSPName();
                            }
                        break;
                        case MSC_TYPE_SIMPLE_SEND:
                            if (0 == mp_obj.step2_Value())
                            {
                                propertyId = mp_obj.getProperty();
                                amount = mp_obj.getAmount();
                                showReference = true;
                                //check crowdsale invest?
                                crowdPurchase = isCrowdsalePurchase(wtxid, refAddress, &crowdPropertyId, &crowdTokens, &issuerTokens);
                                if (crowdPurchase)
                                {
                                    MPTxType = "Crowdsale Purchase";
                                    CMPSPInfo::Entry sp;
                                    if (false == _my_sps->getSP(crowdPropertyId, sp)) { return MP_CROWDSALE_WITHOUT_PROPERTY; }

                                    crowdName = sp.name;
                                    crowdDivisible = sp.isDivisible();
                                }
                            }
                        break;
                        case MSC_TYPE_TRADE_OFFER:
                            if (0 == mp_obj.step2_Value())
                            {
                                propertyId = mp_obj.getProperty();
                                amount = mp_obj.getAmount();
                            }
                            if (0 <= mp_obj.interpretPacket(&temp_offer))
                            {
                                sell_minfee = temp_offer.getMinFee();
                                sell_timelimit = temp_offer.getBlockTimeLimit();
                                sell_subaction = temp_offer.getSubaction();
                                sell_btcdesired = temp_offer.getBTCDesiredOriginal();
                                if (3 < sell_subaction) sell_subaction=0; // case where subaction byte >3, to have been allowed must be a v0 sell, flip byte to 0
                                // for now must mark all v0 sells as new until we can store subaction for v0 sells
                                if ((0 == sell_subaction) && (0 < amount)) sell_subaction=1; // case where subaction byte=0, must be a v0 sell, infer action from amount
                                if ((0 == sell_subaction) && (0 == amount)) sell_subaction=3; // case where subaction byte=0, must be a v0 sell, infer action from amount
                            }
                            if ((valid) && (amountNew>0)) amount=amountNew; //amount has been amended, update
                        break;
                        case MSC_TYPE_ACCEPT_OFFER_BTC:
                            if (0 == mp_obj.step2_Value())
                            {
                                propertyId = mp_obj.getProperty();
                                amount = mp_obj.getAmount();
                                showReference = true;
                            }
                            if ((valid) && (amountNew>0)) amount=amountNew; //amount has been amended, update
                        break;
                        case MSC_TYPE_CLOSE_CROWDSALE:
                            if (0 == mp_obj.step2_Value())
                            {
                                propertyId = mp_obj.getProperty();
                            }
                        break;
                        case  MSC_TYPE_SEND_TO_OWNERS:
                            if (0 == mp_obj.step2_Value())
                            {
                                propertyId = mp_obj.getProperty();
                                amount = mp_obj.getAmount();
                            }
                        break;
                    } // end switch
                    divisible=isPropertyDivisible(propertyId);
                } // end step 1 if
                else
                {
                    return -3336; // "Not a Master Protocol transaction"
                }
            } // end payment check if
        } //negative RC check
        else
        {
            return MP_INVALID_TX_IN_DB_FOUND;
        }
    }
    else
    {
        return MP_TX_IS_NOT_MASTER_PROTOCOL;
    }

    if (isMPTx)
    {
        // test sender and reference against ismine to determine which address is ours
        // if both ours (eg sending to another address in wallet) use reference
        bIsMine = IsMyAddress(senderAddress);
        if (!bIsMine)
        {
            bIsMine = IsMyAddress(refAddress);
        }
        txobj->push_back(Pair("txid", wtxid.GetHex()));
        txobj->push_back(Pair("sendingaddress", senderAddress));
        if (showReference) txobj->push_back(Pair("referenceaddress", refAddress));
        txobj->push_back(Pair("ismine", bIsMine));
        txobj->push_back(Pair("confirmations", confirmations));
        txobj->push_back(Pair("fee", ValueFromAmount(nFee)));
        txobj->push_back(Pair("blocktime", blockTime));
        txobj->push_back(Pair("type", MPTxType));
        if (!MSC_TYPE_METADEX == MPTxTypeInt) txobj->push_back(Pair("propertyid", propertyId));
        if ((MSC_TYPE_CREATE_PROPERTY_VARIABLE == MPTxTypeInt) || (MSC_TYPE_CREATE_PROPERTY_FIXED == MPTxTypeInt) || (MSC_TYPE_CREATE_PROPERTY_MANUAL == MPTxTypeInt))
        {
            txobj->push_back(Pair("propertyname", propertyName));
        }
        if (!MSC_TYPE_METADEX == MPTxTypeInt) txobj->push_back(Pair("divisible", divisible));
        if (!MSC_TYPE_METADEX == MPTxTypeInt)
        {
            if (divisible)
            {
                txobj->push_back(Pair("amount", FormatDivisibleMP(amount))); //divisible, format w/ bitcoins VFA func
            }
            else
            {
                txobj->push_back(Pair("amount", FormatIndivisibleMP(amount))); //indivisible, push raw 64
            }
        }
        if (crowdPurchase)
        {
            txobj->push_back(Pair("purchasedpropertyid", crowdPropertyId));
            txobj->push_back(Pair("purchasedpropertyname", crowdName));
            txobj->push_back(Pair("purchasedpropertydivisible", crowdDivisible));
            if (crowdDivisible)
            {
                txobj->push_back(Pair("purchasedtokens", FormatDivisibleMP(crowdTokens))); //divisible, format w/ bitcoins VFA func
                txobj->push_back(Pair("issuertokens", FormatDivisibleMP(issuerTokens)));
            }
            else
            {
                txobj->push_back(Pair("purchasedtokens", FormatIndivisibleMP(crowdTokens))); //indivisible, push raw 64
                txobj->push_back(Pair("issuertokens", FormatIndivisibleMP(issuerTokens)));
            }
        }
        if (MSC_TYPE_TRADE_OFFER == MPTxTypeInt)
        {
            txobj->push_back(Pair("feerequired", ValueFromAmount(sell_minfee)));
            txobj->push_back(Pair("timelimit", sell_timelimit));
            if (1 == sell_subaction) txobj->push_back(Pair("subaction", "New"));
            if (2 == sell_subaction) txobj->push_back(Pair("subaction", "Update"));
            if (3 == sell_subaction) txobj->push_back(Pair("subaction", "Cancel"));
            txobj->push_back(Pair("bitcoindesired", ValueFromAmount(sell_btcdesired)));
        }
        if (MSC_TYPE_METADEX == MPTxTypeInt)
        {
            string amountOffered;
            string amountDesired;
            if (mdex_propertyId_Div) {amountOffered=FormatDivisibleMP(amount);} else {amountOffered=FormatIndivisibleMP(amount);}
            if (mdex_propertyWanted_Div) {amountDesired=FormatDivisibleMP(mdex_amountWanted);} else {amountDesired=FormatIndivisibleMP(mdex_amountWanted);}

            txobj->push_back(Pair("amountoffered", amountOffered));
            txobj->push_back(Pair("propertyoffered", propertyId));
            txobj->push_back(Pair("propertyofferedisdivisible", mdex_propertyId_Div));
            txobj->push_back(Pair("amountdesired", amountDesired));
            txobj->push_back(Pair("propertydesired", mdex_propertyWanted));
            txobj->push_back(Pair("propertydesiredisdivisible", mdex_propertyWanted_Div));
            txobj->push_back(Pair("action", (uint64_t) mdex_action));
            //txobj->push_back(Pair("unit_price", mdex_unitPrice ) );
            //txobj->push_back(Pair("inverse_unit_price", mdex_invUnitPrice ) );
            //active?
            //txobj->push_back(Pair("amount_original", FormatDivisibleMP(mdex_amt_orig_sale)));
            //txobj->push_back(Pair("amount_desired", FormatDivisibleMP(mdex_amt_des)));
        }
        txobj->push_back(Pair("valid", valid));
    }
    return 0;
}

Value gettransaction_MP(const Array& params, bool fHelp)
{
    // note this call has been refactored to use the singular populateRPCTransactionObject function

    if (fHelp || params.size() != 1)
        throw runtime_error(
            "gettransaction_MP \"txid\"\n"
            "\nGet detailed information about a Master Protocol transaction <txid>\n"
            "\nArguments:\n"
            "1. \"txid\"    (string, required) The transaction id\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"transactionid\",   (string) The transaction id\n"
            "  \"sendingaddress\" : \"sender\",    (string) The sending address\n"
            "  \"referenceaddress\" : \"receiving address\",    (string) The receiving address (if applicable)\n"
            "  \"ismine\" : true/false\",    (boolean) Whether the transaction involes an address in the wallet\n"
            "  \"confirmations\" : n,     (numeric) The number of confirmations\n"
            "  \"blocktime\" : ttt,       (numeric) The time in seconds since epoch (1 Jan 1970 GMT)\n"
            "  \"type\" : \"transaction type\",    (string) The type of transaction\n"
            "  \"propertyid\" : \"property id\",    (numeric) The ID of the property transacted\n"
            "  \"propertyname\" : \"property name\",    (string) The name of the property (for creation transactions)\n"
            "  \"divisible\" : true/false\",    (boolean) Whether the property is divisible\n"
            "  \"amount\" : \"x.xxx\",     (string) The transaction amount\n"
            "  \"valid\" : true/false\",    (boolean) Whether the transaction is valid\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("gettransaction_MP", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
            + HelpExampleRpc("gettransaction_MP", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
        );

    uint256 hash;
    hash.SetHex(params[0].get_str());
    Object txobj;
    // make a request to new RPC populator function to populate a transaction object
    int populateResult = populateRPCTransactionObject(hash, &txobj);
    // check the response, throw any error codes if false
    if (0>populateResult)
    {
        // TODO: consider throwing other error codes, check back with Bitcoin Core
        switch (populateResult)
        {
            case MP_TX_NOT_FOUND:
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");
                
            case MP_TX_UNCONFIRMED:
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unconfirmed transactions are not supported");
                
            case MP_BLOCK_NOT_IN_CHAIN:
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not part of the active chain");
                
            case MP_CROWDSALE_WITHOUT_PROPERTY:
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Potential database corruption: \
                                                      \"Crowdsale Purchase\" without valid property identifier");
                    
            case MP_INVALID_TX_IN_DB_FOUND:
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Potential database corruption: Invalid transaction found");
                
            case MP_TX_IS_NOT_MASTER_PROTOCOL:
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Not a Master Protocol transaction");
        }
    }
    // everything seems ok, return the object
    return txobj;
}

Value listtransactions_MP(const Array& params, bool fHelp)
{
    // note this call has been refactored to use the singular populateRPCTransactionObject function
    // note address filtering has been readded to this call, and searchtransactions_MP has been removed since performance hit no longer an issue

    if (fHelp || params.size() > 5)
        throw runtime_error(
            "listtransactions_MP ( \"address\" count skip startblock endblock )\n" //todo increase verbosity in help
            "\nList wallet transactions filtered on counts and block boundaries\n"
            + HelpExampleCli("listtransactions_MP", "")
            + HelpExampleRpc("listtransactions_MP", "")
        );

    CWallet *wallet = pwalletMain;
    string sAddress = "";
    string addressParam = "";
    bool addressFilter;

    //if 0 params consider all addresses in wallet, otherwise first param is filter address
    addressFilter = false;
    if (params.size() > 0)
    {
        // allow setting "" or "*" to use nCount and nFrom params with all addresses in wallet
        if ( ("*" != params[0].get_str()) && ("" != params[0].get_str()) )
        {
            addressParam = params[0].get_str();
            addressFilter = true;
        }
    }

    int64_t nCount = 10;
    if (params.size() > 1) nCount = params[1].get_int64();
    if (nCount < 0) throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
    int64_t nFrom = 0;
    if (params.size() > 2) nFrom = params[2].get_int64();
    if (nFrom < 0) throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative from");
    int64_t nStartBlock = 0;
    if (params.size() > 3) nStartBlock = params[3].get_int64();
    if (nStartBlock < 0) throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative start block");
    int64_t nEndBlock = 999999;
    if (params.size() > 4) nEndBlock = params[4].get_int64();
    if (nEndBlock < 0) throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative end block");

    Array response; //prep an array to hold our output

    // rewrite to use original listtransactions methodology from core
    LOCK(wallet->cs_wallet);
    std::list<CAccountingEntry> acentries;
    CWallet::TxItems txOrdered = pwalletMain->OrderedTxItems(acentries, "*");

    // iterate backwards until we have nCount items to return:
    for (CWallet::TxItems::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
    {
        CWalletTx *const pwtx = (*it).second.first;
        if (pwtx != 0)
        {
            // get the height of the transaction and check it's within the chosen parameters
            uint256 blockHash = pwtx->hashBlock;
            if ((0 == blockHash) || (NULL == mapBlockIndex[blockHash])) continue;
            CBlockIndex* pBlockIndex = mapBlockIndex[blockHash];
            if (NULL == pBlockIndex) continue;
            int blockHeight = pBlockIndex->nHeight;
            if ((blockHeight < nStartBlock) || (blockHeight > nEndBlock)) continue; // ignore it if not within our range

            // populateRPCTransactionObject will throw us a non-0 rc if this isn't a MP transaction, speeds up search by orders of magnitude
            uint256 hash = pwtx->GetHash();
            Object txobj;

            // make a request to new RPC populator function to populate a transaction object (if it is a MP transaction)
            int populateResult = -1;
            if (addressFilter)
            {
                populateResult = populateRPCTransactionObject(hash, &txobj, addressParam); // pass in an address filter
            }
            else
            {
                populateResult = populateRPCTransactionObject(hash, &txobj); // no address filter
            }
            if (0 == populateResult) response.push_back(txobj); // add the transaction object to the response array if we get a 0 rc

            // don't burn time doing more work than we need to
            if ((int)response.size() >= (nCount+nFrom)) break;
        }
    }
    // sort array here and cut on nFrom and nCount
    if (nFrom > (int)response.size())
        nFrom = response.size();
    if ((nFrom + nCount) > (int)response.size())
        nCount = response.size() - nFrom;
    Array::iterator first = response.begin();
    std::advance(first, nFrom);
    Array::iterator last = response.begin();
    std::advance(last, nFrom+nCount);

    if (last != response.end()) response.erase(last, response.end());
    if (first != response.begin()) response.erase(response.begin(), first);

    std::reverse(response.begin(), response.end()); // return oldest to newest?
    return response;   // return response array for JSON serialization
}

Value gettrade_MP(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "gettrade_MP \"txid\"\n"
            "\nGet detailed information and trade matches for a Master Protocol MetaDEx trade offer <txid>\n"
            "\nArguments:\n"
            "1. \"txid\"    (string, required) The transaction id\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"transactionid\",   (string) The transaction id\n"
            + HelpExampleCli("gettrade_MP", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
            + HelpExampleRpc("gettrade_MP", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
        );

    uint256 hash;
    hash.SetHex(params[0].get_str());
    Object tradeobj;
    Object txobj;

    //get sender & propId
    string senderAddress;
    unsigned int propertyId = 0;
    CTransaction wtx;
    uint256 blockHash = 0;
    if (!GetTransaction(hash, wtx, blockHash, true)) { return MP_TX_NOT_FOUND; }
    CMPTransaction mp_obj;
    int parseRC = parseTransaction(true, wtx, 0, 0, &mp_obj);
    if (0 <= parseRC) //negative RC means no MP content/badly encoded TX, we shouldn't see this if TX in levelDB but check for sa$
    {
        if (0<=mp_obj.step1())
        {
            senderAddress = mp_obj.getSender();
            if (0 == mp_obj.step2_Value())
            {
                propertyId = mp_obj.getProperty();
            }
        }
    }
    if (propertyId == 0) // something went wrong, couldn't decode property ID - bad packet?
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Not a Master Protocol transaction");
    if (senderAddress.empty()) // something went wrong, couldn't decode transaction
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Not a Master Protocol transaction");

    // make a request to new RPC populator function to populate a transaction object
    int populateResult = populateRPCTransactionObject(hash, &txobj);

    // check the response, throw any error codes if false
    if (0>populateResult)
    {
        // TODO: consider throwing other error codes, check back with Bitcoin Core
        switch (populateResult)
        {
            case MP_TX_NOT_FOUND:
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");
            case MP_TX_UNCONFIRMED:
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unconfirmed transactions are not supported");
            case MP_BLOCK_NOT_IN_CHAIN:
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not part of the active chain");
            case MP_CROWDSALE_WITHOUT_PROPERTY:
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Potential database corruption: \
                                                      \"Crowdsale Purchase\" without valid property identifier");
            case MP_INVALID_TX_IN_DB_FOUND:
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Potential database corruption: Invalid transaction found");
            case MP_TX_IS_NOT_MASTER_PROTOCOL:
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Not a Master Protocol transaction");
        }
    }

    // everything seems ok, now add status and get an array of matches to add to the object
    // status - is order cancelled/closed-filled/open/open-partialfilled?
    bool orderOpen = false;
    // is the sell offer still open - need more efficient way to do this
    for (md_PropertiesMap::iterator my_it = metadex.begin(); my_it != metadex.end(); ++my_it)
    {
        if (my_it->first == propertyId) //at bear minimum only go deeper if it's the right property id
        {
             md_PricesMap & prices = my_it->second;
             for (md_PricesMap::iterator it = prices.begin(); it != prices.end(); ++it)
             {
                  md_Set & indexes = (it->second);
                  for (md_Set::iterator it = indexes.begin(); it != indexes.end(); ++it)
                  {
                       CMPMetaDEx obj = *it;
                       if( obj.getHash().GetHex() == hash.GetHex() ) orderOpen = true;
                  }
             }
        }
    }
    txobj.push_back(Pair("active", orderOpen));

    // create array of matches
    Array tradeArray;
    uint64_t totalBought;
    t_tradelistdb->getMatchingTrades(hash, propertyId, &tradeArray, &totalBought);

    // add array to object
    txobj.push_back(Pair("matches", tradeArray));

    // return object
    return txobj;
}
