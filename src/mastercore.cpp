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

#include <boost/assign/list_of.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"

#include "leveldb/db.h"

#include <openssl/sha.h>

// #define DISABLE_LOG_FILE 
static FILE *mp_fp = NULL;

#include "mastercore.h"

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;
using namespace leveldb;

int nWaterlineBlock = 0;  // the DEX block, using Zathras' msc_balances_290629.txt

// uint64_t global_MSC_total = 0;
// uint64_t global_MSC_RESERVED_total = 0;

static string exodus = "1EXoDusjGwvnjZUyKkxZ4UHEf77z6A5S4P";
static uint64_t exodus_prev = DEV_MSC_BLOCK_290629;
// static uint64_t exodus_prev = 0; // Bart has 0 for some reason ???
static uint64_t exodus_balance;

static boost::filesystem::path MPPersistencePath;

/*
int msc_debug0 = 0;
int msc_debug  = 0;
int msc_debug2 = 1;
int msc_debug3 = 0;
int msc_debug4 = 1;
int msc_debug5 = 1;
int msc_debug6 = 0;
*/
int msc_debug0 = 1;
int msc_debug  = 1;
int msc_debug2 = 1;
int msc_debug3 = 1;
int msc_debug4 = 1;
int msc_debug5 = 1;
int msc_debug6 = 1;

int msc_debug_dex = 1;
int msc_debug_send = 1;
int msc_debug_spec = 1;

// follow this variable through the code to see how/which Master Protocol transactions get invalidated
static int InvalidCount_per_spec = 0; // consolidate error messages into a nice log, for now just keep a count
static int BitcoinCore_errors = 0;    // TODO: watch this count, check returns of all/most Bitcoin core functions !

// disable TMSC handling for now, has more legacy corner cases
static int ignore_all_but_MSC = 1;
static int disableLevelDB = 0;
static int disable_Persistence = 1;

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

char *c_strMastercoinCurrency(unsigned int i)
{
  // test user-token
  if (0x80000000 & i)
  {
    // TODO: extend it to general case currencies
    switch (0x7FFFFFFF & i)
    {
      case 0: return ((char *)"test user token 0");
      case 1: return ((char *)"test user token 1");
      case 2: return ((char *)"test user token 2");
      case 3: return ((char *)"test user token 3");
      case 4: return ((char *)"test user token 4");
      default: return ((char *)"* unknown test currency *");
    }
  }
  else
  switch (i)
  {
    case 0: return ((char *)"BTC");
    case MASTERCOIN_CURRENCY_MSC: return ((char *)"MSC");
    case MASTERCOIN_CURRENCY_TMSC: return ((char *)"TMSC");
    case MASTERCOIN_CURRENCY_SP1: return ((char *)"SP");  // first SP MaidSafe coin -- name will be pulled from the protocol of course, TODO
    default: return ((char *)"* unknown currency *");
  }
}

