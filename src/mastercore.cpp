//
// first & so far only Master protocol source file
// WARNING: Work In Progress -- major refactoring will be occurring often
//
// I am adding comments to aid with navigation and overall understanding of the design.
// this is the 'core' portion of the node+wallet: mastercored
// see 'qt' subdirectory for UI files
//
// for the Sprint -- search for: TODO, FIXME, consensus
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

// comment out MY_DIV_HACK & others here - used for Unit Testing only !
// #define MY_DIV_HACK
// #define DISABLE_LOG_FILE 

static FILE *mp_fp = NULL;

#include "mastercore.h"

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;
using namespace leveldb;

int nWaterlineBlock = 0;  //

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
int msc_debug_verbose=0;
int msc_debug_vin   = 0;
int msc_debug_script= 0;
int msc_debug_dex   = 0;
int msc_debug_send  = 1;
int msc_debug_spec  = 1;
int msc_debug_exo   = 1;
int msc_debug_tally = 1;
int msc_debug_sp    = 1;
int msc_debug_sto   = 1;
int msc_debug_txdb  = 0;
int msc_debug_persistence = 0;

// follow this variable through the code to see how/which Master Protocol transactions get invalidated
static int InvalidCount_per_spec = 0; // consolidate error messages into a nice log, for now just keep a count
static int BitcoinCore_errors = 0;    // TODO: watch this count, check returns of all/most Bitcoin core functions !

static int disable_Divs = 0;

static int disableLevelDB = 0;

#ifdef  MY_DIV_HACK
static int disable_Persistence = 1;
#else
static int disable_Persistence = 0;
#endif

static int mastercoreInitialized = 0;

// this is the internal format for the offer primary key (TODO: replace by a class method)
#define STR_SELLOFFER_ADDR_CURR_COMBO(x) ( x + "-" + strprintf("%d", curr))
#define STR_ACCEPT_ADDR_CURR_ADDR_COMBO( _seller , _buyer ) ( _seller + "-" + strprintf("%d", curr) + "+" + _buyer)

static CMPTxList *p_txlistdb;

