//
// first & so far only Master protocol source file
// WARNING: Work In Progress -- major refactoring will be occurring often
//
// I am adding comments to aid with navigation and overall understanding of the design.
// this is the 'core' portion of the node+wallet: mastercored
// see 'qt' subdirectory for UI files
//
// remaining work, search for: TODO, FIXME
//

//
// global TODO: need locks on the maps in this file & balances (moneys[],reserved[] & raccept[]) !!!
//

#include "base58.h"
#include "rpcserver.h"
#include "init.h"
#include "net.h"
#include "netbase.h"
#include "util.h"
#include "wallet.h"
#include "walletdb.h"
#include "coincontrol.h"

#include <stdint.h>
#include <string.h>
#include <map>
#include <queue>

#include <fstream>
#include <algorithm>

#include <vector>

#include <utility>
#include <string>

#include <boost/assign/list_of.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"

#include "leveldb/db.h"
#include "leveldb/write_batch.h"

#include <openssl/sha.h>

#include <boost/multiprecision/cpp_int.hpp>

// comment out MY_HACK & others here - used for Unit Testing only !
// #define MY_HACK
// #define DISABLE_LOG_FILE 

static FILE *mp_fp = NULL;

#include "mastercore.h"

using boost::multiprecision::int128_t;
using boost::multiprecision::cpp_int;
using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;
using namespace leveldb;

// part of 'breakout' feature
static const int nBlockTop = 0;
// static const int nBlockTop = 271000;

static int nWaterlineBlock = 0;  //

// uint64_t global_MSC_total = 0;
// uint64_t global_MSC_RESERVED_total = 0;

static string exodus = "1EXoDusjGwvnjZUyKkxZ4UHEf77z6A5S4P";
static const string exodus_testnet = "mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv";
static const string getmoney_testnet = "moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP";

static uint64_t exodus_prev = 0;
// static uint64_t exodus_prev = 0; // Bart has 0 for some reason ???
static uint64_t exodus_balance;

static boost::filesystem::path MPPersistencePath;

int msc_debug_parser_data = 0;
int msc_debug_parser= 0;
int msc_debug_verbose=1;
int msc_debug_verbose2=0;
int msc_debug_vin   = 0;
int msc_debug_script= 0;
int msc_debug_dex   = 1;
int msc_debug_send  = 1;
int msc_debug_spec  = 1;
int msc_debug_exo   = 0;
int msc_debug_tally = 1;
int msc_debug_sp    = 1;
int msc_debug_sto   = 1;
int msc_debug_txdb  = 0;
int msc_debug_persistence = 0;
int msc_debug_metadex= 1;

// follow this variable through the code to see how/which Master Protocol transactions get invalidated
static int InvalidCount_per_spec = 0; // consolidate error messages into a nice log, for now just keep a count

static int disable_Divs = 0;

static int disableLevelDB = 0;

static int mastercoreInitialized = 0;

// TODO: there would be a block height for each TX version -- rework together with BLOCKHEIGHTRESTRICTIONS above
static const int txRestrictionsRules[][3] = {
  {MSC_TYPE_SIMPLE_SEND,              GENESIS_BLOCK,      MP_TX_PKT_V0},
  {MSC_TYPE_TRADE_OFFER,              MSC_DEX_BLOCK,      MP_TX_PKT_V1},
  {MSC_TYPE_ACCEPT_OFFER_BTC,         MSC_DEX_BLOCK,      MP_TX_PKT_V0},
  {MSC_TYPE_CREATE_PROPERTY_FIXED,    MSC_SP_BLOCK,       MP_TX_PKT_V0},
  {MSC_TYPE_CREATE_PROPERTY_VARIABLE, MSC_SP_BLOCK,       MP_TX_PKT_V1},
  {MSC_TYPE_CLOSE_CROWDSALE,          MSC_SP_BLOCK,       MP_TX_PKT_V0},
  {MSC_TYPE_SEND_TO_OWNERS,           MSC_STO_BLOCK,      MP_TX_PKT_V0},
  {MSC_TYPE_METADEX,                  MSC_METADEX_BLOCK,  MP_TX_PKT_V0},
  {MSC_TYPE_OFFER_ACCEPT_A_BET,       MSC_BET_BLOCK,      MP_TX_PKT_V0},
  {MSC_TYPE_CREATE_PROPERTY_MANUAL,   MSC_MANUALSP_BLOCK,      MP_TX_PKT_V0},
  {MSC_TYPE_GRANT_PROPERTY_TOKENS,    MSC_MANUALSP_BLOCK,      MP_TX_PKT_V0},
  {MSC_TYPE_REVOKE_PROPERTY_TOKENS,   MSC_MANUALSP_BLOCK,      MP_TX_PKT_V0},

// end of array marker, in addition to sizeof/sizeof
  {-1,-1},
};

// this is the internal format for the offer primary key (TODO: replace by a class method)
#define STR_SELLOFFER_ADDR_CURR_COMBO(x) ( x + "-" + strprintf("%d", curr))
#define STR_ACCEPT_ADDR_CURR_ADDR_COMBO( _seller , _buyer ) ( _seller + "-" + strprintf("%d", curr) + "+" + _buyer)
#define STR_PAYMENT_SUBKEY_TXID_PAYMENT_COMBO(txidStr) ( txidStr + "-" + strprintf("%d", paymentNumber))

static CMPTxList *p_txlistdb;

// a copy from main.cpp -- unfortunately that one is in a private namespace
static int GetHeight()
{
  if (0 < nBlockTop) return nBlockTop;

  LOCK(cs_main);
  return chainActive.Height();
}

// indicate whether persistence is enabled at this point, or not
// used to write/read files, for breakout mode, debugging, etc.
static bool readPersistence()
{
#ifdef  MY_HACK
  return false;
#else
  return true;
#endif
}

// indicate whether persistence is enabled at this point, or not
// used to write/read files, for breakout mode, debugging, etc.
static bool writePersistence(int block_now)
{
  // if too far away from the top -- do not write
  if (GetHeight() > (block_now + MAX_STATE_HISTORY)) return false;

  return true;
}

string strMPCurrency(unsigned int i)
{
string str = "*unknown*";

  // test user-token
  if (0x80000000 & i)
  {
    str = strprintf("Test token: %d : 0x%08X", 0x7FFFFFFF & i, i);
  }
  else
  switch (i)
  {
    case MASTERCOIN_CURRENCY_BTC: str = "BTC"; break;
    case MASTERCOIN_CURRENCY_MSC: str = "MSC"; break;
    case MASTERCOIN_CURRENCY_TMSC: str = "TMSC"; break;
    default: str = strprintf("SP token: %d", i);
  }

  return str;
}

char *c_strMastercoinType(int i)
{
  switch (i)
  {
    case MSC_TYPE_SIMPLE_SEND: return ((char *)"Simple Send");
    case MSC_TYPE_RESTRICTED_SEND: return ((char *)"Restricted Send");
    case MSC_TYPE_SEND_TO_OWNERS: return ((char *)"Send To Owners");
    case MSC_TYPE_AUTOMATIC_DISPENSARY: return ((char *)"Automatic Dispensary");
    case MSC_TYPE_TRADE_OFFER: return ((char *)"DEx Sell Offer");
    case MSC_TYPE_METADEX: return ((char *)"MetaDEx: Offer/Accept one Master Protocol Coins for another");
    case MSC_TYPE_ACCEPT_OFFER_BTC: return ((char *)"DEx Accept Offer");
    case MSC_TYPE_CREATE_PROPERTY_FIXED: return ((char *)"Create Property - Fixed");
    case MSC_TYPE_CREATE_PROPERTY_VARIABLE: return ((char *)"Create Property - Variable");
    case MSC_TYPE_PROMOTE_PROPERTY: return ((char *)"Promote Property");
    case MSC_TYPE_CLOSE_CROWDSALE: return ((char *)"Close Crowdsale");
    case MSC_TYPE_CREATE_PROPERTY_MANUAL: return ((char *)"Create Property - Manual");
    case MSC_TYPE_GRANT_PROPERTY_TOKENS: return ((char *)"Grant Property Tokens");
    case MSC_TYPE_REVOKE_PROPERTY_TOKENS: return ((char *)"Revoke Property Tokens");

    default: return ((char *)"* unknown type *");
  }
}

char *c_strPropertyType(int i)
{
  switch (i)
  {
    case MSC_PROPERTY_TYPE_DIVISIBLE: return (char *) "divisible";
    case MSC_PROPERTY_TYPE_INDIVISIBLE: return (char *) "indivisible";
  }

  return (char *) "*** property type error ***";
}

// TODO: FIXME: only do swaps for little-endian system(s) !
void swapByteOrder16(unsigned short& us)
{
    us = (us >> 8) |
         (us << 8);
}

// TODO: FIXME: only do swaps for little-endian system(s) !
void swapByteOrder32(unsigned int& ui)
{
    ui = (ui >> 24) |
         ((ui<<8) & 0x00FF0000) |
         ((ui>>8) & 0x0000FF00) |
         (ui << 24);
}

// TODO: FIXME: only do swaps for little-endian system(s) !
void swapByteOrder64(uint64_t& ull)
{
    ull = (ull >> 56) |
          ((ull<<40) & 0x00FF000000000000) |
          ((ull<<24) & 0x0000FF0000000000) |
          ((ull<<8) & 0x000000FF00000000) |
          ((ull>>8) & 0x00000000FF000000) |
          ((ull>>24) & 0x0000000000FF0000) |
          ((ull>>40) & 0x000000000000FF00) |
          (ull << 56);
}

uint64_t rounduint64(double d)
{
  return (uint64_t)(abs(0.5 + d));
}

bool isNonMainNet()
{
  return (TestNet() || RegTest());
}

// mostly taken from Bitcoin's FormatMoney()
string FormatDivisibleMP(int64_t n, bool fSign)
{
// Note: not using straight sprintf here because we do NOT want
// localized number formatting.
int64_t n_abs = (n > 0 ? n : -n);
int64_t quotient = n_abs/COIN;
int64_t remainder = n_abs%COIN;
string str = strprintf("%d.%08d", quotient, remainder);

  if (!fSign) return str;

  if (n < 0)
      str.insert((unsigned int)0, 1, '-');
  else
      str.insert((unsigned int)0, 1, '+');
  return str;
}

int64_t strToInt64(std::string strAmount, bool divisible)
{
  int64_t Amount = 0;

  //check for a negative (minus sign) and invalidate if present
  size_t negSignPos = strAmount.find("-");
  if (negSignPos!=std::string::npos) return 0;

  //convert the string into a usable int64
  if (divisible)
  {
      //check for existance of decimal point
      size_t pos = strAmount.find(".");
      if (pos==std::string::npos)
      { //no decimal point but divisible so pad 8 zeros on right
          strAmount+="00000000";
      }
      else
      {
          size_t posSecond = strAmount.find(".", pos+1); //check for existence of second decimal point, if so invalidate amount
          if (posSecond!=std::string::npos) return 0;
          if ((strAmount.size()-pos)<8)
          { //there are decimals either exact or not enough, pad as needed
              string strRightOfDecimal = strAmount.substr(pos+1);
              unsigned int zerosToPad = 8-strRightOfDecimal.size();
              if (zerosToPad>0) //do we need to pad?
              {
                  for(unsigned int it = 0; it != zerosToPad; it++)
                  {
                      strAmount+="0";
                  }
              }
          }
          else
          { //there are too many decimals, truncate after 8
              strAmount = strAmount.substr(0,pos+9);
          }
      }
      strAmount.erase(std::remove(strAmount.begin(), strAmount.end(), '.'), strAmount.end());
      try { Amount = boost::lexical_cast<int64_t>(strAmount); } catch(const boost::bad_lexical_cast &e) { }
  }
  else
  {
      size_t pos = strAmount.find(".");
      string newStrAmount = strAmount.substr(0,pos);
      try { Amount = boost::lexical_cast<int64_t>(newStrAmount); } catch(const boost::bad_lexical_cast &e) { }
  }
return Amount;
}

string FormatIndivisibleMP(int64_t n)
{
  string str = strprintf("%lu", n);
  return str;
}

// a single outstanding offer -- from one seller of one currency, internally may have many accepts
class CMPOffer
{
private:
  int offerBlock;
  uint64_t offer_amount_original; // the amount of MSC for sale specified when the offer was placed
  unsigned int currency;
  uint64_t BTC_desired_original; // amount desired, in BTC
  uint64_t min_fee;
  unsigned char blocktimelimit;
  uint256 txid;
  unsigned char subaction;

public:
  uint256 getHash() const { return txid; }
  unsigned int getCurrency() const { return currency; }
  uint64_t getMinFee() const { return min_fee ; }
  unsigned char getBlockTimeLimit() { return blocktimelimit; }
  unsigned char getSubaction() { return subaction; }

  uint64_t getOfferAmountOriginal() { return offer_amount_original; }
  uint64_t getBTCDesiredOriginal() { return BTC_desired_original; }

  CMPOffer():offerBlock(0),offer_amount_original(0),currency(0),BTC_desired_original(0),min_fee(0),blocktimelimit(0),txid(0)
  {
  }

  CMPOffer(int b, uint64_t a, unsigned int cu, uint64_t d, uint64_t fee, unsigned char btl, const uint256 &tx)
   :offerBlock(b),offer_amount_original(a),currency(cu),BTC_desired_original(d),min_fee(fee),blocktimelimit(btl),txid(tx)
  {
    if (msc_debug_dex) fprintf(mp_fp, "%s(%lu): %s , line %d, file: %s\n", __FUNCTION__, a, txid.GetHex().c_str(), __LINE__, __FILE__);
  }

  void Set(uint64_t d, uint64_t fee, unsigned char btl, unsigned char suba)
  {
    BTC_desired_original = d;
    min_fee = fee;
    blocktimelimit = btl;
    subaction = suba;
  }

  void saveOffer(ofstream &file, SHA256_CTX *shaCtx, string const &addr ) const {
    // compose the outputline
    // seller-address, ...
    string lineOut = (boost::format("%s,%d,%d,%d,%d,%d,%d,%s")
      % addr
      % offerBlock
      % offer_amount_original
      % currency
      % BTC_desired_original
      % min_fee
      % (int)blocktimelimit
      % txid.ToString()).str();

    // add the line to the hash
    SHA256_Update(shaCtx, lineOut.c_str(), lineOut.length());

    // write the line
    file << lineOut << endl;
  }
};  // end of CMPOffer class

// do a map of buyers, primary key is buyer+currency
// MUST account for many possible accepts and EACH currency offer
class CMPAccept
{
private:
  uint64_t accept_amount_original;    // amount of MSC/TMSC desired to purchased
  uint64_t accept_amount_remaining;   // amount of MSC/TMSC remaining to purchased
// once accept is seen on the network the amount of MSC being purchased is taken out of seller's SellOffer-Reserve and put into this Buyer's Accept-Reserve
  unsigned char blocktimelimit;       // copied from the offer during creation
  unsigned int currency;              // copied from the offer during creation

  uint64_t offer_amount_original; // copied from the Offer during Accept's creation
  uint64_t BTC_desired_original;  // copied from the Offer during Accept's creation

  uint256 offer_txid; // the original offers TXIDs, needed to match Accept to the Offer during Accept's destruction, etc.

public:
  uint256 getHash() const { return offer_txid; }

  uint64_t getOfferAmountOriginal() { return offer_amount_original; }
  uint64_t getBTCDesiredOriginal() { return BTC_desired_original; }

  int block;          // 'accept' message sent in this block

  unsigned char getBlockTimeLimit() { return blocktimelimit; }
  unsigned int getCurrency() const { return currency; }

  int getAcceptBlock()  { return block; }

  CMPAccept(uint64_t a, int b, unsigned char blt, unsigned int c, uint64_t o, uint64_t btc, const uint256 &txid):accept_amount_remaining(a),blocktimelimit(blt),currency(c),
   offer_amount_original(o), BTC_desired_original(btc),offer_txid(txid),block(b)
  {
    accept_amount_original = accept_amount_remaining;
    fprintf(mp_fp, "%s(%lu), line %d, file: %s\n", __FUNCTION__, a, __LINE__, __FILE__);
  }

  CMPAccept(uint64_t origA, uint64_t remA, int b, unsigned char blt, unsigned int c, uint64_t o, uint64_t btc, const uint256 &txid):accept_amount_original(origA),accept_amount_remaining(remA),blocktimelimit(blt),currency(c),
   offer_amount_original(o), BTC_desired_original(btc),offer_txid(txid),block(b)
  {
    fprintf(mp_fp, "%s(%lu[%lu]), line %d, file: %s\n", __FUNCTION__, remA, origA, __LINE__, __FILE__);
  }

  void print()
  {
    fprintf(mp_fp, "buying: %12.8lf (originally= %12.8lf) in block# %d\n",
     (double)accept_amount_remaining/(double)COIN, (double)accept_amount_original/(double)COIN, block);
  }

  uint64_t getAcceptAmountRemaining() const
  { 
    fprintf(mp_fp, "%s(); buyer still wants = %lu, line %d, file: %s\n", __FUNCTION__, accept_amount_remaining, __LINE__, __FILE__);

    return accept_amount_remaining;
  }

  // reduce accept_amount_remaining and return "true" if the customer is fully satisfied (nothing desired to be purchased)
  bool reduceAcceptAmountRemaining_andIsZero(const uint64_t really_purchased)
  {
  bool bRet = false;

    if (really_purchased >= accept_amount_remaining) bRet = true;

    accept_amount_remaining -= really_purchased;

    return bRet;
  }

  void saveAccept(ofstream &file, SHA256_CTX *shaCtx, string const &addr, string const &buyer ) const {
    // compose the outputline
    // seller-address, currency, buyer-address, amount, fee, block
    string lineOut = (boost::format("%s,%d,%s,%d,%d,%d,%d,%d,%d,%s")
      % addr
      % currency
      % buyer
      % block
      % accept_amount_remaining
      % accept_amount_original
      % (int)blocktimelimit
      % offer_amount_original
      % BTC_desired_original
      % offer_txid.ToString()).str();

    // add the line to the hash
    SHA256_Update(shaCtx, lineOut.c_str(), lineOut.length());

    // write the line
    file << lineOut << endl;
  }

};  // end of CMPAccept class

// live crowdsales are these objects in a map
class CMPCrowd
{
private:
  unsigned int propertyId;

  uint64_t nValue;

  unsigned int currency_desired;
  uint64_t deadline;
  unsigned char early_bird;
  unsigned char percentage;

  uint64_t u_created;
  uint64_t i_created;

  uint256 txid;  // NOTE: not persisted as it doesnt seem used

  std::map<std::string, std::vector<uint64_t> > txFundraiserData;  // schema is 'txid:amtSent:deadlineUnix:userIssuedTokens:IssuerIssuedTokens;'
public:
  CMPCrowd():propertyId(0),nValue(0),currency_desired(0),deadline(0),early_bird(0),percentage(0),u_created(0),i_created(0)
  {
  }

  CMPCrowd(unsigned int pid, uint64_t nv, unsigned int cd, uint64_t dl, unsigned char eb, unsigned char per, uint64_t uct, uint64_t ict):
   propertyId(pid),nValue(nv),currency_desired(cd),deadline(dl),early_bird(eb),percentage(per),u_created(uct),i_created(ict)
  {
  }

  unsigned int getPropertyId() const { return propertyId; }

  uint64_t getDeadline() const { return deadline; }
  uint64_t getCurrDes() const { return currency_desired; }

  void incTokensUserCreated(uint64_t amount) { u_created += amount; }
  void incTokensIssuerCreated(uint64_t amount) { i_created += amount; }

  uint64_t getUserCreated() const { return u_created; }
  uint64_t getIssuerCreated() const { return i_created; }

  void insertDatabase(std::string txhash, std::vector<uint64_t> txdata ) { txFundraiserData.insert(std::make_pair<std::string, std::vector<uint64_t>& >(txhash,txdata)); }
  std::map<std::string, std::vector<uint64_t> > getDatabase() const { return txFundraiserData; }

  void print(const string & address, FILE *fp = stdout) const
  {
    fprintf(fp, "%34s : id=%u=%X; curr=%u, value= %lu, deadline: %s (%lX)\n", address.c_str(), propertyId, propertyId,
     currency_desired, nValue, DateTimeStrFormat("%Y-%m-%d %H:%M:%S", deadline).c_str(), deadline);
  }

  void saveCrowdSale(ofstream &file, SHA256_CTX *shaCtx, string const &addr) const
  {
    // compose the outputline
    // addr,propertyId,nValue,currency_desired,deadline,early_bird,percentage,created,mined
    string lineOut = (boost::format("%s,%d,%d,%d,%d,%d,%d,%d,%d")
      % addr
      % propertyId
      % nValue
      % currency_desired
      % deadline
      % (int)early_bird
      % (int)percentage
      % u_created
      % i_created ).str();

    // append N pairs of address=nValue;blockTime for the database
    std::map<std::string, std::vector<uint64_t> >::const_iterator iter;
    for (iter = txFundraiserData.begin(); iter != txFundraiserData.end(); ++iter) {
      lineOut.append((boost::format(",%s=") % (*iter).first).str());
      std::vector<uint64_t> const &vals = (*iter).second;

      std::vector<uint64_t>::const_iterator valIter;
      for (valIter = vals.begin(); valIter != vals.end(); ++valIter) {
        if (valIter != vals.begin()) {
          lineOut.append(";");
        }

        lineOut.append((boost::format("%d") % (*valIter)).str());
      }
    }

    // add the line to the hash
    SHA256_Update(shaCtx, lineOut.c_str(), lineOut.length());

    // write the line
    file << lineOut << endl;
  }
};  // end of CMPCrowd class


class CMPSPInfo
{
public:
  struct Entry {
    // common SP data
    string issuer;
    unsigned short prop_type;
    unsigned int prev_prop_id;
    string category;
    string subcategory;
    string name;
    string url;
    string data;
    uint64_t num_tokens;

    // Crowdsale generated SP
    unsigned int currency_desired;
    uint64_t deadline;
    unsigned char early_bird;
    unsigned char percentage;

    //We need this. Closedearly states if the SP was a crowdsale and closed due to MAXTOKENS or CLOSE command
    bool close_early;
    bool max_tokens;
    uint64_t timeclosed;
    uint256 txid_close;

    // other information
    uint256 txid;
    bool fixed;
    bool manual;

    // for crowdsale properties, schema is 'txid:amtSent:deadlineUnix:userIssuedTokens:IssuerIssuedTokens;'
    // for manual properties, schema is 'txid:grantAmount:revokeAmount;'
    std::map<std::string, std::vector<uint64_t> > historicalData;
    
    Entry()
    : issuer()
    , prop_type(0)
    , prev_prop_id(0)
    , category()
    , subcategory()
    , name()
    , url()
    , data()
    , num_tokens(0)
    , currency_desired(0)
    , deadline(0)
    , early_bird(0)
    , percentage(0)
    , close_early(0)
    , max_tokens(0)
    , timeclosed(0)
    , txid_close()
    , txid()
    , fixed(false)
    , manual(false)
    , historicalData()
    {
    }

    Object toJSON() const
    {
      Object spInfo;
      spInfo.push_back(Pair("issuer", issuer));
      spInfo.push_back(Pair("prop_type", prop_type));
      spInfo.push_back(Pair("prev_prop_id", (uint64_t)prev_prop_id));
      spInfo.push_back(Pair("category", category));
      spInfo.push_back(Pair("subcategory", subcategory));
      spInfo.push_back(Pair("name", name));
      spInfo.push_back(Pair("url", url));
      spInfo.push_back(Pair("data", data));
      spInfo.push_back(Pair("fixed", fixed));
      spInfo.push_back(Pair("num_tokens", (boost::format("%d") % num_tokens).str()));
      if (false == fixed && false == manual) {
        spInfo.push_back(Pair("currency_desired", (uint64_t)currency_desired));
        spInfo.push_back(Pair("deadline", (boost::format("%d") % deadline).str()));
        spInfo.push_back(Pair("early_bird", (int)early_bird));
        spInfo.push_back(Pair("percentage", (int)percentage));

        spInfo.push_back(Pair("close_early", (int)close_early));
        spInfo.push_back(Pair("max_tokens", (int)max_tokens));
        spInfo.push_back(Pair("timeclosed", (boost::format("%d") % timeclosed).str()));
        spInfo.push_back(Pair("txid_close", (boost::format("%s") % txid_close.ToString()).str()));
      }

      //Initialize values
      std::map<std::string, std::vector<uint64_t> >::const_iterator it;
      
      std::string values_long = "";
      std::string values = "";

      //fprintf(mp_fp,"\ncrowdsale started to save, size of db %ld", database.size());

      //Iterate through fundraiser data, serializing it with txid:val:val:val:val;
      bool crowdsale = !fixed && !manual;
      for(it = historicalData.begin(); it != historicalData.end(); it++) {
         values += it->first.c_str();
         if (crowdsale) {
           values += ":" + boost::lexical_cast<std::string>(it->second.at(0));
           values += ":" + boost::lexical_cast<std::string>(it->second.at(1));
           values += ":" + boost::lexical_cast<std::string>(it->second.at(2));
           values += ":" + boost::lexical_cast<std::string>(it->second.at(3));
         } else if (manual) {
           values += ":" + boost::lexical_cast<std::string>(it->second.at(0));
           values += ":" + boost::lexical_cast<std::string>(it->second.at(1));
         }
         values += ";";
         values_long += values;
      }
      //fprintf(mp_fp,"\ncrowdsale saved %s", values_long.c_str());

      spInfo.push_back(Pair("historicalData", values_long));
      spInfo.push_back(Pair("txid", (boost::format("%s") % txid.ToString()).str()));
      spInfo.push_back(Pair("manual", manual));


      return spInfo;
    }

    void fromJSON(Object const &json)
    {
      unsigned int idx = 0;
      issuer = json[idx++].value_.get_str();
      prop_type = (unsigned short)json[idx++].value_.get_int();
      prev_prop_id = (unsigned int)json[idx++].value_.get_uint64();
      category = json[idx++].value_.get_str();
      subcategory = json[idx++].value_.get_str();
      name = json[idx++].value_.get_str();
      url = json[idx++].value_.get_str();
      data = json[idx++].value_.get_str();
      fixed = json[idx++].value_.get_bool();
      if (fixed || json.size() < 16) {
        manual = json[12].value_.get_bool();
      } else {
        manual = false;
      }

      num_tokens = boost::lexical_cast<uint64_t>(json[idx++].value_.get_str());
      if (false == fixed && false == manual) {
        currency_desired = (unsigned int)json[idx++].value_.get_uint64();
        deadline = boost::lexical_cast<uint64_t>(json[idx++].value_.get_str());
        early_bird = (unsigned char)json[idx++].value_.get_int();
        percentage = (unsigned char)json[idx++].value_.get_int();

        close_early = (unsigned char)json[idx++].value_.get_int();
        max_tokens = (unsigned char)json[idx++].value_.get_int();
        timeclosed = boost::lexical_cast<uint64_t>(json[idx++].value_.get_str());
        txid_close = uint256(json[idx++].value_.get_str());
      }

      //reconstruct database
      std::string longstr = json[idx++].value_.get_str();
      
      //fprintf(mp_fp,"\nDESERIALIZE GO ----> %s" ,longstr.c_str() );
      
      std::vector<std::string> strngs_vec;
      
      //split serialized form up
      boost::split(strngs_vec, longstr, boost::is_any_of(";"));

      //fprintf(mp_fp,"\nDATABASE PRE-DESERIALIZE SUCCESS, %ld, %s" ,strngs_vec.size(), strngs_vec[0].c_str());
      
      //Go through and deserialize the database
      bool crowdsale = !fixed && !manual;
      for(std::vector<std::string>::size_type i = 0; i != strngs_vec.size(); i++) {
        if (strngs_vec[i].empty()) {
          continue;
        }
        
        std::vector<std::string> str_split_vec;
        boost::split(str_split_vec, strngs_vec[i], boost::is_any_of(":"));

        std::vector<uint64_t> txDataVec;

        if ( crowdsale && str_split_vec.size() == 5) {
          //fprintf(mp_fp,"\n Deserialized values: %s, %s %s %s %s", str_split_vec.at(0).c_str(), str_split_vec.at(1).c_str(), str_split_vec.at(2).c_str(), str_split_vec.at(3).c_str(), str_split_vec.at(4).c_str());
          txDataVec.push_back(boost::lexical_cast<uint64_t>( str_split_vec.at(1) ));
          txDataVec.push_back(boost::lexical_cast<uint64_t>( str_split_vec.at(2) ));
          txDataVec.push_back(boost::lexical_cast<uint64_t>( str_split_vec.at(3) ));
          txDataVec.push_back(boost::lexical_cast<uint64_t>( str_split_vec.at(4) ));
        } else if (manual && str_split_vec.size() == 3) {
          txDataVec.push_back(boost::lexical_cast<uint64_t>( str_split_vec.at(1) ));
          txDataVec.push_back(boost::lexical_cast<uint64_t>( str_split_vec.at(2) ));
        }

        historicalData.insert(std::make_pair( str_split_vec.at(0), txDataVec ) ) ;
      }
      //fprintf(mp_fp,"\nDATABASE DESERIALIZE SUCCESS %lu", database.size());
      txid = uint256(json[idx++].value_.get_str());
      idx++; // increment for manual that had to be parsed earlier for logic
    }

    bool isDivisible() const
    {
      switch (prop_type)
      {
        case MSC_PROPERTY_TYPE_DIVISIBLE:
        case MSC_PROPERTY_TYPE_DIVISIBLE_REPLACING:
        case MSC_PROPERTY_TYPE_DIVISIBLE_APPENDING:
          return true;
      }
      return false;
    }

    void print() const
    {
      printf("%s:%s(Fixed=%s,Divisible=%s):%lu:%s/%s, %s %s\n",
        issuer.c_str(),
        name.c_str(),
        fixed ? "Yes":"No",
        isDivisible() ? "Yes":"No",
        num_tokens,
        category.c_str(), subcategory.c_str(), url.c_str(), data.c_str());
    }

  };

private:
  leveldb::DB *pDb;

  // implied version of msc and tmsc so they don't hit the leveldb
  Entry implied_msc;
  Entry implied_tmsc;

  unsigned int next_spid;
  unsigned int next_test_spid;

public:

  CMPSPInfo(const boost::filesystem::path &path)
  {
    leveldb::Options options;
    options.paranoid_checks = true;
    options.create_if_missing = true;

    leveldb::Status s = leveldb::DB::Open(options, path.string(), &pDb);

    if (false == s.ok()) {
      printf("Failed to create or read LevelDB for Smart Property at %s", path.c_str());
    }

    // special cases for constant SPs MSC and TMSC
    implied_msc.issuer = exodus;
    implied_msc.prop_type = MSC_PROPERTY_TYPE_DIVISIBLE;
    implied_msc.num_tokens = 700000;
    implied_msc.category = "N/A";
    implied_msc.subcategory = "N/A";
    implied_msc.name = "MasterCoin";
    implied_msc.url = "www.mastercoin.org";
    implied_msc.data = "***data***";
    implied_tmsc.issuer = exodus;
    implied_tmsc.prop_type = MSC_PROPERTY_TYPE_DIVISIBLE;
    implied_tmsc.num_tokens = 700000;
    implied_tmsc.category = "N/A";
    implied_tmsc.subcategory = "N/A";
    implied_tmsc.name = "Test MasterCoin";
    implied_tmsc.url = "www.mastercoin.org";
    implied_tmsc.data = "***data***";

    init();
  }

  ~CMPSPInfo()
  {
    delete pDb;
    pDb = NULL;
  }

  void init(unsigned int nextSPID = 0x3UL, unsigned int nextTestSPID = TEST_ECO_PROPERTY_1)
  {
    next_spid = nextSPID;
    next_test_spid = nextTestSPID;
  }

  unsigned int peekNextSPID(unsigned char ecosystem)
  {
    unsigned int nextId = 0;

    switch(ecosystem) {
    case MASTERCOIN_CURRENCY_MSC: // mastercoin ecosystem, MSC: 1, TMSC: 2, First available SP = 3
      nextId = next_spid;
      break;
    case MASTERCOIN_CURRENCY_TMSC: // Test MSC ecosystem, same as above with high bit set
      nextId = next_test_spid;
      break;
    default: // non standard ecosystem, ID's start at 0
      nextId = 0;
    }

    return nextId;
  }