char *c_strMastercoinType(int i)
{
  switch (i)
  {
    case MSC_TYPE_SIMPLE_SEND: return ((char *)"Simple Send");
    case 1: return ((char *)"Investment Send");
    case MSC_TYPE_TRADE_OFFER: return ((char *)"DEx Sell Offer");
    case 21: return ((char *)"Offer/Accept one Master Protocol Coins for another");
    case MSC_TYPE_ACCEPT_OFFER_BTC: return ((char *)"DEx Accept Offer");
    case MSC_TYPE_CREATE_PROPERTY_FIXED: return ((char *)"Create Property - Fixed");
    case MSC_TYPE_CREATE_PROPERTY_VARIABLE: return ((char *)"Create Property - Variable");
    case MSC_TYPE_PROMOTE_PROPERTY: return ((char *)"Promote Property");
    default: return ((char *)"* unknown type *");
  }
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
    if (msc_debug4) fprintf(mp_fp, "%s(%lu): %s , line %d, file: %s\n", __FUNCTION__, a, txid.GetHex().c_str(), __LINE__, __FILE__);
  }

  void set(uint64_t d, uint64_t fee, unsigned char btl, unsigned char suba)
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

  void print(const string &addr, bool bFlag = false)
  {
    // placeholder
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
      // hm, can't access the outer class' map member to get the currency unit label... do we care?
//      fprintf(mp_fp, "buying: %12.8lf (originally= %12.8lf) in block# %d, fee: %2.8lf\n",
//       (double)accept_amount_remaining/(double)COIN, (double)accept_amount_original/(double)COIN, block, (double)fee_paid/(double)COIN);
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

CCriticalSection cs_tally;

static map<string, CMPOffer> my_offers;
static map<string, CMPAccept> my_accepts;

// this is the master list of all amounts for all addresses for all currencies, map is sorted by Bitcoin address
map<string, CMPTally> mp_tally_map;

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

// bSet will SET the amount into the address, instead of just updating it
// bool update_tally_map(string who, unsigned int which, int64_t amount, bool bReserved = false, bool bSet = false)
//
// return true if everything is ok
bool update_tally_map(string who, unsigned int which, int64_t amount, TallyType ttype, bool bSet = false)
{
bool bRet = false;
uint64_t before, after;

  LOCK(cs_tally);

  before = getMPbalance(who, which, ttype);

  map<string, CMPTally>::iterator my_it = mp_tally_map.find(who);
  if (my_it == mp_tally_map.end())
  {
    // insert an empty element
    my_it = (mp_tally_map.insert(std::make_pair(who,CMPTally(which)))).first;
  }

  CMPTally &tally = my_it->second;

  switch (ttype)
  {
    case MONEY:
      bRet = tally.msc_update_moneys(which, amount);
      break;

    case SELLOFFER_RESERVE:
      bRet = tally.msc_update_reserved(which, amount);
      break;

    case ACCEPT_RESERVE:
      bRet = tally.msc_update_raccepted(which, amount);
      break;
  }

  after = getMPbalance(who, which, ttype);
  if (!bRet) fprintf(mp_fp, "%s(%s, %d, %+ld, ttype= %d) INSUFFICIENT FUNDS\n", __FUNCTION__, who.c_str(), which, amount, ttype);

  if (msc_debug_dex) fprintf(mp_fp, "%s(%s, %d, %+ld, ttype=%d); before=%lu, after=%lu\n",
   __FUNCTION__, who.c_str(), which, amount, ttype, before, after);

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

  if (DEx_offerExists(seller_addr, curr)) return (DEX_ERROR_SELLOFFER -10);  // offer already exists

  const string combo = STR_SELLOFFER_ADDR_CURR_COMBO(seller_addr);

  if (msc_debug4)
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

  if (msc_debug4)
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
int rc = DEX_ERROR_ACCEPT;
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
  offer.print((my_it->first), true);

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
int rc = DEX_ERROR_ACCEPT;
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
int curr = MASTERCOIN_CURRENCY_MSC; // FIXME: hard-coded to MSC
CMPAccept *p_accept = DEx_getAccept(seller, curr, buyer);

  if (msc_debug4) fprintf(mp_fp, "%s(%s, %s), line %d, file: %s\n", __FUNCTION__, seller.c_str(), buyer.c_str(), __LINE__, __FILE__);

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

// this class is the in-memory structure for the various MSC transactions (types) I've processed
//  ordered by the block #
// The class responsible for sorted tx parsing. It does the initial block parsing (via a priority_queue, sorted).
// Also used as new block are received.
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
  unsigned char pkt[MAX_PACKETS * PACKET_SIZE];
  uint64_t nValue;
  int multi;  // Class A = 0, Class B = 1
  uint64_t tx_fee_paid;
  unsigned int type, currency;
  unsigned short version; // = MP_TX_PKT_V0;
  uint64_t nNewValue;

public:
//  mutable CCriticalSection cs_msc;  // TODO: need to refactor first...

  unsigned int getType() const { return type; }
  const string getTypeString() const { return string(c_strMastercoinType(getType())); }
  unsigned int getCurrency() const { return currency; }
  unsigned short getVersion() const { return version; }
  uint64_t getFeePaid() const { return tx_fee_paid; }

  const string & getSender() const { return sender; }
  const string & getReceiver() const { return receiver; }

  uint64_t getAmount() const { return nValue; }
  uint64_t getNewAmount() const { return nNewValue; }

 // the 31-byte packet & the packet #
 // int interpretPacket(int blocknow, unsigned char pkt[], int size)
 // NOTE: TMSC is ignored for now...
 //
 // RETURNS:  0 if the packet is fully valid
 // RETURNS: <0 if the packet is invalid
 // RETURNS: >0 if the packet is valid, BUT nValue was augmented into nNewValue (funds adjusted up or down, use getNewAmount())
 //
 // 
 // TODO: verify with Zathras & Faiz !!!
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
 uint64_t amount_desired, min_fee;
 unsigned char blocktimelimit, subaction = 0;
 int rc = PKT_ERROR;

  if (0>parse()) return -98765;

  if ((obj_o) && (MSC_TYPE_TRADE_OFFER != type)) return -777; // can't fill in the Offer object !

  if ((MSC_TYPE_SIMPLE_SEND != type) && (290630>block)) return -88888;

  // further processing for complex types
  // TODO: version may play a role here !
  switch(type)
  {
    case MSC_TYPE_SIMPLE_SEND:
      if (sender.empty()) ++InvalidCount_per_spec;
      // special case: if can't find the receiver -- assume sending to itself !
      // may also be true for BTC payments........
      // TODO: think about this..........
      if (receiver.empty())
      {
        receiver = sender;
      }
      if (receiver.empty()) ++InvalidCount_per_spec;
      if (!update_tally_map(sender, currency, - nValue, MONEY)) break;
      update_tally_map(receiver, currency, nValue, MONEY);
      rc = 0;
      break;

    case MSC_TYPE_TRADE_OFFER:
    {
    enum ActionTypes { INVALID = 0, NEW = 1, UPDATE = 2, CANCEL = 3 };
    const char * const subaction_name[] = { "empty", "new", "update", "cancel" };

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
        obj_o->set(amount_desired, min_fee, blocktimelimit, subaction);
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

      break;
    } // end of TRADE_OFFER

    case MSC_TYPE_ACCEPT_OFFER_BTC:
      // the min fee spec requirement is checked in the following function
      rc = DEx_acceptCreate(sender, receiver, currency, nValue, block, tx_fee_paid, &nNewValue);
      break;

    case MSC_TYPE_CREATE_PROPERTY_FIXED:

      break;

    case MSC_TYPE_CREATE_PROPERTY_VARIABLE:

      break;

    default:

      return (PKT_ERROR -100);
  }

  return rc;
 }

 int parse()
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
  memcpy(&currency, &pkt[4], 4);
  memcpy(&nValue, &pkt[8], 8);

  // FIXME: only do swaps for little-endian system(s) !
  swapByteOrder32(type);
  swapByteOrder32(currency);
  swapByteOrder64(nValue);

  if (ignore_all_but_MSC)
  if (currency != MASTERCOIN_CURRENCY_MSC)
  {
    fprintf(mp_fp, "IGNORING NON-MSC packet for NOW !!!\n");
    return (PKT_ERROR -2);
  }

  fprintf(mp_fp, "version: %d, Class %s\n", version, !multi ? "A":"B");

  fprintf(mp_fp, "\t            type: %u (%s)\n", type, c_strMastercoinType(type));
  fprintf(mp_fp, "\t        currency: %u (%s)\n", currency, c_strMastercoinCurrency(currency));
  fprintf(mp_fp, "\t           value: %lu.%08lu\n", nValue/COIN, nValue%COIN);

  return (type);
 }

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

    memset(&pkt, 0, sizeof(pkt));
  }

  CMPTransaction()
  {
    SetNull();
  }

  void set(const uint256 &t, int b, unsigned int idx, uint64_t txf = 0)
  {
    txid = t;
    block = b;
    tx_idx = idx;
  }

  void set(string s, string r, uint64_t n, const uint256 &t, int b, unsigned int idx, unsigned char *p, unsigned int size, int fMultisig, uint64_t txf)
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

  if (msc_debug0) fprintf(mp_fp, "devmsc=%lu, exodus_prev=%lu, exodus_delta=%ld\n", devmsc, exodus_prev, exodus_delta);

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