// a copy from main.cpp -- unfortunately that one is in a private namespace
static int GetHeight()
{
    LOCK(cs_main);
    return chainActive.Height();
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
    case MSC_TYPE_CLOSE_CROWDSALE: return ((char *)"Close Crowsale");

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

void swapByteOrder16(unsigned short& us)
{
    us = (us >> 8) |
         (us << 8);
}

void swapByteOrder32(unsigned int& ui)
{
    ui = (ui >> 24) |
         ((ui<<8) & 0x00FF0000) |
         ((ui>>8) & 0x0000FF00) |
         (ui << 24);
}

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

    // other information
    uint256 txid;
    uint256 creation_block;
    uint256 update_block;
    bool fixed;


    std::map<std::string, std::vector<uint64_t> > txFundraiserData; // schema is 'txid:amtSent:deadlineUnix:userIssuedTokens:IssuerIssuedTokens;'
    
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
    , txid()
    , creation_block()
    , update_block()
    , fixed(false)
    , txFundraiserData()
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
      if (false == fixed) {
        spInfo.push_back(Pair("currency_desired", (uint64_t)currency_desired));
        spInfo.push_back(Pair("deadline", (boost::format("%d") % deadline).str()));
        spInfo.push_back(Pair("early_bird", (int)early_bird));
        spInfo.push_back(Pair("percentage", (int)percentage));
      }

      //Initialize values
      std::map<std::string, std::vector<uint64_t> >::const_iterator it;
      
      std::string values_long = "";
      std::string values = "";

      //fprintf(mp_fp,"\ncrowdsale started to save, size of db %ld", database.size());

      //Iterate through fundraiser data, serializing it with txid:val:val:val:val;
      for(it = txFundraiserData.begin(); it != txFundraiserData.end(); it++) {
         values += it->first.c_str();
         values += ":" + boost::lexical_cast<std::string>(it->second.at(0));
         values += ":" + boost::lexical_cast<std::string>(it->second.at(1));
         values += ":" + boost::lexical_cast<std::string>(it->second.at(2));
         values += ":" + boost::lexical_cast<std::string>(it->second.at(3));
         values += ";";
         values_long += values;
      }
      //fprintf(mp_fp,"\ncrowdsale saved %s", values_long.c_str());

      spInfo.push_back(Pair("txFundraiserData", values_long)); 
      spInfo.push_back(Pair("txid", (boost::format("%s") % txid.ToString()).str()));
      spInfo.push_back(Pair("creation_block", (boost::format("%s") % creation_block.ToString()).str()));
      spInfo.push_back(Pair("update_block", (boost::format("%s") % update_block.ToString()).str()));

      return spInfo;
    }

    void fromJSON(Object const &json)
    {
      int idx = 0;
      issuer = json[idx++].value_.get_str();
      prop_type = (unsigned short)json[idx++].value_.get_int();
      prev_prop_id = (unsigned int)json[idx++].value_.get_uint64();
      category = json[idx++].value_.get_str();
      subcategory = json[idx++].value_.get_str();
      name = json[idx++].value_.get_str();
      url = json[idx++].value_.get_str();
      data = json[idx++].value_.get_str();
      fixed = json[idx++].value_.get_bool();
      num_tokens = boost::lexical_cast<uint64_t>(json[idx++].value_.get_str());
      if (false == fixed) {
        currency_desired = (unsigned int)json[idx++].value_.get_uint64();
        deadline = boost::lexical_cast<uint64_t>(json[idx++].value_.get_str());
        early_bird = (unsigned char)json[idx++].value_.get_int();
        percentage = (unsigned char)json[idx++].value_.get_int();
      }

      //reconstruct database
      std::string longstr = json[idx++].value_.get_str();
      
      //fprintf(mp_fp,"\nDESERIALIZE GO ----> %s" ,longstr.c_str() );
      
      std::vector<std::string> strngs_vec;
      
      //split serialized form up
      boost::split(strngs_vec, longstr, boost::is_any_of(";"));

      //fprintf(mp_fp,"\nDATABASE PRE-DESERIALIZE SUCCESS, %ld, %s" ,strngs_vec.size(), strngs_vec[0].c_str());
      
      //Go through and deserialize the database
      for(std::vector<std::string>::size_type i = 0; i != strngs_vec.size(); i++) {
        
        std::vector<std::string> str_split_vec;
        boost::split(str_split_vec, strngs_vec[i], boost::is_any_of(":"));

        if ( str_split_vec.size() == 5) {
          //fprintf(mp_fp,"\n Deserialized values: %s, %s %s %s %s", str_split_vec.at(0).c_str(), str_split_vec.at(1).c_str(), str_split_vec.at(2).c_str(), str_split_vec.at(3).c_str(), str_split_vec.at(4).c_str());
          uint64_t txdata[] = { 
            boost::lexical_cast<uint64_t>( str_split_vec.at(1) ), 
            boost::lexical_cast<uint64_t>( str_split_vec.at(2) ), 
            boost::lexical_cast<uint64_t>( str_split_vec.at(3) ), 
            boost::lexical_cast<uint64_t>( str_split_vec.at(4) )
          };
          
          std::vector<uint64_t> txDataVec(txdata, txdata + sizeof(txdata)/sizeof(txdata[0]) );
          txFundraiserData.insert(std::make_pair( str_split_vec.at(0), txDataVec ) ) ;
        }
      }
      //fprintf(mp_fp,"\nDATABASE DESERIALIZE SUCCESS %lu", database.size());
      txid = uint256(json[idx++].value_.get_str());
      creation_block = uint256(json[idx++].value_.get_str());
      update_block = uint256(json[idx++].value_.get_str());
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
  boost::filesystem::path const path;

  // implied version of msc and tmsc so they don't hit the leveldb
  Entry implied_msc;
  Entry implied_tmsc;

  unsigned int next_spid;
  unsigned int next_test_spid;

  void openDB() {
    leveldb::Options options;
    options.paranoid_checks = true;
    options.create_if_missing = true;

    leveldb::Status s = leveldb::DB::Open(options, path.string(), &pDb);

     if (false == s.ok()) {
       printf("Failed to create or read LevelDB for Smart Property at %s", path.c_str());
     }
  }

  void closeDB() {
    delete pDb;
    pDb = NULL;
  }

public:

  CMPSPInfo(const boost::filesystem::path &_path)
    : path(_path)
  {
    openDB();

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
    closeDB();
  }

  void init(unsigned int nextSPID = 0x3UL, unsigned int nextTestSPID = TEST_ECO_PROPERTY_1)
  {
    next_spid = nextSPID;
    next_test_spid = nextTestSPID;
  }

  void clear() {
    closeDB();
    leveldb::DestroyDB(path.string(), leveldb::Options());
    openDB();
    init();
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
    // cannot update implied SP
    if (MASTERCOIN_CURRENCY_MSC == propertyID || MASTERCOIN_CURRENCY_TMSC == propertyID) {
      return propertyID;
    }

    std::string nextIdStr;
    unsigned int res = propertyID;

    Object spInfo = info.toJSON();

    // generate the SP id
    string spKey = (boost::format("sp-%d") % propertyID).str();
    string spValue = write_string(Value(spInfo), false);
    string spPrevKey = (boost::format("blk-%s:sp-%d") % info.update_block.ToString() % propertyID).str();
    string spPrevValue;

    leveldb::WriteBatch commitBatch;
    
    leveldb::ReadOptions readOpts;
    readOpts.fill_cache = false;
    leveldb::WriteOptions writeOptions;
    writeOptions.sync = true;

    // if a value exists move it to the old key
    if (false == pDb->Get(readOpts, spKey, &spPrevValue).IsNotFound()) {
      commitBatch.Put(spPrevKey, spPrevValue);
    }
    commitBatch.Put(spKey, spValue);
    pDb->Write(writeOptions, &commitBatch);

    fprintf(mp_fp,"\nUpdated LevelDB with SP data successfully\n");
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
    bool res = (iter->Valid() && iter->key().compare(spKey) == 0);
    // clean up the iterator
    delete iter;

    return res;
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

  int popBlock(uint256 const &block_hash)
  {
    int res = 0;
    int remainingSPs = 0;
    leveldb::WriteBatch commitBatch;

    leveldb::ReadOptions readOpts;
    readOpts.fill_cache = false;
    leveldb::Iterator *iter = pDb->NewIterator(readOpts);
    for (iter->Seek("sp-"); iter->Valid() && iter->key().starts_with("sp-"); iter->Next()) {
      // parse the encoded json, failing if it doesnt parse or is an object
      Value spInfoVal;
      if (read_string(iter->value().ToString(), spInfoVal) && spInfoVal.type() == obj_type ) {
        Entry info;
        info.fromJSON(spInfoVal.get_obj());
        if (info.update_block == block_hash) {
          // need to roll this SP back
          if (info.update_block == info.creation_block) {
            // this is the block that created this SP, so delete the SP and the tx index entry
            string txIndexKey = (boost::format(FORMAT_BOOST_TXINDEXKEY) % info.txid.ToString() ).str();
            commitBatch.Delete(iter->key());
            commitBatch.Delete(txIndexKey);
          } else {
            std::vector<std::string> vstr;
            std::string key = iter->key().ToString();
            boost::split(vstr, key, boost::is_any_of("-"), token_compress_on);
            unsigned int propertyID = boost::lexical_cast<unsigned int>(vstr[1]);

            string spPrevKey = (boost::format("blk-%s:sp-%d") % info.update_block.ToString() % propertyID).str();
            string spPrevValue;

            if (false == pDb->Get(readOpts, spPrevKey, &spPrevValue).IsNotFound()) {
              // copy the prev state to the current state and delete the old state
              commitBatch.Put(iter->key(), spPrevValue);
              commitBatch.Delete(spPrevKey);
              remainingSPs++;
            } else {
              // failed to find a previous SP entry, trigger reparse
              res = -1;
            }
          }
        } else {
          remainingSPs++;
        }
      } else {
        // failed to parse the db json, trigger reparse
        res = -1;
      }
    }

    // clean up the iterator
    delete iter;

    leveldb::WriteOptions writeOptions;
    writeOptions.sync = true;

    pDb->Write(writeOptions, &commitBatch);

    if (res < 0) {
      return res;
    } else {
      return remainingSPs;
    }
  }

  static string const watermarkKey;
  void setWatermark(uint256 const &watermark)
  {
    leveldb::WriteOptions writeOptions;
    writeOptions.sync = true;

    leveldb::WriteBatch commitBatch;
    commitBatch.Delete(watermarkKey);
    commitBatch.Put(watermarkKey, watermark.ToString());
    pDb->Write(writeOptions, &commitBatch);
  }

  int getWatermark(uint256 &watermark)
  {
    leveldb::ReadOptions readOpts;
    readOpts.fill_cache = false;
    string watermarkVal;
    if (pDb->Get(readOpts, watermarkKey, &watermarkVal).ok()) {
      watermark.SetHex(watermarkVal);
      return 0;
    } else {
      return -1;
    }
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

    // clean up the iterator
    delete iter;
  }
};

string const CMPSPInfo::watermarkKey("watermark");

CCriticalSection cs_tally;

typedef std::map<string, CMPCrowd> CrowdMap;

static map<string, CMPOffer> my_offers;
static map<string, CMPAccept> my_accepts;
static CMPSPInfo *_my_sps;
CrowdMap my_crowds;

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
bool isCrowdsalePurchase(uint256 txid, string address, int64_t *propertyId = NULL, int64_t *userTokens = NULL)
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
              *propertyId = foundPropertyId;
              *userTokens = tmpUserTokens;
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
               std::map<std::string, std::vector<uint64_t> > database = sp.txFundraiserData;
               std::map<std::string, std::vector<uint64_t> >::const_iterator it;
               for(it = database.begin(); it != database.end(); it++)
               {
                   string tmpTxid = it->first; //uint256 txid = it->first;
                   string compTxid = txid.GetHex().c_str(); //convert to string to compare since this is how stored in levelDB
                   if (tmpTxid == compTxid)
                   {
                       int64_t tmpUserTokens = it->second.at(2);
                       *propertyId = tmpPropertyId;
                       *userTokens = tmpUserTokens;
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
               std::map<std::string, std::vector<uint64_t> > database = sp.txFundraiserData;
               std::map<std::string, std::vector<uint64_t> >::const_iterator it;
               for(it = database.begin(); it != database.end(); it++)
               {
                   string tmpTxid = it->first; //uint256 txid = it->first;
                   string compTxid = txid.GetHex().c_str(); //convert to string to compare since this is how stored in levelDB
                   if (tmpTxid == compTxid)
                   {
                       int64_t tmpUserTokens = it->second.at(2);
                       *propertyId = tmpPropertyId;
                       *userTokens = tmpUserTokens;
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
  if (msc_debug_dex) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
const string combo = STR_SELLOFFER_ADDR_CURR_COMBO(seller_addr);
map<string, CMPOffer>::iterator my_it = my_offers.find(combo);

  if (my_it != my_offers.end()) return &(my_it->second);

  return (CMPOffer *) NULL;
}

// getAccept may replace DEx_acceptExists() in the near future
// TODO: locks are needed around map's insert & erase
CMPAccept *DEx_getAccept(const string &seller_addr, unsigned int curr, const string &buyer_addr)
{
  if (msc_debug_dex) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
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

  if (DEx_offerExists(seller_addr, curr)) return (DEX_ERROR_SELLOFFER -10);  // offer already exists

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

// check to see if such an accept exists
bool DEx_acceptExists(const string &seller_addr, unsigned int curr, const string &buyer_addr)
{
  if (msc_debug_dex) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
const string combo = STR_ACCEPT_ADDR_CURR_ADDR_COMBO(seller_addr, buyer_addr);
map<string, CMPAccept>::iterator my_it = my_accepts.find(combo);

  return !(my_it == my_accepts.end());
}

// returns 0 if everything is OK
int DEx_acceptCreate(const string &buyer, const string &seller, int curr, uint64_t nValue, int block, uint64_t fee_paid, uint64_t *nAmended = NULL)
{
  if (msc_debug_dex) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
int rc = DEX_ERROR_ACCEPT - 10;
map<string, CMPOffer>::iterator my_it;
const string selloffer_combo = STR_SELLOFFER_ADDR_CURR_COMBO(seller);
const string accept_combo = STR_ACCEPT_ADDR_CURR_ADDR_COMBO(seller, buyer);
uint64_t nActualAmount = getMPbalance(seller, curr, SELLOFFER_RESERVE);

  my_it = my_offers.find(selloffer_combo);

  if (my_it == my_offers.end()) return -15;

  CMPOffer &offer = my_it->second;

  // here we ensure the correct BTC fee was paid in this acceptance message, per spec
  if (fee_paid < offer.getMinFee())
  {
    fprintf(mp_fp, "ERROR: fee too small -- the ACCEPT is rejected! (%lu is smaller than %lu)\n", fee_paid, offer.getMinFee());
    ++InvalidCount_per_spec;
    return -105;
  }

  fprintf(mp_fp, "%s(%s) OFFER FOUND, line %d, file: %s\n", __FUNCTION__, selloffer_combo.c_str(), __LINE__, __FILE__);

  // Zathras said the older accept is the valid one !!!!!!!! do not accept any new ones!
  if (DEx_acceptExists(seller, curr, buyer))
  {
    fprintf(mp_fp, "%s() ERROR: an accept from this same seller for this same offer is already open !!!!!\n", __FUNCTION__);
    return -205;
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
int DEx_payment(string seller, string buyer, uint64_t BTC_paid, int blockNow, uint64_t *nAmended = NULL)
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

// save info from the crowdsale that's being erased
void dumpCrowdsaleInfo(const string &address, CMPCrowd &crowd, bool bExpired = false)
{
FILE *fp = fopen("/tmp/dead.log", "a");

  if (!fp) return;

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

unsigned int eraseExpiredCrowdsale(CBlockIndex const * pBlockIndex)
{
  const int64_t blockTime = pBlockIndex->GetBlockTime();
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


      //fprintf(mp_fp,"\nValues coming out of calculateFractional(): Total tokens, Tokens created, Tokens for issuer, amountMissed: issuer %s %ld %ld %ld %f\n",sp.issuer.c_str(), crowd.getUserCreated() + crowd.getIssuerCreated(), crowd.getUserCreated(), crowd.getIssuerCreated(), missedTokens);

      //get txdata
      sp.txFundraiserData = crowd.getDatabase();
      sp.update_block = pBlockIndex->GetBlockHash();

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

//calculateFundraiser does token calculations per transaction
//calcluateFractional does calculations for missed tokens
void calculateFundraiser(unsigned short int propType, uint64_t amtTransfer, unsigned char bonusPerc, 
  uint64_t fundraiserSecs, uint64_t currentSecs, uint64_t numProps, unsigned char issuerPerc, 
  std::pair<uint64_t, uint64_t>& tokens )
{
  
  //see calculateFractional() for more info

  //calc bonus in sec
  uint64_t bonusSeconds = fundraiserSecs - currentSecs;

  //printf("\n calc layer 1 %ld %ld %ld\n", fundraiserSecs, currentSecs, bonusSeconds); 

  //turn it into weeks and %
  double weeks = bonusSeconds / (double) 604800;
  double ebPercentage = weeks * bonusPerc;
  double bonusPercentage = ( ebPercentage / 100 ) + 1;

  //printf("\n calc layer 2 %f %f %f",  weeks, bonusPercentage, ebPercentage ); 

  double issuerPercentage = (double) (issuerPerc * 0.01);

  //init more values
  double createdTokens;
  double issuerTokens;

  //printf("\nbonusSeconds is %ld, bonusPercentage is %f, and issuerPercentage is %f\n", 
  //bonusSeconds, bonusPercentage, issuerPercentage);
  
  //indiv vs div calcs, one truncated 
  if( 2 == propType ) {
    //get tokens created and issued
    createdTokens = (amtTransfer/1e8) * (double) numProps * bonusPercentage ;
    issuerTokens = createdTokens * issuerPercentage;
    //printf("prop div 2: is %f, and %f", createdTokens / 1e8, issuerTokens / 1e8 );

    //add to tokens struct
    tokens = std::make_pair(createdTokens, issuerTokens);

  } else {
    //calculate tokens created and issued
    createdTokens = (uint64_t) ( (amtTransfer/1e8) * (double) numProps * bonusPercentage);
    issuerTokens = (uint64_t) (createdTokens * issuerPercentage) ;
    //fprintf(mp_fp,"prop indiv 1: is %ld, and %ld", (uint64_t) createdTokens, (uint64_t) issuerTokens);

    //add to tokens struct
    tokens = std::make_pair( (uint64_t) createdTokens, (uint64_t) issuerTokens);
  }
}

// certain transaction types are not live on the network until some specific block height
bool isTransactionTypeAllowed(int txBlock, unsigned int txCurrency, unsigned int txType)
{
unsigned int type;
int block_FirstAllowed;

  if ((RegTest() || TestNet()) || isTestEcosystemProperty(txCurrency)) return true; // everything is always allowed on Bitcoin's TestNet or with TMSC/TestEcosystem on MainNet

  // BTC as currency/property is never allowed
  if (MASTERCOIN_CURRENCY_BTC == txCurrency) return false;

  for (unsigned int i = 0; i < sizeof(txBlockRestrictions)/sizeof(txBlockRestrictions[0]); i++)
  {
    type = txBlockRestrictions[i][0];
    block_FirstAllowed = txBlockRestrictions[i][1];

    if (txType != type) continue;

    if (0 > block_FirstAllowed) break;

    if (block_FirstAllowed <= txBlock) return true;
  }

  return false;
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

  class SendToOwners_compare
  {
  public:

    bool operator()(pair<long, string> p1, pair < long, string> p2) const
    {
      if (p1.first == p2.first) return p1.second > p2.second; // reverse check
      else return p1.first < p2.first;
    }
  };

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

          fprintf(mp_fp, "%s(INVESTMENT SEND to Crowdsale Issuer: %s), line %d, file: %s\n", __FUNCTION__, receiver.c_str(), __LINE__, __FILE__);
          
          if (spFound)
          {
            //init this struct
            std::pair <uint64_t,uint64_t> tokens;
            
            //get txid
            string sp_txid =  sp.txid.GetHex().c_str();

            //fprintf(mp_fp, "\nValues going into calculateFundraiser(): hexid %s nValue %lu earlyBird %d deadline %lu blockTime %ld numProps %lu issuerPerc %d \n", txid.GetHex().c_str(), nValue, sp.early_bird, sp.deadline, (uint64_t) blockTime, sp.num_tokens, sp.percentage);

            // calc tokens per this fundraise
            calculateFundraiser(sp.prop_type,         //u short
                                nValue,               // u int 64
                                sp.early_bird,        // u char
                                sp.deadline,          // u int 64
                                (uint64_t) blockTime, // int 64
                                sp.num_tokens,      // u int 64
                                sp.percentage,        // u char
                                tokens );

            //fprintf(mp_fp,"\n before incrementing global tokens user: %ld issuer: %ld\n", crowd->getUserCreated(), crowd->getIssuerCreated());
            
            //dead code? nothing uses this AFAIK
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
  enum ActionTypes { INVALID = 0, NEW = 1, UPDATE = 2, CANCEL = 3 };
  const char * const subaction_name[] = { "empty", "new", "update", "cancel" };

      if ((MASTERCOIN_CURRENCY_TMSC != currency) && (MASTERCOIN_CURRENCY_MSC != currency))
      {
        fprintf(mp_fp, "No smart properties allowed on the DeX...\n");
        return -90972;
      }

      // block height checks, for instance DEX is only available on MSC starting with block 290630
      if (!isTransactionTypeAllowed(block, currency, type)) return -88888;

      memcpy(&amount_desired, &pkt[16], 8);
      memcpy(&blocktimelimit, &pkt[24], 1);
      memcpy(&min_fee, &pkt[25], 8);
      memcpy(&subaction, &pkt[33], 1);

      // FIXME: only do swaps for little-endian system(s) !
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

  int logicMath_SendToOwners()
  {
  int rc = PKT_ERROR_STO -1000;

      if (!isTransactionTypeAllowed(block, currency, type)) return (PKT_ERROR_STO -888);

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

      typedef set<pair<int64_t, string>, SendToOwners_compare> OwnerAddrType;
      OwnerAddrType OwnerAddrSet;

      {
        for(map<string, CMPTally>::reverse_iterator my_it = mp_tally_map.rbegin(); my_it != mp_tally_map.rend(); ++my_it)
        {
          const string address = (my_it->first).c_str();

          // do not count the sender
          if (address == sender) continue;

          // do not count the Exodus either
//          if (address == exodus) continue;

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
        printf("#%ld: %lu = %s\n", n_owners, (my_it->first), (my_it->second).c_str());
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
      long double percentage, piece;
      rc = 0; // almost good, the for-loop will set the error code
      for(OwnerAddrType::reverse_iterator my_it = OwnerAddrSet.rbegin(); my_it != OwnerAddrSet.rend(); ++my_it)
      {
      const string address = my_it->second;

        owns = my_it->first;
        percentage = (long double) owns / (long double) totalTokens;
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
         fprintf(mp_fp, "%14lu = %s, perc= %20.10Lf, piece= %20.10Lf, should_get= %14lu, will_really_get= %14lu, sent_so_far= %14lu\n",
          owns, address.c_str(), percentage, piece, should_receive, will_really_receive, sent_so_far);

        if (!update_tally_map(sender, currency, - will_really_receive, MONEY))
        {
          return (PKT_ERROR_STO -1);
        }

        update_tally_map(address, currency, will_really_receive, MONEY);

        if (sent_so_far >= nValue)
        {
          printf("SendToOwners: DONE HERE : those who could get paid got paid, SOME DID NOT, but that's ok\n");
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
        newSP.creation_block = newSP.update_block = chainActive[block]->GetBlockHash();

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
        newSP.creation_block = newSP.update_block = chainActive[block]->GetBlockHash();

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
        
        sp.txFundraiserData = crowd.getDatabase();
        sp.update_block = chainActive[block]->GetBlockHash();
        
        _my_sps->updateSP(crowd.getPropertyId() , sp);
        
        update_tally_map(sp.issuer, crowd.getPropertyId(), missedTokens, MONEY);
        //End

        my_crowds.erase(it);

        rc = 0;
      }
      break;
    }

    case MSC_TYPE_SEND_TO_OWNERS:
    if (disable_Divs) break;
    else
    {
      step_rc = step2_Value();
      if (0>step_rc) return step_rc;

      rc = logicMath_SendToOwners();
    }
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

  // FIXME: only do swaps for little-endian system(s) !
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

//  if nNewValue > 9223372036854775807 return 9064 // nvalue is over int64, invalidate 

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

  if (!isTransactionTypeAllowed(block, prop_id, type))
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
    fprintf(mp_fp, "===BLOCK: %d =txid: %s =fee: %1.8lf ====\n", block, txid.GetHex().c_str(), (double)tx_fee_paid/(double)COIN);
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
// THE TODO LIST, WHAT'S MISSING, NOT DONE YET:
//  1) checks to ensure the seller has enough funds
//  2) checks to ensure the sender has enough funds
//  3) checks to ensure there are enough funds when the 'accept' message is received
//  4) partial order fulfilment is not yet handled (spec says all is sold if larger than available is put on sale, etc.)
//  5) return false as needed and check returns of msc_update_* functions
//  6) verify large-number calculations (especially divisions & multiplications)
//  7) need to detect cancelations & updates of sell offers -- and handle partially fullfilled offers...
//  8) most important: figure out all the coins in existence and add all that prebuilt data
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
const double part_available = 1 - pow(0.5, years); // do I need 'long double' ? powl()
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
  (void) eraseExpiredCrowdsale(pBlockIndex);

  return 0;
}

// called once per block, after the block has been processed
// TODO: consolidate into *handler_block_begin() << need to adjust Accept expiry check.............
// it performs cleanup and other functions
int mastercore_handler_block_end(int nBlockNow, CBlockIndex const * pBlockIndex)
{
  if (!mastercoreInitialized) {
    mastercore_init();
  }
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
  if (!disable_Persistence) mastercore_save_state(pBlockIndex);

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

      if (msc_debug_verbose) if (5>j) fprintf(mp_fp, "%d: sha256 hex: %s\n", j, ObfsHashes[j].c_str());
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
  if ((nBlock >= GENESIS_BLOCK && nBlock <= LAST_EXODUS_BLOCK) || (TestNet()) || (RegTest()))
  { //Exodus Fundraiser start/end blocks
    //printf("transaction: %s\n", wtx.ToString().c_str() );
    int deadline_timeleft=1377993600-nTime;
    double bonus= 1 + std::max( 0.10 * deadline_timeleft / (60 * 60 * 24 * 7), 0.0 );

    if (TestNet() || RegTest())
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

// RETURNS: 0 if parsed a MP TX

int parseTransaction(const CTransaction &wtx, int nBlock, unsigned int idx, CMPTransaction *mp_tx, unsigned int nTime=0)
{
string strSender;
// class A: data & address storage -- combine them into a structure or something
vector<string>script_data;
vector<string>address_data;
// vector<uint64_t>value_data;
vector<int64_t>value_data;
int64_t ExodusValues[MAX_BTC_OUTPUTS];
int64_t TestNetMoneyValues[MAX_BTC_OUTPUTS] = { 0 };
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
                else if (( RegTest() || TestNet()) && (getmoney_testnet == strAddress))
                {
                  TestNetMoneyValues[getmoney_count++] = wtx.vout[i].nValue;
                }
              }
            }

            if ((RegTest() || TestNet()) && getmoney_count)

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
                if (TX_PUBKEYHASH == whichType) validType=true; // ignore non pay-to-pubkeyhash output

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
              ++BitcoinCore_errors;
              return -101;
            }

            unsigned int n = wtx.vin[i].prevout.n;

            CTxDestination source;

            uint64_t nValue = txPrev.vout[n].nValue;
            txnouttype whichType;

              inAll += nValue;

              if (ExtractDestination(txPrev.vout[n].scriptPubKey, source))  // extract the destination of the previous transaction's vout[n]
              {
                // we only allow pay-to-pubkeyhash & probably pay-to-pubkey (?)
                {
                  if (!getOutputType(txPrev.vout[n].scriptPubKey, whichType)) ++inputs_errors;
                  if ((TX_PUBKEYHASH != whichType) /* || (TX_PUBKEY != whichType) */ ) ++inputs_errors;

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
                  nMax = nTemp;
                  strSender = my_it->first;
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
            if (RegTest() || TestNet())
            {
              if (MONEYMAN_TESTNET_BLOCK <= nBlock) BTC_amount = TestNetMoneyValues[0];
            }

            fprintf(mp_fp, "%s()amount = %ld , nBlock = %d, line %d, file: %s\n", __FUNCTION__, BTC_amount, nBlock, __LINE__, __FILE__);

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
                  if (TX_PUBKEYHASH != whichType) break; // ignore non pay-to-pubkeyhash output

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
                      if (TX_PUBKEYHASH != whichType) break; // ignore non pay-to-pubkeyhash output

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
                          if (TX_PUBKEYHASH != whichType) break; // ignore non pay-to-pubkeyhash output

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
                    fprintf(mp_fp, "payment #%d %s %11.8lf\n", ++count, strAddress.c_str(), (double)wtx.vout[i].nValue/(double)COIN);

                    // check everything & pay BTC for the currency we are buying here...
                    DEx_payment(strAddress, strSender, wtx.vout[i].nValue, nBlock);
                  }
                }
              }
              return count;
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

    mastercore_handler_block_end(blockNum, pblockindex);
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

    update_tally_map(strAddress, currency, balance, MONEY);
    update_tally_map(strAddress, currency, sellReserved, SELLOFFER_RESERVE);
    update_tally_map(strAddress, currency, acceptReserved, ACCEPT_RESERVE);
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

  LogPrintf("Loading %s ... \n", filename);

  ifstream file;
  file.open(filename.c_str());
  if (!file.is_open())
  {
    LogPrintf("%s(): file not found, line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
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

  printf("%s(): file: %s , loaded lines= %d\n", __FUNCTION__, filename.c_str(), lines);
  LogPrintf("%s(): file: %s , loaded lines= %d\n", __FUNCTION__, filename, lines);

  return res;
}

/*
static int msc_preseed_file_load(int what)
{
  // uses boost::filesystem::path
  const string filename = (GetDataDir() / mastercore_filenames[what]).string();
  return msc_file_load(filename, what);
}
*/

static char const * const statePrefix[NUM_FILETYPES] = {
    "balances",
    "offers",
    "accepts",
    "globals",
    "crowdsales",
};

// returns the height of the state loaded
static int load_most_relevant_state()
{
  int res = -1;
  // check the SP database and roll it back to its latest valid state
  // according to the active chain
  uint256 spWatermark;
  if (0 > _my_sps->getWatermark(spWatermark)) {
    //trigger a full reparse, if the SP database has no watermark
    return -1;
  }

  CBlockIndex const *spBlockIndex = mapBlockIndex[spWatermark];
  if (NULL == spBlockIndex) {
    //trigger a full reparse, if the watermark isn't a real block
    return -1;
  }

  while(false == chainActive.Contains(spBlockIndex)) {
    int remainingSPs = _my_sps->popBlock(*spBlockIndex->phashBlock);
    if (remainingSPs < 0) {
      // trigger a full reparse, if the levelDB cannot roll back
      return -1;
    } /*else if (remainingSPs == 0) {
      // potential optimization here?
    }*/
    spBlockIndex = spBlockIndex->pprev;
    _my_sps->setWatermark(*spBlockIndex->phashBlock);
  }

  // prepare a set of available files by block hash pruning any that are
  // not in the active chain
  std::set<uint256> persistedBlocks;
  boost::filesystem::directory_iterator dIter(MPPersistencePath);
  boost::filesystem::directory_iterator endIter;
  for (; dIter != endIter; ++dIter) {
    if (false == boost::filesystem::is_regular_file(dIter->status()) || dIter->path().empty()) {
      // skip funny business
      continue;
    }

    std::string fName = (*--dIter->path().end()).c_str();
    std::vector<std::string> vstr;
    boost::split(vstr, fName, boost::is_any_of("-."), token_compress_on);
    if (  vstr.size() == 3 &&
          boost::equals(vstr[2], "dat")) {
      uint256 blockHash;
      blockHash.SetHex(vstr[1]);
      CBlockIndex *pBlockIndex = mapBlockIndex[blockHash];
      if (pBlockIndex == NULL || false == chainActive.Contains(pBlockIndex)) {
        continue;
      }

      // this is a valid block in the active chain, store it
      persistedBlocks.insert(blockHash);
    }
  }

  // using the SP's watermark after its fixed-up as the tip
  // walk backwards until we find a valid and full set of persisted state files
  // for each block we discard, roll back the SP database
  CBlockIndex const *curTip = spBlockIndex;
  while (NULL != curTip && persistedBlocks.size() > 0) {
    if (persistedBlocks.find(*spBlockIndex->phashBlock) != persistedBlocks.end()) {
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

      // remove this from the persistedBlock Set
      persistedBlocks.erase(*spBlockIndex->phashBlock);
    }

    // go to the previous block
    if (0 > _my_sps->popBlock(*curTip->phashBlock)) {
      // trigger a full reparse, if the levelDB cannot roll back
      return -1;
    }
    curTip = curTip->pprev;
    _my_sps->setWatermark(*curTip->phashBlock);
  }

  if (persistedBlocks.size() == 0) {
    // trigger a reparse if we exhausted the persistence files without success
    return -1;
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
    boost::split(vstr, (*iter).first, boost::is_any_of("-"), token_compress_on);
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
  static int const MAX_STATE_HISTORY = 50;

  // build a set of blockHashes for which we have any state files
  std::set<uint256> statefulBlockHashes;

  boost::filesystem::directory_iterator dIter(MPPersistencePath);
  boost::filesystem::directory_iterator endIter;
  for (; dIter != endIter; ++dIter) {
    std::string fName = dIter->path().empty() ? "<invalid>" : (*--dIter->path().end()).c_str();
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

  _my_sps->setWatermark(*pBlockIndex->phashBlock);

  return 0;
}

// called from init.cpp of Bitcoin Core
int mastercore_init()
{
  printf("%s()%s, line %d, file: %s\n", __FUNCTION__, TestNet() ? "TESTNET":"", __LINE__, __FILE__);

#ifdef  WIN32
#error  Need boost path here too
#else
#ifndef  DISABLE_LOG_FILE
  mp_fp = fopen ("/tmp/mastercore.log", "a");
#else
  mp_fp = stdout;
#endif
#endif
  fprintf(mp_fp, "\n%s MASTERCORE INIT, build date: " __DATE__ " " __TIME__ "\n\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()).c_str());

  if (TestNet())
  {
    exodus = exodus_testnet;
  }
  if (RegTest())
  {
    exodus = exodus_testnet;
  }

  p_txlistdb = new CMPTxList(GetDataDir() / "MP_txlist", 1<<20, false, fReindex);
  _my_sps = new CMPSPInfo(GetDataDir() / "MP_spinfo");
  MPPersistencePath = GetDataDir() / "MP_persist";
  boost::filesystem::create_directories(MPPersistencePath);

  // legacy code, setting to pre-genesis-block
  static int snapshotHeight = (GENESIS_BLOCK - 1);
  static const uint64_t snapshotDevMSC = 0;

  if (TestNet()) snapshotHeight = START_TESTNET_BLOCK - 1;

  if (RegTest()) snapshotHeight = START_REGTEST_BLOCK - 1;

  ++mastercoreInitialized;

  if (!disable_Persistence)
  {
    nWaterlineBlock = load_most_relevant_state();
    if (nWaterlineBlock < 0) {
      // persistence says we reparse!, nuke some stuff in case the partial loads left stale bits
      mp_tally_map.clear();
      my_offers.clear();
      my_accepts.clear();
      my_crowds.clear();
      _my_sps->clear();
    }

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

    nWaterlineBlock = GENESIS_BLOCK - 1;  // the DEX block, using Zathras' msc_balances_290629.txt

#ifdef  MY_DIV_HACK     //
    nWaterlineBlock = MSC_SP_BLOCK-3;
    nWaterlineBlock = MSC_DEX_BLOCK-3;
//    nWaterlineBlock = 296163 - 3; // bad Deadline
    nWaterlineBlock = MSC_SP_BLOCK-3;
    nWaterlineBlock = 292665;
    nWaterlineBlock = 303550;
    nWaterlineBlock = 303550;
    nWaterlineBlock = 308500;
    nWaterlineBlock = MSC_DEX_BLOCK-3;

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
    nWaterlineBlock = 304000;
#endif

    if (TestNet()) nWaterlineBlock = START_TESTNET_BLOCK; //testnet3

    if (RegTest()) nWaterlineBlock = START_REGTEST_BLOCK; //testnet3
  }

  // collect the real Exodus balances available at the snapshot time
  exodus_balance = getMPbalance(exodus, MASTERCOIN_CURRENCY_MSC, MONEY);
  printf("Exodus balance: %lu\n", exodus_balance);

  (void) msc_initial_scan(nWaterlineBlock);
/*
  if (!TestNet())
  {
    (void) msc_initial_scan(nWaterlineBlock);
  }
  else
  {
    // testnet
    (void) msc_initial_scan(GetHeight()-10000); // sometimes testnet blocks get generated very fast, scan the last 1000 just for fun
  }
*/

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

  if (_my_sps)
  {
    delete _my_sps; _my_sps = NULL;
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

  pop_ret = parseTransaction(tx, nBlock, idx, &mp_obj, pBlockIndex->GetBlockTime() );
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
// Do we care if this is true: pubkeys[i].IsCompressed() ???
//
static int ClassB_send(const string &senderAddress, const string &receiverAddress, const string &data_packet, CCoinControl &coinControl, uint256 & txid)
{
const int n_keys = 2;
int i = 0;
std::vector<CPubKey> pubkeys;
pubkeys.resize(n_keys);
CWallet *wallet = pwalletMain;
const int64_t nDustLimit = MP_DUST_LIMIT;

  txid = 0;

  // partially copied from _createmultisig()

  CBitcoinAddress address(senderAddress);
  if (wallet && address.IsValid())
  {
  CKeyID keyID;

    if (!address.GetKeyID(keyID)) return -20;

    CPubKey vchPubKey;
    if (!wallet->GetPubKey(keyID, vchPubKey)) return -21;
    if (!vchPubKey.IsFullyValid()) return -22;

    pubkeys[i++] = vchPubKey;
  }
  else return -23;

  pubkeys[i] = ParseHex(data_packet);

  // 2nd (& 3rd) is the data packet(s)
  if (!pubkeys[i].IsFullyValid()) return -1;

  CScript multisig_output;
  multisig_output.SetMultisig(1, pubkeys);
  printf("%s(): %s, line %d, file: %s\n", __FUNCTION__, multisig_output.ToString().c_str(), __LINE__, __FILE__);

  CWalletTx wtxNew;
  int64_t nFeeRet = 0;
  vector< pair<CScript, int64_t> > vecSend;
  std::string strFailReason;
  CReserveKey reserveKey(wallet);

  CBitcoinAddress addr = CBitcoinAddress(senderAddress);  // change goes back to us
  coinControl.destChange = addr.Get();

  if (!wallet) return -5;

  CScript scriptPubKey;

  // the 1-multisig-2 Class B with data & sender
  vecSend.push_back(make_pair(multisig_output, nDustLimit));

  // the reference/recepient/receiver
  if (!receiverAddress.empty())
  {
    // Send To Owners is the first use case where the receiver is empty
    scriptPubKey.SetDestination(CBitcoinAddress(receiverAddress).Get());
    vecSend.push_back(make_pair(scriptPubKey, nDustLimit));
  }

  // the marker output
  scriptPubKey.SetDestination(CBitcoinAddress(exodus).Get());
  vecSend.push_back(make_pair(scriptPubKey, nDustLimit));

  // selected in the parent function, i.e.: ensure we are only using the address passed in as the Sender
  if (!coinControl.HasSelected()) return -6;

  LOCK(wallet->cs_wallet);  // TODO: is this needed?

  // the fee will be computed by Bitcoin Core, need an override (?)
  // TODO: look at Bitcoin Core's global: nTransactionFee (?)
  if (!wallet->CreateTransaction(vecSend, wtxNew, reserveKey, nFeeRet, strFailReason, &coinControl)) return -11;

  printf("%s():%s; nFeeRet = %lu, line %d, file: %s\n", __FUNCTION__, wtxNew.ToString().c_str(), nFeeRet, __LINE__, __FILE__);

  if (!wallet->CommitTransaction(wtxNew, reserveKey)) return -13;

  txid = wtxNew.GetHash();

  return 0;
}

// WIP: expanding the function to a general-purpose one, but still sending 1 packet only for now (30-31 bytes)
static uint256 send_INTERNAL_1packet(const string &FromAddress, const string &ToAddress, unsigned int CurrencyID, uint64_t Amount, unsigned int TransactionType)
{
const uint64_t nAvailable = getMPbalance(FromAddress, CurrencyID, MONEY);
CWallet *wallet = pwalletMain;
CCoinControl coinControl; // I am using coin control to send from
int rc = 0;
uint256 txid = 0;

  if (msc_debug_send) fprintf(mp_fp, "%s(From: %s , To: %s , Currency= %u, Amount= %lu), line %d, file: %s\n",
   __FUNCTION__, FromAddress.c_str(), ToAddress.c_str(), CurrencyID, Amount, __LINE__, __FILE__);

  LOCK(wallet->cs_wallet);

  // make sure this address has enough MP currency available!
  if ((nAvailable < Amount) || (0 == Amount))
  {
    LogPrintf("%s(): aborted -- not enough MP currency (%lu < %lu)\n", __FUNCTION__, nAvailable, Amount);
    if (msc_debug_send) fprintf(mp_fp, "%s(): aborted -- not enough MP currency (%lu < %lu)\n", __FUNCTION__, nAvailable, Amount);
    ++InvalidCount_per_spec;

    return 0;
  }

    {
    string sAddress="";

        for (map<uint256, CWalletTx>::iterator it = wallet->mapWallet.begin(); it != wallet->mapWallet.end(); ++it)
        {
        const uint256& wtxid = it->first;
        const CWalletTx* pcoin = &(*it).second;
        bool bIsMine;
        bool bIsSpent;

            if (pcoin->IsTrusted())
            {
            const int64_t nAvailable = pcoin->GetAvailableCredit();

              if (!nAvailable) continue;

     for (unsigned int i = 0; i < pcoin->vout.size(); i++)
        {
                CTxDestination dest;

                if(!ExtractDestination(pcoin->vout[i].scriptPubKey, dest))
                    continue;

                bIsMine = IsMine(*wallet, dest);
                bIsSpent = wallet->IsSpent(wtxid, i);

                if (!bIsMine || bIsSpent) continue;

                int64_t n = bIsSpent ? 0 : pcoin->vout[i].nValue;

                sAddress = CBitcoinAddress(dest).ToString();
                if (msc_debug_send) fprintf(mp_fp, "%s:IsMine()=%s:IsSpent()=%s:%s: i=%d, nValue= %lu\n",
                 sAddress.c_str(), bIsMine ? "yes":"NO", bIsSpent ? "YES":"no", wtxid.ToString().c_str(), i, n);

            // only use funds from the Sender's address for our MP transaction
            // TODO: may want to a little more selective here, i.e. use smallest possible (~0.1 BTC), but large amounts lead to faster confirmations !
            if (FromAddress == sAddress)
            {
              COutPoint outpt(wtxid, i);
              coinControl.Select(outpt);
            }
        }
            }
        }
    }

  string strObfuscatedHashes[1+MAX_SHA256_OBFUSCATION_TIMES];
  prepareObfuscatedHashes(FromAddress, strObfuscatedHashes);

  unsigned char packet[MAX_PACKETS * PACKET_SIZE];
  memset(&packet, 0, sizeof(packet));

  swapByteOrder32(TransactionType);
  swapByteOrder32(CurrencyID);
  swapByteOrder64(Amount);

  // TODO: beautify later
  packet[0] = 0x01; // seq
  memcpy(&packet[1], &TransactionType, 4);
  memcpy(&packet[5], &CurrencyID, 4);
  memcpy(&packet[9], &Amount, 8);

  printf("pkt : %s\n", HexStr(packet, PACKET_SIZE + packet, false).c_str());

  vector<unsigned char>hash = ParseHex(strObfuscatedHashes[1]);
  for (unsigned int i=0;i<hash.size();i++)
  {
    packet[i] ^= hash[i];
  }

  printf("     hash   :   %s\n", HexStr(hash).c_str());
  printf("     packet :   %s\n", HexStr(packet, PACKET_SIZE + packet, false).c_str());

  vector<unsigned char> vec_pkt;
  vec_pkt.resize(2+PACKET_SIZE);
  vec_pkt[0]=0x02;
  memcpy(&vec_pkt[1], &packet, PACKET_SIZE);

  CPubKey pubKey;
  unsigned char random_byte = (unsigned char)(GetRand(256));
  for (unsigned int i = 0; i < 0xFF ; i++)
  {
    vec_pkt[1+PACKET_SIZE] = random_byte;

    pubKey = CPubKey(vec_pkt);
    printf("pubKey check: %s\n", (HexStr(pubKey.begin(), pubKey.end()).c_str()));

    if (pubKey.IsFullyValid()) break;

    ++random_byte; // cycle 256 times, if we must to find a valid ECDSA point
  }

  rc = ClassB_send(FromAddress, ToAddress, HexStr(vec_pkt), coinControl, txid);
  if (msc_debug_send) fprintf(mp_fp, "ClassB_send returned %d\n", rc);

  return txid;
}

uint256 send_MP(const string &FromAddress, const string &ToAddress, unsigned int CurrencyID, uint64_t Amount)
{
  return send_INTERNAL_1packet(FromAddress, ToAddress, CurrencyID, Amount, MSC_TYPE_SIMPLE_SEND);
}

uint256 send_To_Owners(const string &FromAddress, unsigned int CurrencyID, uint64_t Amount)
{
  return send_INTERNAL_1packet(FromAddress, "", CurrencyID, Amount, MSC_TYPE_SEND_TO_OWNERS);
}

// send a MP transaction via RPC - simple send
Value send_MP(const Array& params, bool fHelp)
{
if (fHelp || params.size() != 4)
        throw runtime_error(
            "send_MP\n"
            "\nCreates and broadcasts a simple send for a given amount and currency/property ID.\n"
            "\nResult:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">mastercored send_MP 1FromAddress 1ToAddress CurrencyID Amount\n"
        );

  std::string FromAddress = (params[0].get_str());
  std::string ToAddress = (params[1].get_str());

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

  double tmpAmount = params[3].get_real();
  int64_t Amount = 0;

  if (divisible)
  {
      if (tmpAmount <= 0.0 || tmpAmount > 92233720.36854775)
           throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");

      Amount = roundint64(tmpAmount * COIN);
  }
  else // indivisible
  {
      if (tmpAmount <= 0.0 || tmpAmount > 9223372036854775807)
           throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");

      Amount = int64_t(tmpAmount); // I believe this cast will always truncate (please correct me if wrong?)
  }

  printf("%s() %40.25lf, %lu, line %d, file: %s\n", __FUNCTION__, tmpAmount, Amount, __LINE__, __FILE__);

  if (0 >= Amount)
           throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

  //some sanity checking of the data supplied?
  uint256 newTX = send_MP(FromAddress, ToAddress, propertyId, Amount);

  //we need to do better than just returning a string of 0000000 here if we can't send the TX
  return newTX.GetHex();
}

// send a MP transaction via RPC - simple send
Value sendtoowners_MP(const Array& params, bool fHelp)
{
if (fHelp || params.size() != 3)
        throw runtime_error(
            "sendtoowners_MP\n"
            "\nCreates and broadcasts a send-to-owners transaction for a given amount and currency/property ID.\n"
            "\nResult:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">mastercored send_MP 1FromAddress PropertyID Amount\n"
        );

  std::string FromAddress = (params[0].get_str());

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

  double tmpAmount = params[2].get_real();
  int64_t Amount = 0;

  if (divisible)
  {
      if (tmpAmount <= 0.0 || tmpAmount > 92233720.36854775807)
           throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");

      Amount = roundint64(tmpAmount * COIN);
  }
  else // indivisible
  {
      if (tmpAmount <= 0.0 || tmpAmount > 9223372036854775807)
           throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");

      Amount = int64_t(tmpAmount); // I believe this cast will always truncate (please correct me if wrong?)
  }

  printf("%s() %40.25lf, %lu, line %d, file: %s\n", __FUNCTION__, tmpAmount, Amount, __LINE__, __FILE__);

  if (0 >= Amount)
           throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

  //some sanity checking of the data supplied?
  uint256 newTX = send_To_Owners(FromAddress, propertyId, Amount);

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

    int64_t tmpbal = getMPbalance(address, propertyId, MONEY);
    if (divisible)
    {
        return ValueFromAmount(tmpbal);
    }
    else
    {
        return tmpbal;
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
bool CMPTxList::isMPinBlockRange(int starting_block, int ending_block)
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

      if ((starting_block <= block) && (block <= ending_block)) ++n_found;
    }
  }

  printf("%s(%d, %d); n_found= %d, line %d, file: %s\n", __FUNCTION__, starting_block, ending_block, n_found, __LINE__, __FILE__);

  delete it;

  return (n_found);
}

// global wrapper, block numbers are inclusive, if ending_block is 0 top of the chain will be used
bool isMPinBlockRange(int starting_block, int ending_block)
{
  if (!p_txlistdb) return false;

  if (0 == ending_block) ending_block = GetHeight(); // will scan 'til the end

  return p_txlistdb->isMPinBlockRange(starting_block, ending_block);
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
            "  \"amount\" : x.xxx,        (numeric) The transaction amount in btc\n"
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
            "      \"amount\" : x.xxx                  (numeric) The amount in btc\n"
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
                int nFee;
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

                bool crowdPurchase = false;
                int64_t crowdPropertyId = 0;
                int64_t crowdTokens = 0;
                bool crowdDivisible = false;
                string crowdName;

                if ((0 == blockHash) || (NULL == mapBlockIndex[blockHash]))
                        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Exception: blockHash is 0");
                CBlockIndex* pBlockIndex = mapBlockIndex[blockHash];
                if (NULL == pBlockIndex)
                        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Exception: pBlockIndex is NULL");
                int blockHeight = pBlockIndex->nHeight;
                int confirmations =  1 + chainActive.Height() - pBlockIndex->nHeight;
                int64_t blockTime = mapBlockIndex[blockHash]->nTime; 


                mp_obj.SetNull();
                CMPOffer temp_offer;
                if (0 == parseTransaction(wtx, blockHeight, 0, &mp_obj))
                {
                        // OK, a valid MP transaction so far
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
                                          propertyId = _my_sps->findSPByTX(wtxid); // propertyId of created property (if valid)
                                          amount = getTotalTokens(propertyId);
                                     break;
                                     case MSC_TYPE_CREATE_PROPERTY_VARIABLE:
                                          propertyId = _my_sps->findSPByTX(wtxid); // propertyId of created property (if valid)
                                          amount = 0; // crowdsale txs always create zero tokens
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
                        txobj.push_back(Pair("divisible", divisible));
                        if (divisible)
                        {
                                txobj.push_back(Pair("amount", ValueFromAmount(amount))); //divisible, format w/ bitcoins VFA func
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
                                     txobj.push_back(Pair("purchasedtokens", ValueFromAmount(crowdTokens))); //divisible, format w/ bitcoins VFA func
                                }
                                else
                                {
                                     txobj.push_back(Pair("purchasedtokens", crowdTokens)); //indivisible, push raw 64
                                }
                        }
                        if (MSC_TYPE_TRADE_OFFER == MPTxTypeInt)
                        {
                        txobj.push_back(Pair("feerequired", ValueFromAmount(sell_minfee)));
                        txobj.push_back(Pair("timelimit", sell_timelimit));
                        if (1 == sell_subaction) txobj.push_back(Pair("subaction", "New"));
			if (2 == sell_subaction) txobj.push_back(Pair("subaction", "Update"));
			if (3 == sell_subaction) txobj.push_back(Pair("subaction", "Cancel"));
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
string addressParam = "";
bool addressFilter;

    if (fHelp || params.size() > 5)
        throw runtime_error(
            "*** SOME *** HELP *** GOES *** HERE ***\n"
            + HelpExampleCli("*************_MP", "\"-------------\"")
            + HelpExampleRpc("*****************_MP", "\"-----------------\"")
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
                if (0 == parseTransaction(*pwtx, blockHeight, 0, &mp_obj))
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
                                        txobj.push_back(Pair("amount", ValueFromAmount(amount))); //divisible, format w/ bitcoins VFA func
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
                                        txobj.push_back(Pair("purchasedtokens", ValueFromAmount(crowdTokens))); //divisible, format w/ bitcoins VFA func
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
            "  \"balance\" : x.xxx,     (numeric) The available balance of the address\n"
            "  \"reservedbyselloffer\" : x.xxx,   (numeric) The amount reserved by sell offers\n"
            "  \"reservedbyacceptoffer\" : x.xxx,   (numeric) The amount reserved by accepts\n"
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

        addressbal.push_back(Pair("address", address));
        if(divisible)
        {
        addressbal.push_back(Pair("balance", ValueFromAmount(getMPbalance(address, propertyId, MONEY))));
        addressbal.push_back(Pair("reservedbyoffer", ValueFromAmount(getMPbalance(address, propertyId, SELLOFFER_RESERVE))));
        if(propertyId <3) addressbal.push_back(Pair("reservedbyaccept", ValueFromAmount(getMPbalance(address, propertyId, ACCEPT_RESERVE))));
        }
        else
        {
        addressbal.push_back(Pair("balance", getMPbalance(address, propertyId, MONEY)));
        addressbal.push_back(Pair("reservedbyoffer", getMPbalance(address, propertyId, SELLOFFER_RESERVE)));
        if(propertyId <3) addressbal.push_back(Pair("reservedbyaccept", getMPbalance(address, propertyId, ACCEPT_RESERVE)));
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
            "  \"balance\" : x.xxx,     (numeric) The available balance of the address\n"
            "  \"reservedbyselloffer\" : x.xxx,   (numeric) The amount reserved by sell offers\n"
            "  \"reservedbyacceptoffer\" : x.xxx,   (numeric) The amount reserved by accepts\n"
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
            if (divisible)
            {
                    propertyBal.push_back(Pair("balance", ValueFromAmount(getMPbalance(address, propertyId, MONEY))));
                    propertyBal.push_back(Pair("reservedbyoffer", ValueFromAmount(getMPbalance(address, propertyId, SELLOFFER_RESERVE))));
                    if (propertyId<3) propertyBal.push_back(Pair("reservedbyaccept", ValueFromAmount(getMPbalance(address, propertyId, ACCEPT_RESERVE))));
            }
            else
            {
                    propertyBal.push_back(Pair("balance", getMPbalance(address, propertyId, MONEY)));
                    propertyBal.push_back(Pair("reservedbyoffer", getMPbalance(address, propertyId, SELLOFFER_RESERVE)));
                    if (propertyId<3) propertyBal.push_back(Pair("reservedbyaccept", getMPbalance(address, propertyId, ACCEPT_RESERVE)));
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
            "  \"totaltokens\" : x     (numeric) the total number of tokens in existence\n"
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
            response.push_back(Pair("totaltokens", ValueFromAmount(totalTokens)));
        }
        else
        {
            response.push_back(Pair("totaltokens", totalTokens));
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
            "  \"endedtime\" : xxx,     (numeric) the time the crowdsale ended\n"
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
    if (fixedIssuance) // property was not a variable issuance
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
        database = sp.txFundraiserData;
    }

    fprintf(mp_fp,"\nSIZE OF DB %lu\n", sp.txFundraiserData.size() ); 
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
             participanttx.push_back(Pair("amountsent", ValueFromAmount(amountSent)));
        }
        else
        {
             participanttx.push_back(Pair("amountsent", amountSent));
        }
        if (divisible)
        {
             participanttx.push_back(Pair("participanttokens", ValueFromAmount(userTokens)));
        }
        else
        {
             participanttx.push_back(Pair("participanttokens", userTokens));
        }
        if (divisible)
        {
             participanttx.push_back(Pair("issuertokens", ValueFromAmount(issuerTokens)));
        }
        else
        {
             participanttx.push_back(Pair("issuertokens", issuerTokens));
        }
        participanttxs.push_back(participanttx);
    }

    response.push_back(Pair("name", propertyName));
    response.push_back(Pair("active", active));
    response.push_back(Pair("issuer", issuer));
    response.push_back(Pair("propertyiddesired", propertyIdDesired));
    if (divisible)
    {
        response.push_back(Pair("tokensperunit", ValueFromAmount(tokensPerUnit)));
    }
    else
    {
        response.push_back(Pair("tokensperunit", tokensPerUnit));
    }
    response.push_back(Pair("earlybonus", earlyBonus));
    response.push_back(Pair("percenttoissuer", percentToIssuer));
    response.push_back(Pair("starttime", startTime));
    response.push_back(Pair("deadline", deadline));

    if (divisibleDesired)
    {
        response.push_back(Pair("amountraised", ValueFromAmount(amountRaised)));
    }
    else
    {
        response.push_back(Pair("amountraised", amountRaised));
    }
    if (divisible)
    {
        response.push_back(Pair("tokensissued", ValueFromAmount(tokensIssued)));
    }
    else
    {
        response.push_back(Pair("tokensissued", tokensIssued));
    }
    if (!active) response.push_back(Pair("closedearly", "unknown"));
    if (!active) response.push_back(Pair("endedtime", "unknown"));

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
                  responseObj.push_back(Pair("tokensperunit", ValueFromAmount(tokensPerUnit)));
              }
              else
              {
                  responseObj.push_back(Pair("tokensperunit", tokensPerUnit));
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

Value listblocktransactions_MP(const Array& params, bool fHelp)
{
   if (fHelp)
        throw runtime_error(
            "listblocktransactions_MP\n"
            "\nList MP TXIDs in a block\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"txid\",     (string) MP txid\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("listblocktransactions_MP", "")
            + HelpExampleRpc("listblocktransactions_MP", "")
        );

  // firstly let's get the block height given in the param
  int blockHeight = params[0].get_int();
  if (blockHeight < 0 || blockHeight > chainActive.Height())
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
        // display all offers with accepts
/*
        for(map<string, CMPOffer>::iterator my_it = my_offers.begin(); my_it != my_offers.end(); ++my_it)
        {
          // my_it->first = key
          // my_it->second = value
        }
*/

        fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);

        // display all balances
        for(map<string, CMPTally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it)
        {
          // my_it->first = key
          // my_it->second = value

          printf("%34s => ", (my_it->first).c_str());
          (my_it->second).print(extra2, bDivisible);
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
      printf("isMPinBlockRange(%d,%d)=%s\n", extra2, extra3, isMPinBlockRange(extra2, extra3) ? "YES":"NO");
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

int mastercore_handler_disc_begin(int nBlockNow, CBlockIndex const * pBlockIndex) {
    return 0;
}

int mastercore_handler_disc_end(int nBlockNow, CBlockIndex const * pBlockIndex) {
    printf("\n\n\n BLOCK DISCONNECTED !!!! blockinfo %s \n\n\n", pBlockIndex->ToString().c_str() );
    //from prune_state_files();
    const char *blockHashStr = pBlockIndex->GetBlockHash().ToString().c_str();
    for (int i = 0; i < NUM_FILETYPES; ++i) {
      boost::filesystem::remove(MPPersistencePath / (boost::format("%s-%s.dat") % statePrefix[i] % blockHashStr).str());
    }

    printf("\n\n\n Noticed a re-org, disabling persistence for your own safety. Please restart this instance for the most accurate balances. \n\n\n" );
    //Bad blocks found, must restart to persist.
    disable_Persistence = 1;

    int scan_ret=msc_initial_scan(pBlockIndex->nHeight);
    printf("Restarted scan? %d", scan_ret);
    
    return 0;
}