  unsigned int updateSP(unsigned int propertyID, Entry const &info)
  {
    std::string nextIdStr;
    unsigned int res = propertyID;

    Object spInfo = info.toJSON();

    // generate the SP id
    string spKey = (boost::format(FORMAT_BOOST_SPKEY) % propertyID).str();
    string spValue = write_string(Value(spInfo), false);
    string txIndexKey = (boost::format("index-tx-%s") % info.txid.ToString() ).str();
    string txValue = (boost::format("%d") % propertyID).str();

    fprintf(mp_fp,"\nUpdated LevelDB with SP data successfully\n");
    
    // atomically write both the the SP and the index to the database
    leveldb::WriteOptions writeOptions;
    writeOptions.sync = true;

    leveldb::WriteBatch commitBatch;
    commitBatch.Put(spKey, spValue);
    commitBatch.Put(txIndexKey, txValue);

    pDb->Write(writeOptions, &commitBatch);
    return res;
  }

  unsigned int putSP(unsigned char ecosystem, Entry const &info)
  {
    std::string nextIdStr;
    unsigned int res = 0;
    switch(ecosystem) {
    case MASTERCOIN_CURRENCY_MSC: // mastercoin ecosystem, MSC: 1, TMSC: 2, First available SP = 3
      res = next_spid++;
      break;
    case MASTERCOIN_CURRENCY_TMSC: // Test MSC ecosystem, same as above with high bit set
      res = next_test_spid++;
      break;
    default: // non standard ecosystem, ID's start at 0
      res = 0;
    }

    Object spInfo = info.toJSON();

    // generate the SP id
    string spKey = (boost::format(FORMAT_BOOST_SPKEY) % res).str();
    string spValue = write_string(Value(spInfo), false);
    string txIndexKey = (boost::format(FORMAT_BOOST_TXINDEXKEY) % info.txid.ToString() ).str();
    string txValue = (boost::format("%d") % res).str();

    // sanity checking
    string existingEntry;
    leveldb::ReadOptions readOpts;
    readOpts.fill_cache = true;
    if (false == pDb->Get(readOpts, spKey, &existingEntry).IsNotFound() && false == boost::equals(spValue, existingEntry)) {
      fprintf(mp_fp, "%s WRITING SP %d TO LEVELDB WHEN A DIFFERENT SP ALREADY EXISTS FOR THAT ID!!!\n", __FUNCTION__, res);
    } else if (false == pDb->Get(readOpts, txIndexKey, &existingEntry).IsNotFound() && false == boost::equals(txValue, existingEntry)) {
      fprintf(mp_fp, "%s WRITING INDEX TXID %s : SP %d IS OVERWRITING A DIFFERENT VALUE!!!\n", __FUNCTION__, info.txid.ToString().c_str(), res);
    }

    // atomically write both the the SP and the index to the database
    leveldb::WriteOptions writeOptions;
    writeOptions.sync = true;

    leveldb::WriteBatch commitBatch;
    commitBatch.Put(spKey, spValue);
    commitBatch.Put(txIndexKey, txValue);

    pDb->Write(writeOptions, &commitBatch);
    return res;
  }

  bool getSP(unsigned int spid, Entry &info)
  {
    // special cases for constant SPs MSC and TMSC
    if (MASTERCOIN_CURRENCY_MSC == spid) {
      info = implied_msc;
      return true;
    } else if (MASTERCOIN_CURRENCY_TMSC == spid) {
      info = implied_tmsc;
      return true;
    }

    leveldb::ReadOptions readOpts;
    readOpts.fill_cache = true;

    string spKey = (boost::format(FORMAT_BOOST_SPKEY) % spid).str();
    string spInfoStr;
    if (false == pDb->Get(readOpts, spKey, &spInfoStr).ok()) {
      return false;
    }

    // parse the encoded json, failing if it doesnt parse or is an object
    Value spInfoVal;
    if (false == read_string(spInfoStr, spInfoVal) || spInfoVal.type() != obj_type ) {
      return false;
    }

    // transfer to the Entry structure
    Object &spInfo = spInfoVal.get_obj();
    info.fromJSON(spInfo);
    return true;
  }

  bool hasSP(unsigned int spid)
  {
    // special cases for constant SPs MSC and TMSC
    if (MASTERCOIN_CURRENCY_MSC == spid || MASTERCOIN_CURRENCY_TMSC == spid) {
      return true;
    }

    leveldb::ReadOptions readOpts;
    readOpts.fill_cache = true;

    string spKey = (boost::format(FORMAT_BOOST_SPKEY) % spid).str();
    leveldb::Iterator *iter = pDb->NewIterator(readOpts);
    iter->Seek(spKey);
    if (iter->Valid() && iter->key().compare(spKey) == 0) {
      return true;
    }

    return false;
  }

  unsigned int findSPByTX(uint256 const &txid)
  {
    unsigned int res = 0;
    leveldb::ReadOptions readOpts;
    readOpts.fill_cache = true;

    string txIndexKey = (boost::format(FORMAT_BOOST_TXINDEXKEY) % txid.ToString() ).str();
    string spidStr;
    if (pDb->Get(readOpts, txIndexKey, &spidStr).ok()) {
      res = boost::lexical_cast<unsigned int>(spidStr);
    }

    return res;
  }

  void printAll()
  {
    // print off the hard coded MSC and TMSC entries
    for (unsigned int idx = MASTERCOIN_CURRENCY_MSC; idx <= MASTERCOIN_CURRENCY_TMSC; idx++ ) {
      Entry info;
      printf("%10d => ", idx);
      if (getSP(idx, info)) {
        info.print();
      } else {
        printf("<Internal Error on implicit SP>\n");
      }
    }

    leveldb::ReadOptions readOpts;
    readOpts.fill_cache = false;
    leveldb::Iterator *iter = pDb->NewIterator(readOpts);
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
      if (iter->key().starts_with("sp-")) {
        std::vector<std::string> vstr;
        std::string key = iter->key().ToString();
        boost::split(vstr, key, boost::is_any_of("-"), token_compress_on);

        printf("%10s => ", vstr[1].c_str());

        // parse the encoded json, failing if it doesnt parse or is an object
        Value spInfoVal;
        if (read_string(iter->value().ToString(), spInfoVal) && spInfoVal.type() == obj_type ) {
          Entry info;
          info.fromJSON(spInfoVal.get_obj());
          info.print();
        } else {
          printf("<Malformed JSON in DB>\n");
        }
      }
    }
  }
};

CCriticalSection cs_tally;

typedef std::map<string, CMPCrowd> CrowdMap;

static map<string, CMPOffer> my_offers;
static map<string, CMPAccept> my_accepts;
static CMPSPInfo *_my_sps;
CrowdMap my_crowds;
static MetaDExMap metadex;

CMPMetaDEx *getMetaDEx(const string &sender_addr, unsigned int curr)
{
  if (msc_debug_metadex) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);

const string combo = STR_SELLOFFER_ADDR_CURR_COMBO(sender_addr);
map<string, CMPMetaDEx>::iterator it = metadex.find(combo);

  if (it != metadex.end()) return &(it->second);

  return (CMPMetaDEx *) NULL;
}

static uint64_t getGoodFractionalPartPrecision(uint64_t n1, uint64_t n2)
{
  if (!n2) return 0;

  const uint64_t remainder = n1 % n2;
  const double frac = (double)remainder / (double)n2;

  return (GOOD_PRECISION * frac);
}

void CMPMetaDEx::Set0(int b, unsigned int c, uint64_t nValue, unsigned int cd, uint64_t ad, const uint256 &tx, unsigned int i)
{
  block = b;
  txid = tx;
  currency = c;
  amount_original = nValue;
  desired_currency = cd;
  desired_amount_original = ad;

  idx = i;
}

void CMPMetaDEx::Set(uint64_t nValue, uint64_t ad)
{
  if (ad && nValue) // div by zero protection once more
  {
    price_int   = ad / nValue;
    price_frac  = getGoodFractionalPartPrecision(ad, nValue);

    inverse_int = nValue / ad;
    inverse_frac= getGoodFractionalPartPrecision(nValue, ad);
  }
}

void CMPMetaDEx::Set(uint64_t pi, uint64_t pf, uint64_t ii, uint64_t i_f)
{
  price_int   = pi;
  price_frac  = pf;

  inverse_int = ii;
  inverse_frac= i_f;
}

CMPMetaDEx::CMPMetaDEx(int b, unsigned int c, uint64_t nValue, unsigned int cd, uint64_t ad, const uint256 &tx, unsigned int i)
{
  Set0(b,c,nValue,cd,ad,tx,i);
  Set(nValue,ad);
}

CMPMetaDEx::CMPMetaDEx(int b, unsigned int c, uint64_t nValue, unsigned int cd, uint64_t ad, const uint256 &tx, unsigned int i,
 uint64_t pi, uint64_t pf, uint64_t ii, uint64_t i_f)
{
  Set0(b,c,nValue,cd,ad,tx,i);
  Set(pi, pf, ii, i_f);
}

std::string CMPMetaDEx::ToString() const
{
  return strprintf("block=%d, idx=%u, trade prop %u %s for %u %s; unit_price = %lu.%010lu, inverse= %lu.%010lu", block, idx,
   currency, FormatDivisibleMP(amount_original), desired_currency, FormatDivisibleMP(desired_amount_original),
   price_int, price_frac, inverse_int, inverse_frac);
}

// this is the master list of all amounts for all addresses for all currencies, map is sorted by Bitcoin address
map<string, CMPTally> mp_tally_map;

CMPTally *getTally(const string & address)
{
  LOCK (cs_tally);

  map<string, CMPTally>::iterator it = mp_tally_map.find(address);

  if (it != mp_tally_map.end()) return &(it->second);

  return (CMPTally *) NULL;
}

CMPCrowd *getCrowd(const string & address)
{
CrowdMap::iterator my_it = my_crowds.find(address);

  if (my_it != my_crowds.end()) return &(my_it->second);

  return (CMPCrowd *) NULL;
}

// look at balance for an address
uint64_t getMPbalance(const string &Address, unsigned int currency, TallyType ttype)
{
uint64_t balance = 0;

  LOCK(cs_tally);

const map<string, CMPTally>::iterator my_it = mp_tally_map.find(Address);

  if (my_it != mp_tally_map.end())
  {
    balance = (my_it->second).getMoney(currency, ttype);
  }

  return balance;
}

// returns false if we are out of range and/or overflow
// call just before multiplying large numbers
bool isMultiplicationOK(const uint64_t a, const uint64_t b)
{
//  printf("%s(%lu, %lu): ", __FUNCTION__, a, b);

  if (!a || !b) return true;

  if (MAX_INT_8_BYTES < a) return false;
  if (MAX_INT_8_BYTES < b) return false;

  const uint64_t result = a*b;

  if (MAX_INT_8_BYTES < result) return false;

  if ((0 != a) && (result / a != b)) return false;

  return true;
}

bool isTestEcosystemProperty(unsigned int property)
{
  if ((MASTERCOIN_CURRENCY_TMSC == property) || (TEST_ECO_PROPERTY_1 <= property)) return true;

  return false;
}

bool isPropertyDivisible(unsigned int propertyId)
{
// TODO: is a lock here needed
CMPSPInfo::Entry sp;

  if (_my_sps->getSP(propertyId, sp)) return sp.isDivisible();

  return true;
}

bool isCrowdsaleActive(unsigned int propertyId)
{
  for(CrowdMap::const_iterator it = my_crowds.begin(); it != my_crowds.end(); ++it)
  {
      CMPCrowd crowd = it->second;
      unsigned int foundPropertyId = crowd.getPropertyId();
      if (foundPropertyId == propertyId) return true;
  }
  return false;
}

//go hunting for whether a simple send is a crowdsale purchase
//TODO !!!! horribly inefficient !!!! find a more efficient way to do this
bool isCrowdsalePurchase(uint256 txid, string address, int64_t *propertyId = NULL, int64_t *userTokens = NULL, int64_t *issuerTokens = NULL)
{
//1. loop crowdsales (active/non-active) looking for issuer address
//2. loop those crowdsales for that address and check their participant txs in database

  //check for an active crowdsale to this address
  CMPCrowd *crowd;
  crowd = getCrowd(address);
  if (crowd)
  {
      unsigned int foundPropertyId = crowd->getPropertyId();
      std::map<std::string, std::vector<uint64_t> > database = crowd->getDatabase();
      std::map<std::string, std::vector<uint64_t> >::const_iterator it;
      for(it = database.begin(); it != database.end(); it++)
      {
          string tmpTxid = it->first; //uint256 txid = it->first;
          string compTxid = txid.GetHex().c_str(); //convert to string to compare since this is how stored in levelDB
          if (tmpTxid == compTxid)
          {
              int64_t tmpUserTokens = it->second.at(2);
              int64_t tmpIssuerTokens = it->second.at(3);
              *propertyId = foundPropertyId;
              *userTokens = tmpUserTokens;
              *issuerTokens = tmpIssuerTokens;
              return true;
          }
      }
   }

   //if we still haven't found txid, check non active crowdsales to this address
   int64_t tmpPropertyId;
   unsigned int nextSPID = _my_sps->peekNextSPID(1);
   unsigned int nextTestSPID = _my_sps->peekNextSPID(2);

   for (tmpPropertyId = 1; tmpPropertyId<nextSPID; tmpPropertyId++)
   {
       CMPSPInfo::Entry sp;
       if (false != _my_sps->getSP(tmpPropertyId, sp))
       {
           if (sp.issuer == address)
           {
               std::map<std::string, std::vector<uint64_t> > database = sp.historicalData;
               std::map<std::string, std::vector<uint64_t> >::const_iterator it;
               for(it = database.begin(); it != database.end(); it++)
               {
                   string tmpTxid = it->first; //uint256 txid = it->first;
                   string compTxid = txid.GetHex().c_str(); //convert to string to compare since this is how stored in levelDB
                   if (tmpTxid == compTxid)
                   {
                       int64_t tmpUserTokens = it->second.at(2);
                       int64_t tmpIssuerTokens = it->second.at(3);
                       *propertyId = tmpPropertyId;
                       *userTokens = tmpUserTokens;
                       *issuerTokens = tmpIssuerTokens;
                       return true;
                   }
               }
           }
       }
   }
   for (tmpPropertyId = TEST_ECO_PROPERTY_1; tmpPropertyId<nextTestSPID; tmpPropertyId++)
   {
       CMPSPInfo::Entry sp;
       if (false != _my_sps->getSP(tmpPropertyId, sp))
       {
           if (sp.issuer == address)
           {
               std::map<std::string, std::vector<uint64_t> > database = sp.historicalData;
               std::map<std::string, std::vector<uint64_t> >::const_iterator it;
               for(it = database.begin(); it != database.end(); it++)
               {
                   string tmpTxid = it->first; //uint256 txid = it->first;
                   string compTxid = txid.GetHex().c_str(); //convert to string to compare since this is how stored in levelDB
                   if (tmpTxid == compTxid)
                   {
                       int64_t tmpUserTokens = it->second.at(2);
                       int64_t tmpIssuerTokens = it->second.at(3);
                       *propertyId = tmpPropertyId;
                       *userTokens = tmpUserTokens;
                       *issuerTokens = tmpIssuerTokens;
                       return true;
                   }
               }
           }
       }
   }

//didn't find anything, not a crowdsale purchase
return false;
}

// get total tokens for a property
// optionally counters the number of addresses who own that property: n_owners_total
int64_t getTotalTokens(unsigned int propertyId, int64_t *n_owners_total = NULL)
{
int64_t prev = 0, owners = 0;

  LOCK(cs_tally);

  CMPSPInfo::Entry property;
  if (false == _my_sps->getSP(propertyId, property)) return 0; // property ID does not exist

  int64_t totalTokens = 0;
  bool fixedIssuance = property.fixed;

  if (!fixedIssuance || n_owners_total)
  {
      for(map<string, CMPTally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it)
      {
          string address = (my_it->first).c_str();
          totalTokens += getMPbalance(address, propertyId, MONEY);
          totalTokens += getMPbalance(address, propertyId, SELLOFFER_RESERVE);
          if (propertyId<3) totalTokens += getMPbalance(address, propertyId, ACCEPT_RESERVE);

          if (prev != totalTokens)
          {
            prev = totalTokens;
            owners++;
          }
      }
  }

  if (fixedIssuance)
  {
      totalTokens = property.num_tokens; //only valid for TX50
  }

  if (n_owners_total) *n_owners_total = owners;

  return totalTokens;
}

// return true if everything is ok
bool update_tally_map(string who, unsigned int which_currency, int64_t amount, TallyType ttype)
{
bool bRet = false;
uint64_t before, after;

  if (0 == amount)
  {
    fprintf(mp_fp, "%s(%s, %u=0x%X, %+ld, ttype= %d) 0 FUNDS !\n", __FUNCTION__, who.c_str(), which_currency, which_currency, amount, ttype);
    return false;
  }

  LOCK(cs_tally);

  before = getMPbalance(who, which_currency, ttype);

  map<string, CMPTally>::iterator my_it = mp_tally_map.find(who);
  if (my_it == mp_tally_map.end())
  {
    // insert an empty element
    my_it = (mp_tally_map.insert(std::make_pair(who,CMPTally()))).first;
  }

  CMPTally &tally = my_it->second;

  bRet = tally.updateMoney(which_currency, amount, ttype);

  after = getMPbalance(who, which_currency, ttype);
  if (!bRet) fprintf(mp_fp, "%s(%s, %u=0x%X, %+ld, ttype= %d) INSUFFICIENT FUNDS\n", __FUNCTION__, who.c_str(), which_currency, which_currency, amount, ttype);

  if (msc_debug_tally)
  {
    if ((exodus != who) || (exodus == who && msc_debug_exo))
    {
      fprintf(mp_fp, "%s(%s, %u=0x%X, %+ld, ttype=%d); before=%lu, after=%lu\n",
       __FUNCTION__, who.c_str(), which_currency, which_currency, amount, ttype, before, after);
    }
  }

  return bRet;
}

// check to see if such a sell offer exists
bool DEx_offerExists(const string &seller_addr, unsigned int curr)
{
  if (msc_debug_dex) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
const string combo = STR_SELLOFFER_ADDR_CURR_COMBO(seller_addr);
map<string, CMPOffer>::iterator my_it = my_offers.find(combo);

  return !(my_it == my_offers.end());
}

// getOffer may replace DEx_offerExists() in the near future
// TODO: locks are needed around map's insert & erase
CMPOffer *DEx_getOffer(const string &seller_addr, unsigned int curr)
{
  if (msc_debug_dex) fprintf(mp_fp, "%s(%s, %u)\n", __FUNCTION__, seller_addr.c_str(), curr);
const string combo = STR_SELLOFFER_ADDR_CURR_COMBO(seller_addr);
map<string, CMPOffer>::iterator my_it = my_offers.find(combo);

  if (my_it != my_offers.end()) return &(my_it->second);

  return (CMPOffer *) NULL;
}

// TODO: locks are needed around map's insert & erase
CMPAccept *DEx_getAccept(const string &seller_addr, unsigned int curr, const string &buyer_addr)
{
  if (msc_debug_dex) fprintf(mp_fp, "%s(%s, %u, %s)\n", __FUNCTION__, seller_addr.c_str(), curr, buyer_addr.c_str());
const string combo = STR_ACCEPT_ADDR_CURR_ADDR_COMBO(seller_addr, buyer_addr);
map<string, CMPAccept>::iterator my_it = my_accepts.find(combo);

  if (my_it != my_accepts.end()) return &(my_it->second);

  return (CMPAccept *) NULL;
}

// returns 0 if everything is OK
int DEx_offerCreate(string seller_addr, unsigned int curr, uint64_t nValue, int block, uint64_t amount_desired, uint64_t fee, unsigned char btl, const uint256 &txid, uint64_t *nAmended = NULL) 
{
  if (msc_debug_dex) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
int rc = DEX_ERROR_SELLOFFER;

  // sanity check our params are OK
  if ((!btl) || (!amount_desired)) return (DEX_ERROR_SELLOFFER -101); // time limit or amount desired empty

  if (DEx_getOffer(seller_addr, curr)) return (DEX_ERROR_SELLOFFER -10);  // offer already exists

  const string combo = STR_SELLOFFER_ADDR_CURR_COMBO(seller_addr);

  if (msc_debug_dex)
   fprintf(mp_fp, "%s(%s|%s), nValue=%lu), line %d, file: %s\n", __FUNCTION__, seller_addr.c_str(), combo.c_str(), nValue, __LINE__, __FILE__);

  const uint64_t balanceReallyAvailable = getMPbalance(seller_addr, curr, MONEY);

  // if offering more than available -- put everything up on sale
  if (nValue > balanceReallyAvailable)
  {
  double BTC;

    // AND we must also re-adjust the BTC desired in this case...
    BTC = amount_desired * balanceReallyAvailable;
    BTC /= (double)nValue;
    amount_desired = rounduint64(BTC);

    nValue = balanceReallyAvailable;

    if (nAmended) *nAmended = nValue;
  }

  if (update_tally_map(seller_addr, curr, - nValue, MONEY)) // subtract from what's available
  {
    update_tally_map(seller_addr, curr, nValue, SELLOFFER_RESERVE); // put in reserve

    my_offers.insert(std::make_pair(combo, CMPOffer(block, nValue, curr, amount_desired, fee, btl, txid)));

    rc = 0;
  }

  return rc;
}

// returns 0 if everything is OK
int DEx_offerDestroy(const string &seller_addr, unsigned int curr)
{
  if (msc_debug_dex) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
const uint64_t amount = getMPbalance(seller_addr, curr, SELLOFFER_RESERVE);

  if (!DEx_offerExists(seller_addr, curr)) return (DEX_ERROR_SELLOFFER -11); // offer does not exist

  const string combo = STR_SELLOFFER_ADDR_CURR_COMBO(seller_addr);

  map<string, CMPOffer>::iterator my_it;

  my_it = my_offers.find(combo);

  if (amount)
  {
    update_tally_map(seller_addr, curr, amount, MONEY);   // give money back to the seller from SellOffer-Reserve
    update_tally_map(seller_addr, curr, - amount, SELLOFFER_RESERVE);
  }

  // delete the offer
  my_offers.erase(my_it);

  if (msc_debug_dex)
   fprintf(mp_fp, "%s(%s|%s), line %d, file: %s\n", __FUNCTION__, seller_addr.c_str(), combo.c_str(), __LINE__, __FILE__);

  return 0;
}

// returns 0 if everything is OK
int DEx_offerUpdate(const string &seller_addr, unsigned int curr, uint64_t nValue, int block, uint64_t desired, uint64_t fee, unsigned char btl, const uint256 &txid, uint64_t *nAmended = NULL) 
{
  if (msc_debug_dex) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
int rc = DEX_ERROR_SELLOFFER;

  fprintf(mp_fp, "%s(%s, %d), line %d, file: %s\n", __FUNCTION__, seller_addr.c_str(), curr, __LINE__, __FILE__);

  if (!DEx_offerExists(seller_addr, curr)) return (DEX_ERROR_SELLOFFER -12); // offer does not exist

  rc = DEx_offerDestroy(seller_addr, curr);

  if (!rc)
  {
    rc = DEx_offerCreate(seller_addr, curr, nValue, block, desired, fee, btl, txid, nAmended);
  }

  return rc;
}

// returns 0 if everything is OK
int DEx_acceptCreate(const string &buyer, const string &seller, int curr, uint64_t nValue, int block, uint64_t fee_paid, uint64_t *nAmended = NULL)
{
int rc = DEX_ERROR_ACCEPT - 10;

  if (msc_debug_dex) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);

map<string, CMPOffer>::iterator my_it;
const string selloffer_combo = STR_SELLOFFER_ADDR_CURR_COMBO(seller);
const string accept_combo = STR_ACCEPT_ADDR_CURR_ADDR_COMBO(seller, buyer);
uint64_t nActualAmount = getMPbalance(seller, curr, SELLOFFER_RESERVE);

  my_it = my_offers.find(selloffer_combo);

  if (my_it == my_offers.end()) return DEX_ERROR_ACCEPT -15;

  CMPOffer &offer = my_it->second;

  // here we ensure the correct BTC fee was paid in this acceptance message, per spec
  if (fee_paid < offer.getMinFee())
  {
    fprintf(mp_fp, "ERROR: fee too small -- the ACCEPT is rejected! (%lu is smaller than %lu)\n", fee_paid, offer.getMinFee());
    ++InvalidCount_per_spec;
    return DEX_ERROR_ACCEPT -105;
  }

  fprintf(mp_fp, "%s(%s) OFFER FOUND, line %d, file: %s\n", __FUNCTION__, selloffer_combo.c_str(), __LINE__, __FILE__);

  // the older accept is the valid one: do not accept any new ones!
  if (DEx_getAccept(seller, curr, buyer))
  {
    fprintf(mp_fp, "%s() ERROR: an accept from this same seller for this same offer is already open !!!!!\n", __FUNCTION__);
    return DEX_ERROR_ACCEPT -205;
  }

  if (nActualAmount > nValue)
  {
    nActualAmount = nValue;

    if (nAmended) *nAmended = nActualAmount;
  }

  // TODO: think if we want to save nValue -- as the amount coming off the wire into the object or not
  if (update_tally_map(seller, curr, - nActualAmount, SELLOFFER_RESERVE))
  {
    if (update_tally_map(seller, curr, nActualAmount, ACCEPT_RESERVE))
    {
      // insert into the map !
      my_accepts.insert(std::make_pair(accept_combo, CMPAccept(nActualAmount, block,
       offer.getBlockTimeLimit(), offer.getCurrency(), offer.getOfferAmountOriginal(), offer.getBTCDesiredOriginal(), offer.getHash() )));

      rc = 0;
    }
  }

  return rc;
}

// this function is called by handler_block() for each Accept that has expired
// this function is also called when the purchase has been completed (the buyer bought everything he was allocated)
//
// returns 0 if everything is OK
int DEx_acceptDestroy(const string &buyer, const string &seller, int curr, bool bForceErase = false)
{
  if (msc_debug_dex) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
int rc = DEX_ERROR_ACCEPT - 20;
CMPOffer *p_offer = DEx_getOffer(seller, curr);
CMPAccept *p_accept = DEx_getAccept(seller, curr, buyer);
bool bReturnToMoney; // return to MONEY of the seller, otherwise return to SELLOFFER_RESERVE
const string accept_combo = STR_ACCEPT_ADDR_CURR_ADDR_COMBO(seller, buyer);

  if (!p_accept) return rc; // sanity check

  const uint64_t nActualAmount = p_accept->getAcceptAmountRemaining();

/*
[1:33:22 AM] Michael: so if the offer is there the acceptDestroy should bring the ACCEPT_RESERVE back to SELLOFFER_RESERVE?
[1:33:33 AM] Michael: if the offer is gone it should go back to MONEY
[1:33:50 AM] Michael: if there was a NEW offer created, it should still go back to MONEY
[1:33:59 AM] zathrasc: i think so yeah
[1:34:42 AM] Michael: so the block # is our key
[1:35:25 AM] Michael: the block #of the offer should be given to accept to determine whether the offer is still there or not
[1:35:40 AM] zathrasc: perhaps txid?
[1:35:56 AM] Michael: sure
*/

  // if the offer is gone ACCEPT_RESERVE should go back to MONEY
  if (!p_offer)
  {
    bReturnToMoney = true;
  }
  else
  {
    fprintf(mp_fp, "%s() HASHES: offer=%s, accept=%s, line %d, file: %s\n", __FUNCTION__,
     p_offer->getHash().GetHex().c_str(), p_accept->getHash().GetHex().c_str(), __LINE__, __FILE__);

    // offer exists, determine whether it's the original offer or some random new one
    if (p_offer->getHash() == p_accept->getHash())
    {
      // same offer, return to SELLOFFER_RESERVE
      bReturnToMoney = false;
    }
    else
    {
      // old offer is gone !
      bReturnToMoney = true;
    }
  }

  if (bReturnToMoney)
  {
    if (update_tally_map(seller, curr, - nActualAmount, ACCEPT_RESERVE))
    {
      update_tally_map(seller, curr, nActualAmount, MONEY);
      rc = 0;
    }
  }
  else
  {
    // return to SELLOFFER_RESERVE
    if (update_tally_map(seller, curr, - nActualAmount, ACCEPT_RESERVE))
    {
      update_tally_map(seller, curr, nActualAmount, SELLOFFER_RESERVE);
      rc = 0;
    }
  }

  // can only erase when is NOT called from an iterator loop
  if (bForceErase)
  {
  const map<string, CMPAccept>::iterator my_it = my_accepts.find(accept_combo);

    if (my_accepts.end() !=my_it) my_accepts.erase(my_it);
  }

  return rc;
}

// incoming BTC payment for the offer
// TODO: verify proper partial payment handling
int DEx_payment(uint256 txid, unsigned int vout, string seller, string buyer, uint64_t BTC_paid, int blockNow, uint64_t *nAmended = NULL)
{
  if (msc_debug_dex) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
int rc = DEX_ERROR_PAYMENT;
CMPAccept *p_accept;
int curr;

curr = MASTERCOIN_CURRENCY_MSC; //test for MSC accept first
p_accept = DEx_getAccept(seller, curr, buyer);

  if (!p_accept) 
  {
    curr = MASTERCOIN_CURRENCY_TMSC; //test for TMSC accept second
    p_accept = DEx_getAccept(seller, curr, buyer); 
  }

  if (msc_debug_dex) fprintf(mp_fp, "%s(%s, %s), line %d, file: %s\n", __FUNCTION__, seller.c_str(), buyer.c_str(), __LINE__, __FILE__);

  if (!p_accept) return (DEX_ERROR_PAYMENT -1);  // there must be an active Accept for this payment

  const double BTC_desired_original = p_accept->getBTCDesiredOriginal();
  const double offer_amount_original = p_accept->getOfferAmountOriginal();

  if (0==(double)BTC_desired_original) return (DEX_ERROR_PAYMENT -2);  // divide by 0 protection

  double perc_X = (double)BTC_paid/BTC_desired_original;
  double Purchased = offer_amount_original * perc_X;

  uint64_t units_purchased = rounduint64(Purchased);

  const uint64_t nActualAmount = p_accept->getAcceptAmountRemaining();  // actual amount desired, in the Accept

  if (msc_debug_dex)
   fprintf(mp_fp, "BTC_desired= %30.20lf , offer_amount=%30.20lf , perc_X= %30.20lf , Purchased= %30.20lf , units_purchased= %lu\n",
   BTC_desired_original, offer_amount_original, perc_X, Purchased, units_purchased);

  // if units_purchased is greater than what's in the Accept, the buyer gets only what's in the Accept
  if (nActualAmount < units_purchased)
  {
    units_purchased = nActualAmount;

    if (nAmended) *nAmended = units_purchased;
  }

  if (update_tally_map(seller, curr, - units_purchased, ACCEPT_RESERVE))
  {
      update_tally_map(buyer, curr, units_purchased, MONEY);
      rc = 0;
      bool bValid = true;
      p_txlistdb->recordPaymentTX(txid, bValid, blockNow, vout, curr, units_purchased, buyer, seller);

      fprintf(mp_fp, "#######################################################\n");
  }

  // reduce the amount of units still desired by the buyer and if 0 must destroy the Accept
  if (p_accept->reduceAcceptAmountRemaining_andIsZero(units_purchased))
  {
  const uint64_t selloffer_reserve = getMPbalance(seller, curr, SELLOFFER_RESERVE);
  const uint64_t accept_reserve = getMPbalance(seller, curr, ACCEPT_RESERVE);

    DEx_acceptDestroy(buyer, seller, curr, true);

    // delete the Offer object if there is nothing in its Reserves -- everything got puchased and paid for
    if ((0 == selloffer_reserve) && (0 == accept_reserve))
    {
      DEx_offerDestroy(seller, curr);
    }
  }

  return rc;
}