// called once per block
// it performs cleanup and other functions
int mastercore_handler_block(int nBlockNow, CBlockIndex const * pBlockIndex)
{
// for every new received block must do:
// 1) remove expired entries from the accept list (per spec accept entries are valid until their blocklimit expiration; because the customer can keep paying BTC for the offer in several installments)
// 2) update the amount in the Exodus address
uint64_t devmsc = 0;
unsigned int how_many_erased = eraseExpiredAccepts(nBlockNow);

  if (how_many_erased) fprintf(mp_fp, "%s(%d); erased %u accepts this block, line %d, file: %s\n",
   __FUNCTION__, how_many_erased, nBlockNow, __LINE__, __FILE__);

  // calculate devmsc as of this block and update the Exodus' balance
  devmsc = calculate_and_update_devmsc(pBlockIndex->GetBlockTime());

  if (msc_debug0) fprintf(mp_fp, "devmsc for block %d: %lu, Exodus balance: %lu\n", nBlockNow, devmsc, getMPbalance(exodus, MASTERCOIN_CURRENCY_MSC, MONEY));

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

      if (msc_debug6) if (5>j) fprintf(mp_fp, "%d: sha256 hex: %s\n", j, ObfsHashes[j].c_str());
      strcpy((char *)sha_input, ObfsHashes[j].c_str());
  }
}

static bool getOutputType(const CScript& scriptPubKey, txnouttype& whichTypeRet)
{
vector<vector<unsigned char> > vSolutions;

  if (!Solver(scriptPubKey, whichTypeRet, vSolutions)) return false;

  return true;
}

int TXExodusFundraiser(const CTransaction &wtx, int nBlock, unsigned int nTime) {
  printf("nBlock is: %d\n, nTime is: %u", nBlock, nTime);
  if (nBlock >= 249498 && nBlock <= 255365) { //Exodus Fundraiser start/end blocks
    printf("transaction: %s\n", wtx.ToString().c_str() );
    return 0;
  }
  return -1;
}

// idx is position within the block, 0-based
// int msc_tx_push(const CTransaction &wtx, int nBlock, unsigned int idx)

// RETURNS: 0 if parsed a MP TX

int msc_tx_populate(const CTransaction &wtx, int nBlock, unsigned int idx, CMPTransaction *mp_tx, unsigned int nTime=0 )
{
string strSender;
// class A: data & address storage -- combine them into a structure or something
vector<string>script_data;
vector<string>address_data;
// vector<uint64_t>value_data;
vector<int64_t>value_data;
int64_t ExodusValues[MAX_BTC_OUTPUTS];
int64_t ExodusHighestValue = 0;
string strReference;
unsigned char single_pkt[MAX_PACKETS * PACKET_SIZE];
unsigned int packet_size = 0;
int fMultisig = 0;
int marker_count = 0;
// class B: multisig data storage
vector<string>multisig_script_data;
uint64_t inAll = 0;
uint64_t outAll = 0;
uint64_t txFee = 0;

            mp_tx->set(wtx.GetHash(), nBlock, idx);

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
                  // TODO: add other checks to verify a mastercoin tx !!!
                  ExodusValues[marker_count++] = wtx.vout[i].nValue;

                  if (ExodusHighestValue < wtx.vout[i].nValue) ExodusHighestValue = wtx.vout[i].nValue;
                }
              }
            }

            // TODO: ensure only 1 output to the Exodus address is allowed (corner case?)
//            if (1 != marker_count)
            if (!marker_count)  // 1Exodus is a special case, TODO: resolve later after PoC Sprint
// https://github.com/m21/mastercore/commit/fbf06f6dbda06b5a8cf061414ff76f42194544d8#diff-7322bd4b20fe7eed15aa568e8905f657R607
            {
              // not Mastercoin -- no output to the 'marker' Exodus or more than 1 -- more than 1 is OK per Zathras, May 2014
              // TODO: if multiple markers are visible : how is nExodusValue calculated?
  // [11:33:08 PM] my99key: I don't have a case -- so if multiple outputs to 1Exodus are found in a Class A TX -- it is not a valid MP TX?
  // [11:33:36 PM] faiz: as far as i know, the only case that it would be valid is if the TX was actually also sent by exodus
  // So, send from 1Exodus to self may have multiple outputs in a Class A TX !!!
              return -1;
            }
            
            if(0==TXExodusFundraiser(wtx, nBlock, nTime)) {
               //Exodus Fundraiser
               printf("I've done nothing.\n");
               //fprintf data
            }

            fprintf(mp_fp, "%s(block=%d, idx= %d), line %d, file: %s\n", __FUNCTION__, nBlock, idx, __LINE__, __FILE__);
            fprintf(mp_fp, "____________________________________________________________________________________________________________________________________\n");
            if (msc_debug3) fprintf(mp_fp, "================BLOCK: %d======\ntxid: %s\n", nBlock, wtx.GetHash().GetHex().c_str());

            // now save output addresses & scripts for later use
            // also determine if there is a multisig in there, if so = Class B
            for (unsigned int i = 0; i < wtx.vout.size(); i++)
            {
            CTxDestination dest;
            string strAddress;

              if (ExtractDestination(wtx.vout[i].scriptPubKey, dest))
              {
                strAddress = CBitcoinAddress(dest).ToString();

                if (exodus != strAddress)
                {
                  if (msc_debug3) fprintf(mp_fp, "saving address_data #%d: %s:%s\n", i, strAddress.c_str(), wtx.vout[i].scriptPubKey.ToString().c_str());

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

            if (msc_debug3)
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

            if (msc_debug) fprintf(mp_fp, "vin=%d:%s\n", i, wtx.vin[i].scriptSig.ToString().c_str());

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

              if (msc_debug) fprintf(mp_fp, "vin=%d:%s\n", i, wtx.vin[i].ToString().c_str());
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
              if (msc_debug2) fprintf(mp_fp, "The Sender: %s : His Input Sum of Values= %lu.%08lu ; fee= %lu.%08lu\n",
               strSender.c_str(), nMax / COIN, nMax % COIN, txFee/COIN, txFee%COIN);
            }
            else
            {
              fprintf(mp_fp, "The sender is still EMPTY !!! txid: %s\n", wtx.GetHash().GetHex().c_str());
              return -5;
            }

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
        if (msc_debug) fprintf(mp_fp, "scriptPubKey: %s\n", wtx.vout[i].scriptPubKey.getHex().c_str());

        if (ExtractDestinations(wtx.vout[i].scriptPubKey, type, vDest, nRequired))
        {
          if (msc_debug) fprintf(mp_fp, " >> multisig: ");
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
              if (msc_debug) fprintf(mp_fp, "%s ; ", address.ToString().c_str());

            }
          if (msc_debug) fprintf(mp_fp, "\n");

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
                          if (msc_debug3) fprintf(mp_fp, "Data Address located - data[%d]:%s: %s (%lu.%08lu)\n", k, script_data[k].c_str(), address_data[k].c_str(), value_data[k] / COIN, value_data[k] % COIN);
                      }
                      else
                      {
                          // invalidate - Class A cannot be more than one data packet - possible collision, treat as default (BTC payment)
                          strDataAddress = ""; //empty strScriptData to block further parsing
                          if (msc_debug3) fprintf(mp_fp, "Multiple Data Addresses found (collision?) Class A invalidated, defaulting to BTC payment\n");
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
                              if (msc_debug3) fprintf(mp_fp, "Reference Address located via seqnum - data[%d]:%s: %s (%lu.%08lu)\n", k, script_data[k].c_str(), address_data[k].c_str(), value_data[k] / COIN, value_data[k] % COIN);
                          }
                          else
                          {
                              // can't trust sequence numbers to provide reference address, there is a collision with >1 address with expected seqnum
                              strRefAddress = ""; // blank ref address
                              if (msc_debug3) fprintf(mp_fp, "Reference Address sequence number collision, will fall back to evaluating matching output amounts\n");
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
                                           if (msc_debug3) fprintf(mp_fp, "Reference Address located via matching amounts - data[%d]:%s: %s (%lu.%08lu)\n", k, script_data[k].c_str(), address_data[k].c_str(), value_data[k] / COIN, value_data[k] % COIN);
                                       }
                                       else
                                       {
                                           strRefAddress = "";
                                           if (msc_debug3) fprintf(mp_fp, "Reference Address collision, multiple potential candidates. Class A invalidated, defaulting to BTC payment\n");
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
              if (msc_debug2 || msc_debug4) fprintf(mp_fp, "\n================BLOCK: %d======\ntxid: %s\n", nBlock, wtx.GetHash().GetHex().c_str());
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
//                    if (strSender == strAddress) continue;
                    fprintf(mp_fp, "payment #%d %s %11.8lf\n", ++count, strAddress.c_str(), (double)wtx.vout[i].nValue/(double)COIN);

                    // check everything & pay BTC for the currency we are buying here...
                    DEx_payment(strAddress, strSender, wtx.vout[i].nValue, nBlock);
//                    return 0;
                  }
                }
              }
              return count;
            }
            else
            {
            // valid Class A packet almost ready
              if (msc_debug3) fprintf(mp_fp, "valid Class A:from=%s:to=%s:data=%s\n", strSender.c_str(), strReference.c_str(), strScriptData.c_str());
              packet_size = PACKET_SIZE_CLASS_A;
              memcpy(single_pkt, &ParseHex(strScriptData)[0], packet_size);
            }
          }
          else // if (fMultisig)
          {
            unsigned int k = 0;
            // gotta find the Reference - Z rewrite - scrappy & inefficient, can be optimized

            if (msc_debug3) fprintf(mp_fp, "Beginning reference identification\n");

            bool referenceFound = false; // bool to hold whether we've found the reference yet
            bool changeRemoved = false; // bool to hold whether we've ignored the first output to sender as change
            unsigned int potentialReferenceOutputs = 0; // int to hold number of potential reference outputs

            // how many potential reference outputs do we have, if just one select it right here
            BOOST_FOREACH(const string &addr, address_data)
            {
                // keep Michael's original debug info & k int as used elsewhere
                if (msc_debug3) fprintf(mp_fp, "ref? data[%d]:%s: %s (%lu.%08lu)\n",
                 k, script_data[k].c_str(), addr.c_str(), value_data[k] / COIN, value_data[k] % COIN);
                ++k;

                if (addr != exodus)
                {
                        ++potentialReferenceOutputs;
                        if (1 == potentialReferenceOutputs)
                        {
                                strReference = addr;
                                referenceFound = true;
                                if (msc_debug3) fprintf(mp_fp, "Single reference potentially id'd as follows: %s \n", strReference.c_str());
                        }
                        else //as soon as potentialReferenceOutputs > 1 we need to go fishing
                        {
                                strReference = ""; // avoid leaving strReference populated for sanity
                                referenceFound = false;
                                if (msc_debug3) fprintf(mp_fp, "More than one potential reference candidate, blanking strReference, need to go fishing\n");
                        }
                }
            }

            // do we have a reference now? or do we need to dig deeper
            if (!referenceFound) // multiple possible reference addresses
            {
                if (msc_debug3) fprintf(mp_fp, "Reference has not been found yet, going fishing\n");

                BOOST_FOREACH(const string &addr, address_data)
                {
                        // !!!! address_data is ordered by vout (i think - please confirm that's correct analysis?)
                        if (addr != exodus) // removed strSender restriction, not to spec
                        {
                                if ((addr == strSender) && (!changeRemoved))
                                {
                                        // per spec ignore first output to sender as change if multiple possible ref addresses
                                        changeRemoved = true;
                                        if (msc_debug3) fprintf(mp_fp, "Removed change\n");
                                }
                                else
                                {
                                        // this may be set several times, but last time will be highest vout
                                        strReference = addr;
                                        if (msc_debug3) fprintf(mp_fp, "Resetting strReference as follows: %s \n ", strReference.c_str());
                                }
                        }
                }
            }

          if (msc_debug3) fprintf(mp_fp, "Ending reference identification\n");
          if (msc_debug3) fprintf(mp_fp, "Final decision on reference identification is: %s\n", strReference.c_str());

          if (msc_debug0) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
          // multisig , Class B; get the data packets that are found here
          for (unsigned int k = 0; k<multisig_script_data.size();k++)
          {

          if (msc_debug0) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
            CPubKey key(ParseHex(multisig_script_data[k]));
            CKeyID keyID = key.GetID();
            string strAddress = CBitcoinAddress(keyID).ToString();
            char *c_addr_type = (char *)"";
            string strPacket;

          if (msc_debug0) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
/*
            if (exodus == strAddress) c_addr_type = (char *)" (EXODUS)";
            else
            if (strAddress == strSender)
            {
              c_addr_type = (char *)" (SENDER)";
              // Are we the sender of BTC here?
              // TODO: do we care???
            }
            else
*/
            {
              // this is a data packet, must deobfuscate now
              vector<unsigned char>hash = ParseHex(strObfuscatedHashes[mdata_count+1]);      
              vector<unsigned char>packet = ParseHex(multisig_script_data[k].substr(2*1,2*PACKET_SIZE));

              for (unsigned int i=0;i<packet.size();i++)
              {
                packet[i] ^= hash[i];
              }

              // ensure the first byte of the first packet is 01-0x10; is it the sequence number???
//              if (MAX_NUMBER_OF_DATA_PACKETS_PER_MULTISIG >= packet[0])
//              if (03 >= packet[0])

                memcpy(&packets[mdata_count], &packet[0], PACKET_SIZE);
                strPacket = HexStr(packet.begin(),packet.end(), false);
                ++mdata_count;

                if (MAX_PACKETS <= mdata_count)
                {
                  fprintf(mp_fp, "increase MAX_PACKETS ! mdata_count= %d\n", mdata_count);
                  return -222;
                }
            }

          if (msc_debug3) fprintf(mp_fp, "multisig_data[%d]:%s: %s%s\n", k, multisig_script_data[k].c_str(), strAddress.c_str(), c_addr_type);

            if (!strPacket.empty())
            {
              if (msc_debug) fprintf(mp_fp, "packet #%d: %s\n", mdata_count, strPacket.c_str());
            }
          if (msc_debug0) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
          }

          packet_size = mdata_count * (PACKET_SIZE - 1);

          if (sizeof(single_pkt)<packet_size)
          {
            return -111;
          }

          if (msc_debug0) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
          } // end of if (fMultisig)
          if (msc_debug0) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);

            // now decode mastercoin packets
            for (int m=0;m<mdata_count;m++)
            {
              if (msc_debug0) fprintf(mp_fp, "m=%d: %s\n", m, HexStr(packets[m], PACKET_SIZE + packets[m], false).c_str());

              // check to ensure the sequence numbers are sequential and begin with 01 !
              if (1+m != packets[m][0])
              {
                if (msc_debug_spec) fprintf(mp_fp, "Error: non-sequential seqnum ! expected=%d, got=%d\n", 1+m, packets[m][0]);
              }

              // now ignoring sequence numbers for Class B packets
              memcpy(m*(PACKET_SIZE-1)+single_pkt, 1+packets[m], PACKET_SIZE-1);
            }

  if (msc_debug2) fprintf(mp_fp, "single_pkt: %s\n", HexStr(single_pkt, packet_size + single_pkt, false).c_str());

  mp_tx->set(strSender, strReference, 0, wtx.GetHash(), nBlock, idx, (unsigned char *)&single_pkt, packet_size, fMultisig, (inAll-outAll));  

  return 0;
}