unsigned int eraseExpiredAccepts(int blockNow)
{
unsigned int how_many_erased = 0;
map<string, CMPAccept>::iterator my_it = my_accepts.begin();

  while (my_accepts.end() != my_it)
  {
    // my_it->first = key
    // my_it->second = value

    CMPAccept &mpaccept = my_it->second;

    if ((blockNow - mpaccept.block) >= (int) mpaccept.getBlockTimeLimit())
    {
      fprintf(mp_fp, "%s() FOUND EXPIRED ACCEPT, erasing: blockNow=%d, offer block=%d, blocktimelimit= %d\n",
       __FUNCTION__, blockNow, mpaccept.block, mpaccept.getBlockTimeLimit());

      // extract the seller, buyer & currency from the Key
      std::vector<std::string> vstr;
      boost::split(vstr, my_it->first, boost::is_any_of("-+"), token_compress_on);
      string seller = vstr[0];
      int currency = atoi(vstr[1]);
      string buyer = vstr[2];

      DEx_acceptDestroy(buyer, seller, currency);

      my_accepts.erase(my_it++);

      ++how_many_erased;
    }
    else my_it++;

  }

  return how_many_erased;
}

int MetaDEx_Trade(const string &customer, unsigned int currency, unsigned int currency_desired, uint64_t amount_desired,
 uint64_t price_int, uint64_t price_frac)
{
  return 0;
}

// bSell = true when selling property for MSC/TMSC
int MetaDEx_Phase1(const string &addr, unsigned int property, bool bSell, const uint256 &txid, unsigned int idx)
{
  fprintf(mp_fp, "%s(%s, bSell=%s), line %d, file: %s\n", __FUNCTION__, addr.c_str(), bSell ? "YES":"NO", __LINE__, __FILE__);

  return 0;
}

int MetaDEx_Create(const string &sender_addr, unsigned int curr, uint64_t amount, int block, unsigned int currency_desired, uint64_t amount_desired, const uint256 &txid, unsigned int idx)
{
int rc = METADEX_ERROR -1;
// uint64_t price_int, price_frac, inverse_int, inverse_frac; // UNUSED WARNING
bool bPhase1Seller = true; // seller (property for MSC) or buyer (property for MSC); only applies to phase 1 code

  if (msc_debug_metadex) fprintf(mp_fp, "%s(%s, %u, %lu)\n", __FUNCTION__, sender_addr.c_str(), curr, amount);

  // MetaDEx implementation phase 1 check
  if ((curr != MASTERCOIN_CURRENCY_MSC) && (currency_desired != MASTERCOIN_CURRENCY_MSC) &&
   (curr != MASTERCOIN_CURRENCY_TMSC) && (currency_desired != MASTERCOIN_CURRENCY_TMSC))
  {
    return METADEX_ERROR -800;
  }

  if ((curr == MASTERCOIN_CURRENCY_MSC) || (curr == MASTERCOIN_CURRENCY_TMSC)) bPhase1Seller = false;

  const string combo = STR_SELLOFFER_ADDR_CURR_COMBO(sender_addr);

  (void) MetaDEx_Phase1(sender_addr, bPhase1Seller ? curr:currency_desired, bPhase1Seller, txid, idx);

  // TODO: add more code
  // ...

  if (update_tally_map(sender_addr, curr, - amount, MONEY)) // subtract from what's available
  {
    update_tally_map(sender_addr, curr, amount, SELLOFFER_RESERVE); // put in reserve

    metadex.insert(std::make_pair(combo, CMPMetaDEx(block, curr, amount, currency_desired, amount_desired, txid, idx)));
//    metadex.insert(std::make_pair(combo, CMPMetaDEx(block, curr, amount, currency_desired, amount_desired, txid, idx,
//     price_int, price_frac, inverse_int, inverse_frac)));

    rc = 0;
  }

  return rc;
}

// returns 0 if everything is OK
int MetaDEx_Destroy(const string &sender_addr, unsigned int curr)
{
  if (msc_debug_metadex) fprintf(mp_fp, "%s(%s, %u)\n", __FUNCTION__, sender_addr.c_str(), curr);

  if (!getMetaDEx(sender_addr, curr)) return (METADEX_ERROR -11); // does the trade exist?

  const string combo = STR_SELLOFFER_ADDR_CURR_COMBO(sender_addr);

  MetaDExMap::iterator my_it;

  my_it = metadex.find(combo);

  const uint64_t amount = getMPbalance(sender_addr, curr, SELLOFFER_RESERVE);

  if (amount)
  {
    update_tally_map(sender_addr, curr, amount, MONEY);   // give money back to the sender from SellOffer-Reserve
    update_tally_map(sender_addr, curr, - amount, SELLOFFER_RESERVE);
  }

  // delete the offer
  metadex.erase(my_it);

  if (msc_debug_metadex)
   fprintf(mp_fp, "%s(%s|%s), line %d, file: %s\n", __FUNCTION__, sender_addr.c_str(), combo.c_str(), __LINE__, __FILE__);

  return 0;
}

int MetaDEx_Update(const string &sender_addr, unsigned int curr, uint64_t nValue, int block, unsigned int currency_desired, uint64_t amount_desired, const uint256 &txid, unsigned int idx)
{
int rc = METADEX_ERROR -8;

  if (msc_debug_metadex) fprintf(mp_fp, "%s(%s, %u)\n", __FUNCTION__, sender_addr.c_str(), curr);

  // TODO: add the code
  // ...

  rc = MetaDEx_Destroy(sender_addr, curr);

  if (!rc)
  {
    rc = MetaDEx_Create(sender_addr, curr, nValue, block, currency_desired, amount_desired, txid, idx);
  }

  return rc;
}