// display the tally map & the offer/accept list(s)
Value mscrpc(const Array& params, bool fHelp)
{
int extra = 0;

    if (fHelp || params.size() > 1)
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

  // various extra tests
  switch (extra)
  {
    case 0: // the old output
        // display all offers with accepts
        for(map<string, CMPOffer>::iterator my_it = my_offers.begin(); my_it != my_offers.end(); ++my_it)
        {
          // my_it->first = key
          // my_it->second = value
          (my_it->second).print((my_it->first), true);
        }

        fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);

        // display all balances
        for(map<string, CMPTally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it)
        {
          // my_it->first = key
          // my_it->second = value

          printf("%34s => ", (my_it->first).c_str());
          (my_it->second).print();
        }
      break;

    case 1:
      // display the whole CMPTxList (leveldb)
      p_txlistdb->printAll();
      p_txlistdb->printStats();
      break;
  }

  return GetHeight();
}

// parse blocks, starting right after the preseed
int msc_post_preseed(int nHeight)
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

    if (msc_debug0) fprintf(mp_fp, "%s(%d; max=%d):%s, line %d, file: %s\n",
     __FUNCTION__, blockNum, max_block, strBlockHash.c_str(), __LINE__, __FILE__);

    ReadBlockFromDisk(block, pblockindex);

    int tx_count = 0;
    BOOST_FOREACH(const CTransaction&tx, block.vtx)
    {
      mastercore_handler_tx(tx, blockNum, tx_count, block.GetBlockHeader());

      ++tx_count;
    }

    n_total += tx_count;
    if (msc_debug0) fprintf(mp_fp, "%4d:n_total= %d, n_found= %d\n", blockNum, n_total, n_found);

    mastercore_handler_block(blockNum, pblockindex);
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
uint64_t  uValue = 0, uSellReserved = 0, uAcceptReserved = 0;
std::vector<std::string> vstr;
boost::split(vstr, s, boost::is_any_of(" ,="), token_compress_on);
int i = 0;
string strAddress = vstr[0];