// save info from the crowdsale that's being erased
void dumpCrowdsaleInfo(const string &address, CMPCrowd &crowd, bool bExpired = false)
{
  boost::filesystem::path pathInfo = GetDataDir() / INFO_FILENAME;
  FILE *fp = fopen(pathInfo.string().c_str(), "a");

  if (!fp)
  {
    fprintf(mp_fp, "\nPROBLEM writing %s, errno= %d\n", INFO_FILENAME, errno);
    return;
  }

  fprintf(fp, "\n%s\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()).c_str());
  fprintf(fp, "\nCrowdsale ended: %s\n", bExpired ? "Expired" : "Was closed");

  crowd.print(address, fp);

  fflush(fp);
  fclose(fp);
}

// calculates and returns fundraiser bonus, issuer premine, and total tokens
// propType : divisible/indiv
// bonusPerc: bonus percentage
// currentSecs: number of seconds of current tx
// numProps: number of properties
// issuerPerc: percentage of tokens to issuer
int calculateFractional(unsigned short int propType, unsigned char bonusPerc, uint64_t fundraiserSecs, 
  uint64_t numProps, unsigned char issuerPerc, const std::map<std::string, std::vector<uint64_t> > txFundraiserData, 
  const uint64_t amountPremined  )
{

  //initialize variables
  double totalCreated = 0;
  double issuerPercentage = (double) (issuerPerc * 0.01);

  std::map<std::string, std::vector<uint64_t> >::const_iterator it;

  //iterate through fundraiser data
  for(it = txFundraiserData.begin(); it != txFundraiserData.end(); it++) {

    // grab the seconds and amt transferred from this tx
    uint64_t currentSecs = it->second.at(1);
    double amtTransfer = it->second.at(0);

    //make calc for bonus given in sec
    uint64_t bonusSeconds = fundraiserSecs - currentSecs;
  
    //turn it into weeks
    double weeks = bonusSeconds / (double) 604800;
    
    //make it a %
    double ebPercentage = weeks * bonusPerc;
    double bonusPercentage = ( ebPercentage / 100 ) + 1;
  
    //init var
    double createdTokens;

    //if indiv or div, do different truncation
    if( MSC_PROPERTY_TYPE_DIVISIBLE == propType ) {
      //calculate tokens
      createdTokens = (amtTransfer/1e8) * (double) numProps * bonusPercentage ;
      
      //printf("prop 2: is %Lf, and %Lf \n", createdTokens, issuerTokens);
      
      //add totals up
      totalCreated += createdTokens;
    } else {
      //printf("amount xfer %Lf and props %f and bonus percs %Lf \n", amtTransfer, (double) numProps, bonusPercentage);
      
      //same here
      createdTokens = (uint64_t) ( (amtTransfer/1e8) * (double) numProps * bonusPercentage);
      
      totalCreated += createdTokens;
    }
  };

  // calculate premine
  double totalPremined = totalCreated * issuerPercentage;
  double missedTokens;

  // calculate based on div/indiv, truncation/not
  if( 2 == propType ) {
    missedTokens = totalPremined - amountPremined;
  } else {
    missedTokens = (uint64_t) (totalPremined - amountPremined);
  }

  //return value
  return missedTokens;
}

void eraseMaxedCrowdsale(const string &address, uint64_t blockTime)
{
    CrowdMap::iterator it = my_crowds.find(address);
    
    if (it != my_crowds.end()) {

      CMPCrowd &crowd = it->second;
      fprintf(mp_fp, "%s() FOUND MAXED OUT CROWDSALE from address= '%s', erasing...\n", __FUNCTION__, address.c_str());

      dumpCrowdsaleInfo(address, crowd);
      
      CMPSPInfo::Entry sp;
      
      //get sp from data struct
      _my_sps->getSP(crowd.getPropertyId(), sp);
      
      //get txdata
      sp.historicalData = crowd.getDatabase();
      sp.close_early = 1;
      sp.max_tokens = 1;
      sp.timeclosed = blockTime;
      
      //update SP with this data
      _my_sps->updateSP(crowd.getPropertyId() , sp);
      
      //No calculate fractional calls here, no more tokens (at MAX)
      
      my_crowds.erase(it);
    }
}
unsigned int eraseExpiredCrowdsale(const int64_t blockTime)
{
unsigned int how_many_erased = 0;
CrowdMap::iterator my_it = my_crowds.begin();

  while (my_crowds.end() != my_it)
  {
    // my_it->first = key
    // my_it->second = value

    CMPCrowd &crowd = my_it->second;

    if (blockTime > (int64_t)crowd.getDeadline())
    {
      fprintf(mp_fp, "%s() FOUND EXPIRED CROWDSALE from address= '%s', erasing...\n", __FUNCTION__, (my_it->first).c_str());

      // TODO: dump the info about this crowdsale being delete into a TXT file (JSON perhaps)
      dumpCrowdsaleInfo(my_it->first, my_it->second, true);
      
      // Begin calculate Fractional 
      CMPSPInfo::Entry sp;
      
      //get sp from data struct
      _my_sps->getSP(crowd.getPropertyId(), sp);

      //fprintf(mp_fp, "\nValues going into calculateFractional(): hexid %s earlyBird %d deadline %lu numProps %lu issuerPerc %d, issuerCreated %ld \n", sp.txid.GetHex().c_str(), sp.early_bird, sp.deadline, sp.num_tokens, sp.percentage, crowd.getIssuerCreated());

      //find missing tokens
      double missedTokens = calculateFractional(sp.prop_type,
                          sp.early_bird,
                          sp.deadline,
                          sp.num_tokens,
                          sp.percentage,
                          crowd.getDatabase(),
                          crowd.getIssuerCreated());


      //fprintf(mp_fp,"\nValues coming out of calculateFractional(): Total tokens, Tokens created, Tokens for issuer, amountMissed: issuer %s %lu %lu %lu %f\n",sp.issuer.c_str(), crowd.getUserCreated() + crowd.getIssuerCreated(), crowd.getUserCreated(), crowd.getIssuerCreated(), missedTokens);

      //get txdata
      sp.historicalData = crowd.getDatabase();

      //update SP with this data
      _my_sps->updateSP(crowd.getPropertyId() , sp);

      //update values
      update_tally_map(sp.issuer, crowd.getPropertyId(), missedTokens, MONEY);
      //End
                     
      my_crowds.erase(my_it++);

      ++how_many_erased;
    }
    else my_it++;

  }

  return how_many_erased;
}

std::string p128(int128_t quantity)
{
    //printf("\nTest # was %s\n", boost::lexical_cast<std::string>(quantity).c_str() );
   return boost::lexical_cast<std::string>(quantity);
}
std::string p_arb(cpp_int quantity)
{
    //printf("\nTest # was %s\n", boost::lexical_cast<std::string>(quantity).c_str() );
   return boost::lexical_cast<std::string>(quantity);
}
//calculateFundraiser does token calculations per transaction
//calcluateFractional does calculations for missed tokens
void calculateFundraiser(unsigned short int propType, uint64_t amtTransfer, unsigned char bonusPerc, 
  uint64_t fundraiserSecs, uint64_t currentSecs, uint64_t numProps, unsigned char issuerPerc, uint64_t totalTokens, 
  std::pair<uint64_t, uint64_t>& tokens, bool &close_crowdsale )
{
  //uint64_t weeks_sec = 604800;
  int128_t weeks_sec_ = 604800L;
  //define weeks in seconds
  int128_t precision_ = 1000000000000L;
  //define precision for all non-bitcoin values (bonus percentages, for example)
  int128_t percentage_precision = 100L;
  //define precision for all percentages (10/100 = 10%)

  //uint64_t bonusSeconds = fundraiserSecs - currentSecs;
  //calcluate the bonusseconds
  //printf("\n bonus sec %lu\n", bonusSeconds);
  int128_t bonusSeconds_ = fundraiserSecs - currentSecs;

  //double weeks_d = bonusSeconds / (double) weeks_sec;
  //debugging
  
  int128_t weeks_ = (bonusSeconds_ / weeks_sec_) * precision_ + ( (bonusSeconds_ % weeks_sec_ ) * precision_) / weeks_sec_;
  //calculate the whole number of weeks to apply bonus

  //printf("\n weeks_d: %.8lf \n weeks: %s + (%s / %s) =~ %.8lf \n", weeks_d, p128(bonusSeconds_ / weeks_sec_).c_str(), p128(bonusSeconds_ % weeks_sec_).c_str(), p128(weeks_sec_).c_str(), boost::lexical_cast<double>(bonusSeconds_ / weeks_sec_) + boost::lexical_cast<double> (bonusSeconds_ % weeks_sec_) / boost::lexical_cast<double>(weeks_sec_) );
  //debugging lines

  //double ebPercentage_d = weeks_d * bonusPerc;
  //debugging lines

  int128_t ebPercentage_ = weeks_ * bonusPerc;
  //calculate the earlybird percentage to be applied

  //printf("\n ebPercentage_d: %.8lf \n ebPercentage: %s + (%s / %s ) =~ %.8lf \n", ebPercentage_d, p128(ebPercentage_ / precision_).c_str(), p128( (ebPercentage_) % precision_).c_str() , p128(precision_).c_str(), boost::lexical_cast<double>(ebPercentage_ / precision_) + boost::lexical_cast<double>(ebPercentage_ % precision_) / boost::lexical_cast<double>(precision_));
  //debugging
  
  //double bonusPercentage_d = ( ebPercentage_d / 100 ) + 1;
  //debugging

  int128_t bonusPercentage_ = (ebPercentage_ + (precision_ * percentage_precision) ) / percentage_precision; 
  //calcluate the bonus percentage to apply up to 'percentage_precision' number of digits

  //printf("\n bonusPercentage_d: %.18lf \n bonusPercentage: %s + (%s / %s) =~ %.11lf \n", bonusPercentage_d, p128(bonusPercentage_ / precision_).c_str(), p128(bonusPercentage_ % precision_).c_str(), p128(precision_).c_str(), boost::lexical_cast<double>(bonusPercentage_ / precision_) + boost::lexical_cast<double>(bonusPercentage_ % precision_) / boost::lexical_cast<double>(precision_));
  //debugging

  //double issuerPercentage_d = (double) (issuerPerc * 0.01);
  //debugging

  int128_t issuerPercentage_ = (int128_t)issuerPerc * precision_ / percentage_precision;

  //printf("\n issuerPercentage_d: %.8lf \n issuerPercentage: %s + (%s / %s) =~ %.8lf \n", issuerPercentage_d, p128(issuerPercentage_ / precision_ ).c_str(), p128(issuerPercentage_ % precision_).c_str(), p128( precision_ ).c_str(), boost::lexical_cast<double>(issuerPercentage_ / precision_) + boost::lexical_cast<double>(issuerPercentage_ % precision_) / boost::lexical_cast<double>(precision_));
  //debugging

  int128_t satoshi_precision_ = 100000000;
  //define the precision for bitcoin amounts (satoshi)
  //uint64_t createdTokens, createdTokens_decimal;
  //declare used variables for total created tokens

  //uint64_t issuerTokens, issuerTokens_decimal;
  //declare used variables for total issuer tokens

  //printf("\n NUMBER OF PROPERTIES %ld", numProps); 
  //printf("\n AMOUNT INVESTED: %ld BONUS PERCENTAGE: %.11f and %s", amtTransfer,bonusPercentage_d, p128(bonusPercentage_).c_str());
  
  //long double ct = ((amtTransfer/1e8) * (long double) numProps * bonusPercentage_d);

  //int128_t createdTokens_ = (int128_t)amtTransfer*(int128_t)numProps* bonusPercentage_;

  cpp_int createdTokens = boost::lexical_cast<cpp_int>((int128_t)amtTransfer*(int128_t)numProps)* boost::lexical_cast<cpp_int>(bonusPercentage_);

  //printf("\n CREATED TOKENS UINT %s \n", p_arb(createdTokens).c_str());

  //printf("\n CREATED TOKENS %.8Lf, %s + (%s / %s) ~= %.8lf",ct, p128(createdTokens_ / (precision_ * satoshi_precision_) ).c_str(), p128(createdTokens_ % (precision_ * satoshi_precision_) ).c_str() , p128( precision_*satoshi_precision_ ).c_str(), boost::lexical_cast<double>(createdTokens_ / (precision_ * satoshi_precision_) ) + boost::lexical_cast<double>(createdTokens_ % (precision_ * satoshi_precision_)) / boost::lexical_cast<double>(precision_*satoshi_precision_));

  //long double it = (uint64_t) ct * issuerPercentage_d;

  //int128_t issuerTokens_ = (createdTokens_ / (satoshi_precision_ * precision_ )) * (issuerPercentage_ / 100) * precision_;
  
  cpp_int issuerTokens = (createdTokens / (satoshi_precision_ * precision_ )) * (issuerPercentage_ / 100) * precision_;

  //printf("\n ISSUER TOKENS: %.8Lf, %s + (%s / %s ) ~= %.8lf \n",it, p128(issuerTokens_ / (precision_ * satoshi_precision_ * 100 ) ).c_str(), p128( issuerTokens_ % (precision_ * satoshi_precision_ * 100 ) ).c_str(), p128(precision_*satoshi_precision_*100).c_str(), boost::lexical_cast<double>(issuerTokens_ / (precision_ * satoshi_precision_ * 100))  + boost::lexical_cast<double>(issuerTokens_ % (satoshi_precision_*precision_*100) )/ boost::lexical_cast<double>(satoshi_precision_*precision_*100)); 
  
  //printf("\n UINT %s \n", p_arb(issuerTokens).c_str());
  //total tokens including remainders

  //printf("\n DIVISIBLE TOKENS (UI LAYER) CREATED: is ~= %.8lf, and %.8lf\n",(double)createdTokens + (double)createdTokens_decimal/(satoshi_precision *precision), (double) issuerTokens + (double)issuerTokens_decimal/(satoshi_precision*precision*percentage_precision) );
  //if (2 == propType)
    //printf("\n DIVISIBLE TOKENS (UI LAYER) CREATED: is ~= %.8lf, and %.8lf\n", (uint64_t) (boost::lexical_cast<double>(createdTokens_ / (precision_ * satoshi_precision_) ) + boost::lexical_cast<double>(createdTokens_ % (precision_ * satoshi_precision_)) / boost::lexical_cast<double>(precision_*satoshi_precision_) )/1e8, (uint64_t) (boost::lexical_cast<double>(issuerTokens_ / (precision_ * satoshi_precision_ * 100))  + boost::lexical_cast<double>(issuerTokens_ % (satoshi_precision_*precision_*100) )/ boost::lexical_cast<double>(satoshi_precision_*precision_*100)) / 1e8  );
  //else
    //printf("\n INDIVISIBLE TOKENS (UI LAYER) CREATED: is = %lu, and %lu\n", boost::lexical_cast<uint64_t>(createdTokens_ / (precision_ * satoshi_precision_ ) ), boost::lexical_cast<uint64_t>(issuerTokens_ / (precision_ * satoshi_precision_ * 100)));
  
  cpp_int createdTokens_int = createdTokens / (precision_ * satoshi_precision_);
  cpp_int issuerTokens_int = issuerTokens / (precision_ * satoshi_precision_ * 100 );
  cpp_int newTotalCreated = totalTokens + createdTokens_int  + issuerTokens_int;

  if ( newTotalCreated > MAX_INT_8_BYTES) {
    cpp_int maxCreatable = MAX_INT_8_BYTES - totalTokens;

    cpp_int created = createdTokens_int + issuerTokens_int;
    cpp_int ratio = (created * precision_ * satoshi_precision_) / maxCreatable;

    //printf("\n created %s, ratio %s, maxCreatable %s, totalTokens %s, createdTokens_int %s, issuerTokens_int %s \n", p_arb(created).c_str(), p_arb(ratio).c_str(), p_arb(maxCreatable).c_str(), p_arb(totalTokens).c_str(), p_arb(createdTokens_int).c_str(), p_arb(issuerTokens_int).c_str() );
    //debugging
  
    issuerTokens_int = (issuerTokens_int * precision_ * satoshi_precision_)/ratio;
    //calcluate the ratio of tokens for what we can create and apply it
    createdTokens_int = MAX_INT_8_BYTES - issuerTokens_int ;
    //give the rest to the user

    //printf("\n created %s, ratio %s, maxCreatable %s, totalTokens %s, createdTokens_int %s, issuerTokens_int %s \n", p_arb(created).c_str(), p_arb(ratio).c_str(), p_arb(maxCreatable).c_str(), p_arb(totalTokens).c_str(), p_arb(createdTokens_int).c_str(), p_arb(issuerTokens_int).c_str() );
    //debugging
    close_crowdsale = true; //close up the crowdsale after assigning all tokens
  }
  tokens = std::make_pair(boost::lexical_cast<uint64_t>(createdTokens_int) , boost::lexical_cast<uint64_t>(issuerTokens_int));
  //give tokens
}

// certain transaction types are not live on the network until some specific block height
// certain transactions will be unknown to the client, i.e. "black holes" based on their version
// the Restrictions array is as such: type, block-allowed-in, top-version-allowed
bool isTransactionTypeAllowed(int txBlock, unsigned int txCurrency, unsigned int txType, unsigned short version)
{
bool bAllowed = false;
bool bBlackHole = false;
unsigned int type;
int block_FirstAllowed;
unsigned short version_TopAllowed;

  // BTC as currency/property is never allowed
  if (MASTERCOIN_CURRENCY_BTC == txCurrency) return false;

  // everything is always allowed on Bitcoin's TestNet or with TMSC/TestEcosystem on MainNet
  if ((isNonMainNet()) || isTestEcosystemProperty(txCurrency))
  {
    bAllowed = true;
  }

  for (unsigned int i = 0; i < sizeof(txRestrictionsRules)/sizeof(txRestrictionsRules[0]); i++)
  {
    type = txRestrictionsRules[i][0];
    block_FirstAllowed = txRestrictionsRules[i][1];
    version_TopAllowed = txRestrictionsRules[i][2];

    if (txType != type) continue;

    if (version_TopAllowed < version)
    {
      fprintf(mp_fp, "Black Hole identified !!! %d, %u, %u, %u\n", txBlock, txCurrency, txType, version);

      bBlackHole = true;

      // TODO: what else?
      // ...
    }

    if (0 > block_FirstAllowed) break;  // array contains a negative -- nothing's allowed or done parsing

    if (block_FirstAllowed <= txBlock) bAllowed = true;
  }

  return bAllowed && !bBlackHole;
}

// The class responsible for tx interpreting/parsing.
//
// It invokes other classes & methods: offers, accepts, tallies (balances).
class CMPTransaction
{
private:
  string sender;
  string receiver;
  uint256 txid;
  int block;
  unsigned int tx_idx;  // tx # within the block, 0-based
  int pkt_size;
  unsigned char pkt[1 + MAX_PACKETS * PACKET_SIZE];
  uint64_t nValue;
  int multi;  // Class A = 0, Class B = 1
  uint64_t tx_fee_paid;
  unsigned int type;
  unsigned int currency;
  unsigned short version; // = MP_TX_PKT_V0;
  uint64_t nNewValue;
  int64_t blockTime;  // internally nTime is still an "unsigned int"

// SP additions, perhaps a new class or a union is needed
  unsigned char ecosystem;
  unsigned short prop_type;
  unsigned int prev_prop_id;

  char category[SP_STRING_FIELD_LEN];
  char subcategory[SP_STRING_FIELD_LEN];
  char name[SP_STRING_FIELD_LEN];
  char url[SP_STRING_FIELD_LEN];
  char data[SP_STRING_FIELD_LEN];

  uint64_t deadline;
  unsigned char early_bird;
  unsigned char percentage;

  // METADEX additions
  unsigned int desired_currency;
  uint64_t desired_value;

  class SendToOwners_compare
  {
  public:

    bool operator()(pair<long, string> p1, pair<long, string> p2) const
    {
      if (p1.first == p2.first) return p1.second > p2.second; // reverse check
      else return p1.first < p2.first;
    }
  };

  enum ActionTypes { INVALID = 0, NEW = 1, UPDATE = 2, CANCEL = 3 };

public:
//  mutable CCriticalSection cs_msc;  // TODO: need to refactor first...

  unsigned int getType() const { return type; }
  const string getTypeString() const { return string(c_strMastercoinType(getType())); }
  unsigned int getCurrency() const { return currency; }
  unsigned short getVersion() const { return version; }
  unsigned short getPropertyType() const { return prop_type; }
  uint64_t getFeePaid() const { return tx_fee_paid; }

  const string & getSender() const { return sender; }
  const string & getReceiver() const { return receiver; }

  uint64_t getAmount() const { return nValue; }
  uint64_t getNewAmount() const { return nNewValue; }

  string getSPName() const { return string(name); }

  void SetNull()
  {
    currency = 0;
    type = 0;
    txid = 0;
    tx_idx = 0;  // tx # within the block, 0-based
    nValue = 0;
    nNewValue = 0;
    tx_fee_paid = 0;
    block = -1;
    pkt_size = 0;
    sender.erase();
    receiver.erase();

    blockTime = 0;

    ecosystem = 0;
    prop_type = 0;
    prev_prop_id = 0;

    memset(&pkt, 0, sizeof(pkt));

    memset(&category, 0, sizeof(category));
    memset(&subcategory, 0, sizeof(subcategory));
    memset(&name, 0, sizeof(name));
    memset(&url, 0, sizeof(url));
    memset(&data, 0, sizeof(data));
  }

  CMPTransaction()
  {
    SetNull();
  }

  int logicMath_SimpleSend()
  {
  int rc = PKT_ERROR_SEND -1000;

      if (!isTransactionTypeAllowed(block, currency, type, version)) return (PKT_ERROR_SEND -22);

      if (sender.empty()) ++InvalidCount_per_spec;
      // special case: if can't find the receiver -- assume sending to itself !
      // may also be true for BTC payments........
      // TODO: think about this..........
      if (receiver.empty())
      {
        receiver = sender;
      }
      if (receiver.empty()) ++InvalidCount_per_spec;

      // insufficient funds check & return
      if (!update_tally_map(sender, currency, - nValue, MONEY))
      {
        return (PKT_ERROR -111);
      }

      update_tally_map(receiver, currency, nValue, MONEY);

      // is there a crowdsale running from this recepient ?
      {
      CMPCrowd *crowd;

        crowd = getCrowd(receiver);

        if (crowd && (crowd->getCurrDes() == currency) )
        {
          CMPSPInfo::Entry sp;
          bool spFound = _my_sps->getSP(crowd->getPropertyId(), sp);

          fprintf(mp_fp, "INVESTMENT SEND to Crowdsale Issuer: %s\n", receiver.c_str());
          
          if (spFound)
          {
            //init this struct
            std::pair <uint64_t,uint64_t> tokens;
            //pass this in by reference to determine if max_tokens has been reached
            bool close_crowdsale = false; 
            //get txid
            string sp_txid =  sp.txid.GetHex().c_str();

            //Units going into the calculateFundraiser function must
            //match the unit of the fundraiser's property_type.
            //By default this means Satoshis in and satoshis out.
            //In the condition that your fundraiser is Divisible,
            //but you are accepting indivisible tokens, you must
            //account for 1.0 Div != 1 Indiv but actually 1.0 Div == 100000000 Indiv.
            //The unit must be shifted or your values will be incorrect,
            //that is what we check for below.
            if ( !(isPropertyDivisible(currency)) ) {
              nValue = nValue * 1e8;
            }

            //fprintf(mp_fp, "\nValues going into calculateFundraiser(): hexid %s nValue %lu earlyBird %d deadline %lu blockTime %ld numProps %lu issuerPerc %d \n", txid.GetHex().c_str(), nValue, sp.early_bird, sp.deadline, (uint64_t) blockTime, sp.num_tokens, sp.percentage);

            // calc tokens per this fundraise
            calculateFundraiser(sp.prop_type,         //u short
                                nValue,               // u int 64
                                sp.early_bird,        // u char
                                sp.deadline,          // u int 64
                                (uint64_t) blockTime, // int 64
                                sp.num_tokens,      // u int 64
                                sp.percentage,        // u char
                                getTotalTokens(crowd->getPropertyId()),
                                tokens,
                                close_crowdsale);

            //fprintf(mp_fp,"\n before incrementing global tokens user: %ld issuer: %ld\n", crowd->getUserCreated(), crowd->getIssuerCreated());
            
            //getIssuerCreated() is passed into calcluateFractional() at close
            //getUserCreated() is a convenient way to get user created during a crowdsale
            crowd->incTokensUserCreated(tokens.first);
            crowd->incTokensIssuerCreated(tokens.second);
            
            //fprintf(mp_fp,"\n after incrementing global tokens user: %ld issuer: %ld\n", crowd->getUserCreated(), crowd->getIssuerCreated());
            
            //init data to pass to txFundraiserData
            uint64_t txdata[] = { (uint64_t) nValue, (uint64_t) blockTime, (uint64_t) tokens.first, (uint64_t) tokens.second };
            
            std::vector<uint64_t> txDataVec(txdata, txdata + sizeof(txdata)/sizeof(txdata[0]) );

            //insert data
            crowd->insertDatabase(txid.GetHex().c_str(), txDataVec  );

            //fprintf(mp_fp,"\nValues coming out of calculateFundraiser(): hex %s: Tokens created, Tokens for issuer: %ld %ld\n",txid.GetHex().c_str(), tokens.first, tokens.second);

            //update sender/rec
            update_tally_map(sender, crowd->getPropertyId(), tokens.first, MONEY);
            update_tally_map(receiver, crowd->getPropertyId(), tokens.second, MONEY);

            // close crowdsale if we hit MAX_TOKENS
            if( close_crowdsale ) {
              eraseMaxedCrowdsale(receiver, blockTime);
            }
          }
        }
      }

      rc = 0;

    return rc;
  }

  int logicMath_TradeOffer(CMPOffer *obj_o)
  {
  int rc = PKT_ERROR_TRADEOFFER;
  uint64_t amount_desired, min_fee;
  unsigned char blocktimelimit, subaction = 0;
  static const char * const subaction_name[] = { "empty", "new", "update", "cancel" };

      if ((MASTERCOIN_CURRENCY_TMSC != currency) && (MASTERCOIN_CURRENCY_MSC != currency))
      {
        fprintf(mp_fp, "No smart properties allowed on the DeX...\n");
        return PKT_ERROR_TRADEOFFER -72;
      }

      // block height checks, for instance DEX is only available on MSC starting with block 290630
      if (!isTransactionTypeAllowed(block, currency, type, version)) return -88888;

      memcpy(&amount_desired, &pkt[16], 8);
      memcpy(&blocktimelimit, &pkt[24], 1);
      memcpy(&min_fee, &pkt[25], 8);
      memcpy(&subaction, &pkt[33], 1);

      swapByteOrder64(amount_desired);
      swapByteOrder64(min_fee);

    fprintf(mp_fp, "\t  amount desired: %lu.%08lu\n", amount_desired / COIN, amount_desired % COIN);
    fprintf(mp_fp, "\tblock time limit: %u\n", blocktimelimit);
    fprintf(mp_fp, "\t         min fee: %lu.%08lu\n", min_fee / COIN, min_fee % COIN);
    fprintf(mp_fp, "\t      sub-action: %u (%s)\n", subaction, subaction < sizeof(subaction_name)/sizeof(subaction_name[0]) ? subaction_name[subaction] : "");

      if (obj_o)
      {
        obj_o->Set(amount_desired, min_fee, blocktimelimit, subaction);
        return PKT_RETURN_OFFER;
      }

      // figure out which Action this is based on amount for sale, version & etc.
      switch (version)
      {
        case MP_TX_PKT_V0:
          if (0 != nValue)
          {
            if (!DEx_offerExists(sender, currency))
            {
              rc = DEx_offerCreate(sender, currency, nValue, block, amount_desired, min_fee, blocktimelimit, txid, &nNewValue);
            }
            else
            {
              rc = DEx_offerUpdate(sender, currency, nValue, block, amount_desired, min_fee, blocktimelimit, txid, &nNewValue);
            }
          }
          else
          // what happens if nValue is 0 for V0 ?  ANSWER: check if exists and it does -- cancel, otherwise invalid
          {
            if (DEx_offerExists(sender, currency))
            {
              rc = DEx_offerDestroy(sender, currency);
            }
          }

          break;

        case MP_TX_PKT_V1:
        {
          if (DEx_offerExists(sender, currency))
          {
            if ((CANCEL != subaction) && (UPDATE != subaction))
            {
              fprintf(mp_fp, "%s() INVALID SELL OFFER -- ONE ALREADY EXISTS, line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
              ++InvalidCount_per_spec;
              rc = PKT_ERROR_TRADEOFFER -11;
              break;
            }
          }
          else
          {
            // Offer does not exist
            if ((NEW != subaction))
            {
              fprintf(mp_fp, "%s() INVALID SELL OFFER -- UPDATE OR CANCEL ACTION WHEN NONE IS POSSIBLE\n", __FUNCTION__);
              ++InvalidCount_per_spec;
              rc = PKT_ERROR_TRADEOFFER -12;
              break;
            }
          }
 
          switch (subaction)
          {
            case NEW:
              rc = DEx_offerCreate(sender, currency, nValue, block, amount_desired, min_fee, blocktimelimit, txid, &nNewValue);
              break;

            case UPDATE:
              rc = DEx_offerUpdate(sender, currency, nValue, block, amount_desired, min_fee, blocktimelimit, txid, &nNewValue);
              break;

            case CANCEL:
              rc = DEx_offerDestroy(sender, currency);
              break;

            default:
              ++InvalidCount_per_spec;
              rc = (PKT_ERROR -999);
              break;
          }

          break;
        }

        default:
          rc = (PKT_ERROR -500);  // neither V0 nor V1
          break;
      };

    return rc;
  }

  int logicMath_AcceptOffer_BTC()
  {
  int rc = DEX_ERROR_ACCEPT;

    // the min fee spec requirement is checked in the following function
    rc = DEx_acceptCreate(sender, receiver, currency, nValue, block, tx_fee_paid, &nNewValue);

    return rc;
  }

  int logicMath_SendToOwners(FILE *fp = NULL)
  {
  int rc = PKT_ERROR_STO -1000;

      if (!isTransactionTypeAllowed(block, currency, type, version)) return (PKT_ERROR_STO -888);

      // totalTokens will be 0 for non-existing currency
      int64_t totalTokens = getTotalTokens(currency);
      bool bDivisible = isPropertyDivisible(currency);

      if (!bDivisible)fprintf(mp_fp, "\t    Total Tokens: %lu\n", totalTokens);
      else fprintf(mp_fp, "\t    Total Tokens: %lu.%08lu\n", totalTokens/COIN, totalTokens%COIN);

      if (0 >= totalTokens)
      {
        return (PKT_ERROR_STO -2);
      }

      // does the sender have enough of the property he's trying to "Send To Owners" ?
      if (getMPbalance(sender, currency, MONEY) < nValue)
      {
        return (PKT_ERROR_STO -3);
      }

      totalTokens = 0;
      int64_t n_owners = 0;

      typedef std::set<pair<int64_t, string>, SendToOwners_compare> OwnerAddrType;
      OwnerAddrType OwnerAddrSet;

      {
        for(map<string, CMPTally>::reverse_iterator my_it = mp_tally_map.rbegin(); my_it != mp_tally_map.rend(); ++my_it)
        {
          const string address = (my_it->first).c_str();

          // do not count the sender
          if (address == sender) continue;

          int64_t tokens = 0;

          tokens += getMPbalance(address, currency, MONEY);
          tokens += getMPbalance(address, currency, SELLOFFER_RESERVE);
          tokens += getMPbalance(address, currency, ACCEPT_RESERVE);

          if (tokens)
          {
            OwnerAddrSet.insert(make_pair(tokens, address));
            totalTokens += tokens;
          }
        }
      }

      if (!bDivisible)fprintf(mp_fp, "  Excluding Sender: %lu\n", totalTokens);
      else fprintf(mp_fp, "  Excluding Sender: %lu.%08lu\n", totalTokens/COIN, totalTokens%COIN);

      // loop #1 -- count the actual number of owners to receive the payment
      for(OwnerAddrType::reverse_iterator my_it = OwnerAddrSet.rbegin(); my_it != OwnerAddrSet.rend(); ++my_it)
      {
        n_owners++;

        // record the detailed info as needed
        if (fp) fprintf(fp, "#%ld: %lu = %s\n", n_owners, (my_it->first), (my_it->second).c_str());
      }

      fprintf(mp_fp, "\t          Owners: %lu\n", n_owners);

      // make sure we found some owners
      if (0 >= n_owners)
      {
        return (PKT_ERROR_STO -4);
      }

      uint64_t nXferFee = TRANSFER_FEE_PER_OWNER * n_owners;

      // determine which currency the fee will be paid in
      const unsigned int feeCurrency = isTestEcosystemProperty(currency) ? MASTERCOIN_CURRENCY_TMSC : MASTERCOIN_CURRENCY_MSC;

      fprintf(mp_fp, "\t    Transfer fee: %lu.%08lu %s\n", nXferFee/COIN, nXferFee%COIN, strMPCurrency(feeCurrency).c_str());

      // enough coins to pay the fee?
      if (getMPbalance(sender, feeCurrency, MONEY) < nXferFee)
      {
        return (PKT_ERROR_STO -5);
      }

      // special case check, only if distributing MSC or TMSC -- the currency the fee will be paid in
      if (feeCurrency == currency)
      {
        if (getMPbalance(sender, feeCurrency, MONEY) < (nValue + nXferFee))
        {
          return (PKT_ERROR_STO -55);
        }
      }

      // burn MSC or TMSC here: take the transfer fee away from the sender
      if (!update_tally_map(sender, feeCurrency, - nXferFee, MONEY))
      {
        // impossible to reach this, the check was done just before (the check is not necessary since update_tally_map checks balances too)
        return (PKT_ERROR_STO -500);
      }

      // loop #2
      // split up what was taken and distribute between all holders
      uint64_t owns, should_receive, will_really_receive, sent_so_far = 0;
      double percentage, piece;
      rc = 0; // almost good, the for-loop will set the error code
      for(OwnerAddrType::reverse_iterator my_it = OwnerAddrSet.rbegin(); my_it != OwnerAddrSet.rend(); ++my_it)
      {
      const string address = my_it->second;

        owns = my_it->first;
        percentage = (double) owns / (double) totalTokens;
        piece = percentage * nValue;
        should_receive = ceil(piece);

        // ensure that much is still available
        if ((nValue - sent_so_far) < should_receive)
        {
          will_really_receive = nValue - sent_so_far;
        }
        else
        {
          will_really_receive = should_receive;
        }

        sent_so_far += will_really_receive;

        if (msc_debug_sto)
         fprintf(mp_fp, "%14lu = %s, perc= %20.10lf, piece= %20.10lf, should_get= %14lu, will_really_get= %14lu, sent_so_far= %14lu\n",
          owns, address.c_str(), percentage, piece, should_receive, will_really_receive, sent_so_far);

        if (!update_tally_map(sender, currency, - will_really_receive, MONEY))
        {
          return (PKT_ERROR_STO -1);
        }

        update_tally_map(address, currency, will_really_receive, MONEY);

        if (sent_so_far >= nValue)
        {
          fprintf(mp_fp, "SendToOwners: DONE HERE : those who could get paid got paid, SOME DID NOT, but that's ok\n");
          break; // done here, everybody who could get paid got paid
        }
      }

      // sent_so_far must equal nValue here
      if (sent_so_far != nValue)
      {
        fprintf(mp_fp, "send_so_far= %14lu, nValue= %14lu, n_owners= %lu\n",
         sent_so_far, nValue, n_owners);

        // rc = ???
      }

    return rc;
  }

/*
[12:16:00 PM] ... some open spec issues
[12:16:05 PM] zathrasc: just picked these out real quick
[12:16:10 PM] zathrasc: should be mostly related to metadex
https://github.com/mastercoin-MSC/spec/issues/187

https://github.com/mastercoin-MSC/spec/issues/184

https://github.com/mastercoin-MSC/spec/issues/179

https://github.com/mastercoin-MSC/spec/issues/175

https://github.com/mastercoin-MSC/spec/issues/173

https://github.com/mastercoin-MSC/spec/issues/155

https://github.com/mastercoin-MSC/spec/issues/142

big one

https://github.com/mastercoin-MSC/spec/issues/170
[12:16:26 PM] zathrasc: but yeah for thorough would be a fishing expedition mate
*/

  int logicMath_MetaDEx()
  {
  int rc = PKT_ERROR_METADEX -100;
  unsigned char action = 0;

    if (!isTransactionTypeAllowed(block, currency, type, version)) return (PKT_ERROR_METADEX -888);

    memcpy(&desired_currency, &pkt[16], 4);
    swapByteOrder32(desired_currency);

    memcpy(&desired_value, &pkt[20], 8);
    swapByteOrder64(desired_value);

    fprintf(mp_fp, "\tdesired currency: %u (%s)\n", desired_currency, strMPCurrency(desired_currency).c_str());
    fprintf(mp_fp, "\t   desired value: %lu.%08lu\n", desired_value/COIN, desired_value%COIN);

    memcpy(&action, &pkt[28], 1);

    fprintf(mp_fp, "\t          action: %u\n", action);

    nNewValue = getMPbalance(sender, currency, MONEY);

    // here we are copying nValue into nNewValue to be stored into our leveldb later: MP_txlist
    if (nNewValue > nValue) nNewValue = nValue;

    CMPMetaDEx *p_metadex = getMetaDEx(sender, currency);

    // do checks that are not application for the Cancel action
    if (CANCEL != action)
    {
      if (!isTransactionTypeAllowed(block, desired_currency, type, version)) return (PKT_ERROR_METADEX -889);

      // ensure no cross-over of currencies from Test Eco to normal
      if (isTestEcosystemProperty(currency) != isTestEcosystemProperty(desired_currency)) return (PKT_ERROR_METADEX -4);

      // ensure the desired currency exists in our universe
      if (!_my_sps->hasSP(desired_currency)) return (PKT_ERROR_METADEX -30);

      if (!nValue) return (PKT_ERROR_METADEX -11);
      if (!desired_value) return (PKT_ERROR_METADEX -12);
    }

    // TODO: use the nNewValue as the amount the seller/sender actually has to trade with
    // ...

    switch (action)
    {
      case NEW:
        // Does the sender have any money?
        if (0 >= nNewValue) return (PKT_ERROR_METADEX -3);

        // An address cannot create a new offer while that address has an active sell offer with the same currencies in the same roles.
        if (p_metadex) return (PKT_ERROR_METADEX -10);

        // rough logic now: match the trade vs existing offers -- if not fully satisfied -- add to the metadex map
        // ...

        // TODO: more stuff like the old offer MONEY into RESERVE; then add offer to map

        rc = MetaDEx_Create(sender, currency, nNewValue, block, desired_currency, desired_value, txid, tx_idx);

        // ...

        break;

      case UPDATE:
        if (!p_metadex) return (PKT_ERROR_METADEX -105);  // not found, nothing to update

        // TODO: check if the sender has enough money... for an update

        rc = MetaDEx_Update(sender, currency, nNewValue, block, desired_currency, desired_value, txid, tx_idx);

        break;

      case CANCEL:
        if (!p_metadex) return (PKT_ERROR_METADEX -111);  // not found, nothing to cancel

        rc = MetaDEx_Destroy(sender, currency);

        break;

      default: return (PKT_ERROR_METADEX -999);
    }

    return rc;
  }

  int logicMath_GrantTokens() {
    int rc = PKT_ERROR_TOKENS - 1000;

    if (!isTransactionTypeAllowed(block, currency, type, version)) {
      fprintf(mp_fp, "\tRejecting Grant: Transaction type not yet allowed\n");
      return (PKT_ERROR_TOKENS - 22);
    }

    if (sender.empty()) {
      ++InvalidCount_per_spec;
      fprintf(mp_fp, "\tRejecting Grant: Sender is empty\n");
      return (PKT_ERROR_TOKENS - 23);
    }

    // manual issuance check
    if (false == _my_sps->hasSP(currency)) {
      fprintf(mp_fp, "\tRejecting Grant: SP id:%d does not exist\n", currency);
      return (PKT_ERROR_TOKENS - 24);
    }

    CMPSPInfo::Entry sp;
    _my_sps->getSP(currency, sp);

    if (false == sp.manual) {
      fprintf(mp_fp, "\tRejecting Grant: SP id:%d was not issued with a TX 54\n", currency);
      return (PKT_ERROR_TOKENS - 25);
    }


    // issuer check
    if (false == boost::iequals(sender, sp.issuer)) {
      fprintf(mp_fp, "\tRejecting Grant: %s is not the issuer of SP id:%d\n", sender.c_str(), currency);
      return (PKT_ERROR_TOKENS - 26);
    }

    // overflow tokens check
    if (MAX_INT_8_BYTES - sp.num_tokens < nValue) {
      char prettyTokens[256];
      if (sp.isDivisible()) {
        snprintf(prettyTokens, 256, "%lu.%08lu", nValue / COIN, nValue % COIN);
      } else {
        snprintf(prettyTokens, 256, "%lu", nValue);
      }
      fprintf(mp_fp, "\tRejecting Grant: granting %s tokens on SP id:%d would overflow the maximum limit for tokens in a smart property\n", prettyTokens, currency);
      return (PKT_ERROR_TOKENS - 27);
    }

    // grant the tokens
    update_tally_map(sender, currency, nValue, MONEY);

    // call the send logic
    rc = logicMath_SimpleSend();

    // record this grant
    std::vector<uint64_t> dataPt;
    dataPt.push_back(nValue);
    dataPt.push_back(0);
    string txidStr = txid.ToString();
    sp.historicalData.insert(std::make_pair(txidStr, dataPt));
    _my_sps->updateSP(currency, sp);

    return rc;
  }

  int logicMath_RevokeTokens() {
    int rc = PKT_ERROR_TOKENS - 1000;

    if (!isTransactionTypeAllowed(block, currency, type, version)) {
      fprintf(mp_fp, "\tRejecting Revoke: Transaction type not yet allowed\n");
      return (PKT_ERROR_TOKENS - 22);
    }

    if (sender.empty()) {
      ++InvalidCount_per_spec;
      fprintf(mp_fp, "\tRejecting Revoke: Sender is empty\n");
      return (PKT_ERROR_TOKENS - 23);
    }

    // manual issuance check
    if (false == _my_sps->hasSP(currency)) {
      fprintf(mp_fp, "\tRejecting Revoke: SP id:%d does not exist\n", currency);
      return (PKT_ERROR_TOKENS - 24);
    }

    CMPSPInfo::Entry sp;
    _my_sps->getSP(currency, sp);

    if (false == sp.manual) {
      fprintf(mp_fp, "\tRejecting Revoke: SP id:%d was not issued with a TX 54\n", currency);
      return (PKT_ERROR_TOKENS - 25);
    }


    // issuer check
    if (false == boost::iequals(sender, sp.issuer)) {
      fprintf(mp_fp, "\tRejecting Revoke: %s is not the issuer of SP id:%d\n", sender.c_str(), currency);
      return (PKT_ERROR_TOKENS - 26);
    }

    // insufficient funds check and revoke
    if (false == update_tally_map(sender, currency, -nValue, MONEY)) {
      fprintf(mp_fp, "\tRejecting Revoke: insufficient funds\n");
      return (PKT_ERROR_TOKENS - 111);
    }

    // record this revoke
    std::vector<uint64_t> dataPt;
    dataPt.push_back(0);
    dataPt.push_back(nValue);
    string txidStr = txid.ToString();
    sp.historicalData.insert(std::make_pair(txidStr, dataPt));
    _my_sps->updateSP(currency, sp);

    rc = 0;
    return rc;
  }


 // the 31-byte packet & the packet #
 // int interpretPacket(int blocknow, unsigned char pkt[], int size)
 //
 // RETURNS:  0 if the packet is fully valid
 // RETURNS: <0 if the packet is invalid
 // RETURNS: >0 the only known case today is: return PKT_RETURN_OFFER
 //
 // 
 // the following functions may augment the amount in question (nValue):
 // DEx_offerCreate()
 // DEx_offerUpdate()
 // DEx_acceptCreate()
 // DEx_payment() -- DOES NOT fit nicely into the model, as it currently is NOT a MP TX (not even in leveldb) -- gotta rethink
 //
 // optional: provide the pointer to the CMPOffer object, it will get filled in
 // verify that it does via if (MSC_TYPE_TRADE_OFFER == mp_obj.getType())
 //
 int interpretPacket(CMPOffer *obj_o = NULL)
 {
 int rc = PKT_ERROR;
 int step_rc;

  if (0>step1()) return -98765;

  if ((obj_o) && (MSC_TYPE_TRADE_OFFER != type)) return -777; // can't fill in the Offer object !

  // further processing for complex types
  // TODO: version may play a role here !
  switch(type)
  {
    case MSC_TYPE_SIMPLE_SEND:
      step_rc = step2_Value();
      if (0>step_rc) return step_rc;

      rc = logicMath_SimpleSend();
      break;

    case MSC_TYPE_TRADE_OFFER:
      step_rc = step2_Value();
      if (0>step_rc) return step_rc;

      rc = logicMath_TradeOffer(obj_o);
      break;

    case MSC_TYPE_ACCEPT_OFFER_BTC:
      step_rc = step2_Value();
      if (0>step_rc) return step_rc;

      rc = logicMath_AcceptOffer_BTC();
      break;

    case MSC_TYPE_CREATE_PROPERTY_FIXED:
    {
      const char *p = step2_SmartProperty(step_rc);
      if (0>step_rc) return step_rc;
      if (!p) return (PKT_ERROR_SP -11);

      step_rc = step3_sp_fixed(p);
      if (0>step_rc) return step_rc;

      if (0 == step_rc)
      {
        CMPSPInfo::Entry newSP;
        newSP.issuer = sender;
        newSP.txid = txid;
        newSP.prop_type = prop_type;
        newSP.num_tokens = nValue;
        newSP.category.assign(category);
        newSP.subcategory.assign(subcategory);
        newSP.name.assign(name);
        newSP.url.assign(url);
        newSP.data.assign(data);
        newSP.fixed = true;

        const unsigned int id = _my_sps->putSP(ecosystem, newSP);
        update_tally_map(sender, id, nValue, MONEY);
      }
      rc = 0;
      break;
    }

    case MSC_TYPE_CREATE_PROPERTY_VARIABLE:
    {
      const char *p = step2_SmartProperty(step_rc);
      if (0>step_rc) return step_rc;
      if (!p) return (PKT_ERROR_SP -12);

      step_rc = step3_sp_variable(p);
      if (0>step_rc) return step_rc;

      // check if one exists for this address already !
      if (NULL != getCrowd(sender)) return (PKT_ERROR_SP -20);

      // must check that the desired currency exists in our universe
      if (false == _my_sps->hasSP(currency)) return (PKT_ERROR_SP -30);

      if (0 == step_rc)
      {
        CMPSPInfo::Entry newSP;
        newSP.issuer = sender;
        newSP.txid = txid;
        newSP.prop_type = prop_type;
        newSP.num_tokens = nValue;
        newSP.category.assign(category);
        newSP.subcategory.assign(subcategory);
        newSP.name.assign(name);
        newSP.url.assign(url);
        newSP.data.assign(data);
        newSP.fixed = false;
        newSP.currency_desired = currency;
        newSP.deadline = deadline;
        newSP.early_bird = early_bird;
        newSP.percentage = percentage;

        const unsigned int id = _my_sps->putSP(ecosystem, newSP);
        my_crowds.insert(std::make_pair(sender, CMPCrowd(id, nValue, currency, deadline, early_bird, percentage, 0, 0)));
        fprintf(mp_fp, "\nCREATED CROWDSALE id: %u value: %lu currency: %u\n", id, nValue, currency);  
      }
      rc = 0;
      break;
    }

    case MSC_TYPE_CLOSE_CROWDSALE:
    {
    CrowdMap::iterator it = my_crowds.find(sender);

      if (it != my_crowds.end())
      {
        // retrieve the property id from the incoming packet
        memcpy(&currency, &pkt[4], 4);
        swapByteOrder32(currency);

        if (msc_debug_sp) fprintf(mp_fp, "%s() trying to ERASE CROWDSALE for propid= %u=%X, line %d, file: %s\n",
         __FUNCTION__, currency, currency, __LINE__, __FILE__);

        // ensure we are closing the crowdsale which we opened by checking the currency
        if ((it->second).getPropertyId() != currency)
        {
          rc = (PKT_ERROR_SP -606);
          break;
        }

        dumpCrowdsaleInfo(it->first, it->second);

        // Begin calculate Fractional 

        CMPCrowd &crowd = it->second;
        
        CMPSPInfo::Entry sp;
        _my_sps->getSP(crowd.getPropertyId(), sp);

        //fprintf(mp_fp, "\nValues going into calculateFractional(): hexid %s earlyBird %d deadline %lu numProps %lu issuerPerc %d, issuerCreated %ld \n", sp.txid.GetHex().c_str(), sp.early_bird, sp.deadline, sp.num_tokens, sp.percentage, crowd.getIssuerCreated());

        double missedTokens = calculateFractional(sp.prop_type,
                            sp.early_bird,
                            sp.deadline,
                            sp.num_tokens,
                            sp.percentage,
                            crowd.getDatabase(),
                            crowd.getIssuerCreated());

        //fprintf(mp_fp,"\nValues coming out of calculateFractional(): Total tokens, Tokens created, Tokens for issuer, amountMissed: issuer %s %ld %ld %ld %f\n",sp.issuer.c_str(), crowd.getUserCreated() + crowd.getIssuerCreated(), crowd.getUserCreated(), crowd.getIssuerCreated(), missedTokens);
        
        sp.historicalData = crowd.getDatabase();
        sp.close_early = 1;
        sp.timeclosed = blockTime;
        sp.txid_close = txid;

        _my_sps->updateSP(crowd.getPropertyId() , sp);
        
        update_tally_map(sp.issuer, crowd.getPropertyId(), missedTokens, MONEY);
        //End

        my_crowds.erase(it);

        rc = 0;
      }
      break;
    }

    case MSC_TYPE_CREATE_PROPERTY_MANUAL:
    {
      const char *p = step2_SmartProperty(step_rc);
      if (0>step_rc) return step_rc;
      if (!p) return (PKT_ERROR_SP -11);

      if (0 == step_rc)
      {
        CMPSPInfo::Entry newSP;
        newSP.issuer = sender;
        newSP.txid = txid;
        newSP.prop_type = prop_type;
        newSP.category.assign(category);
        newSP.subcategory.assign(subcategory);
        newSP.name.assign(name);
        newSP.url.assign(url);
        newSP.data.assign(data);
        newSP.fixed = false;
        newSP.manual = true;

        const unsigned int id = _my_sps->putSP(ecosystem, newSP);
        fprintf(mp_fp, "\nCREATED MANUAL PROPERTY id: %u admin: %s \n", id, sender.c_str());
      }
      rc = 0;
      break;
    }

    case MSC_TYPE_GRANT_PROPERTY_TOKENS:
      step_rc = step2_Value();
      if (0>step_rc) return step_rc;

      rc = logicMath_GrantTokens();
      break;

    case MSC_TYPE_REVOKE_PROPERTY_TOKENS:
      step_rc = step2_Value();
      if (0>step_rc) return step_rc;

      rc = logicMath_RevokeTokens();
      break;

    case MSC_TYPE_SEND_TO_OWNERS:
    if (disable_Divs) break;
    else
    {
      step_rc = step2_Value();
      if (0>step_rc) return step_rc;

      boost::filesystem::path pathOwners = GetDataDir() / OWNERS_FILENAME;
      FILE *fp = fopen(pathOwners.string().c_str(), "a");

      // TODO: write info line into the file, timestamp, block #, txid etc.........
      // ...

      rc = logicMath_SendToOwners(fp);

      if (fp) fclose(fp);
    }
    break;

    case MSC_TYPE_METADEX:
      step_rc = step2_Value();
      if (0>step_rc) return step_rc;

      rc = logicMath_MetaDEx();
      break;

    default:

      return (PKT_ERROR -100);
  }

  return rc;
 }

 // initial packet interpret step
 int step1()
 {
  if (PACKET_SIZE_CLASS_A > pkt_size)  // class A could be 19 bytes
  {
    fprintf(mp_fp, "%s() ERROR PACKET TOO SMALL; size = %d, line %d, file: %s\n", __FUNCTION__, pkt_size, __LINE__, __FILE__);
    return -(PKT_ERROR -1);
  }

  // collect version
  memcpy(&version, &pkt[0], 2);
  swapByteOrder16(version);

  // blank out version bytes in the packet
  pkt[0]=0; pkt[1]=0;
  
  memcpy(&type, &pkt[0], 4);

  swapByteOrder32(type);

  fprintf(mp_fp, "version: %d, Class %s\n", version, !multi ? "A":"B");
  fprintf(mp_fp, "\t            type: %u (%s)\n", type, c_strMastercoinType(type));

  return (type);
 }

 // extract Value for certain types of packets
 int step2_Value()
 {
  memcpy(&nValue, &pkt[8], 8);
  swapByteOrder64(nValue);

  // here we are copying nValue into nNewValue to be stored into our leveldb later: MP_txlist
  nNewValue = nValue;

  memcpy(&currency, &pkt[4], 4);
  swapByteOrder32(currency);

  fprintf(mp_fp, "\t        currency: %u (%s)\n", currency, strMPCurrency(currency).c_str());
  fprintf(mp_fp, "\t           value: %lu.%08lu\n", nValue/COIN, nValue%COIN);

  if (MAX_INT_8_BYTES < nValue)
  {
    return (PKT_ERROR -801);  // out of range
  }

  return 0;
 }

 // overrun check, are we beyond the end of packet?
 bool isOverrun(const char *p, unsigned int line)
 {
 int now = (char *)p - (char *)&pkt;
 bool bRet = (now > pkt_size);

    if (bRet) fprintf(mp_fp, "%s(%sline=%u):now= %u, pkt_size= %u\n", __FUNCTION__, bRet ? "OVERRUN !!! ":"", line, now, pkt_size);

    return bRet;
 }

 // extract Smart Property data
 // RETURNS: the pointer to the next piece to be parsed
 // ERROR is returns NULL and/or sets the error_code
 const char *step2_SmartProperty(int &error_code)
 {
 const char *p = 11 + (char *)&pkt;
 std::vector<std::string>spstr;
 unsigned int i;
 unsigned int prop_id;

  error_code = 0;

  memcpy(&ecosystem, &pkt[4], 1);
  fprintf(mp_fp, "\t       Ecosystem: %u\n", ecosystem);

  // valid values are 1 & 2
  if ((MASTERCOIN_CURRENCY_MSC != ecosystem) && (MASTERCOIN_CURRENCY_TMSC != ecosystem))
  {
    error_code = (PKT_ERROR_SP -501);
    return NULL;
  }

  prop_id = _my_sps->peekNextSPID(ecosystem);

  memcpy(&prop_type, &pkt[5], 2);
  swapByteOrder16(prop_type);

  memcpy(&prev_prop_id, &pkt[7], 4);
  swapByteOrder32(prev_prop_id);

  fprintf(mp_fp, "\t     Property ID: %u (%s)\n", prop_id, strMPCurrency(prop_id).c_str());
  fprintf(mp_fp, "\t   Property type: %u (%s)\n", prop_type, c_strPropertyType(prop_type));
  fprintf(mp_fp, "\tPrev Property ID: %u\n", prev_prop_id);

  // only 1 & 2 are valid right now
  if ((MSC_PROPERTY_TYPE_INDIVISIBLE != prop_type) && (MSC_PROPERTY_TYPE_DIVISIBLE != prop_type))
  {
    error_code = (PKT_ERROR_SP -502);
    return NULL;
  }

  for (i = 0; i<5; i++)
  {
    spstr.push_back(std::string(p));
    p += spstr.back().size() + 1;
  }

  i = 0;
  memcpy(category, spstr[i++].c_str(), sizeof(category));
  memcpy(subcategory, spstr[i++].c_str(), sizeof(subcategory));
  memcpy(name, spstr[i++].c_str(), sizeof(name));
  memcpy(url, spstr[i++].c_str(), sizeof(url));
  memcpy(data, spstr[i++].c_str(), sizeof(data));

  fprintf(mp_fp, "\t        Category: %s\n", category);
  fprintf(mp_fp, "\t     Subcategory: %s\n", subcategory);
  fprintf(mp_fp, "\t            Name: %s\n", name);
  fprintf(mp_fp, "\t             URL: %s\n", url);
  fprintf(mp_fp, "\t            Data: %s\n", data);

  if (!isTransactionTypeAllowed(block, prop_id, type, version))
  {
    error_code = (PKT_ERROR_SP -503);
    return NULL;
  }

  // name can not be NULL
  if ('\0' == name[0])
  {
    error_code = (PKT_ERROR_SP -505);
    return NULL;
  }

  if (!p) error_code = (PKT_ERROR_SP -510);

  if (isOverrun(p, __LINE__))
  {
    error_code = (PKT_ERROR_SP -800);
    return NULL;
  }

  return p;
 }

 int step3_sp_fixed(const char *p)
 {
  if (!p) return (PKT_ERROR_SP -1);

  memcpy(&nValue, p, 8);
  swapByteOrder64(nValue);
  p += 8;

  // here we are copying nValue into nNewValue to be stored into our leveldb later: MP_txlist
  nNewValue = nValue;

  if (MSC_PROPERTY_TYPE_INDIVISIBLE == prop_type)
  {
    fprintf(mp_fp, "\t           value: %lu\n", nValue);
    if (0 == nValue) return (PKT_ERROR_SP -101);
  }
  else
  if (MSC_PROPERTY_TYPE_DIVISIBLE == prop_type)
  {
    fprintf(mp_fp, "\t           value: %lu.%08lu\n", nValue/COIN, nValue%COIN);
    if (0 == nValue) return (PKT_ERROR_SP -102);
  }

  if (MAX_INT_8_BYTES < nValue)
  {
    return (PKT_ERROR -802);  // out of range
  }

  if (isOverrun(p, __LINE__)) return (PKT_ERROR_SP -900);

  return 0;
 }

 int step3_sp_variable(const char *p)
 {
  if (!p) return (PKT_ERROR_SP -1);

  memcpy(&currency, p, 4);  // currency desired
  swapByteOrder32(currency);
  p += 4;

  fprintf(mp_fp, "\t        currency: %u (%s)\n", currency, strMPCurrency(currency).c_str());

  memcpy(&nValue, p, 8);
  swapByteOrder64(nValue);
  p += 8;

  // here we are copying nValue into nNewValue to be stored into our leveldb later: MP_txlist
  nNewValue = nValue;

  if (MSC_PROPERTY_TYPE_INDIVISIBLE == prop_type)
  {
    fprintf(mp_fp, "\t           value: %lu\n", nValue);
    if (0 == nValue) return (PKT_ERROR_SP -201);
  }
  else
  if (MSC_PROPERTY_TYPE_DIVISIBLE == prop_type)
  {
    fprintf(mp_fp, "\t           value: %lu.%08lu\n", nValue/COIN, nValue%COIN);
    if (0 == nValue) return (PKT_ERROR_SP -202);
  }

  if (MAX_INT_8_BYTES < nValue)
  {
    return (PKT_ERROR -803);  // out of range
  }

  memcpy(&deadline, p, 8);
  swapByteOrder64(deadline);
  p += 8;
  fprintf(mp_fp, "\t        Deadline: %s (%lX)\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", deadline).c_str(), deadline);

  if (!deadline) return (PKT_ERROR_SP -203);  // deadline cannot be 0

  // deadline can not be smaller than the timestamp of the current block
  if (deadline < (uint64_t)blockTime) return (PKT_ERROR_SP -204);

  memcpy(&early_bird, p++, 1);
  fprintf(mp_fp, "\tEarly Bird Bonus: %u\n", early_bird);

  memcpy(&percentage, p++, 1);
  fprintf(mp_fp, "\t      Percentage: %u\n", percentage);

  if (isOverrun(p, __LINE__)) return (PKT_ERROR_SP -765);

  return 0;
 }

  void Set(const uint256 &t, int b, unsigned int idx, int64_t bt)
  {
    txid = t;
    block = b;
    tx_idx = idx;
    blockTime = bt;
  }

  void Set(string s, string r, uint64_t n, const uint256 &t, int b, unsigned int idx, unsigned char *p, unsigned int size, int fMultisig, uint64_t txf)
  {
    sender = s;
    receiver = r;
    txid = t;
    block = b;
    tx_idx = idx;
    pkt_size = size < sizeof(pkt) ? size : sizeof(pkt);
    nValue = n;
    nNewValue = n;
    multi= fMultisig;
    tx_fee_paid = txf;

    memcpy(&pkt, p, pkt_size);
  }

  bool operator<(const CMPTransaction& other) const
  {
    // sort by block # & additionally the tx index within the block
    if (block != other.block) return block > other.block;
    return tx_idx > other.tx_idx;
  }

  void print()
  {
    fprintf(mp_fp, "BLOCK: %d =txid: %s =fee: %1.8lf\n", block, txid.GetHex().c_str(), (double)tx_fee_paid/(double)COIN);
    fprintf(mp_fp, "sender: %s ; receiver: %s\n", sender.c_str(), receiver.c_str());

    if (0<pkt_size)
    {
      fprintf(mp_fp, "pkt: %s\n", HexStr(pkt, pkt_size + pkt, false).c_str());
    }
    else
    {
      ++InvalidCount_per_spec;
    }
  }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// some old TODOs
//  6) verify large-number calculations (especially divisions & multiplications)
//  9) build in consesus checks with the masterchain.info & masterchest.info -- possibly run them automatically, daily (?)
// 10) need a locking mechanism between Core & Qt -- to retrieve the tally, for instance, this and similar to this: LOCK(wallet->cs_wallet);
//

uint64_t calculate_and_update_devmsc(unsigned int nTime)
{
//do nothing if before end of fundraiser 
if (nTime < 1377993874) return -9919;

// taken mainly from msc_validate.py: def get_available_reward(height, c)
uint64_t devmsc = 0;
int64_t exodus_delta;
// spec constants:
const uint64_t all_reward = 5631623576222;
const double seconds_in_one_year = 31556926;
const double seconds_passed = nTime - 1377993874; // exodus bootstrap deadline
const double years = seconds_passed/seconds_in_one_year;
const double part_available = 1 - pow(0.5, years);
const double available_reward=all_reward * part_available;

  devmsc = rounduint64(available_reward);
  exodus_delta = devmsc - exodus_prev;

  if (msc_debug_exo) fprintf(mp_fp, "devmsc=%lu, exodus_prev=%lu, exodus_delta=%ld\n", devmsc, exodus_prev, exodus_delta);

  // per Zathras -- skip if a block's timestamp is older than that of a previous one!
  if (0>exodus_delta) return 0;

  update_tally_map(exodus, MASTERCOIN_CURRENCY_MSC, exodus_delta, MONEY);
  exodus_prev = devmsc;

  return devmsc;
}

// TODO: optimize efficiency -- iterate only over wallet's addresses in the future
int set_wallet_totals()
{
int my_addresses_count = 0;
/* DISABLE FOR NOW !!!
const unsigned int currency = MASTERCOIN_CURRENCY_MSC;  // FIXME: hard-coded for MSC only, for PoC

  global_MSC_total = 0;
  global_MSC_RESERVED_total = 0;

  for(map<string, CMPTally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it)
  {
    // my_it->first = key
    // my_it->second = value

    if (IsMyAddress(my_it->first))
    {
      ++my_addresses_count;

      global_MSC_total += (my_it->second).getMoney(currency, false);
      global_MSC_RESERVED_total += (my_it->second).getMoney(currency, true);
    }
  }
*/

  return (my_addresses_count);
}

int mastercore_handler_block_begin(int nBlockNow, CBlockIndex const * pBlockIndex)
{
  if (0 < nBlockTop) if (nBlockTop < nBlockNow) return 0;

  (void) eraseExpiredCrowdsale(pBlockIndex->GetBlockTime());

  return 0;
}

// called once per block, after the block has been processed
// TODO: consolidate into *handler_block_begin() << need to adjust Accept expiry check.............
// it performs cleanup and other functions
int mastercore_handler_block_end(int nBlockNow, CBlockIndex const * pBlockIndex, unsigned int countMP)
{
  if (!mastercoreInitialized) {
    mastercore_init();
  }

  if (0 < nBlockTop) if (nBlockTop < nBlockNow) return 0;

// for every new received block must do:
// 1) remove expired entries from the accept list (per spec accept entries are valid until their blocklimit expiration; because the customer can keep paying BTC for the offer in several installments)
// 2) update the amount in the Exodus address
uint64_t devmsc = 0;
unsigned int how_many_erased = eraseExpiredAccepts(nBlockNow);

  if (how_many_erased) fprintf(mp_fp, "%s(%d); erased %u accepts this block, line %d, file: %s\n",
   __FUNCTION__, how_many_erased, nBlockNow, __LINE__, __FILE__);

  // calculate devmsc as of this block and update the Exodus' balance
  devmsc = calculate_and_update_devmsc(pBlockIndex->GetBlockTime());

  if (msc_debug_exo) fprintf(mp_fp, "devmsc for block %d: %lu, Exodus balance: %lu\n",
   nBlockNow, devmsc, getMPbalance(exodus, MASTERCOIN_CURRENCY_MSC, MONEY));

  // get the total MSC for this wallet, for QT display
  (void) set_wallet_totals();
//  printf("the globals: MSC_total= %lu, MSC_RESERVED_total= %lu\n", global_MSC_total, global_MSC_RESERVED_total);

  if (mp_fp) fflush(mp_fp);

  // save out the state after this block
  if (writePersistence(nBlockNow)) mastercore_save_state(pBlockIndex);

  return 0;
}

static void prepareObfuscatedHashes(const string &address, string (&ObfsHashes)[1+MAX_SHA256_OBFUSCATION_TIMES])
{
unsigned char sha_input[128];
unsigned char sha_result[128];
vector<unsigned char> vec_chars;

  strcpy((char *)sha_input, address.c_str());
  // do only as many re-hashes as there are mastercoin packets, per spec, 255 per Zathras
  for (unsigned int j = 1; j<=MAX_SHA256_OBFUSCATION_TIMES;j++)
  {
    SHA256(sha_input, strlen((const char *)sha_input), sha_result);

      vec_chars.resize(32);
      memcpy(&vec_chars[0], &sha_result[0], 32);
      ObfsHashes[j] = HexStr(vec_chars);
      boost::to_upper(ObfsHashes[j]); // uppercase per spec

      if (msc_debug_verbose2) if (5>j) fprintf(mp_fp, "%d: sha256 hex: %s\n", j, ObfsHashes[j].c_str());
      strcpy((char *)sha_input, ObfsHashes[j].c_str());
  }
}

static bool getOutputType(const CScript& scriptPubKey, txnouttype& whichTypeRet)
{
vector<vector<unsigned char> > vSolutions;

  if (!Solver(scriptPubKey, whichTypeRet, vSolutions)) return false;

  return true;
}


int TXExodusFundraiser(const CTransaction &wtx, const string &sender, int64_t ExodusHighestValue, int nBlock, unsigned int nTime)
{
  if ((nBlock >= GENESIS_BLOCK && nBlock <= LAST_EXODUS_BLOCK) || (isNonMainNet()))
  { //Exodus Fundraiser start/end blocks
    //printf("transaction: %s\n", wtx.ToString().c_str() );
    int deadline_timeleft=1377993600-nTime;
    double bonus= 1 + std::max( 0.10 * deadline_timeleft / (60 * 60 * 24 * 7), 0.0 );

    if (isNonMainNet())
    {
      bonus = 1;

      if (sender == exodus) return 1; // sending from Exodus should not be fundraising anything
    }

    uint64_t msc_tot= round( 100 * ExodusHighestValue * bonus ); 
    if (msc_debug_exo) fprintf(mp_fp, "Exodus Fundraiser tx detected, tx %s generated %lu.%08lu\n",wtx.GetHash().ToString().c_str(), msc_tot / COIN, msc_tot % COIN);
 
    update_tally_map(sender, MASTERCOIN_CURRENCY_MSC, msc_tot, MONEY);
    update_tally_map(sender, MASTERCOIN_CURRENCY_TMSC, msc_tot, MONEY);

    return 0;
  }
  return -1;
}

// idx is position within the block, 0-based
// int msc_tx_push(const CTransaction &wtx, int nBlock, unsigned int idx)
// INPUT: bRPConly -- set to true to avoid moving funds; to be called from various RPC calls like this
// RETURNS: 0 if parsed a MP TX
// RETURNS: < 0 if a non-MP-TX or invalid
// RETURNS: >0 if 1 or more payments have been made
int parseTransaction(bool bRPConly, const CTransaction &wtx, int nBlock, unsigned int idx, CMPTransaction *mp_tx, unsigned int nTime=0)
{
string strSender;
// class A: data & address storage -- combine them into a structure or something
vector<string>script_data;
vector<string>address_data;
// vector<uint64_t>value_data;
vector<int64_t>value_data;
int64_t ExodusValues[MAX_BTC_OUTPUTS];
int64_t TestNetMoneyValues[MAX_BTC_OUTPUTS] = { 0 };  // new way to get funded on TestNet, send TBTC to moneyman address
string strReference;
unsigned char single_pkt[MAX_PACKETS * PACKET_SIZE];
unsigned int packet_size = 0;
int fMultisig = 0;
int marker_count = 0, getmoney_count = 0;
// class B: multisig data storage
vector<string>multisig_script_data;
uint64_t inAll = 0;
uint64_t outAll = 0;
uint64_t txFee = 0;
int p2shAllowed = 0;

            if (P2SH_BLOCK <= nBlock || isNonMainNet()) {
              p2shAllowed = 1;
            }

            mp_tx->Set(wtx.GetHash(), nBlock, idx, nTime);

            // quickly go through the outputs & ensure there is a marker (a send to the Exodus address)
            for (unsigned int i = 0; i < wtx.vout.size(); i++)
            {
            CTxDestination dest;
            string strAddress;

              outAll += wtx.vout[i].nValue;

              if (ExtractDestination(wtx.vout[i].scriptPubKey, dest))
              {
                strAddress = CBitcoinAddress(dest).ToString();

                if (exodus == strAddress)
                {
                  ExodusValues[marker_count++] = wtx.vout[i].nValue;
                }
                else if (isNonMainNet() && (getmoney_testnet == strAddress))
                {
                  TestNetMoneyValues[getmoney_count++] = wtx.vout[i].nValue;
                }
              }
            }
            if ((isNonMainNet() && getmoney_count))
            {
            }
            else if (!marker_count)
            {
              return -1;
            }

            fprintf(mp_fp, "____________________________________________________________________________________________________________________________________\n");
            fprintf(mp_fp, "%s(block=%d, idx= %d); txid: %s\n", __FUNCTION__, nBlock, idx, wtx.GetHash().GetHex().c_str());

            // now save output addresses & scripts for later use
            // also determine if there is a multisig in there, if so = Class B
            for (unsigned int i = 0; i < wtx.vout.size(); i++)
            {
            CTxDestination dest;
            string strAddress;

              if (ExtractDestination(wtx.vout[i].scriptPubKey, dest))
              {
                txnouttype whichType;
                bool validType = false;
                if (!getOutputType(wtx.vout[i].scriptPubKey, whichType)) validType=false;
                if (TX_PUBKEYHASH == whichType || (p2shAllowed && TX_SCRIPTHASH == whichType)) validType=true; // ignore non pay-to-pubkeyhash or pay-to-scripthash output

                strAddress = CBitcoinAddress(dest).ToString();

                if ((exodus != strAddress) && (validType))
                {
                  if (msc_debug_parser_data) fprintf(mp_fp, "saving address_data #%d: %s:%s\n", i, strAddress.c_str(), wtx.vout[i].scriptPubKey.ToString().c_str());

                  // saving for Class A processing or reference
                  wtx.vout[i].scriptPubKey.mscore_parse(script_data);
                  address_data.push_back(strAddress);
                  value_data.push_back(wtx.vout[i].nValue);
                }
              }
              else
              {
              // a multisig ?
              txnouttype type;
              std::vector<CTxDestination> vDest;
              int nRequired;

                if (ExtractDestinations(wtx.vout[i].scriptPubKey, type, vDest, nRequired))
                {
                  ++fMultisig;
                }
              }
            }

            if (msc_debug_parser_data)
            {
              fprintf(mp_fp, "address_data.size=%lu\n", address_data.size());
              fprintf(mp_fp, "script_data.size=%lu\n", script_data.size());
              fprintf(mp_fp, "value_data.size=%lu\n", value_data.size());
            }

            int inputs_errors = 0;  // several types of erroroneous MP TX inputs
            map <string, uint64_t> inputs_sum_of_values;
            // now go through inputs & identify the sender, collect input amounts
            // go through inputs, find the largest per Mastercoin protocol, the Sender
            for (unsigned int i = 0; i < wtx.vin.size(); i++)
            {
            CTxDestination address;

            if (msc_debug_vin) fprintf(mp_fp, "vin=%d:%s\n", i, wtx.vin[i].scriptSig.ToString().c_str());

            CTransaction txPrev;
            uint256 hashBlock;
            if (!GetTransaction(wtx.vin[i].prevout.hash, txPrev, hashBlock, true))  // get the vin's previous transaction 
            {
              return -101;
            }

            unsigned int n = wtx.vin[i].prevout.n;

            CTxDestination source;

            uint64_t nValue = txPrev.vout[n].nValue;
            txnouttype whichType;

              inAll += nValue;

              if (ExtractDestination(txPrev.vout[n].scriptPubKey, source))  // extract the destination of the previous transaction's vout[n]
              {
                // we only allow pay-to-pubkeyhash, pay-to-scripthash & probably pay-to-pubkey (?)
                {
                  if (!getOutputType(txPrev.vout[n].scriptPubKey, whichType)) ++inputs_errors;
                  if ((TX_PUBKEYHASH != whichType && (p2shAllowed && TX_SCRIPTHASH != whichType)) /* || (TX_PUBKEY != whichType) */ ) ++inputs_errors;

                  if (inputs_errors) break;
                }

                CBitcoinAddress addressSource(source);              // convert this to an address

                inputs_sum_of_values[addressSource.ToString()] += nValue;
              }
              else ++inputs_errors;

              if (msc_debug_vin) fprintf(mp_fp, "vin=%d:%s\n", i, wtx.vin[i].ToString().c_str());
            } // end of inputs for loop

            txFee = inAll - outAll; // this is the fee paid to miners for this TX

            if (inputs_errors)  // not a valid MP TX
            {
              return -101;
            }

            // largest by sum of values
            uint64_t nMax = 0;
            for(map<string, uint64_t>::iterator my_it = inputs_sum_of_values.begin(); my_it != inputs_sum_of_values.end(); ++my_it)
            {
            uint64_t nTemp = my_it->second;

                if (nTemp > nMax)
                {
                  strSender = my_it->first;
                  if (msc_debug_exo) fprintf(mp_fp, "looking for The Sender: %s , nMax=%lu, nTemp=%lu\n", strSender.c_str(), nMax, nTemp);
                  nMax = nTemp;
                }
            }

            if (!strSender.empty())
            {
              if (msc_debug_verbose) fprintf(mp_fp, "The Sender: %s : His Input Sum of Values= %lu.%08lu ; fee= %lu.%08lu\n",
               strSender.c_str(), nMax / COIN, nMax % COIN, txFee/COIN, txFee%COIN);
            }
            else
            {
              fprintf(mp_fp, "The sender is still EMPTY !!! txid: %s\n", wtx.GetHash().GetHex().c_str());
              return -5;
            }
            
            //This calculates exodus fundraiser for each tx within a given block
            int64_t BTC_amount = ExodusValues[0];
            if (isNonMainNet())
            {
              if (MONEYMAN_TESTNET_BLOCK <= nBlock) BTC_amount = TestNetMoneyValues[0];
            }

            if (RegTest()) 
            { 
              if (MONEYMAN_REGTEST_BLOCK <= nBlock) BTC_amount = TestNetMoneyValues[0];
            }

//            fprintf(mp_fp, "%s() amount = %ld , nBlock = %d, line %d, file: %s\n", __FUNCTION__, BTC_amount, nBlock, __LINE__, __FILE__);

            if (0 < BTC_amount) (void) TXExodusFundraiser(wtx, strSender, BTC_amount, nBlock, nTime);

            // go through the outputs
            for (unsigned int i = 0; i < wtx.vout.size(); i++)
            {
            CTxDestination address;

              // if TRUE -- non-multisig
              if (ExtractDestination(wtx.vout[i].scriptPubKey, address))
              {
              }
              else
              {
                // probably a multisig -- get them

        txnouttype type;
        std::vector<CTxDestination> vDest;
        int nRequired;

        // CScript is a std::vector
        if (msc_debug_script) fprintf(mp_fp, "scriptPubKey: %s\n", wtx.vout[i].scriptPubKey.getHex().c_str());

        if (ExtractDestinations(wtx.vout[i].scriptPubKey, type, vDest, nRequired))
        {
          if (msc_debug_script) fprintf(mp_fp, " >> multisig: ");
            BOOST_FOREACH(const CTxDestination &dest, vDest)
            {
            CBitcoinAddress address = CBitcoinAddress(dest);
            CKeyID keyID;

            if (!address.GetKeyID(keyID))
            {
            // TODO: add an error handler
            }

              // base_uint is a superclass of dest, size(), GetHex() is the same as ToString()
//              fprintf(mp_fp, "%s size=%d (%s); ", address.ToString().c_str(), keyID.size(), keyID.GetHex().c_str());
              if (msc_debug_script) fprintf(mp_fp, "%s ; ", address.ToString().c_str());

            }
          if (msc_debug_script) fprintf(mp_fp, "\n");

          // TODO: verify that we can handle multiple multisigs per tx
          wtx.vout[i].scriptPubKey.mscore_parse(multisig_script_data, false);

//          break;  // get out of processing this first multisig  , Michael Jun 24
        }
              }
            } // end of the outputs' for loop

          string strObfuscatedHashes[1+MAX_SHA256_OBFUSCATION_TIMES];
          prepareObfuscatedHashes(strSender, strObfuscatedHashes);

          unsigned char packets[MAX_PACKETS][32];
          int mdata_count = 0;  // multisig data count
          if (!fMultisig)
          {
              // ---------------------------------- Class A parsing ---------------------------

              // Init vars
              string strScriptData;
              string strDataAddress;
              string strRefAddress;
              unsigned char dataAddressSeq = 0xFF;
              unsigned char seq = 0xFF;
              int64_t dataAddressValue = 0;

              // Step 1, locate the data packet
              for (unsigned k = 0; k<script_data.size();k++) // loop through outputs
              {
                  txnouttype whichType;
                  if (!getOutputType(wtx.vout[k].scriptPubKey, whichType)) break; // unable to determine type, ignore output
                  if (TX_PUBKEYHASH != whichType && (p2shAllowed && TX_SCRIPTHASH != whichType)) break; // ignore non pay-to-pubkeyhash output

                  string strSub = script_data[k].substr(2,16); // retrieve bytes 1-9 of packet for peek & decode comparison
                  seq = (ParseHex(script_data[k].substr(0,2)))[0]; // retrieve sequence number

                  if (("0000000000000001" == strSub) || ("0000000000000002" == strSub)) // peek & decode comparison
                  {
                      if (strScriptData.empty()) // confirm we have not already located a data address
                      {
                          strScriptData = script_data[k].substr(2*1,2*PACKET_SIZE_CLASS_A); // populate data packet
                          strDataAddress = address_data[k]; // record data address
                          dataAddressSeq = seq; // record data address seq num for reference matching
                          dataAddressValue = value_data[k]; // record data address amount for reference matching
                          if (msc_debug_parser_data) fprintf(mp_fp, "Data Address located - data[%d]:%s: %s (%lu.%08lu)\n", k, script_data[k].c_str(), address_data[k].c_str(), value_data[k] / COIN, value_data[k] % COIN);
                      }
                      else
                      {
                          // invalidate - Class A cannot be more than one data packet - possible collision, treat as default (BTC payment)
                          strDataAddress = ""; //empty strScriptData to block further parsing
                          if (msc_debug_parser_data) fprintf(mp_fp, "Multiple Data Addresses found (collision?) Class A invalidated, defaulting to BTC payment\n");
                          break;
                      }
                  }
              }

              // Step 2, see if we can locate an address with a seqnum +1 of DataAddressSeq
              if (!strDataAddress.empty()) // verify Step 1, we should now have a valid data packet, if so continue parsing
              {
                  unsigned char expectedRefAddressSeq = dataAddressSeq + 1;
                  for (unsigned k = 0; k<script_data.size();k++) // loop through outputs
                  {
                      txnouttype whichType;
                      if (!getOutputType(wtx.vout[k].scriptPubKey, whichType)) break; // unable to determine type, ignore output
                      if (TX_PUBKEYHASH != whichType && (p2shAllowed && TX_SCRIPTHASH != whichType)) break; // ignore non pay-to-pubkeyhash output

                      seq = (ParseHex(script_data[k].substr(0,2)))[0]; // retrieve sequence number

                      if ((address_data[k] != strDataAddress) && (address_data[k] != exodus) && (expectedRefAddressSeq == seq)) // found reference address with matching sequence number
                      {
                          if (strRefAddress.empty()) // confirm we have not already located a reference address
                          {
                              strRefAddress = address_data[k]; // set ref address
                              if (msc_debug_parser_data) fprintf(mp_fp, "Reference Address located via seqnum - data[%d]:%s: %s (%lu.%08lu)\n", k, script_data[k].c_str(), address_data[k].c_str(), value_data[k] / COIN, value_data[k] % COIN);
                          }
                          else
                          {
                              // can't trust sequence numbers to provide reference address, there is a collision with >1 address with expected seqnum
                              strRefAddress = ""; // blank ref address
                              if (msc_debug_parser_data) fprintf(mp_fp, "Reference Address sequence number collision, will fall back to evaluating matching output amounts\n");
                              break;
                          }
                      }
                  }
                  // Step 3, if we still don't have a reference address, see if we can locate an address with matching output amounts
                  if (strRefAddress.empty())
                  {
                      for (unsigned k = 0; k<script_data.size();k++) // loop through outputs
                      {
                          txnouttype whichType;
                          if (!getOutputType(wtx.vout[k].scriptPubKey, whichType)) break; // unable to determine type, ignore output
                          if (TX_PUBKEYHASH != whichType && (p2shAllowed && TX_SCRIPTHASH != whichType)) break; // ignore non pay-to-pubkeyhash output

                          if ((address_data[k] != strDataAddress) && (address_data[k] != exodus) && (dataAddressValue == value_data[k])) // this output matches data output, check if matches exodus output
                          {
                              for (int exodus_idx=0;exodus_idx<marker_count;exodus_idx++)
                              {
                                  if (value_data[k] == ExodusValues[exodus_idx]) //this output matches data address value and exodus address value, choose as ref
                                  {
                                       if (strRefAddress.empty())
                                       {
                                           strRefAddress = address_data[k];
                                           if (msc_debug_parser_data) fprintf(mp_fp, "Reference Address located via matching amounts - data[%d]:%s: %s (%lu.%08lu)\n", k, script_data[k].c_str(), address_data[k].c_str(), value_data[k] / COIN, value_data[k] % COIN);
                                       }
                                       else
                                       {
                                           strRefAddress = "";
                                           if (msc_debug_parser_data) fprintf(mp_fp, "Reference Address collision, multiple potential candidates. Class A invalidated, defaulting to BTC payment\n");
                                           break;
                                       }
                                  }
                              }
                          }
                      }
                  }
              } // end if (!strDataAddress.empty())

              // Populate expected var strReference with chosen address (if not empty)
              if (!strRefAddress.empty()) strReference=strRefAddress;

              // Last validation step, if strRefAddress is empty, blank strDataAddress so we default to BTC payment
              if (strRefAddress.empty()) strDataAddress="";

              // -------------------------------- End Class A parsing -------------------------

              if (strDataAddress.empty()) // an empty Data Address here means it is not Class A valid and should be defaulted to a BTC payment
              {
              // this must be the BTC payment - validate (?)
              // TODO
              // ...
//              if (msc_debug_verbose) fprintf(mp_fp, "\n================BLOCK: %d======\ntxid: %s\n", nBlock, wtx.GetHash().GetHex().c_str());
              fprintf(mp_fp, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
              fprintf(mp_fp, "sender: %s , receiver: %s\n", strSender.c_str(), strReference.c_str());
              fprintf(mp_fp, "!!!!!!!!!!!!!!!!! this may be the BTC payment for an offer !!!!!!!!!!!!!!!!!!!!!!!!\n");

              // TODO collect all payments made to non-itself & non-exodus and their amounts -- these may be purchases!!!

              int count = 0;
              // go through the outputs, once again...
              {
                for (unsigned int i = 0; i < wtx.vout.size(); i++)
                {
                CTxDestination dest;

                  if (ExtractDestination(wtx.vout[i].scriptPubKey, dest))
                  {
                  const string strAddress = CBitcoinAddress(dest).ToString();

                    if (exodus == strAddress) continue;
                    fprintf(mp_fp, "payment #%d %s %11.8lf\n", count, strAddress.c_str(), (double)wtx.vout[i].nValue/(double)COIN);

                    // check everything & pay BTC for the currency we are buying here...
                    if (bRPConly) count = 55555;  // no real way to validate a payment during simple RPC call
                    else if (0 == DEx_payment(wtx.GetHash(), i, strAddress, strSender, wtx.vout[i].nValue, nBlock)) ++count;
                  }
                }
              }
              return count ? count : -5678; // return count -- the actual number of payments within this TX or error if none were made
            }
            else
            {
            // valid Class A packet almost ready
              if (msc_debug_parser_data) fprintf(mp_fp, "valid Class A:from=%s:to=%s:data=%s\n", strSender.c_str(), strReference.c_str(), strScriptData.c_str());
              packet_size = PACKET_SIZE_CLASS_A;
              memcpy(single_pkt, &ParseHex(strScriptData)[0], packet_size);
            }
          }
          else // if (fMultisig)
          {
            unsigned int k = 0;
            // gotta find the Reference - Z rewrite - scrappy & inefficient, can be optimized

            if (msc_debug_parser_data) fprintf(mp_fp, "Beginning reference identification\n");

            bool referenceFound = false; // bool to hold whether we've found the reference yet
            bool changeRemoved = false; // bool to hold whether we've ignored the first output to sender as change
            unsigned int potentialReferenceOutputs = 0; // int to hold number of potential reference outputs

            // how many potential reference outputs do we have, if just one select it right here
            BOOST_FOREACH(const string &addr, address_data)
            {
                // keep Michael's original debug info & k int as used elsewhere
                if (msc_debug_parser_data) fprintf(mp_fp, "ref? data[%d]:%s: %s (%lu.%08lu)\n",
                 k, script_data[k].c_str(), addr.c_str(), value_data[k] / COIN, value_data[k] % COIN);
                ++k;

                if (addr != exodus)
                {
                        ++potentialReferenceOutputs;
                        if (1 == potentialReferenceOutputs)
                        {
                                strReference = addr;
                                referenceFound = true;
                                if (msc_debug_parser_data) fprintf(mp_fp, "Single reference potentially id'd as follows: %s \n", strReference.c_str());
                        }
                        else //as soon as potentialReferenceOutputs > 1 we need to go fishing
                        {
                                strReference = ""; // avoid leaving strReference populated for sanity
                                referenceFound = false;
                                if (msc_debug_parser_data) fprintf(mp_fp, "More than one potential reference candidate, blanking strReference, need to go fishing\n");
                        }
                }
            }

            // do we have a reference now? or do we need to dig deeper
            if (!referenceFound) // multiple possible reference addresses
            {
                if (msc_debug_parser_data) fprintf(mp_fp, "Reference has not been found yet, going fishing\n");

                BOOST_FOREACH(const string &addr, address_data)
                {
                        // !!!! address_data is ordered by vout (i think - please confirm that's correct analysis?)
                        if (addr != exodus) // removed strSender restriction, not to spec
                        {
                                if ((addr == strSender) && (!changeRemoved))
                                {
                                        // per spec ignore first output to sender as change if multiple possible ref addresses
                                        changeRemoved = true;
                                        if (msc_debug_parser_data) fprintf(mp_fp, "Removed change\n");
                                }
                                else
                                {
                                        // this may be set several times, but last time will be highest vout
                                        strReference = addr;
                                        if (msc_debug_parser_data) fprintf(mp_fp, "Resetting strReference as follows: %s \n ", strReference.c_str());
                                }
                        }
                }
            }

          if (msc_debug_parser_data) fprintf(mp_fp, "Ending reference identification\n");
          if (msc_debug_parser_data) fprintf(mp_fp, "Final decision on reference identification is: %s\n", strReference.c_str());

          if (msc_debug_parser) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
          // multisig , Class B; get the data packets that are found here
          for (unsigned int k = 0; k<multisig_script_data.size();k++)
          {
            if (msc_debug_parser) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
            CPubKey key(ParseHex(multisig_script_data[k]));
            CKeyID keyID = key.GetID();
            string strAddress = CBitcoinAddress(keyID).ToString();
            char *c_addr_type = (char *)"";
            string strPacket;

            if (msc_debug_parser) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
            {
              // this is a data packet, must deobfuscate now
              vector<unsigned char>hash = ParseHex(strObfuscatedHashes[mdata_count+1]);      
              vector<unsigned char>packet = ParseHex(multisig_script_data[k].substr(2*1,2*PACKET_SIZE));

              for (unsigned int i=0;i<packet.size();i++)
              {
                packet[i] ^= hash[i];
              }

                memcpy(&packets[mdata_count], &packet[0], PACKET_SIZE);
                strPacket = HexStr(packet.begin(),packet.end(), false);
                ++mdata_count;

                if (MAX_PACKETS <= mdata_count)
                {
                  fprintf(mp_fp, "increase MAX_PACKETS ! mdata_count= %d\n", mdata_count);
                  return -222;
                }
            }

          if (msc_debug_parser_data) fprintf(mp_fp, "multisig_data[%d]:%s: %s%s\n", k, multisig_script_data[k].c_str(), strAddress.c_str(), c_addr_type);

            if (!strPacket.empty())
            {
              if (msc_debug_parser) fprintf(mp_fp, "packet #%d: %s\n", mdata_count, strPacket.c_str());
            }
          if (msc_debug_parser) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
          }

          packet_size = mdata_count * (PACKET_SIZE - 1);

          if (sizeof(single_pkt)<packet_size)
          {
            return -111;
          }

          if (msc_debug_parser) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
          } // end of if (fMultisig)
          if (msc_debug_parser) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);

            // now decode mastercoin packets
            for (int m=0;m<mdata_count;m++)
            {
              if (msc_debug_parser) fprintf(mp_fp, "m=%d: %s\n", m, HexStr(packets[m], PACKET_SIZE + packets[m], false).c_str());

              // check to ensure the sequence numbers are sequential and begin with 01 !
              if (1+m != packets[m][0])
              {
                if (msc_debug_spec) fprintf(mp_fp, "Error: non-sequential seqnum ! expected=%d, got=%d\n", 1+m, packets[m][0]);
              }

              // now ignoring sequence numbers for Class B packets
              memcpy(m*(PACKET_SIZE-1)+single_pkt, 1+packets[m], PACKET_SIZE-1);
            }

  if (msc_debug_verbose) fprintf(mp_fp, "single_pkt: %s\n", HexStr(single_pkt, packet_size + single_pkt, false).c_str());

  mp_tx->Set(strSender, strReference, 0, wtx.GetHash(), nBlock, idx, (unsigned char *)&single_pkt, packet_size, fMultisig, (inAll-outAll));  

  return 0;
}


// parse blocks, potential right from Mastercoin's Exodus
int msc_initial_scan(int nHeight)
{
int n_total = 0, n_found = 0;
const int max_block = GetHeight();

  // this function is useless if there are not enough blocks in the blockchain yet!
  if ((0 >= nHeight) || (max_block < nHeight)) return -1;

  printf("starting block= %d, max_block= %d\n", nHeight, max_block);

  CBlock block;
  for (int blockNum = nHeight;blockNum<=max_block;blockNum++)
  {
    CBlockIndex* pblockindex = chainActive[blockNum];
    string strBlockHash = pblockindex->GetBlockHash().GetHex();

    if (msc_debug_exo) fprintf(mp_fp, "%s(%d; max=%d):%s, line %d, file: %s\n",
     __FUNCTION__, blockNum, max_block, strBlockHash.c_str(), __LINE__, __FILE__);

    ReadBlockFromDisk(block, pblockindex);

    int tx_count = 0;
    mastercore_handler_block_begin(blockNum, pblockindex);
    BOOST_FOREACH(const CTransaction&tx, block.vtx)
    {
      if (0 == mastercore_handler_tx(tx, blockNum, tx_count, pblockindex)) n_found++;

      ++tx_count;
    }
    
    n_total += tx_count;

    mastercore_handler_block_end(blockNum, pblockindex, n_found);
#ifdef  MY_DIV_HACK
//    if (20 < n_found) break;
#endif
  }

  printf("\n");
  for(map<string, CMPTally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it)
  {
    // my_it->first = key
    // my_it->second = value

    printf("%34s => ", (my_it->first).c_str());
    (my_it->second).print();
  }

  printf("starting block= %d, max_block= %d\n", nHeight, max_block);
  printf("n_total= %d, n_found= %d\n", n_total, n_found);

  return 0;
}

int input_msc_balances_string(const string &s)
{
  std::vector<std::string> addrData;
  boost::split(addrData, s, boost::is_any_of("="), token_compress_on);
  if (addrData.size() != 2) {
    return -1;
  }

  string strAddress = addrData[0];

  // split the tuples of currencies
  std::vector<std::string> vCurrencies;
  boost::split(vCurrencies, addrData[1], boost::is_any_of(";"), token_compress_on);

  std::vector<std::string>::const_iterator iter;
  for (iter = vCurrencies.begin(); iter != vCurrencies.end(); ++iter) {
    if ((*iter).empty()) {
      continue;
    }

    std::vector<std::string> curData;
    boost::split(curData, *iter, boost::is_any_of(","), token_compress_on);
    if (curData.size() < 1) {
      // malformed currency entry
       return -1;
    }

    size_t delimPos = curData[0].find(':');
    int currency = MASTERCOIN_CURRENCY_MSC;
    uint64_t balance = 0, sellReserved = 0, acceptReserved = 0;

    if (delimPos != curData[0].npos) {
      currency = atoi(curData[0].substr(0,delimPos));
      balance = boost::lexical_cast<boost::uint64_t>(curData[0].substr(delimPos + 1, curData[0].npos));
    } else {
      balance = boost::lexical_cast<boost::uint64_t>(curData[0]);
    }

    if (curData.size() >= 2) {
      sellReserved = boost::lexical_cast<boost::uint64_t>(curData[1]);
    }

    if (curData.size() >= 3) {
      acceptReserved = boost::lexical_cast<boost::uint64_t>(curData[2]);
    }

    if (balance == 0 && sellReserved == 0 && acceptReserved == 0) {
      continue;
    }

    if (balance) update_tally_map(strAddress, currency, balance, MONEY);
    if (sellReserved) update_tally_map(strAddress, currency, sellReserved, SELLOFFER_RESERVE);
    if (acceptReserved) update_tally_map(strAddress, currency, acceptReserved, ACCEPT_RESERVE);
  }

  return 1;
}

// seller-address, offer_block, amount, currency, desired BTC , fee, blocktimelimit
// 13z1JFtDMGTYQvtMq5gs4LmCztK3rmEZga,299076,76375000,1,6415500,10000,6
int input_mp_offers_string(const string &s)
{
  int offerBlock;
  uint64_t amountOriginal, btcDesired, minFee;
  unsigned int curr;
  unsigned char blocktimelimit;
  std::vector<std::string> vstr;
  boost::split(vstr, s, boost::is_any_of(" ,="), token_compress_on);
  string sellerAddr;
  string txidStr;
  int i = 0;

  if (8 != vstr.size()) return -1;

  sellerAddr = vstr[i++];
  offerBlock = atoi(vstr[i++]);
  amountOriginal = boost::lexical_cast<uint64_t>(vstr[i++]);
  curr = boost::lexical_cast<unsigned int>(vstr[i++]);
  btcDesired = boost::lexical_cast<uint64_t>(vstr[i++]);
  minFee = boost::lexical_cast<uint64_t>(vstr[i++]);
  blocktimelimit = atoi(vstr[i++]);
  txidStr = vstr[i++];

  const string combo = STR_SELLOFFER_ADDR_CURR_COMBO(sellerAddr);
  CMPOffer newOffer(offerBlock, amountOriginal, curr, btcDesired, minFee, blocktimelimit, uint256(txidStr));
  if (my_offers.insert(std::make_pair(combo, newOffer)).second) {
    return 0;
  } else {
    return -1;
  }

  return 0;
}

// seller-address, currency, buyer-address, amount, fee, block
// 13z1JFtDMGTYQvtMq5gs4LmCztK3rmEZga,1, 148EFCFXbk2LrUhEHDfs9y3A5dJ4tttKVd,100000,11000,299126
// 13z1JFtDMGTYQvtMq5gs4LmCztK3rmEZga,1,1Md8GwMtWpiobRnjRabMT98EW6Jh4rEUNy,50000000,11000,299132
int input_mp_accepts_string(const string &s)
{
  int nBlock;
  unsigned char blocktimelimit;
  std::vector<std::string> vstr;
  boost::split(vstr, s, boost::is_any_of(" ,="), token_compress_on);
  uint64_t amountRemaining, amountOriginal, offerOriginal, btcDesired;
  unsigned int curr;
  string sellerAddr, buyerAddr, txidStr;
  int i = 0;

  if (10 != vstr.size()) return -1;

  sellerAddr = vstr[i++];
  curr = boost::lexical_cast<unsigned int>(vstr[i++]);
  buyerAddr = vstr[i++];
  nBlock = atoi(vstr[i++]);
  amountRemaining = boost::lexical_cast<uint64_t>(vstr[i++]);
  amountOriginal = boost::lexical_cast<uint64_t>(vstr[i++]);
  blocktimelimit = atoi(vstr[i++]);
  offerOriginal = boost::lexical_cast<uint64_t>(vstr[i++]);
  btcDesired = boost::lexical_cast<uint64_t>(vstr[i++]);
  txidStr = vstr[i++];

  const string combo = STR_ACCEPT_ADDR_CURR_ADDR_COMBO(sellerAddr, buyerAddr);
  CMPAccept newAccept(amountOriginal, amountRemaining, nBlock, blocktimelimit, curr, offerOriginal, btcDesired, uint256(txidStr));
  if (my_accepts.insert(std::make_pair(combo, newAccept)).second) {
    return 0;
  } else {
    return -1;
  }
}

// exodus_prev
int input_globals_state_string(const string &s)
{
  uint64_t exodusPrev;
  unsigned int nextSPID, nextTestSPID;
  std::vector<std::string> vstr;
  boost::split(vstr, s, boost::is_any_of(" ,="), token_compress_on);
  if (3 != vstr.size()) return -1;

  int i = 0;
  exodusPrev = boost::lexical_cast<uint64_t>(vstr[i++]);
  nextSPID = boost::lexical_cast<unsigned int>(vstr[i++]);
  nextTestSPID = boost::lexical_cast<unsigned int>(vstr[i++]);

  exodus_prev = exodusPrev;
  _my_sps->init(nextSPID, nextTestSPID);
  return 0;
}

// addr,propertyId,nValue,currency_desired,deadline,early_bird,percentage,txid
int input_mp_crowdsale_string(const string &s)
{
  string sellerAddr;
  unsigned int propertyId;
  uint64_t nValue;
  unsigned int currency_desired;
  uint64_t deadline;
  unsigned char early_bird;
  unsigned char percentage;
  uint64_t u_created;
  uint64_t i_created;

  std::vector<std::string> vstr;
  boost::split(vstr, s, boost::is_any_of(" ,"), token_compress_on);
  unsigned int i = 0;

  if (9 > vstr.size()) return -1;

  sellerAddr = vstr[i++];
  propertyId = atoi(vstr[i++]);
  nValue = boost::lexical_cast<uint64_t>(vstr[i++]);
  currency_desired = atoi(vstr[i++]);
  deadline = boost::lexical_cast<uint64_t>(vstr[i++]);
  early_bird = (unsigned char)atoi(vstr[i++]);
  percentage = (unsigned char)atoi(vstr[i++]);
  u_created = boost::lexical_cast<uint64_t>(vstr[i++]);
  i_created = boost::lexical_cast<uint64_t>(vstr[i++]);

  CMPCrowd newCrowdsale(propertyId,nValue,currency_desired,deadline,early_bird,percentage,u_created,i_created);

  // load the remaining as database pairs
  while (i < vstr.size()) {
    std::vector<std::string> entryData;
    boost::split(entryData, vstr[i++], boost::is_any_of("="), token_compress_on);
    if ( 2 != entryData.size()) return -1;

    std::vector<std::string> valueData;
    boost::split(valueData, entryData[1], boost::is_any_of(";"), token_compress_on);

    std::vector<uint64_t> vals;
    std::vector<std::string>::const_iterator iter;
    for (iter = valueData.begin(); iter != valueData.end(); ++iter) {
      vals.push_back(boost::lexical_cast<uint64_t>(*iter));
    }

    newCrowdsale.insertDatabase(entryData[0], vals);
  }


  if (my_crowds.insert(std::make_pair(sellerAddr, newCrowdsale)).second) {
    return 0;
  } else {
    return -1;
  }

  return 0;
}


static int msc_file_load(const string &filename, int what, bool verifyHash = false)
{
  int lines = 0;
  int (*inputLineFunc)(const string &) = NULL;

  SHA256_CTX shaCtx;
  SHA256_Init(&shaCtx);

  switch (what)
  {
    case FILETYPE_BALANCES:
      mp_tally_map.clear();
      inputLineFunc = input_msc_balances_string;
      break;

    case FILETYPE_OFFERS:
      my_offers.clear();
      inputLineFunc = input_mp_offers_string;
      break;

    case FILETYPE_ACCEPTS:
      my_accepts.clear();
      inputLineFunc = input_mp_accepts_string;
      break;

    case FILETYPE_GLOBALS:
      inputLineFunc = input_globals_state_string;
      break;

    case FILETYPE_CROWDSALES:
      my_crowds.clear();
      inputLineFunc = input_mp_crowdsale_string;
      break;

    default:
      return -1;
  }

  if (msc_debug_persistence)
  {
    LogPrintf("Loading %s ... \n", filename);
    fprintf(mp_fp, "%s(%s), line %d, file: %s\n", __FUNCTION__, filename.c_str(), __LINE__, __FILE__);
  }

  ifstream file;
  file.open(filename.c_str());
  if (!file.is_open())
  {
    if (msc_debug_persistence) LogPrintf("%s(%s): file not found, line %d, file: %s\n", __FUNCTION__, filename.c_str(), __LINE__, __FILE__);
    return -1;
  }

  int res = 0;

  std::string fileHash;
  while (file.good())
  {
    std::string line;
    std::getline(file, line);
    if (line.empty() || line[0] == '#') continue;

    // remove \r if the file came from Windows
    line.erase( std::remove( line.begin(), line.end(), '\r' ), line.end() ) ;

    // record and skip hashes in the file
    if (line[0] == '!') {
      fileHash = line.substr(1);
      continue;
    }

    // update hash?
    if (verifyHash) {
      SHA256_Update(&shaCtx, line.c_str(), line.length());
    }

    if (inputLineFunc) {
      if (inputLineFunc(line) < 0) {
        res = -1;
        break;
      }
    }

    ++lines;
  }

  file.close();

  if (verifyHash && res == 0) {
    // generate and wite the double hash of all the contents written
    uint256 hash1;
    SHA256_Final((unsigned char*)&hash1, &shaCtx);
    uint256 hash2;
    SHA256((unsigned char*)&hash1, sizeof(hash1), (unsigned char*)&hash2);

    if (false == boost::iequals(hash2.ToString(), fileHash)) {
      fprintf(mp_fp, "File %s loaded, but failed hash validation!\n", filename.c_str());
      res = -1;
    }
  }

  fprintf(mp_fp, "%s(%s), loaded lines= %d\n", __FUNCTION__, filename.c_str(), lines);
  LogPrintf("%s(): file: %s , loaded lines= %d\n", __FUNCTION__, filename, lines);

  return res;
}

static char const * const statePrefix[NUM_FILETYPES] = {
    "balances",
    "offers",
    "accepts",
    "devmsc",
    "crowdsales",
};

// returns the height of the state loaded
static int load_most_relevant_state()
{
  int res = -1;
  // get the tip of the current best chain
  CBlockIndex const *curTip = chainActive.Tip();

  // walk backwards until we find a valid and full set of persisted state files
  while (NULL != curTip) {
    int success = -1;
    for (int i = 0; i < NUM_FILETYPES; ++i) {
      const string filename = (MPPersistencePath / (boost::format("%s-%s.dat") % statePrefix[i] % curTip->GetBlockHash().ToString()).str().c_str()).string();
      success = msc_file_load(filename, i, true);
      if (success < 0) {
        break;
      }
    }

    if (success >= 0) {
      res = curTip->nHeight;
      break;
    }

    // go to the previous block
    curTip = curTip->pprev;
  }

  // return the height of the block we settled at
  return res;
}

static int write_msc_balances(ofstream &file, SHA256_CTX *shaCtx)
{
  LOCK(cs_tally);

  map<string, CMPTally>::iterator iter;
  for (iter = mp_tally_map.begin(); iter != mp_tally_map.end(); ++iter) {
    bool emptyWallet = true;

    string lineOut = (*iter).first;
    lineOut.append("=");
    CMPTally &curAddr = (*iter).second;
    curAddr.init();
    unsigned int curr = 0;
    while (0 != (curr = curAddr.next())) {
      uint64_t balance = (*iter).second.getMoney(curr, MONEY);
      uint64_t sellReserved = (*iter).second.getMoney(curr, SELLOFFER_RESERVE);
      uint64_t acceptReserved = (*iter).second.getMoney(curr, ACCEPT_RESERVE);

      // we don't allow 0 balances to read in, so if we don't write them
      // it makes things match up better between persisted state and processed state
      if ( 0 == balance && 0 == sellReserved && 0 == acceptReserved ) {
        continue;
      }

      emptyWallet = false;

      lineOut.append((boost::format("%d:%d,%d,%d;")
          % curr
          % balance
          % sellReserved
          % acceptReserved).str());

    }

    if (false == emptyWallet) {
      // add the line to the hash
      SHA256_Update(shaCtx, lineOut.c_str(), lineOut.length());

      // write the line
      file << lineOut << endl;
    }
  }

  return 0;
}

static int write_mp_offers(ofstream &file, SHA256_CTX *shaCtx)
{
  map<string, CMPOffer>::const_iterator iter;
  for (iter = my_offers.begin(); iter != my_offers.end(); ++iter) {
    // decompose the key for address
    std::vector<std::string> vstr;
    boost::split(vstr, (*iter).first, boost::is_any_of("-"), token_compress_on);
    CMPOffer const &offer = (*iter).second;
    offer.saveOffer(file, shaCtx, vstr[0]);
  }


  return 0;
}

static int write_mp_accepts(ofstream &file, SHA256_CTX *shaCtx)
{
  map<string, CMPAccept>::const_iterator iter;
  for (iter = my_accepts.begin(); iter != my_accepts.end(); ++iter) {
    // decompose the key for address
    std::vector<std::string> vstr;
    boost::split(vstr, (*iter).first, boost::is_any_of("-+"), token_compress_on);
    CMPAccept const &accept = (*iter).second;
    accept.saveAccept(file, shaCtx, vstr[0], vstr[1]);
  }

  return 0;
}

static int write_globals_state(ofstream &file, SHA256_CTX *shaCtx)
{
  unsigned int nextSPID = _my_sps->peekNextSPID(MASTERCOIN_CURRENCY_MSC);
  unsigned int nextTestSPID = _my_sps->peekNextSPID(MASTERCOIN_CURRENCY_TMSC);
  string lineOut = (boost::format("%d,%d,%d")
    % exodus_prev
    % nextSPID
    % nextTestSPID
    ).str();

  // add the line to the hash
  SHA256_Update(shaCtx, lineOut.c_str(), lineOut.length());

  // write the line
  file << lineOut << endl;

  return 0;
}

static int write_mp_crowdsales(ofstream &file, SHA256_CTX *shaCtx)
{
  CrowdMap::const_iterator iter;
  for (iter = my_crowds.begin(); iter != my_crowds.end(); ++iter) {
    // decompose the key for address
    CMPCrowd const &crowd = (*iter).second;
    crowd.saveCrowdSale(file, shaCtx, (*iter).first);
  }

  return 0;
}


static int write_state_file( CBlockIndex const *pBlockIndex, int what )
{
  const char *blockHash = pBlockIndex->GetBlockHash().ToString().c_str();
  boost::filesystem::path balancePath = MPPersistencePath / (boost::format("%s-%s.dat") % statePrefix[what] % blockHash).str();
  ofstream file;
  file.open(balancePath.string().c_str());

  SHA256_CTX shaCtx;
  SHA256_Init(&shaCtx);

  int result = 0;

  switch(what) {
  case FILETYPE_BALANCES:
    result = write_msc_balances(file, &shaCtx);
    break;

  case FILETYPE_OFFERS:
    result = write_mp_offers(file, &shaCtx);
    break;

  case FILETYPE_ACCEPTS:
    result = write_mp_accepts(file, &shaCtx);
    break;

  case FILETYPE_GLOBALS:
    result = write_globals_state(file, &shaCtx);
    break;

  case FILETYPE_CROWDSALES:
      result = write_mp_crowdsales(file, &shaCtx);
      break;
  }

  // generate and wite the double hash of all the contents written
  uint256 hash1;
  SHA256_Final((unsigned char*)&hash1, &shaCtx);
  uint256 hash2;
  SHA256((unsigned char*)&hash1, sizeof(hash1), (unsigned char*)&hash2);
  file << "!" << hash2.ToString() << endl;

  file.flush();
  file.close();
  return result;
}

static bool is_state_prefix( std::string const &str )
{
  for (int i = 0; i < NUM_FILETYPES; ++i) {
    if (boost::equals(str,  statePrefix[i])) {
      return true;
    }
  }

  return false;
}

static void prune_state_files( CBlockIndex const *topIndex )
{
  // build a set of blockHashes for which we have any state files
  std::set<uint256> statefulBlockHashes;

  boost::filesystem::directory_iterator dIter(MPPersistencePath);
  boost::filesystem::directory_iterator endIter;
  for (; dIter != endIter; ++dIter) {
    std::string fName = dIter->path().empty() ? "<invalid>" : (*--dIter->path().end()).string();
    if (false == boost::filesystem::is_regular_file(dIter->status())) {
      // skip funny business
      fprintf(mp_fp, "Non-regular file found in persistence directory : %s\n", fName.c_str());
      continue;
    }

    std::vector<std::string> vstr;
    boost::split(vstr, fName, boost::is_any_of("-."), token_compress_on);
    if (  vstr.size() == 3 &&
          is_state_prefix(vstr[0]) &&
          boost::equals(vstr[2], "dat")) {
      uint256 blockHash;
      blockHash.SetHex(vstr[1]);
      statefulBlockHashes.insert(blockHash);
    } else {
      fprintf(mp_fp, "None state file found in persistence directory : %s\n", fName.c_str());
    }
  }

  // for each blockHash in the set, determine the distance from the given block
  std::set<uint256>::const_iterator iter;
  for (iter = statefulBlockHashes.begin(); iter != statefulBlockHashes.end(); ++iter) {
    // look up the CBlockIndex for height info
    CBlockIndex const *curIndex = NULL;
    map<uint256,CBlockIndex *>::const_iterator indexIter = mapBlockIndex.find((*iter));
    if (indexIter != mapBlockIndex.end()) {
      curIndex = (*indexIter).second;
    }

    // if we have nothing int the index, or this block is too old..
    if (NULL == curIndex || (topIndex->nHeight - curIndex->nHeight) > MAX_STATE_HISTORY ) {
     if (msc_debug_persistence)
     {
      if (curIndex) {
        fprintf(mp_fp, "State from Block:%s is no longer need, removing files (age-from-tip: %d)\n", (*iter).ToString().c_str(), topIndex->nHeight - curIndex->nHeight);
      } else {
        fprintf(mp_fp, "State from Block:%s is no longer need, removing files (not in index)\n", (*iter).ToString().c_str());
      }
     }

      // destroy the associated files!
      const char *blockHashStr = (*iter).ToString().c_str();
      for (int i = 0; i < NUM_FILETYPES; ++i) {
        boost::filesystem::remove(MPPersistencePath / (boost::format("%s-%s.dat") % statePrefix[i] % blockHashStr).str());
      }
    }
  }
}

int mastercore_save_state( CBlockIndex const *pBlockIndex )
{
  // write the new state as of the given block
  write_state_file(pBlockIndex, FILETYPE_BALANCES);
  write_state_file(pBlockIndex, FILETYPE_OFFERS);
  write_state_file(pBlockIndex, FILETYPE_ACCEPTS);
  write_state_file(pBlockIndex, FILETYPE_GLOBALS);
  write_state_file(pBlockIndex, FILETYPE_CROWDSALES);

  // clean-up the directory
  prune_state_files(pBlockIndex);

  return 0;
}

// called from init.cpp of Bitcoin Core
int mastercore_init()
{
  if (mastercoreInitialized) {
    // nothing to do
    return 0;
  }

  printf("%s()%s, line %d, file: %s\n", __FUNCTION__, isNonMainNet() ? "TESTNET":"", __LINE__, __FILE__);

#ifndef  DISABLE_LOG_FILE
  boost::filesystem::path pathTempLog = GetTempPath() / "mastercore.log";
  mp_fp = fopen(pathTempLog.string().c_str(), "a");
#else
  mp_fp = stdout;
#endif

  if (!mp_fp) mp_fp = stdout; // dump to terminal if file can't be opened

  fprintf(mp_fp, "\n%s MASTERCORE INIT, build date: " __DATE__ " " __TIME__ "\n\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()).c_str());

  if (isNonMainNet())
  {
    exodus = exodus_testnet;
  }
  //If interested in changing regtest address do so here and uncomment
  /*if (RegTest())
  {
    exodus = exodus_testnet;
  }*/

  p_txlistdb = new CMPTxList(GetDataDir() / "MP_txlist", 1<<20, false, fReindex);
  _my_sps = new CMPSPInfo(GetDataDir() / "MP_spinfo");
  MPPersistencePath = GetDataDir() / "MP_persist";
  boost::filesystem::create_directories(MPPersistencePath);

  // legacy code, setting to pre-genesis-block
  static int snapshotHeight = (GENESIS_BLOCK - 1);
  static const uint64_t snapshotDevMSC = 0;

  if (isNonMainNet()) snapshotHeight = START_TESTNET_BLOCK - 1;

  if (RegTest()) snapshotHeight = START_REGTEST_BLOCK - 1;

  ++mastercoreInitialized;

  if (readPersistence())
  {
    nWaterlineBlock = load_most_relevant_state();

    if (nWaterlineBlock < snapshotHeight)
    {
      nWaterlineBlock = snapshotHeight;
      exodus_prev=snapshotDevMSC;
    }

    // advance the waterline so that we start on the next unaccounted for block
    nWaterlineBlock += 1;
  }
  else
  {
  // my old way

    nWaterlineBlock = GENESIS_BLOCK - 1;  // the DEX block

    if (TestNet()) nWaterlineBlock = START_TESTNET_BLOCK; //testnet3

    if (RegTest()) nWaterlineBlock = START_REGTEST_BLOCK; //testnet3

#ifdef  MY_HACK
/*
    nWaterlineBlock = MSC_SP_BLOCK-3;
    nWaterlineBlock = MSC_DEX_BLOCK-3;
//    nWaterlineBlock = 296163 - 3; // bad Deadline
    nWaterlineBlock = MSC_SP_BLOCK-3;
    nWaterlineBlock = 292665;
    nWaterlineBlock = 303550;
    nWaterlineBlock = 303550;
    nWaterlineBlock = 308500;
    nWaterlineBlock = MSC_DEX_BLOCK-3;
*/

    update_tally_map(exodus, MASTERCOIN_CURRENCY_TMSC, COIN*5678, MONEY); // put some TMSC in, for my hack
    update_tally_map("1PVWtK1ATnvbRaRceLRH5xj8XV1LxUBu7n", MASTERCOIN_CURRENCY_MSC, COIN*123, MONEY);
    update_tally_map("1PVWtK1ATnvbRaRceLRH5xj8XV1LxUBu7n", MASTERCOIN_CURRENCY_TMSC, COIN*234, MONEY);
    update_tally_map("1PVWtK1ATnvbRaRceLRH5xj8XV1LxUBu7n", 0x80000009, COIN*345, MONEY);
    update_tally_map("1PVWtK1ATnvbRaRceLRH5xj8XV1LxUBu7n", 0x8000003F, COIN*345, MONEY);
    update_tally_map("1PVWtK1ATnvbRaRceLRH5xj8XV1LxUBu7n", 0x80000040, COIN*345, MONEY);
    update_tally_map("1MCHESTbJhJK27Ygqj4qKkx4Z4ZxhnP826", MASTERCOIN_CURRENCY_MSC, COIN*456, MONEY);
    update_tally_map("1MCHESTbJhJK27Ygqj4qKkx4Z4ZxhnP826", MASTERCOIN_CURRENCY_TMSC, COIN*567, MONEY);
    update_tally_map("1MCHESTxYkPSLoJ57WBQot7vz3xkNahkcb", MASTERCOIN_CURRENCY_MSC, COIN*678, MONEY);
    update_tally_map("1MCHESTxYkPSLoJ57WBQot7vz3xkNahkcb", MASTERCOIN_CURRENCY_TMSC, COIN*789, MONEY);
    update_tally_map("1MCHESTptvd2LnNp7wmr2sGTpRomteAkq8", 0x80000003, COIN*321, MONEY);
//    nWaterlineBlock = 304000;

    update_tally_map("1PfREWL44zJun1MLXkH64s88DSkPZXVxot", MASTERCOIN_CURRENCY_MSC, COIN*123, MONEY);
    update_tally_map("1PfREWL44zJun1MLXkH64s88DSkPZXVxot", MASTERCOIN_CURRENCY_TMSC, COIN*234, MONEY);
//    nWaterlineBlock = 310500;

    update_tally_map("18bAjW3tvSX8QK3XLdcApug71nNKmB4jnU", MASTERCOIN_CURRENCY_MSC, COIN*234, MONEY);
    update_tally_map("18bAjW3tvSX8QK3XLdcApug71nNKmB4jnU", MASTERCOIN_CURRENCY_TMSC, COIN*234, MONEY);

    update_tally_map("18bAjW3tvSX8QK3XLdcApug71nNKmB4jnU", 24, COIN*55555, MONEY);

    update_tally_map("1PRozi3UhpXtC4kZtPD1nfCFXJkXrV27Wp", MASTERCOIN_CURRENCY_MSC, COIN*234, MONEY);
    update_tally_map("1PRozi3UhpXtC4kZtPD1nfCFXJkXrV27Wp", MASTERCOIN_CURRENCY_TMSC, COIN*234, MONEY);
//    nWaterlineBlock = 310000;

    update_tally_map("mxaYwMv2Brbs7CW9r5aYuEr1jKTSDXg1TH", MASTERCOIN_CURRENCY_TMSC, COIN*234, MONEY);
    update_tally_map("mxaYwMv2Brbs7CW9r5aYuEr1jKTSDXg1TH", MASTERCOIN_CURRENCY_MSC, COIN*234, MONEY);
    update_tally_map("mxaYwMv2Brbs7CW9r5aYuEr1jKTSDXg1TH", 2147483652, COIN*234, MONEY);
    update_tally_map("mxaYwMv2Brbs7CW9r5aYuEr1jKTSDXg1TH", 2147483652, COIN*234, MONEY);

    update_tally_map("mfaiZGBkY4mBqt3PHPD2qWgbaafGa7vR64", MASTERCOIN_CURRENCY_TMSC, COIN*234, MONEY);
    update_tally_map("mfaiZGBkY4mBqt3PHPD2qWgbaafGa7vR64", MASTERCOIN_CURRENCY_MSC, COIN*234, MONEY);
    update_tally_map("mfaiZGBkY4mBqt3PHPD2qWgbaafGa7vR64", 2147483652, COIN*234, MONEY);
    update_tally_map("mfaiZGBkY4mBqt3PHPD2qWgbaafGa7vR64", 2147483652, COIN*234, MONEY);

//    if (isNonMainNet()) nWaterlineBlock = 272700;
#endif
  }

  // collect the real Exodus balances available at the snapshot time
  exodus_balance = getMPbalance(exodus, MASTERCOIN_CURRENCY_MSC, MONEY);
  printf("Exodus balance: %lu\n", exodus_balance);

  (void) msc_initial_scan(nWaterlineBlock);

  if (mp_fp) fflush(mp_fp);

  // display Exodus balance
  exodus_balance = getMPbalance(exodus, MASTERCOIN_CURRENCY_MSC, MONEY);
  printf("Exodus balance: %lu\n", exodus_balance);

  return 0;
}

int mastercore_shutdown()
{
  printf("%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);

  if (p_txlistdb)
  {
    delete p_txlistdb; p_txlistdb = NULL;
  }

  if (mp_fp)
  {
    fprintf(mp_fp, "\n%s MASTERCORE SHUTDOWN, build date: " __DATE__ " " __TIME__ "\n\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()).c_str());
#ifndef  DISABLE_LOG_FILE
    fclose(mp_fp);
#endif
    mp_fp = NULL;
  }

  return 0;
}

// this is called for every new transaction that comes in (actually in block parsing loop)
int mastercore_handler_tx(const CTransaction &tx, int nBlock, unsigned int idx, CBlockIndex const * pBlockIndex)
{
  if (!mastercoreInitialized) {
    mastercore_init();
  }

CMPTransaction mp_obj;
// save the augmented offer or accept amount into the database as well (expecting them to be numerically lower than that in the blockchain)
int interp_ret = -555555, pop_ret;

  if (nBlock < nWaterlineBlock) return -1;  // we do not care about parsing blocks prior to our waterline (empty blockchain defense)

  pop_ret = parseTransaction(false, tx, nBlock, idx, &mp_obj, pBlockIndex->GetBlockTime() );
  if (0 == pop_ret)
  {
  // true MP transaction, validity (such as insufficient funds, or offer not found) is determined elsewhere

    interp_ret = mp_obj.interpretPacket();
    if (interp_ret) fprintf(mp_fp, "!!! interpretPacket() returned %d !!!!!!!!!!!!!!!!!!!!!!\n", interp_ret);

    mp_obj.print();

    // TODO : this needs to be pulled into the refactored parsing engine since its validity is not know in this function !
    // FIXME: and of course only MP-related TXs will be recorded...
    if (!disableLevelDB)
    {
    bool bValid = (0 <= interp_ret);

      p_txlistdb->recordTX(tx.GetHash(), bValid, nBlock, mp_obj.getType(), mp_obj.getNewAmount());
    }
  }

  return interp_ret;
}

// IsMine wrapper to determine whether the address is in our local wallet
bool IsMyAddress(const std::string &address) 
{
  if (!pwalletMain) return false;

  const CBitcoinAddress& mscaddress = address;

  CTxDestination lookupaddress = mscaddress.Get(); 

  return (IsMine(*pwalletMain, lookupaddress));
}

// gets a label for a Bitcoin address from the wallet, mainly to the UI (used in demo)
string getLabel(const string &address)
{
CWallet *wallet = pwalletMain;

  if (wallet)
   {
        LOCK(wallet->cs_wallet);
        CBitcoinAddress address_parsed(address);
        std::map<CTxDestination, CAddressBookData>::iterator mi = wallet->mapAddressBook.find(address_parsed.Get());
        if (mi != wallet->mapAddressBook.end())
        {
            return (mi->second.name);
        }
    }

  return string();
}

//
// Determines minimum output amount to be spent by an output based on
// scriptPubKey size in relation to the minimum relay fee.
// 
// This method is very related with IsDust(nMinRelayTxFee) in core.h.
//
int64_t GetDustLimit(const CScript& scriptPubKey)
{
    // The total size is based on a typical scriptSig size of 148 byte,
    // 8 byte accounted for the size of output value and the serialized
    // size of scriptPubKey.
    size_t nSize = ::GetSerializeSize(scriptPubKey, SER_DISK, 0) + 156;

    // The minimum relay fee dictates a threshold value under which a
    // transaction won't be relayed.
    int64_t nRelayTxFee = CTransaction::nMinRelayTxFee;

    // A transaction is considered as "dust", if less than 1/3 of the
    // minimum fee required to relay a transaction is spent by one of
    // it's outputs. The minimum relay fee is defined per 1000 byte.
    int64_t nDustLimit = 1 + (((nSize * nRelayTxFee * 3) - 1) / 1000);

    return nDustLimit;
}

static int selectCoins(const string &FromAddress, CCoinControl &coinControl) {
  CWallet *wallet = pwalletMain;
  const int64_t n_max = (COIN * (20 * (0.0001))); // assume 20KBytes max TX size at 0.0001 per kilobyte
  // FUTURE: remove n_max and try 1st smallest input, then 2 smallest inputs etc. -- i.e. move Coin Control selection closer to CreateTransaction
  int64_t n_total = 0;  // total output funds collected
  LOCK(wallet->cs_wallet);
  {
    string sAddress = "";

    // iterate over the wallet
    for (map<uint256, CWalletTx>::iterator it = wallet->mapWallet.begin();
        it != wallet->mapWallet.end(); ++it) {
      const uint256& wtxid = it->first;
      const CWalletTx* pcoin = &(*it).second;
      bool bIsMine;
      bool bIsSpent;

      if (pcoin->IsTrusted()) {
        const int64_t nAvailable = pcoin->GetAvailableCredit();

        if (!nAvailable)
          continue;

        for (unsigned int i = 0; i < pcoin->vout.size(); i++) {
          CTxDestination dest;

          if (!ExtractDestination(pcoin->vout[i].scriptPubKey, dest))
            continue;

          bIsMine = IsMine(*wallet, dest);
          bIsSpent = wallet->IsSpent(wtxid, i);

          if (!bIsMine || bIsSpent)
            continue;

          int64_t n = bIsSpent ? 0 : pcoin->vout[i].nValue;

          sAddress = CBitcoinAddress(dest).ToString();
          if (msc_debug_send)
            fprintf(mp_fp,
                "%s:IsMine()=%s:IsSpent()=%s:%s: i=%d, nValue= %lu\n",
                sAddress.c_str(), bIsMine ? "yes" : "NO",
                bIsSpent ? "YES" : "no", wtxid.ToString().c_str(), i, n);

          // only use funds from the Sender's address for our MP transaction
          // TODO: may want to a little more selective here, i.e. use smallest possible (~0.1 BTC), but large amounts lead to faster confirmations !
          if (FromAddress == sAddress) {
            COutPoint outpt(wtxid, i);
            coinControl.Select(outpt);

            n_total += n;

            if (n_max <= n_total)
              break;
          }
        } // for pcoin end
      }

      if (n_max <= n_total)
        break;
    } // for iterate over the wallet end
  }

  return 0;
}

#define PUSH_BACK_BYTES(vector, value)\
    vector.insert(vector.end(), reinterpret_cast<unsigned char *>(&(value)), reinterpret_cast<unsigned char *>(&(value)) + sizeof((value)));

#define PUSH_BACK_BYTES_PTR(vector, ptr, size)\
    vector.insert(vector.end(), reinterpret_cast<unsigned char *>((ptr)), reinterpret_cast<unsigned char *>((ptr)) + (size));


//
// Do we care if this is true: pubkeys[i].IsCompressed() ???
// returns 0 if everything is OK, the transaction was sent
static int ClassB_send(const string &senderAddress, const string &receiverAddress, const string &redemptionAddress, const vector<unsigned char> &data, uint256 & txid)
{
CWallet *wallet = pwalletMain;
CCoinControl coinControl;
vector< pair<CScript, int64_t> > vecSend;

  // pick inputs for this transaction
  if (0 > selectCoins(senderAddress, coinControl))
  {
    return (CLASSB_SEND_ERROR -12);
  }

  txid = 0;

  // determine the redeeming public key for our multisig outputs
  // partially copied from _createmultisig()
  CBitcoinAddress address;
  CPubKey redeemingPubKey;
  if (false == redemptionAddress.empty()) {
    address.SetString(redemptionAddress);
  } else {
    address.SetString(senderAddress);
  }

  // validate that the redemption Address is good
  if (wallet && address.IsValid())
  {
    if (address.IsScript()) {

    } else {
      CKeyID keyID;

      if (!address.GetKeyID(keyID))
        return (CLASSB_SEND_ERROR -20);

      if (!wallet->GetPubKey(keyID, redeemingPubKey))
        return (CLASSB_SEND_ERROR -21);
      if (!redeemingPubKey.IsFullyValid())
        return (CLASSB_SEND_ERROR -22);

     }
  }
  else return (CLASSB_SEND_ERROR -23);

  int nRemainingBytes = data.size();
  int nNextByte = 0;
  unsigned char seqNum = 1;
  string strObfuscatedHashes[1+MAX_SHA256_OBFUSCATION_TIMES];
  prepareObfuscatedHashes(senderAddress, strObfuscatedHashes);

  while (nRemainingBytes > 0) {
    int nKeys = 1; //assume one key of data since we have data remaining
    if (nRemainingBytes > (PACKET_SIZE - 1)) {
      // we have enough data for 2 keys in this output
      nKeys += 1;
    }

    vector<CPubKey> keys;
    // always include the redeeming pubkey
    keys.push_back(redeemingPubKey);

    int i;
    for (i = 0; i < nKeys; i++)
    {
      // add sequence number
      vector<unsigned char> fakeKey;
      fakeKey.push_back(seqNum);

      // add up to 30 bytes of data
      int numBytes = nRemainingBytes < (PACKET_SIZE - 1) ? nRemainingBytes: (PACKET_SIZE - 1);
      fakeKey.insert(fakeKey.end(), data.begin() + nNextByte, data.begin() + nNextByte + numBytes);
      nNextByte += numBytes;
      nRemainingBytes -= numBytes;

      // pad to 31 total bytes with zeros
      while (fakeKey.size() < PACKET_SIZE) {
        fakeKey.push_back(0);
      }

      // xor in the obfuscation
      int j;
      vector<unsigned char>hash = ParseHex(strObfuscatedHashes[seqNum]);
      for (j = 0; j < PACKET_SIZE; j++) {
        fakeKey[j] = fakeKey[j] ^ hash[j];
      }

      // prepend the 2
      fakeKey.insert(fakeKey.begin(), 2);

      // fix up the ecdsa code point
      CPubKey pubKey;
      fakeKey.resize(33);
      unsigned char random_byte = (unsigned char)(GetRand(256));
      for (j = 0; i < 256 ; j++)
      {
        fakeKey[32] = random_byte;

        pubKey = CPubKey(fakeKey);
        printf("pubKey check: %s\n", (HexStr(pubKey.begin(), pubKey.end()).c_str()));

        if (pubKey.IsFullyValid()) break;

        ++random_byte; // cycle 256 times, if we must to find a valid ECDSA point
      }

      keys.push_back(pubKey);
      seqNum++;
    }

    CScript multisig_output;
    multisig_output.SetMultisig(1, keys);
    vecSend.push_back(make_pair(multisig_output, GetDustLimit(multisig_output)));
  }

  CWalletTx wtxNew;
  int64_t nFeeRet = 0;
  std::string strFailReason;
  CReserveKey reserveKey(wallet);

  CBitcoinAddress addr = CBitcoinAddress(senderAddress);  // change goes back to us
  coinControl.destChange = addr.Get();

  if (!wallet) return (CLASSB_SEND_ERROR -5);

  CScript scriptPubKey;

  // add the the reference/recepient/receiver ouput if needed
  if (!receiverAddress.empty())
  {
    // Send To Owners is the first use case where the receiver is empty
    scriptPubKey.SetDestination(CBitcoinAddress(receiverAddress).Get());
    vecSend.push_back(make_pair(scriptPubKey, GetDustLimit(scriptPubKey)));
  }

  // add the marker output
  scriptPubKey.SetDestination(CBitcoinAddress(exodus).Get());
  vecSend.push_back(make_pair(scriptPubKey, GetDustLimit(scriptPubKey)));

  // selected in the parent function, i.e.: ensure we are only using the address passed in as the Sender
  if (!coinControl.HasSelected()) return (CLASSB_SEND_ERROR -6);

  LOCK(wallet->cs_wallet);  // TODO: is this needed?

  // the fee will be computed by Bitcoin Core, need an override (?)
  // TODO: look at Bitcoin Core's global: nTransactionFee (?)
  if (!wallet->CreateTransaction(vecSend, wtxNew, reserveKey, nFeeRet, strFailReason, &coinControl)) return (CLASSB_SEND_ERROR -11);

  printf("%s():%s; nFeeRet = %lu, line %d, file: %s\n", __FUNCTION__, wtxNew.ToString().c_str(), nFeeRet, __LINE__, __FILE__);

  if (!wallet->CommitTransaction(wtxNew, reserveKey)) return (CLASSB_SEND_ERROR -13);

  txid = wtxNew.GetHash();

  return 0;
}

// WIP: expanding the function to a general-purpose one, but still sending 1 packet only for now (30-31 bytes)
static uint256 send_INTERNAL_1packet(const string &FromAddress, const string &ToAddress, const string &RedeemAddress, unsigned int CurrencyID, uint64_t Amount, unsigned int TransactionType, int *error_code = NULL)
{
const uint64_t nAvailable = getMPbalance(FromAddress, CurrencyID, MONEY);
int rc = -1;
uint256 txid = 0;

  if (msc_debug_send) fprintf(mp_fp, "%s(From: %s , To: %s , Currency= %u, Amount= %lu)\n",
   __FUNCTION__, FromAddress.c_str(), ToAddress.c_str(), CurrencyID, Amount);

  // make sure this address has enough MP currency available!
  if ((nAvailable < Amount) || (0 == Amount))
  {
    LogPrintf("%s(): aborted -- not enough MP currency (%lu < %lu)\n", __FUNCTION__, nAvailable, Amount);
    if (msc_debug_send) fprintf(mp_fp, "%s(): aborted -- not enough MP currency (%lu < %lu)\n", __FUNCTION__, nAvailable, Amount);
    ++InvalidCount_per_spec;

    if (error_code) *error_code = rc;

    return 0;
  }

  vector<unsigned char> data;
  swapByteOrder32(TransactionType);
  swapByteOrder32(CurrencyID);
  swapByteOrder64(Amount);

  PUSH_BACK_BYTES(data, TransactionType);
  PUSH_BACK_BYTES(data, CurrencyID);
  PUSH_BACK_BYTES(data, Amount);

  rc = ClassB_send(FromAddress, ToAddress, RedeemAddress, data, txid);
  if (msc_debug_send) fprintf(mp_fp, "ClassB_send returned %d\n", rc);

  if (error_code) *error_code = rc;

  return txid;
}

// send a MP transaction via RPC - simple send
Value send_MP(const Array& params, bool fHelp)
{
if (fHelp || params.size() < 4 || params.size() > 5)
        throw runtime_error(
            "send_MP\n"
            "\nCreates and broadcasts a simple send for a given amount and currency/property ID.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send from\n"
            "ToAddress     : the address to send to\n"
            "PropertyID    : the id of the smart property to send\n"
            "Amount        : the amount to send\n"
            "RedeemAddress : (optional) the address that can redeem the bitcoin outputs. Defaults to FromAddress\n"
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
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property ID");
  unsigned int propertyId = int(tmpPropertyId);

  CMPSPInfo::Entry sp;
  if (false == _my_sps->getSP(propertyId, sp)) {
    throw JSONRPCError(RPC_INVALID_PARAMETER, "Property ID does not exist");
  }

  bool divisible = false;
  divisible=sp.isDivisible();

//  printf("%s(), params3='%s' line %d, file: %s\n", __FUNCTION__, params[3].get_str().c_str(), __LINE__, __FILE__);

  string strAmount = params[3].get_str();
  int64_t Amount = 0;
  Amount = strToInt64(strAmount, divisible);

  if ((Amount > 9223372036854775807) || (0 >= Amount))
           throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");

  //some sanity checking of the data supplied?
  int code = 0;
  uint256 newTX = send_INTERNAL_1packet(FromAddress, ToAddress, RedeemAddress, propertyId, Amount, MSC_TYPE_SIMPLE_SEND, &code);

  if (0 != code) throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("error code= %i", code));

  //we need to do better than just returning a string of 0000000 here if we can't send the TX
  return newTX.GetHex();
}

// send a MP transaction via RPC - simple send
Value sendtoowners_MP(const Array& params, bool fHelp)
{
if (fHelp || params.size() < 3 || params.size() > 4)
        throw runtime_error(
            "sendtoowners_MP\n"
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
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property ID");

  unsigned int propertyId = int(tmpPropertyId);

  if (!isTestEcosystemProperty(propertyId)) // restrict usage to test eco only
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Send to owners restricted to test properties only in this build"); 

  CMPSPInfo::Entry sp;
  if (false == _my_sps->getSP(propertyId, sp)) {
    throw JSONRPCError(RPC_INVALID_PARAMETER, "Property ID does not exist");
  }

  bool divisible = false;
  divisible=sp.isDivisible();

//  printf("%s(), params3='%s' line %d, file: %s\n", __FUNCTION__, params[3].get_str().c_str(), __LINE__, __FILE__);

  string strAmount = params[2].get_str();
  int64_t Amount = 0;
  Amount = strToInt64(strAmount, divisible);

  if ((Amount > 9223372036854775807) || (0 >= Amount))
           throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");

//  printf("%s() %40.25lf, %lu, line %d, file: %s\n", __FUNCTION__, tmpAmount, Amount, __LINE__, __FILE__);

  //some sanity checking of the data supplied?
  int code = 0;
  uint256 newTX = send_INTERNAL_1packet(FromAddress, "", RedeemAddress, propertyId, Amount, MSC_TYPE_SEND_TO_OWNERS, &code);

  if (0 != code) throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("error code= %i", code));

  //we need to do better than just returning a string of 0000000 here if we can't send the TX
  return newTX.GetHex();
}

Value sendrawtx_MP(const Array& params, bool fHelp)
{
if (fHelp || params.size() < 3 || params.size() > 4)
        throw runtime_error(
            "sendrawtx_MP\n"
            "\nCreates and broadcasts a simple send for a given amount and currency/property ID.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send from\n"
            "ToAddress     : the address to send to.  This should be empty: (\"\") for transaction\n"
            "                types that do not use a reference/to address\n"
            "RawTX         : the hex-encoded raw transaction\n"
            "RedeemAddress : (optional) the address that can redeem the bitcoin outputs. Defaults to FromAddress\n"
            "\nResult:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">mastercored sendrawtx_MP 1FromAddress 1ToAddress <tx bytes hex> 1RedeemAddress\n"
        );

  std::string FromAddress = (params[0].get_str());
  std::string ToAddress = (params[1].get_str());
  std::string hexTransaction = (params[2].get_str());
  std::string RedeemAddress = (params.size() > 4) ? (params[4].get_str()): "";

  //some sanity checking of the data supplied?
  uint256 newTX;
  vector<unsigned char> data = ParseHex(hexTransaction);
  int rc = ClassB_send(FromAddress, ToAddress, RedeemAddress, data, newTX);

  if (0 != rc) throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("error code= %i", rc));

  //we need to do better than just returning a string of 0000000 here if we can't send the TX
  return newTX.GetHex();
}

// display an MP balance via RPC
Value getbalance_MP(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "getbalance_MP\n"
            "\nReturns the Master Protocol balance for a given address and currency/property.\n"
            "\nResult:\n"
            "n    (numeric) The applicable balance for address:currency/propertyID pair\n"
            "\nExamples:\n"
            ">mastercored getbalance_MP 1EXoDusjGwvnjZUyKkxZ4UHEf77z6A5S4P 1\n"
        );
    std::string address = params[0].get_str();
    int64_t tmpPropertyId = params[1].get_int64();
    if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property ID");

    unsigned int propertyId = int(tmpPropertyId);
    CMPSPInfo::Entry sp;
    if (false == _my_sps->getSP(propertyId, sp)) {
      throw JSONRPCError(RPC_INVALID_PARAMETER, "Property ID does not exist");
    }

    bool divisible = false;
    divisible=sp.isDivisible();

    Object balObj;

    int64_t tmpBalAvailable = getMPbalance(address, propertyId, MONEY);
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

int CMPTxList::getNumberOfPurchases(const uint256 txid)
{
    if (!pdb) return false;
    int numberOfPurchases = 0;
    std::vector<std::string> vstr;
    string strValue;
    Status status = pdb->Get(readoptions, txid.ToString(), &strValue);
    if (status.ok())
    {
        // parse the string returned
        boost::split(vstr, strValue, boost::is_any_of(":"), token_compress_on);
        // obtain the number of purchases
        if (4 <= vstr.size())
        {
            numberOfPurchases = atoi(vstr[3]);
        }
    }
    return numberOfPurchases;
}

bool CMPTxList::getPurchaseDetails(const uint256 txid, int purchaseNumber, string *buyer, string *seller, uint64_t *vout, uint64_t *propertyId, uint64_t *nValue)
{
    if (!pdb) return 0;
    std::vector<std::string> vstr;
    string strValue;
    Status status = pdb->Get(readoptions, txid.ToString()+"-"+to_string(purchaseNumber), &strValue);
    if (status.ok())
    {
        // parse the string returned
        boost::split(vstr, strValue, boost::is_any_of(":"), token_compress_on);
        // obtain the requisite details
        if (5 == vstr.size())
        {
            *vout = atoi(vstr[0]);
            *buyer = vstr[1];
            *seller = vstr[2];
            *propertyId = atoi(vstr[3]);
            *nValue = boost::lexical_cast<boost::uint64_t>(vstr[4]);;
            return true;
        }
    }
    return false;
}

void CMPTxList::recordPaymentTX(const uint256 &txid, bool fValid, int nBlock, unsigned int vout, unsigned int propertyId, uint64_t nValue, string buyer, string seller)
{
  if (!pdb) return;

       // Prep - setup vars
       unsigned int type = 99999999;
       uint64_t numberOfPayments = 1;
       unsigned int paymentNumber = 1;
       uint64_t existingNumberOfPayments = 0;

       // Step 1 - Check TXList to see if this payment TXID exists
       bool paymentEntryExists = p_txlistdb->exists(txid);

       // Step 2a - If doesn't exist leave number of payments & paymentNumber set to 1
       // Step 2b - If does exist add +1 to existing number of payments and set this paymentNumber as new numberOfPayments
       if (paymentEntryExists)
       {
           //retrieve old numberOfPayments
           std::vector<std::string> vstr;
           string strValue;
           Status status = pdb->Get(readoptions, txid.ToString(), &strValue);
           if (status.ok())
           {
               // parse the string returned
               boost::split(vstr, strValue, boost::is_any_of(":"), token_compress_on);

               // obtain the existing number of payments
               if (4 <= vstr.size())
               {
                   existingNumberOfPayments = atoi(vstr[3]);
                   paymentNumber = existingNumberOfPayments + 1;
                   numberOfPayments = existingNumberOfPayments + 1;
               }
           }
       }

       // Step 3 - Create new/update master record for payment tx in TXList
       const string key = txid.ToString();
       const string value = strprintf("%u:%d:%u:%lu", fValid ? 1:0, nBlock, type, numberOfPayments); 
       Status status;
       fprintf(mp_fp, "DEXPAYDEBUG : Writing master record %s(%s, valid=%s, block= %d, type= %d, number of payments= %lu)\n", __FUNCTION__, txid.ToString().c_str(), fValid ? "YES":"NO", nBlock, type, numberOfPayments);
       if (pdb)
       {
           status = pdb->Put(writeoptions, key, value);
           fprintf(mp_fp, "DEXPAYDEBUG : %s(): %s, line %d, file: %s\n", __FUNCTION__, status.ToString().c_str(), __LINE__, __FILE__);
       }

       // Step 4 - Write sub-record with payment details
       const string txidStr = txid.ToString();
       const string subKey = STR_PAYMENT_SUBKEY_TXID_PAYMENT_COMBO(txidStr);
       const string subValue = strprintf("%d:%s:%s:%d:%lu", vout, buyer, seller, propertyId, nValue);
       Status subStatus;
       fprintf(mp_fp, "DEXPAYDEBUG : Writing sub-record %s with value %s\n", subKey.c_str(), subValue.c_str());
       if (pdb)
       {
           subStatus = pdb->Put(writeoptions, subKey, subValue);
           fprintf(mp_fp, "DEXPAYDEBUG : %s(): %s, line %d, file: %s\n", __FUNCTION__, subStatus.ToString().c_str(), __LINE__, __FILE__);
       }
}

void CMPTxList::recordTX(const uint256 &txid, bool fValid, int nBlock, unsigned int type, uint64_t nValue)
{
  if (!pdb) return;

const string key = txid.ToString();
const string value = strprintf("%u:%d:%u:%lu", fValid ? 1:0, nBlock, type, nValue);
Status status;

  fprintf(mp_fp, "%s(%s, valid=%s, block= %d, type= %d, value= %lu)\n",
   __FUNCTION__, txid.ToString().c_str(), fValid ? "YES":"NO", nBlock, type, nValue);

  if (pdb)
  {
    status = pdb->Put(writeoptions, key, value);
    ++nWritten;
    if (msc_debug_txdb) fprintf(mp_fp, "%s(): %s, line %d, file: %s\n", __FUNCTION__, status.ToString().c_str(), __LINE__, __FILE__);
  }
}

bool CMPTxList::exists(const uint256 &txid)
{
  if (!pdb) return false;

string strValue;
Status status = pdb->Get(readoptions, txid.ToString(), &strValue);

  if (!status.ok())
  {
    if (status.IsNotFound()) return false;
  }

  return true;
}

bool CMPTxList::getTX(const uint256 &txid, string &value)
{
Status status = pdb->Get(readoptions, txid.ToString(), &value);

  ++nRead;

  if (status.ok())
  {
    return true;
  }

  return false;
}

void CMPTxList::printStats()
{
  fprintf(mp_fp, "CMPTxList stats: nWritten= %d , nRead= %d\n", nWritten, nRead);
}

void CMPTxList::printAll()
{
int count = 0;
Slice skey, svalue;

  readoptions.fill_cache = false;

  Iterator* it = pdb->NewIterator(readoptions);

  for(it->SeekToFirst(); it->Valid(); it->Next())
  {
    skey = it->key();
    svalue = it->value();
    ++count;
    printf("entry #%8d= %s:%s\n", count, skey.ToString().c_str(), svalue.ToString().c_str());
  }

  delete it;
}

// figure out if there was at least 1 Master Protocol transaction within the block range, or a block if starting equals ending
// block numbers are inclusive
// pass in bDeleteFound = true to erase each entry found within the block range
bool CMPTxList::isMPinBlockRange(int starting_block, int ending_block, bool bDeleteFound)
{
leveldb::Slice skey, svalue;
unsigned int count = 0;
std::vector<std::string> vstr;
int block;
unsigned int n_found = 0;

  leveldb::Iterator* it = pdb->NewIterator(iteroptions);

  for(it->SeekToFirst(); it->Valid(); it->Next())
  {
    skey = it->key();
    svalue = it->value();

    ++count;

//    printf("%5u:%s=%s\n", count, skey.ToString().c_str(), svalue.ToString().c_str());

    string strvalue = it->value().ToString();

    // parse the string returned, find the validity flag/bit & other parameters
    boost::split(vstr, strvalue, boost::is_any_of(":"), token_compress_on);

    // only care about the block number/height here
    if (2 <= vstr.size())
    {
      block = atoi(vstr[1]);

      if ((starting_block <= block) && (block <= ending_block))
      {
        ++n_found;
        if (bDeleteFound) pdb->Delete(writeoptions, skey);
      }
    }
  }

  printf("%s(%d, %d); n_found= %d, line %d, file: %s\n", __FUNCTION__, starting_block, ending_block, n_found, __LINE__, __FILE__);

  delete it;

  return (n_found);
}

// global wrapper, block numbers are inclusive, if ending_block is 0 top of the chain will be used
bool isMPinBlockRange(int starting_block, int ending_block, bool bDeleteFound)
{
  if (!p_txlistdb) return false;

  if (0 == ending_block) ending_block = GetHeight(); // will scan 'til the end

  return p_txlistdb->isMPinBlockRange(starting_block, ending_block, bDeleteFound);
}

// call it like so (variable # of parameters):
// int block = 0;
// ...
// uint64_t nNew = 0;
//
// if (getValidMPTX(txid, &block, &type, &nNew)) // if true -- the TX is a valid MP TX
//
bool getValidMPTX(const uint256 &txid, int *block = NULL, unsigned int *type = NULL, uint64_t *nAmended = NULL)
{
string result;
int validity = 0;

  if (msc_debug_txdb) fprintf(mp_fp, "%s()\n", __FUNCTION__);

  if (!p_txlistdb) return false;

  if (!p_txlistdb->getTX(txid, result)) return false;

  // parse the string returned, find the validity flag/bit & other parameters
  std::vector<std::string> vstr;
  boost::split(vstr, result, boost::is_any_of(":"), token_compress_on);

  fprintf(mp_fp, "%s() size=%lu : %s\n", __FUNCTION__, vstr.size(), result.c_str());

  if (1 <= vstr.size()) validity = atoi(vstr[0]);

  if (block)
  {
    if (2 <= vstr.size()) *block = atoi(vstr[1]);
    else *block = 0;
  }

  if (type)
  {
    if (3 <= vstr.size()) *type = atoi(vstr[2]);
    else *type = 0;
  }

  if (nAmended)
  {
    if (4 <= vstr.size()) *nAmended = boost::lexical_cast<boost::uint64_t>(vstr[3]);
    else nAmended = 0;
  }

  p_txlistdb->printStats();

  if ((int)0 == validity) return false;

  return true;
}

Value gettransaction_MP(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "gettransaction_MP \"txid\"\n"
            "\nGet detailed information about in-wallet transaction <txid>\n"
            "\nArguments:\n"
            "1. \"txid\"    (string, required) The transaction id\n"
            "\nResult:\n"
            "{\n"
            "  \"amount\" : x.xxx,        (string) The transaction amount in btc\n"
            "  \"confirmations\" : n,     (numeric) The number of confirmations\n"
            "  \"blockhash\" : \"hash\",  (string) The block hash\n"
            "  \"blockindex\" : xx,       (numeric) The block index\n"
            "  \"blocktime\" : ttt,       (numeric) The time in seconds since epoch (1 Jan 1970 GMT)\n"
            "  \"txid\" : \"transactionid\",   (string) The transaction id, see also https://blockchain.info/tx/[transactionid]\n"
            "  \"time\" : ttt,            (numeric) The transaction time in seconds since epoch (1 Jan 1970 GMT)\n"
            "  \"timereceived\" : ttt,    (numeric) The time received in seconds since epoch (1 Jan 1970 GMT)\n"
            "  \"details\" : [\n"
            "    {\n"
            "      \"account\" : \"accountname\",  (string) The account name involved in the transaction, can be \"\" for the default account.\n"
            "      \"address\" : \"bitcoinaddress\",   (string) The bitcoin address involved in the transaction\n"
            "      \"category\" : \"send|receive\",    (string) The category, either 'send' or 'receive'\n"
            "      \"amount\" : x.xxx                  (string) The amount in btc\n"
            "    }\n"
            "    ,...\n"
            "  ],\n"
            "  \"hex\" : \"data\"         (string) Raw data for transaction\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("gettransaction_MP", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
            + HelpExampleRpc("gettransaction_MP", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
        );

    uint256 hash;
    hash.SetHex(params[0].get_str());

    Object txobj;

    CTransaction wtx;
    uint256 blockHash = 0;
    if (!GetTransaction(hash, wtx, blockHash, true))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

    // here begins
    CMPTransaction mp_obj;

                uint256 wtxid = wtx.GetHash();
                bool bIsMine;
                bool isMPTx = false;
                int nFee = 0;
                string MPTxType;
                unsigned int MPTxTypeInt;
                string selectedAddress;
                string senderAddress;
                string refAddress;
                bool valid;
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

                if ((0 == blockHash) || (NULL == mapBlockIndex[blockHash]))
                        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Exception: blockHash is 0");
                CBlockIndex* pBlockIndex = mapBlockIndex[blockHash];
                if (NULL == pBlockIndex)
                        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Exception: pBlockIndex is NULL");
                int blockHeight = pBlockIndex->nHeight;
                int confirmations =  1 + GetHeight() - pBlockIndex->nHeight;
                int64_t blockTime = mapBlockIndex[blockHash]->nTime; 

                mp_obj.SetNull();
                CMPOffer temp_offer;
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
                            Object txobj;
                            txobj.push_back(Pair("txid", wtxid.GetHex()));
                            txobj.push_back(Pair("confirmations", confirmations));
                            txobj.push_back(Pair("blocktime", blockTime));
                            txobj.push_back(Pair("type", "DEx Purchase"));
                            // always min one subrecord, grab sender from first sub so we can display in parent as per Adam feedback
                            string tmpBuyer;
                            string tmpSeller;
                            uint64_t tmpVout;
                            uint64_t tmpNValue;
                            uint64_t tmpPropertyId;
                            p_txlistdb->getPurchaseDetails(wtxid,1,&tmpBuyer,&tmpSeller,&tmpVout,&tmpPropertyId,&tmpNValue);
                            txobj.push_back(Pair("sendingaddress", tmpBuyer));
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
                            txobj.push_back(Pair("purchases", purchases));
                            // return the object
                            return txobj;
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
                                isMPTx = true;
                                nFee = mp_obj.getFeePaid();

                                int tmpblock=0;
                                uint32_t tmptype=0;
                                uint64_t amountNew=0;
                                valid=getValidMPTX(wtxid, &tmpblock, &tmptype, &amountNew);
                                //populate based on type of tx
                                switch (MPTxTypeInt)
                                {
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
                                          propertyId = _my_sps->findSPByTX(wtxid); // propertyId of created property (if valid)
                                          amount = 0; // issuance of a managed token does not create tokens
                                     break;
                                     case MSC_TYPE_SIMPLE_SEND:
                                          if (0 == mp_obj.step2_Value())
                                          {
                                               propertyId = mp_obj.getCurrency();
                                               amount = mp_obj.getAmount();
                                               showReference = true;
                                               //check crowdsale invest?
                                               crowdPurchase = isCrowdsalePurchase(wtxid, refAddress, &crowdPropertyId, &crowdTokens, &issuerTokens);
                                               if (crowdPurchase)
                                               {
                                                  MPTxType = "Crowdsale Purchase";
                                                  CMPSPInfo::Entry sp;
                                                  if (false == _my_sps->getSP(crowdPropertyId, sp)) {
                                                       throw JSONRPCError(RPC_INVALID_PARAMETER, "Exception: Crowdsale Purchase but Property ID does not exist");
                                                  }
                                                  crowdName = sp.name;
                                                  crowdDivisible = sp.isDivisible();
                                               } 
                                          }
                                     break;
                                     case MSC_TYPE_TRADE_OFFER:
                                          if (0 == mp_obj.step2_Value())
                                          {
                                               propertyId = mp_obj.getCurrency();
                                               amount = mp_obj.getAmount();
                                          }
                                          if (0 <= mp_obj.interpretPacket(&temp_offer))
                                          {
                                               sell_minfee = temp_offer.getMinFee();
                                               sell_timelimit = temp_offer.getBlockTimeLimit();
                                               sell_subaction = temp_offer.getSubaction();
                                               sell_btcdesired = temp_offer.getBTCDesiredOriginal();
                                          }
                                          if ((valid) && (amountNew>0)) amount=amountNew; //amount has been amended, update
                                     break;
                                     case MSC_TYPE_ACCEPT_OFFER_BTC:
                                          if (0 == mp_obj.step2_Value())
                                          {
                                               propertyId = mp_obj.getCurrency();
                                               amount = mp_obj.getAmount();
                                               showReference = true;
                                          }
                                          if ((valid) && (amountNew>0)) amount=amountNew; //amount has been amended, update
                                     break;
                                     case MSC_TYPE_CLOSE_CROWDSALE:
                                          if (0 == mp_obj.step2_Value())
                                          {
                                               propertyId = mp_obj.getCurrency();
                                          }
                                     break;
                                     case  MSC_TYPE_SEND_TO_OWNERS:
                                          if (0 == mp_obj.step2_Value())
                                          {
                                               propertyId = mp_obj.getCurrency();
                                               amount = mp_obj.getAmount();
                                          }
                                     break;
                                } // end switch 
                            divisible=isPropertyDivisible(propertyId);
                            }
                        }
                    }
                    else
                    {
                        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Not a Master Protocol transaction but TX exists in levelDB.  This may be a bug, please report to the developers.");
                    }
                }
                else
                {
                        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Not a Master Protocol transaction");
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
                        txobj.push_back(Pair("txid", wtxid.GetHex()));
                        txobj.push_back(Pair("sendingaddress", senderAddress));
                        if (showReference) txobj.push_back(Pair("referenceaddress", refAddress));
                        txobj.push_back(Pair("ismine", bIsMine));
                        txobj.push_back(Pair("confirmations", confirmations));
                        txobj.push_back(Pair("fee", ValueFromAmount(nFee)));
                        txobj.push_back(Pair("blocktime", blockTime));
                        txobj.push_back(Pair("type", MPTxType));
                        txobj.push_back(Pair("propertyid", propertyId));
                        if ((MSC_TYPE_CREATE_PROPERTY_VARIABLE == MPTxTypeInt) || (MSC_TYPE_CREATE_PROPERTY_FIXED == MPTxTypeInt)) txobj.push_back(Pair("propertyname", propertyName));
                        txobj.push_back(Pair("divisible", divisible));
                        if (divisible)
                        {
                                txobj.push_back(Pair("amount", FormatDivisibleMP(amount))); //divisible, format w/ bitcoins VFA func
                        }
                        else
                        {
                                txobj.push_back(Pair("amount", FormatIndivisibleMP(amount))); //indivisible, push raw 64
                        }
                        if (crowdPurchase)
                        {
                                txobj.push_back(Pair("purchasedpropertyid", crowdPropertyId));
                                txobj.push_back(Pair("purchasedpropertyname", crowdName));
                                if (crowdDivisible)
                                {
                                     txobj.push_back(Pair("purchasedtokens", FormatDivisibleMP(crowdTokens))); //divisible, format w/ bitcoins VFA func
                                     txobj.push_back(Pair("issuertokens", FormatDivisibleMP(issuerTokens)));
                                }
                                else
                                {
                                     txobj.push_back(Pair("purchasedtokens", FormatIndivisibleMP(crowdTokens))); //indivisible, push raw 64
                                     txobj.push_back(Pair("issuertokens", FormatIndivisibleMP(issuerTokens)));
                                }
                        }
                        if (MSC_TYPE_TRADE_OFFER == MPTxTypeInt)
                        {
                        txobj.push_back(Pair("feerequired", ValueFromAmount(sell_minfee)));
                        txobj.push_back(Pair("timelimit", sell_timelimit));
                        //for now must mark all v0 sells as new until we find a way to store a subaction for v0 sells
                        if ((1 == sell_subaction) || ((0 == sell_subaction) && (0 < amount))) txobj.push_back(Pair("subaction", "New"));
                        if (2 == sell_subaction) txobj.push_back(Pair("subaction", "Update"));
                        if ((3 == sell_subaction) || ((0 == sell_subaction) && (0 == amount))) txobj.push_back(Pair("subaction", "Cancel"));
                        txobj.push_back(Pair("bitcoindesired", ValueFromAmount(sell_btcdesired)));
                        }
                        txobj.push_back(Pair("valid", valid));
                }
    return txobj;
}