//  if (msc_debug4) { BOOST_FOREACH(const string &debug_str, vstr) printf("%s\n", debug_str.c_str()); }

  ++i;

  uValue = boost::lexical_cast<boost::uint64_t>(vstr[i++]);
  uSellReserved = boost::lexical_cast<boost::uint64_t>(vstr[i++]);
  if (vstr.size() > 3) { // FIXME: need to add accepted reserve in here from preseed !
    uAcceptReserved = boost::lexical_cast<boost::uint64_t>(vstr[i++]);
  }
  
  // want to bypass 0-value addresses...
  if ((0 == uValue) && (0 == uSellReserved) && (0 == uAcceptReserved)) return 0;

  // ignoring TMSC for now...
  update_tally_map(strAddress, MASTERCOIN_CURRENCY_MSC, uValue, MONEY);
  update_tally_map(strAddress, MASTERCOIN_CURRENCY_MSC, uSellReserved, SELLOFFER_RESERVE);
  update_tally_map(strAddress, MASTERCOIN_CURRENCY_MSC, uAcceptReserved, ACCEPT_RESERVE);

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
int input_devmsc_state_string(const string &s)
{
  uint64_t exodusPrev;
  std::vector<std::string> vstr;
  boost::split(vstr, s, boost::is_any_of(" ,="), token_compress_on);
  if (1 != vstr.size()) return -1;

  int i = 0;
  exodusPrev = boost::lexical_cast<uint64_t>(vstr[i++]);
  exodus_prev = exodusPrev;

  return 0;
}

static int msc_file_load(const string &filename, int what, bool verifyHash = false)
{
  int lines = 0;
  int (*inputLineFunc)(const string &) = NULL;

  // TODO: think about placement for preseed files -- perhaps the directory where executables live is better?
  // these files are read-only preseeds
  // all run-time updates should go to a KV-store (leveldb is envisioned)

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

    case FILETYPE_DEVMSC:
      inputLineFunc = input_devmsc_state_string;
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

  printf("%s(): file: %s, loaded lines= %d\n", __FUNCTION__, filename.c_str(), lines);
  LogPrintf("%s(): file: %s, loaded lines= %d\n", __FUNCTION__, filename, lines);

  return res;
}

static int msc_preseed_file_load(int what)
{
  // uses boost::filesystem::path
  const string filename = (GetDataDir() / mastercore_filenames[what]).string();
  return msc_file_load(filename, what);
}