Value listtransactions_MP(const Array& params, bool fHelp)
{
CWallet *wallet = pwalletMain;
string sAddress = "";

    if (fHelp || params.size() > 5)
        throw runtime_error(
            "listtransactions_MP\n" //todo increase verbosity in help
            "\nList wallet transactions filtered on counts and block boundaries\n"
            + HelpExampleCli("listtransactions_MP", "")
            + HelpExampleRpc("listtransactions_MP", "")
        );

        int64_t nCount = 10;
        if (params.size() > 0) nCount = params[0].get_int64();
        int64_t nFrom = 0;
        if (params.size() > 1) nFrom = params[1].get_int64();
        int64_t nStartBlock = 0;
        if (params.size() > 2) nStartBlock = params[2].get_int64();
        int64_t nEndBlock = 999999;
        if (params.size() > 3) nEndBlock = params[3].get_int64();

        if (nCount < 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
        if (nFrom < 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative from");
        if (nStartBlock < 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative start block");
        if (nEndBlock < 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative end block");

        Array response; //prep an array to hold our output
        CMPTransaction mp_obj;

        LOCK(wallet->cs_wallet);
        // rewrite to use original listtransactions methodology from core
        std::list<CAccountingEntry> acentries;
        CWallet::TxItems txOrdered = pwalletMain->OrderedTxItems(acentries, "*");

        // iterate backwards until we have nCount items to return:
        for (CWallet::TxItems::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
        {
            CWalletTx *const pwtx = (*it).second.first;
            if (pwtx != 0)
            {
                uint256 wtxid = pwtx->GetHash();
                bool isMPTx = false;
                unsigned int MPTxTypeInt;
                string MPTxType;
                string selectedAddress;
                string senderAddress;
                string refAddress;
                bool valid;
                uint64_t propertyId = 0;  //using 64 instead of 32 here as json::sprint chokes on 32 - research why
                bool divisible;
                bool showReference = false;
                uint64_t amount = 0;
                string result;
                int confirmations = pwtx->GetDepthInMainChain(); //what about conflicted (<0)? how will we display these?
                uint256 blockHash = pwtx->hashBlock;
                if ((0 == blockHash) || (NULL == mapBlockIndex[blockHash])) continue;
                CBlockIndex* pBlockIndex = mapBlockIndex[blockHash];
                if (NULL == pBlockIndex) continue;
                int blockHeight = pBlockIndex->nHeight;
                int64_t blockTime = mapBlockIndex[pwtx->hashBlock]->nTime;
                int blockIndex = pwtx->nIndex;

                //ignore transactions not between nStartBlock and nEndBlock
                if ((blockHeight < nStartBlock) || (blockHeight > nEndBlock)) continue;

                bool crowdPurchase = false;
                int64_t crowdPropertyId = 0;
                int64_t crowdTokens = 0;
                bool crowdDivisible = false;
                string crowdName;

                mp_obj.SetNull();
                CMPOffer temp_offer;

                //rather than attempt to parse every transaction in the wallet again looking for MP messages let's look at levelDB in the first instance
                //this should provide a huge speedup for the example provided where the wallet holds 10,000 or 100,000 bitcoin transactions and only one or two
                //MP messages, meaning we would go through parsing every TX in the wallet looking for our default return (10) MP messages causing a delayed RPC response

                if (p_txlistdb->exists(wtxid))
                {
                    if (0 == parseTransaction(true, *pwtx, blockHeight, 0, &mp_obj))
                    {
                        // OK, a valid MP transaction so far
                        if (0<=mp_obj.step1())
                        {
                                MPTxType = mp_obj.getTypeString();
                                MPTxTypeInt = mp_obj.getType();
                                senderAddress = mp_obj.getSender();
                                refAddress = mp_obj.getReceiver();
                                isMPTx = true;

                                int tmpblock=0;
                                uint32_t tmptype=0;
                                uint64_t amountNew=0;
                                valid=getValidMPTX(wtxid, &tmpblock, &tmptype, &amountNew);

                                //populate based on type of tx
                                switch (MPTxTypeInt)
                                {
                                     case MSC_TYPE_CREATE_PROPERTY_FIXED:
                                          propertyId = _my_sps->findSPByTX(wtxid); // propertyId of created property (if valid)
                                          amount = getTotalTokens(propertyId);
                                     break;
                                     case MSC_TYPE_CREATE_PROPERTY_VARIABLE:
                                          propertyId = _my_sps->findSPByTX(wtxid); // propertyId of created property (if valid)
                                          amount = 0; // crowdsale txs always create zero tokens
                                     break;
                                     case MSC_TYPE_CREATE_PROPERTY_MANUAL:
                                          propertyId = _my_sps->findSPByTX(wtxid); // propertyId of created property (if valid)
                                          amount = 0; // issuance of a managed token does not create tokens
                                     break;
                                     case MSC_TYPE_SIMPLE_SEND:
                                          if (0 == mp_obj.step2_Value())
                                          {
                                               propertyId = mp_obj.getCurrency();
                                               amount = mp_obj.getAmount();
                                               showReference = true;
                                               //check crowdsale invest?
                                               crowdPurchase = isCrowdsalePurchase(wtxid, refAddress, &crowdPropertyId, &crowdTokens);
                                               if (crowdPurchase)
                                               {
                                                  MPTxType = "Crowdsale Purchase";
                                                  CMPSPInfo::Entry sp;
                                                  if (false == _my_sps->getSP(crowdPropertyId, sp)) {
                                                       throw JSONRPCError(RPC_INVALID_PARAMETER, "Exception: Crowdsale Purchase but Property ID does not exist");
                                                  }
                                                  crowdName = sp.name;
                                                  crowdDivisible = sp.isDivisible();
                                               }
                                          }
                                     break;
                                     case MSC_TYPE_TRADE_OFFER:
                                          if (0 == mp_obj.step2_Value())
                                          {
                                               propertyId = mp_obj.getCurrency();
                                               amount = mp_obj.getAmount();
                                          }
                                          if ((valid) && (amountNew>0)) amount=amountNew; //amount has been amended, update
                                     break;
                                     case MSC_TYPE_ACCEPT_OFFER_BTC:
                                          if (0 == mp_obj.step2_Value())
                                          {
                                               propertyId = mp_obj.getCurrency();
                                               amount = mp_obj.getAmount();
                                               showReference = true;
                                          }
                                          if ((valid) && (amountNew>0)) amount=amountNew; //amount has been amended, update
                                     break;
                                     case MSC_TYPE_CLOSE_CROWDSALE:
                                          propertyId = 0; // propertyId of Crowdsale Close
                                     break;
                                     case  MSC_TYPE_SEND_TO_OWNERS:
                                          if (0 == mp_obj.step2_Value())
                                          {
                                               propertyId = mp_obj.getCurrency();
                                               amount = mp_obj.getAmount();
                                          }
                                     break;

                                }
                                divisible=isPropertyDivisible(propertyId);
                          }
                     }
                }

                // is this a MP transaction? switched to parsing rather than leveldb at Michael's request
                if (isMPTx)
                {
                                // add the transaction object to the array
                                Object txobj;
                                txobj.push_back(Pair("txid", wtxid.GetHex()));
                                txobj.push_back(Pair("sendingaddress", senderAddress));
                                if (showReference) txobj.push_back(Pair("referenceaddress", refAddress));
                                txobj.push_back(Pair("confirmations", confirmations));
                                txobj.push_back(Pair("blocktime", blockTime));
                                txobj.push_back(Pair("blockindex", blockIndex));
                                txobj.push_back(Pair("type", MPTxType));
                                txobj.push_back(Pair("propertyid", propertyId));
                                txobj.push_back(Pair("divisible", divisible));
                                if (divisible)
                                {
                                        txobj.push_back(Pair("amount", FormatDivisibleMP(amount))); //divisible, format w/ bitcoins VFA func
                                }
                                else
                                {
                                        txobj.push_back(Pair("amount", amount)); //indivisible, push raw 64
                                }
                                if (crowdPurchase)
                                {
                                    txobj.push_back(Pair("purchasedpropertyid", crowdPropertyId));
                                    txobj.push_back(Pair("purchasedpropertyname", crowdName));
                                    if (crowdDivisible)
                                    {
                                        txobj.push_back(Pair("purchasedtokens", FormatDivisibleMP(crowdTokens))); //divisible, format w/ bitcoins VFA func
                                    }
                                    else
                                    {
                                        txobj.push_back(Pair("purchasedtokens", crowdTokens)); //indivisible, push raw 64
                                    }
                                }
                                txobj.push_back(Pair("valid", valid));
                                response.push_back(txobj);
                }
            }
            if ((int)response.size() >= (nCount+nFrom)) break; //don't burn time doing more work than we need to
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

    std::reverse(response.begin(), response.end()); // Return oldest to newest
    return response;   // return response array for JSON serialization
}

Value searchtransactions_MP(const Array& params, bool fHelp)
{
CWallet *wallet = pwalletMain;
string sAddress = "";
string addressParam = "";
bool addressFilter;

    if (fHelp || params.size() > 5)
        throw runtime_error(
            "searchtransactions_MP\n" //todo increase verbosity in help
            "\nSearch wallet history for transactions filtered on address, counts and block boundaries\n"
            + HelpExampleCli("searchtransactions_MP", "")
            + HelpExampleRpc("searchtransactions_MP", "")
        );

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
        int64_t nFrom = 0;
        if (params.size() > 2) nFrom = params[2].get_int64();
        int64_t nStartBlock = 0;
        if (params.size() > 3) nStartBlock = params[3].get_int64();
        int64_t nEndBlock = 999999;
        if (params.size() > 4) nEndBlock = params[4].get_int64();

        if (nCount < 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
        if (nFrom < 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative from");
        if (nStartBlock < 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative start block");
        if (nEndBlock < 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative end block");

        Array response; //prep an array to hold our output
        CMPTransaction mp_obj;

        LOCK(wallet->cs_wallet);
        // rewrite to use original listtransactions methodology from core
        std::list<CAccountingEntry> acentries;
        CWallet::TxItems txOrdered = pwalletMain->OrderedTxItems(acentries, "*");

        // iterate backwards until we have nCount items to return:
        for (CWallet::TxItems::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
        {
            CWalletTx *const pwtx = (*it).second.first;
            if (pwtx != 0)
            {
                uint256 wtxid = pwtx->GetHash();
                bool bIsMine;
                bool isMPTx = false;
                unsigned int MPTxTypeInt;
                string MPTxType;
                string selectedAddress;
                string senderAddress;
                string refAddress;
                bool valid;
                uint64_t propertyId = 0;  //using 64 instead of 32 here as json::sprint chokes on 32 - research why
                bool divisible;
                bool showReference = false;
                uint64_t amount = 0;
                string result;
                bool outgoingTransaction = false;
                int confirmations = pwtx->GetDepthInMainChain(); //what about conflicted (<0)? how will we display these?
                uint256 blockHash = pwtx->hashBlock;
                if ((0 == blockHash) || (NULL == mapBlockIndex[blockHash])) continue;
                CBlockIndex* pBlockIndex = mapBlockIndex[blockHash];
                if (NULL == pBlockIndex) continue;
                int blockHeight = pBlockIndex->nHeight;
                int64_t blockTime = mapBlockIndex[pwtx->hashBlock]->nTime;
                int blockIndex = pwtx->nIndex;

                //ignore transactions not between nStartBlock and nEndBlock
                if ((blockHeight < nStartBlock) || (blockHeight > nEndBlock)) continue;

                bool crowdPurchase = false;
                int64_t crowdPropertyId = 0;
                int64_t crowdTokens = 0;
                bool crowdDivisible = false;
                string crowdName;

                mp_obj.SetNull();
                CMPOffer temp_offer;

                //rather than attempt to parse every transaction in the wallet again looking for MP messages let's look at levelDB in the first instance
                //this should provide a huge speedup for the example provided where the wallet holds 10,000 or 100,000 bitcoin transactions and only one or two
                //MP messages, meaning we would go through parsing every TX in the wallet looking for our default return (10) MP messages causing a delayed RPC response

                if (p_txlistdb->exists(wtxid))
                {
                    if (0 == parseTransaction(true, *pwtx, blockHeight, 0, &mp_obj))
                    {
                        // OK, a valid MP transaction so far
                        if (0<=mp_obj.step1())
                        {
                                MPTxType = mp_obj.getTypeString();
                                MPTxTypeInt = mp_obj.getType();
                                senderAddress = mp_obj.getSender();
                                refAddress = mp_obj.getReceiver();
                                isMPTx = true;

                                int tmpblock=0;
                                uint32_t tmptype=0;
                                uint64_t amountNew=0;
                                valid=getValidMPTX(wtxid, &tmpblock, &tmptype, &amountNew);

                                //populate based on type of tx
                                switch (MPTxTypeInt)
                                {
                                     case MSC_TYPE_CREATE_PROPERTY_FIXED:
                                          propertyId = _my_sps->findSPByTX(wtxid); // propertyId of created property (if valid)
                                          amount = getTotalTokens(propertyId);
                                     break;
                                     case MSC_TYPE_CREATE_PROPERTY_VARIABLE:
                                          propertyId = _my_sps->findSPByTX(wtxid); // propertyId of created property (if valid)
                                          amount = 0; // crowdsale txs always create zero tokens
                                     break;
                                     case MSC_TYPE_CREATE_PROPERTY_MANUAL:
                                          propertyId = _my_sps->findSPByTX(wtxid); // propertyId of created property (if valid)
                                          amount = 0; // issuance of a managed token does not create tokens
                                     break;
                                     case MSC_TYPE_SIMPLE_SEND:
                                          if (0 == mp_obj.step2_Value())
                                          {
                                               propertyId = mp_obj.getCurrency();
                                               amount = mp_obj.getAmount();
                                               showReference = true;
                                               //check crowdsale invest?
                                               crowdPurchase = isCrowdsalePurchase(wtxid, refAddress, &crowdPropertyId, &crowdTokens);
                                               if (crowdPurchase)
                                               {
                                                  MPTxType = "Crowdsale Purchase";
                                                  CMPSPInfo::Entry sp;
                                                  if (false == _my_sps->getSP(crowdPropertyId, sp)) {
                                                       throw JSONRPCError(RPC_INVALID_PARAMETER, "Exception: Crowdsale Purchase but Property ID does not exist");
                                                  }
                                                  crowdName = sp.name;
                                                  crowdDivisible = sp.isDivisible();
                                               }
                                          }
                                     break;
                                     case MSC_TYPE_TRADE_OFFER:
                                          if (0 == mp_obj.step2_Value())
                                          {
                                               propertyId = mp_obj.getCurrency();
                                               amount = mp_obj.getAmount();
                                          }
                                          if ((valid) && (amountNew>0)) amount=amountNew; //amount has been amended, update
                                     break;
                                     case MSC_TYPE_ACCEPT_OFFER_BTC:
                                          if (0 == mp_obj.step2_Value())
                                          {
                                               propertyId = mp_obj.getCurrency();
                                               amount = mp_obj.getAmount();
                                               showReference = true;
                                          }
                                          if ((valid) && (amountNew>0)) amount=amountNew; //amount has been amended, update
                                     break;
                                     case MSC_TYPE_CLOSE_CROWDSALE:
                                          propertyId = 0; // propertyId of Crowdsale Close
                                     break;
                                     case  MSC_TYPE_SEND_TO_OWNERS:
                                          if (0 == mp_obj.step2_Value())
                                          {
                                               propertyId = mp_obj.getCurrency();
                                               amount = mp_obj.getAmount();
                                          }
                                     break;

                                }
                                divisible=isPropertyDivisible(propertyId);
                          }
                     }
                }

                // is this a MP transaction? switched to parsing rather than leveldb at Michael's request
                if (isMPTx)
                {
                        // test sender and reference against ismine to determine which address is ours
                        // if both ours (eg sending to another address in wallet) use reference
                        bIsMine = IsMyAddress(senderAddress);
                        if (bIsMine)
                        {
                                selectedAddress=senderAddress;
                                outgoingTransaction=true;
                        }
                        bIsMine = IsMyAddress(refAddress);
                        if (bIsMine)
                        {
                                selectedAddress=refAddress;
                        }

                        // are we filtering by address, if so compare
                        if ((!addressFilter) || (senderAddress == addressParam) || (refAddress == addressParam))
                        {
                                // add the transaction object to the array
                                Object txobj;
                                txobj.push_back(Pair("txid", wtxid.GetHex()));
                                txobj.push_back(Pair("sendingaddress", senderAddress));
                                if (showReference) txobj.push_back(Pair("referenceaddress", refAddress));
                                if (outgoingTransaction)
                                {
                                        txobj.push_back(Pair("direction", "out"));
                                }
                                else
                                {
                                        txobj.push_back(Pair("direction", "in"));
                                }

                                txobj.push_back(Pair("confirmations", confirmations));
                                txobj.push_back(Pair("blocktime", blockTime));
                                txobj.push_back(Pair("blockindex", blockIndex));
                                txobj.push_back(Pair("type", MPTxType));
                                txobj.push_back(Pair("propertyid", propertyId));
                                txobj.push_back(Pair("divisible", divisible));
                                if (divisible)
                                {
                                        txobj.push_back(Pair("amount", FormatDivisibleMP(amount))); //divisible, format w/ bitcoins VFA func
                                }
                                else
                                {
                                        txobj.push_back(Pair("amount", FormatIndivisibleMP(amount))); //indivisible, push raw 64
                                }
                                if (crowdPurchase)
                                {
                                    txobj.push_back(Pair("purchasedpropertyid", crowdPropertyId));
                                    txobj.push_back(Pair("purchasedpropertyname", crowdName));
                                    if (crowdDivisible)
                                    {
                                        txobj.push_back(Pair("purchasedtokens", FormatDivisibleMP(crowdTokens))); //divisible, format w/ bitcoins VFA func
                                    }
                                    else
                                    {
                                        txobj.push_back(Pair("purchasedtokens", FormatIndivisibleMP(crowdTokens))); //indivisible, push raw 64
                                    }
                                }
                                txobj.push_back(Pair("valid", valid));
                                response.push_back(txobj);
                        }
                }
            }
            if ((int)response.size() >= (nCount+nFrom)) break; //don't burn time doing more work than we need to
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

    std::reverse(response.begin(), response.end()); // Return oldest to newest
    return response;   // return response array for JSON serialization
}

Value getallbalancesforid_MP(const Array& params, bool fHelp)
{
   if (fHelp || params.size() != 1)
        throw runtime_error(
            "getallbalancesforid_MP currencyID\n"
            "\nGet a list of address balances for a given currency/property ID\n"
            "\nArguments:\n"
            "1. currencyID    (int, required) The currency/property ID\n"
            "\nResult:\n"
            "{\n"
            "  \"address\" : 1Address,        (string) The address\n"
            "  \"balance\" : x.xxx,     (string) The available balance of the address\n"
            "  \"reserved\" : x.xxx,   (string) The amount reserved by sell offers and accepts\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("getallbalancesforid_MP", "1")
            + HelpExampleRpc("getallbalancesforid_MP", "1")
        );

    int64_t tmpPropertyId = params[0].get_int64();
    if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property ID");

    unsigned int propertyId = int(tmpPropertyId);
    CMPSPInfo::Entry sp;
    if (false == _my_sps->getSP(propertyId, sp)) {
      throw JSONRPCError(RPC_INVALID_PARAMETER, "Property ID does not exist");
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

        int64_t tmpBalAvailable = getMPbalance(address, propertyId, MONEY);
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
            "getallbalancesforaddress_MP address\n"
            "\nGet a list of all balances for a given address\n"
            "\nArguments:\n"
            "1. currencyID    (int, required) The currency/property ID\n"
            "\nResult:\n"
            "{\n"
            "  \"propertyid\" : x,        (numeric) the property id\n"
            "  \"balance\" : x.xxx,     (string) The available balance of the address\n"
            "  \"reserved\" : x.xxx,   (string) The amount reserved by sell offers and accepts\n"
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

            int64_t tmpBalAvailable = getMPbalance(address, propertyId, MONEY);
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
            "getproperty_MP propertyID\n"
            "\nGet details for a property ID\n"
            "\nArguments:\n"
            "1. propertyID    (int, required) The property ID\n"
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
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property ID");

    unsigned int propertyId = int(tmpPropertyId);
    CMPSPInfo::Entry sp;
    if (false == _my_sps->getSP(propertyId, sp)) {
      throw JSONRPCError(RPC_INVALID_PARAMETER, "Property ID does not exist");
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
            "\nList smart properties\n"
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
            "getcrowdsale_MP propertyID\n"
            "\nGet crowdsale info for a property ID\n"
            "\nArguments:\n"
            "1. propertyID    (int, required) The property ID\n"
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
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property ID");

    unsigned int propertyId = int(tmpPropertyId);
    CMPSPInfo::Entry sp;
    if (false == _my_sps->getSP(propertyId, sp)) {
      throw JSONRPCError(RPC_INVALID_PARAMETER, "Property ID does not exist");
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
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Exception: blockHash is 0");

    Object response;

    bool active = false;
    active = isCrowdsaleActive(propertyId);
    bool divisible = false;
    divisible=sp.isDivisible();
    string propertyName = sp.name;
    int64_t startTime = mapBlockIndex[hashBlock]->nTime;
    int64_t deadline = sp.deadline;
    int8_t earlyBonus = sp.early_bird;
    int8_t percentToIssuer = sp.percentage;
    bool closeEarly = sp.close_early;
    bool maxTokens = sp.max_tokens;
    int64_t timeClosed = sp.timeclosed;
    string txidClosed = sp.txid_close.GetHex();
    string issuer = sp.issuer;
    int64_t amountRaised = 0;
    int64_t tokensIssued = getTotalTokens(propertyId);
    int64_t tokensPerUnit = sp.num_tokens;
    int64_t propertyIdDesired = sp.currency_desired;
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
                  throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Exception: crowdsale is flagged active but cannot be found in CrowdMap");
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
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Desired property ID does not exist");
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
    }
    else
    {
        response.push_back(Pair("tokensissued", FormatIndivisibleMP(tokensIssued)));
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
            "\nGet active crowdsales\n"
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
                  throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Exception: blockHash is 0");

              bool divisible = false;
              divisible=sp.isDivisible();
              string propertyName = sp.name;
              int64_t startTime = mapBlockIndex[hashBlock]->nTime;
              int64_t deadline = sp.deadline;
              int8_t earlyBonus = sp.early_bird;
              int8_t percentToIssuer = sp.percentage;
              string issuer = sp.issuer;
              int64_t tokensPerUnit = sp.num_tokens;
              int64_t propertyIdDesired = sp.currency_desired;

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
            "getgrants_MP propertyID\n"
            "\nGet grant/revoke info for a property ID\n"
            "\nArguments:\n"
            "1. propertyID    (int, required) The property ID\n"
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
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property ID");

    unsigned int propertyId = int(tmpPropertyId);
    CMPSPInfo::Entry sp;
    if (false == _my_sps->getSP(propertyId, sp)) {
      throw JSONRPCError(RPC_INVALID_PARAMETER, "Property ID does not exist");
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
        throw runtime_error("Cannot display MP transactions for a non-existent block.");

  // next let's obtain the block for this height
  CBlockIndex* mpBlockIndex = chainActive[blockHeight];
  CBlock mpBlock;

  // now let's read this block in from disk so we can loop its transactions
  if(!ReadBlockFromDisk(mpBlock, mpBlockIndex))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Internal Error: Failed to read block from disk");

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

Value getactivedexsells_MP(const Array& params, bool fHelp)
{
   if (fHelp)
        throw runtime_error(
            "getactivedexsells_MP\n"
            "\nGet currently active distributed exchange sell offers\n"
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

      for(map<string, CMPOffer>::iterator it = my_offers.begin(); it != my_offers.end(); ++it)
      {
          CMPOffer selloffer = it->second;
          string sellCombo = it->first;
          string seller = sellCombo.substr(0, sellCombo.size()-2);

          //filtering
          if ((addressFilter) && (seller != addressParam)) continue;

          uint256 sellHash = selloffer.getHash();
          string txid = sellHash.GetHex();
          uint64_t propertyId = selloffer.getCurrency();
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
          for(map<string, CMPAccept>::iterator ait = my_accepts.begin(); ait != my_accepts.end(); ++ait)
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
      for(MetaDExMap::iterator it = metadex.begin(); it != metadex.end(); ++it)
      {
        // it->first = key
        // it->second = value
        printf("%s = %s\n", (it->first).c_str(), (it->second).ToString().c_str());
      }
      break;
  }

  return GetHeight();
}

std::string CScript::mscore_parse(std::vector<std::string>&msc_parsed, bool bNoBypass) const
{
    int count = 0;
    std::string str;
    opcodetype opcode;
    std::vector<unsigned char> vch;
    const_iterator pc = begin();
    while (pc < end())
    {
        if (!str.empty())
        {
            str += "\n";
        }
        if (!GetOp(pc, opcode, vch))
        {
            str += "[error]";
            return str;
        }
        if (0 <= opcode && opcode <= OP_PUSHDATA4)
        {
            str += ValueString(vch);
            if (count || bNoBypass) msc_parsed.push_back(ValueString(vch));
            count++;
        }
        else
        {
            str += GetOpName(opcode);
        }
    }
    return str;
}

int mastercore_handler_disc_begin(int nBlockNow, CBlockIndex const * pBlockIndex) { return 0; }

int mastercore_handler_disc_end(int nBlockNow, CBlockIndex const * pBlockIndex) {
    printf("\n BLOCK DISCONNECTED: blockinfo %s \n", pBlockIndex->ToString().c_str() );

    //delete entry from MP_txlist
    bool foundMPTX = p_txlistdb->isMPinBlockRange(pBlockIndex->nHeight, pBlockIndex->nHeight, true);
    if( foundMPTX ) {
      printf("\n  MProtocol TX was found in orphaned block, please remove ~/.bitcoin/MP_* and restart your client. \n");
      fprintf(mp_fp,"\n  MProtocol TX was found in orphaned block, please remove ~/.bitcoin/MP_* and restart your client. \n");
      AbortNode("\n  MProtocol TX was found in orphaned block, please remove ~/.bitcoin/MP_* and restart your client. \n");
    }
    
    return 0;
}