static char const * const statePrefix[NUM_FILETYPES] = {
    "balances",
    "offers",
    "accepts",
    "devmsc",
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

  map<string, CMPTally>::const_iterator iter;
  for (iter = mp_tally_map.begin(); iter != mp_tally_map.end(); ++iter) {
    uint64_t mscBalance = (*iter).second.getMoney(MASTERCOIN_CURRENCY_MSC, MONEY);
    uint64_t mscSellReserved = (*iter).second.getMoney(MASTERCOIN_CURRENCY_MSC, SELLOFFER_RESERVE);
    uint64_t mscAcceptReserved = (*iter).second.getMoney(MASTERCOIN_CURRENCY_MSC, ACCEPT_RESERVE);

    // we don't allow 0 balances to read in, so if we don't write them
    // it makes things match up better between peristed state and processed state
    if ( 0 == mscBalance && 0 == mscSellReserved && 0 == mscAcceptReserved ) {
      continue;
    }

    string lineOut = (boost::format("%s,%d,%d,%d")
        % (*iter).first
        % mscBalance
        % mscSellReserved
        % mscAcceptReserved).str();

    // add the line to the hash
    SHA256_Update(shaCtx, lineOut.c_str(), lineOut.length());

    // write the line
    file << lineOut << endl;
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

static int write_devmsc_state(ofstream &file, SHA256_CTX *shaCtx)
{
  string lineOut = (boost::format("%d") % exodus_prev).str();

  // add the line to the hash
  SHA256_Update(shaCtx, lineOut.c_str(), lineOut.length());

  // write the line
  file << lineOut << endl;

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

  case FILETYPE_DEVMSC:
    result = write_devmsc_state(file, &shaCtx);
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
      if (curIndex) {
        fprintf(mp_fp, "State from Block:%s is no longer need, removing files (age-from-tip: %d)\n", (*iter).ToString().c_str(), topIndex->nHeight - curIndex->nHeight);
      } else {
        fprintf(mp_fp, "State from Block:%s is no longer need, removing files (not in index)\n", (*iter).ToString().c_str());
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
  write_state_file(pBlockIndex, FILETYPE_DEVMSC);

  // clean-up the directory
  prune_state_files(pBlockIndex);

  return 0;
}

// called from init.cpp of Bitcoin Core
int mastercore_init()
{
const bool bTestnet = TestNet();

  printf("%s()%s, line %d, file: %s\n", __FUNCTION__, bTestnet ? "TESTNET":"", __LINE__, __FILE__);
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

  if (bTestnet)
  {
    exodus = "mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv";
    ignore_all_but_MSC = 0;
  }

  p_txlistdb = new CMPTxList(GetDataDir() / "MP_txlist", 1<<20, false, fReindex);
  MPPersistencePath = GetDataDir() / "MP_persist";
  boost::filesystem::create_directories(MPPersistencePath);

  // this is the height of the data included in the preseeds
  static const int snapshotHeight = 290629;
  static const uint64_t snapshotDevMSC = 1743358325718;

  if (!disable_Persistence)
  {
    nWaterlineBlock = load_most_relevant_state();

    if (nWaterlineBlock < snapshotHeight)
    {
      // the DEX block, using Zathras' msc_balances_290629.txt
      (void) msc_preseed_file_load(FILETYPE_BALANCES);
      (void) msc_preseed_file_load(FILETYPE_OFFERS);
      nWaterlineBlock = snapshotHeight;
      exodus_prev=snapshotDevMSC;
    }

    // advance the waterline so that we start on the next unaccounted for block
    nWaterlineBlock += 1;
  }
  else
  {
  // my old preseed way

    nWaterlineBlock = 249497;  // the DEX block, using Zathras' msc_balances_290629.txt

    if (bTestnet) nWaterlineBlock = 250000; //testnet3

    (void) msc_preseed_file_load(FILETYPE_BALANCES);
  }

  // collect the real Exodus balances available at the snapshot time
  exodus_balance = getMPbalance(exodus, MASTERCOIN_CURRENCY_MSC, MONEY);
  printf("Exodus balance: %lu\n", exodus_balance);

  if (!bTestnet)
  {
    (void) msc_post_preseed(nWaterlineBlock);
  }
  else
  {
    // testnet
    (void) msc_post_preseed(GetHeight()-10000); // sometimes testnet blocks get generated very fast, scan the last 1000 just for fun
  }

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
int mastercore_handler_tx(const CTransaction &tx, int nBlock, unsigned int idx, CBlockHeader pBlockHeader)
{
CMPTransaction mp_obj;
// save the augmented offer or accept amount into the database as well (expecting them to be numerically lower than that in the blockchain)
int rc, pop_ret;

  if (nBlock < nWaterlineBlock) return -1;  // we do not care about parsing blocks prior to our waterline (empty blockchain defense)

  pop_ret = msc_tx_populate(tx, nBlock, idx, &mp_obj, pBlockHeader.nTime );
  if (0 == pop_ret)
  {
  // true MP transaction, validity (such as insufficient funds, or offer not found) is determined elsewhere

    rc = mp_obj.interpretPacket();
    if (rc) fprintf(mp_fp, "!!! interpretPacket() returned %d !!!!!!!!!!!!!!!!!!!!!!\n", rc);

    mp_obj.print();

    // TODO : this needs to be pulled into the refactored parsing engine since its validity is not know in this function !
    // FIXME: and of course only MP-related TXs will be recorded...
    if (!disableLevelDB)
    {
    bool bValid = (0 <= rc);
    bool bValueAugmented = (0 < rc);


      if (bValueAugmented)  // testing...
      {
      }

      p_txlistdb->recordTX(tx.GetHash(), bValid, nBlock, mp_obj.getType(), mp_obj.getNewAmount());
    }
  }

  return 0;
}

string CMPTally::getMSC()
{
  // FIXME: negative numbers -- do they work here?
  return strprintf("%d.%08d", moneys[MASTERCOIN_CURRENCY_MSC]/COIN, moneys[MASTERCOIN_CURRENCY_MSC]%COIN);
}

string CMPTally::getTMSC()
{
    // FIXME: negative numbers -- do they work here?
    return strprintf("%d.%08d", moneys[MASTERCOIN_CURRENCY_TMSC]/COIN, moneys[MASTERCOIN_CURRENCY_TMSC]%COIN);
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
  scriptPubKey.SetDestination(CBitcoinAddress(receiverAddress).Get());
  vecSend.push_back(make_pair(scriptPubKey, nDustLimit));

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

//
uint256 send_MP(const string &FromAddress, const string &ToAddress, unsigned int CurrencyID, uint64_t Amount)
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

  unsigned char packet[128];
  memset(&packet, 0, sizeof(packet));

  swapByteOrder32(CurrencyID);
  swapByteOrder64(Amount);

  // TODO: beautify later
  packet[0] = 0x01; // seq
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

  int64_t Amount = 0;
  if (!ParseMoney(params[3].get_str(), Amount)) {};

  uint32_t CurrencyID = boost::lexical_cast<boost::uint32_t>(params[2].get_str());

  //some sanity checking of the data supplied?

  if (0 >= Amount) throw runtime_error("invalid parameter: Amount");
  if (0 == CurrencyID) throw runtime_error("invalid parameter: CurrencyID");

  //currencyID will need to be checked for divisibility - handle here or in function?
  uint256 newTX = send_MP(FromAddress, ToAddress, CurrencyID, Amount);

//bitcoin returns a transaction ID or error here for equivalent function (sendtoaddress)
//trap errors, if successful return transaction ID
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
    std::string address = (params[0].get_str());
    //assume MSC for PoC, force currencyID to 1
    int64_t tmpbal = getMPbalance(address, MASTERCOIN_CURRENCY_MSC, MONEY);
    return ValueFromAmount(tmpbal);
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
    fprintf(mp_fp, "%s(): %s, line %d, file: %s\n", __FUNCTION__, status.ToString().c_str(), __LINE__, __FILE__);
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

  if (msc_debug6) fprintf(mp_fp, "%s()\n", __FUNCTION__);

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
    CWallet *wallet = pwalletMain;

    Object txobj;
    if (!pwalletMain->mapWallet.count(hash))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
    const CWalletTx& wtx = pwalletMain->mapWallet[hash];

    // here begins
    CMPTransaction mp_obj;

    LOCK(wallet->cs_wallet);

                uint256 wtxid = wtx.GetHash();
                bool bIsMine;
                bool isMPTx = false;
                int nFee;
                string MPTxType;
                string selectedAddress;
                string senderAddress;
                string refAddress;
                bool valid;
                uint64_t curId;  //using 64 instead of 32 here as json::sprint chokes on 32 - research why
                bool divisible;
                uint64_t amount;
                string result;
                bool outgoingTransaction = false;
                bool selloffer = false;
                uint64_t sell_minfee = 0;
                unsigned char sell_timelimit = 0;
                unsigned char sell_subaction = 0;
                uint64_t sell_btcdesired = 0;

                int confirmations = wtx.GetDepthInMainChain(); //what about conflicted (<0)? how will we display these?
                uint256 blockHash = wtx.hashBlock;
                if ((0 == blockHash) || (NULL == mapBlockIndex[blockHash]))
                        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Exception: blockHash is 0");
                CBlockIndex* pBlockIndex = mapBlockIndex[blockHash];
                if (NULL == pBlockIndex)
                        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Exception: pBlockIndex is NULL");
                int blockHeight = pBlockIndex->nHeight;
                int64_t blockTime = mapBlockIndex[wtx.hashBlock]->nTime;
                int blockIndex = wtx.nIndex;

                if ((!TestNet()) && (blockHeight < 290630)) 
                        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not available - prior to preseed");
                if ((TestNet()) && (blockHeight < 253728))
                        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Testnet transaction not avaiable - prior to preseed");

                mp_obj.SetNull();
                CMPOffer temp_offer;
                if (0 == msc_tx_populate(wtx, 0, 0, &mp_obj))
                {
                        // OK, a valid MP transaction so far
                        if (0<=mp_obj.parse())
                        {
                                isMPTx = true;
                                MPTxType = mp_obj.getTypeString();
                                senderAddress = mp_obj.getSender();
                                refAddress = mp_obj.getReceiver();
                                curId = mp_obj.getCurrency();
                                divisible = true; // hard coded for now until SP support
                                amount = mp_obj.getAmount(); // need to go to leveldb for selloffers and accepts
                                nFee = mp_obj.getFeePaid();

           		        if ((0 < mp_obj.interpretPacket(&temp_offer)) && (MSC_TYPE_TRADE_OFFER == mp_obj.getType()))
                                {
                                           sell_minfee = temp_offer.getMinFee();
                                           sell_timelimit = temp_offer.getBlockTimeLimit();
                                           sell_subaction = temp_offer.getSubaction();
                                           sell_btcdesired = temp_offer.getBTCDesiredOriginal();
                                           selloffer = true;
                                }
                        }
                }
                else
                {
                        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Not a Master Protocol transaction");
                }
                if (isMPTx)
                {
                        // use master protocol functions for embedded MP message
                        int tmpblock=0;
                        uint32_t tmptype=0;
                        uint64_t amountNew=0;

                        valid=getValidMPTX(wtxid, &tmpblock, &tmptype, &amountNew);
                        if ((valid) && (amountNew>0)) amount=amountNew; //amount has been amended, update

                        // test sender and reference against ismine to determine which address is ours
                        // if both ours (eg sending to another address in wallet) use reference
                        bIsMine = IsMyAddress(senderAddress);
                        if (bIsMine)
                        {
                                outgoingTransaction=true;
                        }
                        txobj.push_back(Pair("txid", wtxid.GetHex()));
                        txobj.push_back(Pair("sendingaddress", senderAddress));
                        if (!selloffer) txobj.push_back(Pair("referenceaddress", refAddress));
                        if (outgoingTransaction)
                        {
                                txobj.push_back(Pair("direction", "out"));
                        }
                        else
                        {
                                txobj.push_back(Pair("direction", "in"));
                        }
                        txobj.push_back(Pair("confirmations", confirmations));
                        txobj.push_back(Pair("fee", ValueFromAmount(nFee)));
                        txobj.push_back(Pair("blocktime", blockTime));
                        txobj.push_back(Pair("blockindex", blockIndex));
                        txobj.push_back(Pair("type", MPTxType));
                        txobj.push_back(Pair("currency", curId));
                        txobj.push_back(Pair("divisible", divisible));
                        if (divisible)
                        {
                                txobj.push_back(Pair("amount", ValueFromAmount(amount))); //divisible, format w/ bitcoins VFA func
                        }
                        else
                        {
                                txobj.push_back(Pair("amount", amount)); //indivisible, push raw 64
                        }
			if (selloffer)
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

    if (fHelp || params.size() > 3)
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
        int nCount = 10;
        if (params.size() > 1)
                nCount = boost::lexical_cast<boost::int32_t>(params[1].get_str());
        int nFrom = 0;
        if (params.size() > 2)
                 nFrom = boost::lexical_cast<boost::int32_t>(params[2].get_str());
        if (nCount < 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
        if (nFrom < 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative from");

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
                string MPTxType;
                string selectedAddress;
                string senderAddress;
                string refAddress;
                bool valid;
                uint64_t curId;  //using 64 instead of 32 here as json::sprint chokes on 32 - research why
                bool divisible;
                bool selloffer = false;
                uint64_t amount;
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
                if ((!TestNet()) && (blockHeight < 290630)) continue; //do not display transactions prior to preseed
                if ((TestNet()) && (blockHeight < 253728)) continue;

                mp_obj.SetNull();
                if (0 == msc_tx_populate(*pwtx, 0, 0, &mp_obj))
                {
                        // OK, a valid MP transaction so far
                        if (0<=mp_obj.parse())
                        {
                                isMPTx = true;
                                MPTxType = mp_obj.getTypeString();
                                senderAddress = mp_obj.getSender();
                                refAddress = mp_obj.getReceiver();
                                curId = mp_obj.getCurrency();
                                divisible = true; // hard coded for now until SP support
                                amount = mp_obj.getAmount(); // need to go to leveldb for selloffers and accepts
                                if (MSC_TYPE_TRADE_OFFER == mp_obj.getType()) selloffer=true;
                        }
                }

                // is this a MP transaction? switched to parsing rather than leveldb at Michael's request
                if (isMPTx)
                {

                        // use master protocol functions for embedded MP message
                        int tmpblock=0;
                        uint32_t tmptype=0;
                        uint64_t amountNew=0;

                        valid=getValidMPTX(wtxid, &tmpblock, &tmptype, &amountNew);
                        if ((valid) && (amountNew>0)) amount=amountNew; //amount has been amended, update

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
                                if (!selloffer) txobj.push_back(Pair("referenceaddress", refAddress));
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
                                txobj.push_back(Pair("currency", curId));
                                txobj.push_back(Pair("divisible", divisible));
                                if (divisible)
                                {
                                        txobj.push_back(Pair("amount", ValueFromAmount(amount))); //divisible, format w/ bitcoins VFA func
                                }
                                else
                                {
                                        txobj.push_back(Pair("amount", amount)); //indivisible, push raw 64
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
