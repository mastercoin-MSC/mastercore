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

#include "mastercore.h"

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;
using namespace leveldb;

uint64_t global_MSC_total = 0;
uint64_t global_MSC_RESERVED_total = 0;

static string exodus = "1EXoDusjGwvnjZUyKkxZ4UHEf77z6A5S4P";
static uint64_t exodus_prev = 0;
static uint64_t exodus_balance;

static boost::filesystem::path MPPersistencePath;

static FILE *mp_fp = NULL;

int msc_debug0 = 0;
int msc_debug  = 1;
int msc_debug2 = 1;
int msc_debug3 = 1;
int msc_debug4 = 1;
int msc_debug5 = 1;
int msc_debug6 = 1;

// follow this variable through the code to see how/which Master Protocol transactions get invalidated
static int InvalidCount_per_spec = 0;  // consolidate error messages into a nice log, for now just keep a count
static int InsufficientFunds = 0;      // consolidate error messages

// disable TMSC handling for now, has more legacy corner cases
// static int ignore_all_but_MSC = 1;
static int ignore_all_but_MSC = 0;

// this is the internal format for the offer primary key (TODO: replace by a class method)
#define STR_ADDR_CURR_COMBO(x) ( x + "-" + strprintf("%d", curr))

static MP_txlist *p_txlistdb;

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
    case MSC_TYPE_TRADE_OFFER: return ((char *)"Selling coins for BTC, trade offer");
    case 21: return ((char *)"Offer/Accept one Master Protocol Coins for another");
    case MSC_TYPE_ACCEPT_OFFER_BTC: return ((char *)"Accepting offer, buying coins with BTC");
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
  uint64_t offer_amount_remaining;  // amount still available (should probably = offer_amount_original - reserved_accepted_amount - amount_bought)
  uint64_t offer_amount_original; // the amount of MSC for sale specified when the offer was placed
  uint64_t reserved_accepted_amount; // as accepts come in this amount grows, as accepts expire, this amount shrinks
  unsigned int currency;
  uint64_t BTC_desired_original; // amount desired, in BTC
  uint64_t min_fee;
  unsigned char blocktimelimit;

  // do a map of buyers, primary key is buyer+currency
  // MUST account for many possible accepts and EACH currency offer
  class CMPAccept
  {
  private:
    uint64_t accept_amount_remaining;             // amount of MSC/TMSC remaining to be purchased
    uint64_t accept_amount_original;    // amount of MSC/TMSC being desired to purchased
// once accept is seen on the network the amount of MSC being purchased is taken out of seller's Reserve and put into this Buyer's
    uint64_t fee_paid;  //

  public:
    int block;          // 'accept' message sent in this block

    CMPAccept(uint64_t a, uint64_t f, int b):accept_amount_remaining(a),fee_paid(f),block(b)
    {
      accept_amount_original = accept_amount_remaining;
      fprintf(mp_fp, "%s(%lu), line %d, file: %s\n", __FUNCTION__, a, __LINE__, __FILE__);
    }

    CMPAccept(uint64_t origA, uint64_t remA, uint64_t f, int b):accept_amount_remaining(remA),accept_amount_original(origA),fee_paid(f),block(b)
    {
      fprintf(mp_fp, "%s(%lu), line %d, file: %s\n", __FUNCTION__, origA, __LINE__, __FILE__);
    }

    ~CMPAccept()
    {
    }

    void print()
    {
      // hm, can't access the outer class' map member to get the currency unit label... do we care?
      fprintf(mp_fp, "buying: %12.8lf (originally= %12.8lf) in block# %d, fee: %2.8lf\n",
       (double)accept_amount_remaining/(double)COIN, (double)accept_amount_original/(double)COIN, block, (double)fee_paid/(double)COIN);
    }

    uint64_t getAcceptAmount() const
    { 
      fprintf(mp_fp, "%s(); buyer still wants = %lu, line %d, file: %s\n", __FUNCTION__, accept_amount_remaining, __LINE__, __FILE__);

      return accept_amount_remaining;
    }
    void reduceAcceptAmount(uint64_t really_purchased)
    {
      accept_amount_remaining -= really_purchased;
    } // TODO: check for negatives ? assert ?

    void saveAccept(ofstream &file, SHA256_CTX *shaCtx, string const &addr, unsigned int currency, string const &buyer ) const {
      // compose the outputline
      // seller-address, currency, buyer-address, amount, fee, block
      string lineOut = (boost::format("%s,%d,%s,%d,%d,%d,%d")
        % addr
        % currency
        % buyer
        % accept_amount_remaining
        % accept_amount_original
        % fee_paid
        % block).str();

      // add the line to the hash
      SHA256_Update(shaCtx, lineOut.c_str(), lineOut.length());

      // write the line
      file << lineOut << endl;
    }
  };

  map<string, CMPAccept> my_accepts;

public:
  unsigned int getCurrency() const { return currency; }
  uint64_t getOfferAmount() const { return offer_amount_remaining; }

  void reduceOfferAmount(uint64_t accepted)
  {
    offer_amount_remaining -= accepted;
    reserved_accepted_amount += accepted;
  } // TODO: check for negatives ? assert ?

  void increaseOfferAmount(uint64_t accepted)
  {
    offer_amount_remaining += accepted;
    reserved_accepted_amount -= accepted;
  } // TODO: check for negatives ? assert ?

  void reduceReservedAcceptedAmount(uint64_t purchased)
  {
    reserved_accepted_amount -= purchased;
  }

  unsigned int eraseExpiredAccepts(int blockNow)
  {
  unsigned int how_many_erased = 0;

    for(map<string, CMPAccept>::iterator my_it = my_accepts.begin(); my_it != my_accepts.end(); ++my_it)
    {
      // my_it->first = key
      // my_it->second = value

      if ((blockNow - (my_it->second).block) >= (int) blocktimelimit)
      {
        fprintf(mp_fp, "%s() FOUND EXPIRED ACCEPT, erasing: blockNow=%d, offer block=%d, blocktimelimit= %d\n",
         __FUNCTION__, blockNow, (my_it->second).block, blocktimelimit);

        printf("\t%35s ", (my_it->first).c_str());
        (my_it->second).print();
  
        increaseOfferAmount((my_it->second).getAcceptAmount());

        my_accepts.erase(my_it);

        ++how_many_erased;
      }
    }

    return how_many_erased;
  }

  // this is used during payment, given the amount of BTC paid this function returns the amount of currency transacted
  uint64_t getCurrencyAmount(uint64_t BTC_paid, const string &buyer)
  {
  double X;
  double P;
  uint64_t purchased;
  uint64_t actual_amount = 0;
  map<string, CMPAccept>::iterator my_it = my_accepts.find(buyer);

    fprintf(mp_fp, "%s();my_accepts.size= %lu, line %d, file: %s\n", __FUNCTION__, my_accepts.size(), __LINE__, __FILE__);

    // did the buyer pay enough or more than the seller wanted?
    if (BTC_paid >= BTC_desired_original)
    {
      purchased = offer_amount_remaining; // this is how much the seller has offered
    }
    else
    {
      if (0==(double)BTC_desired_original) return 0;  // divide by 0 protection

      X = (double)BTC_paid/(double)BTC_desired_original;
      P = (double)offer_amount_original * X;

      purchased = rounduint64(P); // buyer paid for less than the seller has, that's OK, he'll get what he paid for
    }

    // now check that the buyer had actually ACCEPTed this much...
    {
      if (my_it != my_accepts.end())
      {
        // did the customer pay for less than he wanted? adjust downward
        if (purchased < (my_it->second).getAcceptAmount()) actual_amount = purchased;
        // otherwise he paid just enough or even more -- he gets what he wanted (ACCEPTed)
        else actual_amount = (my_it->second).getAcceptAmount();
      }
    }

    fprintf(mp_fp, "%s();BTC_paid= %lu, BTC_desired_original= %lu, purchased= %lu, accept_amount_remaining= %lu, actual_amount= %lu\n",
     __FUNCTION__, BTC_paid, BTC_desired_original, purchased, (my_it->second).getAcceptAmount(), actual_amount);

    return actual_amount;
  }

  void reduceAcceptAmount(uint64_t purchased, const string &buyer)
  {
  map<string, CMPAccept>::iterator my_it = my_accepts.find(buyer);

    fprintf(mp_fp, "%s();my_accepts.size= %lu, line %d, file: %s\n", __FUNCTION__, my_accepts.size(), __LINE__, __FILE__);
    fprintf(mp_fp, "%s(%s, purchased= %lu), line %d, file: %s\n", __FUNCTION__, buyer.c_str(), purchased, __LINE__, __FILE__);

    if (my_it != my_accepts.end())
    {
      (my_it->second).print();
      (my_it->second).reduceAcceptAmount(purchased);

      // if accept has been fully paid -- erase it
      if (0 == (my_it->second).getAcceptAmount())
      {
        fprintf(mp_fp, "%s(buyer %s:%s purchased= %lu); DONE -- erasing his accept\n",
         __FUNCTION__, buyer.c_str(), (my_it->first).c_str(), purchased);

        my_accepts.erase(my_it);
      }
    }
    fprintf(mp_fp, "%s();my_accepts.size= %lu, line %d, file: %s\n", __FUNCTION__, my_accepts.size(), __LINE__, __FILE__);
  }

  void saveOffer(ofstream &file, SHA256_CTX *shaCtx, string const &addr ) const {
    // compose the outputline
    // seller-address, ...
    string lineOut = (boost::format("%s,%d,%d,%d,%d,%d,%d,%d,%d")
      % addr
      % offerBlock
      % offer_amount_remaining
      % offer_amount_original
      % reserved_accepted_amount
      % currency
      % BTC_desired_original
      % min_fee
      % (int)blocktimelimit).str();

    // add the line to the hash
    SHA256_Update(shaCtx, lineOut.c_str(), lineOut.length());

    // write the line
    file << lineOut << endl;
  }

  void saveAccepts( ofstream &file, SHA256_CTX *shaCtx, string const &addr ) const {
    map<string, CMPOffer::CMPAccept>::const_iterator iter;
    for (iter = my_accepts.begin(); iter != my_accepts.end(); ++iter) {
      (*iter).second.saveAccept(file, shaCtx, addr, currency, (*iter).first);
    }
  }

  int loadAccept( string const &buyerAddr, uint64_t amountRemaining, uint64_t amountOriginal, uint64_t feePaid, int block ) {
    CMPAccept newAccept(amountRemaining, amountOriginal, feePaid, block);
    if (my_accepts.insert(std::make_pair(buyerAddr, newAccept)).second) {
      return 0;
    } else {
      return -1;
    }
  }


  CMPOffer(int b, uint64_t a, unsigned int cu, uint64_t d, uint64_t fee, unsigned char btl)
   :offerBlock(b),offer_amount_remaining(a),offer_amount_original(a),currency(cu),BTC_desired_original(d),min_fee(fee),blocktimelimit(btl)
  {
    if (msc_debug4) fprintf(mp_fp, "%s(%lu), line %d, file: %s\n", __FUNCTION__, a, __LINE__, __FILE__);

    reserved_accepted_amount = 0;

    my_accepts.clear();
  }

  CMPOffer(int b, uint64_t origA, uint64_t remainingA, uint64_t reservedA, unsigned int cu, uint64_t d, uint64_t fee, unsigned char btl)
     :offerBlock(b),offer_amount_remaining(remainingA),offer_amount_original(origA),reserved_accepted_amount(reservedA),currency(cu),BTC_desired_original(d),min_fee(fee),blocktimelimit(btl)
  {
    if (msc_debug4) fprintf(mp_fp, "%s(%lu), line %d, file: %s\n", __FUNCTION__, remainingA, __LINE__, __FILE__);
    my_accepts.clear();
  }

  void offer_update(int b, uint64_t a, unsigned int cu, uint64_t d, uint64_t fee, unsigned char btl)
  {
    fprintf(mp_fp, "%s(%lu), line %d, file: %s\n", __FUNCTION__, a, __LINE__, __FILE__);

    offerBlock = b;
    offer_amount_remaining = a;
    offer_amount_original = a;
    currency = cu;
    BTC_desired_original = d;
    min_fee = fee;
    blocktimelimit = btl;
  }

  // the offer is accepted by a buyer, add this purchase to the accepted list
  void offer_accept(const string &buyer, uint64_t desired, int block, uint64_t fee)
  {
    fprintf(mp_fp, "%s(buyer:%s, desired=%2.8lf, block # %d), line %d, file: %s\n",
     __FUNCTION__, buyer.c_str(), (double)desired/(double)COIN, block, __LINE__, __FILE__);

    // here we ensure the correct BTC fee was paid in this acceptance message, per spec
    if (fee < min_fee)
    {
      fprintf(mp_fp, "ERROR: fee too small -- the ACCEPT is rejected! (%lu is smaller than %lu)\n", fee, min_fee);
      ++InvalidCount_per_spec;
      return;
    }

    map<string, CMPAccept>::iterator my_it = my_accepts.find(buyer);

    // Zathras said the older accept is the valid one !!!!!!!! do not accept any new ones!
    if (my_it != my_accepts.end())
    {
      // protocol error, an accept from this same seller for this same offer is already open
      fprintf(mp_fp, "%s() ERROR: an accept from this same seller for this same offer is already open !!!!!\n", __FUNCTION__);
      ++InvalidCount_per_spec;
    }
    else
    {
      reduceOfferAmount(desired);
      my_accepts.insert(std::make_pair(buyer, CMPAccept(desired, fee, block)));
    }
  }

  void print(string address, bool bPrintAcceptsToo = false)
  {
  const double coins = (double)offer_amount_remaining/(double)COIN;
  const double original_coins = (double)offer_amount_original/(double)COIN;
  const double wants_total = (double)BTC_desired_original/(double)COIN;
  const double price = coins*wants_total ? wants_total/coins : 0;

    fprintf(mp_fp, "%36s selling %12.8lf (%12.8lf available) %4s for %12.8lf BTC (price: %1.8lf), in #%d blimit= %3u, minfee= %1.8lf\n",
     address.c_str(), original_coins, coins, c_strMastercoinCurrency(currency), wants_total, price, offerBlock, blocktimelimit,(double)min_fee/(double)COIN);

        if (bPrintAcceptsToo)
        for(map<string, CMPAccept>::iterator my_it = my_accepts.begin(); my_it != my_accepts.end(); ++my_it)
        {
          // my_it->first = key
          // my_it->second = value

          printf("\t%35s ", (my_it->first).c_str());
          (my_it->second).print();
        }
  }

  bool IsCustomerOnTheAcceptanceListAndWithinBlockTimeLimit(string customer, int blockNow)
  {
  int problem = 0;

    const map<string, CMPAccept>::iterator my_it = my_accepts.find(customer);

    fprintf(mp_fp, "%s();my_accepts.size= %lu, line %d, file: %s\n", __FUNCTION__, my_accepts.size(), __LINE__, __FILE__);

    fprintf(mp_fp, "%s(%s), line %d, file: %s\n", __FUNCTION__, customer.c_str(), __LINE__, __FILE__);

    if (my_it != my_accepts.end())
    {
      fprintf(mp_fp, "%s()now: %d, class: %d, limit: %d, line %d, file: %s\n",
       __FUNCTION__, blockNow, (my_it->second).block, blocktimelimit, __LINE__, __FILE__);

      if ((blockNow - (my_it->second).block) > (int) blocktimelimit)
      {
        problem+=1;
        ++InvalidCount_per_spec;
      }
    }
    else problem+=10;

    fprintf(mp_fp, "%s(%s):problem=%d, line %d, file: %s\n", __FUNCTION__, customer.c_str(), problem, __LINE__, __FILE__);

    return (!problem);
  }

};

static map<string, CMPOffer> my_offers;

CCriticalSection cs_tally;

// this is the master list of all amounts for all addresses for all currencies, map is sorted by Bitcoin address
map<string, mp_tally> mp_tally_map;

// look at balance for an address
uint64_t getMPbalance(const string &Address, unsigned int currency, bool bReserved)
{
uint64_t balance = 0;
const map<string, mp_tally>::iterator my_it = mp_tally_map.find(Address);

  if (my_it != mp_tally_map.end())
  {
    balance = (my_it->second).getMoney(currency, bReserved);
  }

  return balance;
}

// TODO: when HardFail is true -- do not transfer any funds if only a partial transfer would succeed
// bSet will SET the amount into the address, instead of just updating it
bool update_tally_map(string who, unsigned int which, int64_t amount, bool bReserved = false, bool bSet = false)
{
bool bReturn = true;

  if (msc_debug2)
   fprintf(mp_fp, "%s(%s, %d, %+ld%s), line %d, file: %s\n", __FUNCTION__, who.c_str(), which, amount, bReserved ? " RESERVED":"", __LINE__, __FILE__);

  LOCK(cs_tally);

      const map<string, mp_tally>::iterator my_it = mp_tally_map.find(who);
      if (my_it != mp_tally_map.end())
      {
        // element found -- update
        if (!bReserved) bReturn = (my_it->second).msc_update_moneys(which, amount, bSet);
        else bReturn = (my_it->second).msc_update_reserved(which, amount);
      }
      else
      {
        // not found -- insert
        if (0<=amount) mp_tally_map.insert(std::make_pair(who,mp_tally(which, amount)));
        else bReturn = false;
      }

  if (!bReturn) fprintf(mp_fp, "%s(%s, %d, %+ld%s) INSUFFICIENT FUNDS\n", __FUNCTION__, who.c_str(), which, amount, bReserved ? " RESERVED":"");

  return bReturn;
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
  string txid;
  int block;
  unsigned int tx_idx;  // tx # within the block, 0-based
  int pkt_size;
  unsigned char pkt[MAX_PACKETS * PACKET_SIZE];
  uint64_t nValue;
  int multi;  // Class A = 0, Class B = 1
  uint64_t tx_fee_paid;

  void update_offer_map(string seller_addr, unsigned int curr, uint64_t desired, uint64_t fee, unsigned char btl) const
  {
  // handle the offer
  map<string, CMPOffer>::iterator my_it;
  const string combo = STR_ADDR_CURR_COMBO(seller_addr);

    if (msc_debug4)
    fprintf(mp_fp, "%s(%s|%s), nValue=%lu), line %d, file: %s\n",
     __FUNCTION__, seller_addr.c_str(), combo.c_str(), nValue, __LINE__, __FILE__);

      my_it = my_offers.find(combo);

      // TODO: handle an existing offer (cancel, or update or what?) consensus question
      if (my_it != my_offers.end())
      {
        fprintf(mp_fp, "%s() OFFER FOUND - UPDATING, line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
        // element found -- update
        (my_it->second).offer_update(block, nValue, curr, desired, fee, btl);
      }
      else
      {
        fprintf(mp_fp, "%s() NEW OFFER - INSERTING, line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
        // not found -- insert
        my_offers.insert(std::make_pair(combo, CMPOffer(block, nValue, curr, desired, fee, btl)));
      }
  }

  bool offerExists(string seller_addr, unsigned int curr) const
  {
  const string combo = STR_ADDR_CURR_COMBO(seller_addr);
  map<string, CMPOffer>::iterator my_it = my_offers.find(combo);

    return !(my_it == my_offers.end());
  }

  // undo the Offer - return the funds, optionally to Cancel, or just undo to get ready for an update
  void offerUndo(string seller_addr, unsigned int curr, bool bCancel = false) const
  {
  const string combo = STR_ADDR_CURR_COMBO(seller_addr);
  map<string, CMPOffer>::iterator my_it = my_offers.find(combo);
  uint64_t nValue;

    if (my_offers.end() == my_it) return; // offer not found

    nValue = (my_it->second).getOfferAmount();

    // take from RESERVED, give to REAL
    if (!update_tally_map(seller_addr, curr, - nValue, true))
    {
      ++InsufficientFunds;
      return;
    }
    update_tally_map(seller_addr, curr, nValue);

    // erase the offer from the map
    if (bCancel) my_offers.erase(my_it);
  }

  // will record an 'accept' for a specific item from this buyer
  void update_offer_accepts(int curr)
  {
  // find the offer
  map<string, CMPOffer>::iterator my_it;
  const string combo = STR_ADDR_CURR_COMBO(receiver);

      my_it = my_offers.find(combo);
      if (my_it != my_offers.end())
      {
        fprintf(mp_fp, "%s(%s) OFFER FOUND, line %d, file: %s\n", __FUNCTION__, combo.c_str(), __LINE__, __FILE__);
        (my_it->second).print((my_it->first), true);
        // offer found -- update
        (my_it->second).offer_accept(sender, nValue, block, tx_fee_paid);
        (my_it->second).print((my_it->first), true);
      }
      else
      {
        fprintf(mp_fp, "SELL OFFER NOT FOUND %s !!!\n", combo.c_str());
      }
  }

 // the 31-byte packet & the packet #
 // int interpretPacket(int blocknow, unsigned char pkt[], int size)
 // NOTE: TMSC is ignored for now...
 int interpretPacket()
 {
 unsigned short version = MP_TX_PKT_V0;
 unsigned int type, currency;
 uint64_t amount_desired, min_fee;
 unsigned char blocktimelimit, subaction = 0;

  if (PACKET_SIZE_CLASS_A > pkt_size)  // class A could be 19 bytes
  {
    fprintf(mp_fp, "%s() ERROR PACKET TOO SMALL; size = %d, line %d, file: %s\n", __FUNCTION__, pkt_size, __LINE__, __FILE__);
    return -1;
  }

  // collect version
  memcpy(&version, &pkt[0], 2);
  swapByteOrder16(version);

  // blank out version bytes in the packet
  pkt[0]=0; pkt[1]=0;
  
  memcpy(&type, &pkt[0], 4);
  memcpy(&currency, &pkt[4], 4);
  memcpy(&nValue, &pkt[8], 8);

  fprintf(mp_fp, "version: %d, Class %s\n", version, !multi ? "A":"B");

  // FIXME: only do swaps for little-endian system(s) !
  swapByteOrder32(type);
  swapByteOrder32(currency);
  swapByteOrder64(nValue);

  if (ignore_all_but_MSC)
  if (currency != MASTERCOIN_CURRENCY_MSC)
  {
    fprintf(mp_fp, "IGNORING NON-MSC packet for NOW, for this PoC !!!\n");
    return -2;
  }

  fprintf(mp_fp, "\t            type: %u (%s)\n", type, c_strMastercoinType(type));
  fprintf(mp_fp, "\t        currency: %u (%s)\n", currency, c_strMastercoinCurrency(currency));
  fprintf(mp_fp, "\t           value: %lu.%08lu\n", nValue/COIN, nValue%COIN);

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
      if (!update_tally_map(sender, currency, - nValue)) break;
      update_tally_map(receiver, currency, nValue);
      break;

    case MSC_TYPE_TRADE_OFFER:
    {
    enum ActionTypes { INVALID = 0, NEW = 1, UPDATE = 2, CANCEL = 3 };
    const char * const subaction_name[] = { "empty", "new", "update", "cancel" };
    bool bActionNew = false;
    bool bActionUpdate = false;
    bool bActionCancel = false;

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

      // figure out which Action this is based on amount for sale, version & etc.
      switch (version)
      {
        case MP_TX_PKT_V0:
          if ((0 == nValue) && (MASTERCOIN_CURRENCY_TMSC == currency)) bActionCancel = true;

          if (0 != nValue)
          {
            if (!offerExists(sender, currency))
            {
              bActionNew = true;
            }
            else
            {
              bActionUpdate = true;
            }
          }

          break;

        case MP_TX_PKT_V1:  // same as default right now
        default:
        {
          if (offerExists(sender, currency))
          {
            if ((CANCEL != subaction) && (UPDATE != subaction))
            {
              fprintf(mp_fp, "%s() INVALID SELL OFFER -- ONE ALREADY EXISTS, line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
              ++InvalidCount_per_spec;
              break;
            }
          } else {
            // Offer does not exist
            if ((NEW != subaction))
            {
              printf("%s() INVALID SELL OFFER -- UPDATE OR CANCEL ACTION WHEN NONE IS POSSIBLE, line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
              ++InvalidCount_per_spec;
              break;
            }
          }
 
          switch (subaction)
          {
            case NEW: bActionNew = true; break;
            case UPDATE: bActionUpdate = true; break;
            case CANCEL: bActionCancel = true; break;
            default:
              ++InvalidCount_per_spec;
              break;
          }

          break;
        }
      };

      // my simple math is: UPDATE = CANCEL + NEW
      if (bActionUpdate)
      {
        offerUndo(sender, currency);  // tally is adjusted internally
        bActionNew = true;
      }
      
      if (bActionCancel)
      {
        offerUndo(sender, currency, true);  // tally is adjusted internally
      }

      if (bActionNew)
      {
      const uint64_t balanceReallyAvailable = getMPbalance(sender, currency);

        // if offering more than available -- put everything up on sale
        if (nValue > balanceReallyAvailable)
        {
        double BTC;

          // AND we must also re-adjust the BTC desired in this case...
          BTC = amount_desired * balanceReallyAvailable;
          BTC /= (double)nValue;
          amount_desired = rounduint64(BTC);

          nValue = balanceReallyAvailable;
        }

        if (update_tally_map(sender, currency, - nValue)) // subtract from what's available
        {
          update_tally_map(sender, currency, nValue, true); // put in reserve
          update_offer_map(sender, currency, amount_desired, min_fee, blocktimelimit);
        }
      }
      break;
    } // end of TRADE_OFFER

    case MSC_TYPE_ACCEPT_OFFER_BTC:
      // the min fee spec requirement is checked in the following function
      update_offer_accepts(currency);
      break;

    default:
      return -100;
  }

  return 0;
 }

public:
//  mutable CCriticalSection cs_msc;  // TODO: need to refactor first...

  CMPTransaction() : block(-1), pkt_size(0), tx_fee_paid(0)
  {
  }

  void set(const string &t, int b, unsigned int idx, uint64_t txf = 0)
  {
    txid = t;
    block = b;
    tx_idx = idx;
  }

  void set(string s, string r, uint64_t n, string t, int b, unsigned int idx, unsigned char p[], unsigned int size, int fMultisig, uint64_t txf)
  {
    sender = s;
    receiver = r;
    txid = t;
    block = b;
    tx_idx = idx;
    pkt_size = size;
    nValue = n;
    multi= fMultisig;
    tx_fee_paid = txf;

    memcpy(pkt, p, size < sizeof(pkt) ? size : sizeof(pkt));
  }

  bool operator<(const CMPTransaction& other) const
  {
    // sort by block # & additionally the tx index within the block
    if (block != other.block) return block > other.block;
    return tx_idx > other.tx_idx;
  }

  void print()
  {
    fprintf(mp_fp, "===BLOCK: %d =txid: %s =fee: %1.8lf ====\n", block, txid.c_str(), (double)tx_fee_paid/(double)COIN);
    fprintf(mp_fp, "sender: %s ; receiver: %s\n", sender.c_str(), receiver.c_str());

    if (0<pkt_size)
    {
      fprintf(mp_fp, "pkt: %s\n", HexStr(pkt, pkt_size + pkt, false).c_str());
      (void) interpretPacket();
    }
    else
    {
      ++InvalidCount_per_spec;
    }
  }
};

// incoming BTC payment for the offer
// TODO: verify proper partial payment handling
int matchBTCpayment(string seller, string customer, uint64_t BTC_amount, int blockNow)
{
  for (unsigned int curr=0;curr<MSC_MAX_KNOWN_CURRENCIES;curr++)
  {
  const string combo = STR_ADDR_CURR_COMBO(seller);

  if (msc_debug4) fprintf(mp_fp, "%s(looking for: %s), line %d, file: %s\n", __FUNCTION__, combo.c_str(), __LINE__, __FILE__);

  const map<string, CMPOffer>::iterator my_it = my_offers.find(combo);

    if (my_it != my_offers.end())
    {
      fprintf(mp_fp, "%s(%s) FOUND the SELL OFFER, line %d, file: %s\n", __FUNCTION__, combo.c_str(), __LINE__, __FILE__);

      // element found
      CMPOffer &offer = (my_it->second);

      offer.print((my_it->first));
      offer.print((my_it->first), true);

      // must now check if the customer is in the accept list
      // if he is -- he will be removed by the periodic cleanup, upon every new block received as the spec says
      if (offer.IsCustomerOnTheAcceptanceListAndWithinBlockTimeLimit(customer, blockNow))
      {
        uint64_t target_currency_amount = offer.getCurrencyAmount(BTC_amount, customer);

          // good, now adjust the amounts!!!
          if (update_tally_map(seller, offer.getCurrency(), - target_currency_amount, true))  // remove from reserve of the seller
          {
            update_tally_map(customer, offer.getCurrency(), target_currency_amount);  // give to buyer

            // update the amount available in the offer
            offer.reduceReservedAcceptedAmount(target_currency_amount);

            // must also adjust the amount the buyer still wants after this payment
            offer.reduceAcceptAmount(target_currency_amount, customer);

            offer.print((my_it->first), true);

            // now, erase the offer if there is nothing left in Reserve (or offer_amount_remaining for this offer)
            if (0 == getMPbalance(seller, offer.getCurrency(), true))
            {
              fprintf(mp_fp, "%s(%s) ALL SOLD - wiping out the offer, line %d, file: %s\n", __FUNCTION__, combo.c_str(), __LINE__, __FILE__);
              my_offers.erase(my_it);
            }
          }

          fprintf(mp_fp, "#######################################################\n");
      }
    }
  }

  return 0;
}

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

unsigned int cleanup_expired_accepts(int nBlockNow)
{
unsigned int how_many_erased = 0;

  // go over all offers
  for(map<string, CMPOffer>::iterator my_it = my_offers.begin(); my_it != my_offers.end(); ++my_it)
  {
    // my_it->first = key
    // my_it->second = value

//    (my_it->second).print((my_it->first), false);

    how_many_erased += (my_it->second).eraseExpiredAccepts(nBlockNow);
  }

  return how_many_erased;
}

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

  fprintf(mp_fp, "devmsc=%lu, exodus_prev=%lu, exodus_delta=%ld\n", devmsc, exodus_prev, exodus_delta);

  // per Zathras -- skip if a block's timestamp is older than that of a previous one!
  if (0>exodus_delta) return 0;

  update_tally_map(exodus, MASTERCOIN_CURRENCY_MSC, exodus_delta);
  exodus_prev = devmsc;

  return devmsc;
}

// TODO: optimize efficiency -- iterate only over wallet's addresses in the future
int set_wallet_totals()
{
int my_addresses_count = 0;
const unsigned int currency = MASTERCOIN_CURRENCY_MSC;  // FIXME: hard-coded for MSC only, for PoC

  global_MSC_total = 0;
  global_MSC_RESERVED_total = 0;

  for(map<string, mp_tally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it)
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

  return (my_addresses_count);
}

// called once per block
// it performs cleanup and other functions
int mastercore_handler_block(int nBlockNow, CBlockIndex const * pBlockIndex)
{
// for every new received block must do:
// 1) remove expired entries from the accept list (per spec accept entries are valid until their blocklimit expiration; because the customer can keep paying BTC for the offer in several installments)
// 2) update the amount in the Exodus address
unsigned int how_many_erased = 0;
uint64_t devmsc = 0;

  how_many_erased = cleanup_expired_accepts(nBlockNow);
  if (how_many_erased) fprintf(mp_fp, "%s(%d); erased %u accepts this block, line %d, file: %s\n",
   __FUNCTION__, how_many_erased, nBlockNow, __LINE__, __FILE__);

  // calculate devmsc as of this block and update the Exodus' balance
  devmsc = calculate_and_update_devmsc(pBlockIndex->GetBlockTime());

  fprintf(mp_fp, "devmsc for block %d: %lu, Exodus balance: %lu\n", nBlockNow, devmsc, getMPbalance(exodus, MASTERCOIN_CURRENCY_MSC));

  // get the total MSC for this wallet, for QT display
  (void) set_wallet_totals();
//  printf("the globals: MSC_total= %lu, MSC_RESERVED_total= %lu\n", global_MSC_total, global_MSC_RESERVED_total);

  // save out the state after this block
  mastercore_save_state(pBlockIndex);

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

// idx is position within the block, 0-based
// int msc_tx_push(const CTransaction &wtx, int nBlock, unsigned int idx)

// RETURNS: 0 if parsed a MP TX

int msc_tx_populate(const CTransaction &wtx, int nBlock, unsigned int idx, CMPTransaction *mp_tx)
{
string strSender;
uint64_t nMax = 0;
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

            mp_tx->set(wtx.GetHash().GetHex(), nBlock, idx);

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
                  wtx.vout[i].scriptPubKey.msc_parse(script_data);
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

            // now go through inputs & identify the sender, collect input amounts
            // go through inputs, find the largest per Mastercoin protocol, the Sender
            for (unsigned int i = 0; i < wtx.vin.size(); i++)
            {
            CTxDestination address;

            if (msc_debug) fprintf(mp_fp, "vin=%d:%s\n", i, wtx.vin[i].scriptSig.ToString().c_str());

            CTransaction txPrev;
            uint256 hashBlock;
            GetTransaction(wtx.vin[i].prevout.hash, txPrev, hashBlock, true);  // get the vin's previous transaction 
            unsigned int n = wtx.vin[i].prevout.n;
            CTxDestination source;

            uint64_t nValue = txPrev.vout[n].nValue;

             inAll += nValue;

             if (ExtractDestination(txPrev.vout[n].scriptPubKey, source))  // extract the destination of the previous transaction's vout[n]
             {
                CBitcoinAddress addressSource(source);              // convert this to an address
   
                if (nValue > nMax)
                {
                  nMax = nValue;
                  strSender = addressSource.ToString();
                }
             }
              if (msc_debug) fprintf(mp_fp, "vin=%d:%s\n", i, wtx.vin[i].ToString().c_str());
            }

            txFee = inAll - outAll; // this is the fee paid to miners for this TX

            if (!strSender.empty())
            {
              if (msc_debug2) fprintf(mp_fp, "The Sender: %s : His Input Value= %lu.%08lu ; fee= %lu.%08lu\n",
               strSender.c_str(), nMax / COIN, nMax % COIN, txFee/COIN, txFee%COIN);
            }
            else
            {
              fprintf(mp_fp, "The sender is EMPTY !!! txid: %s\n", wtx.GetHash().GetHex().c_str());
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
          wtx.vout[i].scriptPubKey.msc_parse(multisig_script_data);

          break;  // get out of processing this first multisig
        }
              }
            } // end of the outputs' for loop

          string strObfuscatedHashes[1+MAX_SHA256_OBFUSCATION_TIMES];
          prepareObfuscatedHashes(strSender, strObfuscatedHashes);

          unsigned char packets[MAX_PACKETS][32];
          int mdata_count = 0;  // multisig data count
          if (!fMultisig)
          {
          string strScriptData;
          string strDataAddress;
          unsigned char seq = 0xFF;

          // non-multisig data packets from this tx may be found here...
            // TODO: figure out what the data packet(s) are/is for non-multisig Class A transaction
            // from msc_utils_parsing.py, March 2014
            // look for data & for reference:
            // first, check the list of outputs with values equal to Exodus's
            // second, check the list of all outputs even with random values
            // third, not yet implemented here -- is it needed?:
            //    # Level 3
            //    # all output values are equal in size and if there are three outputs
            //    # of these type of outputs total
            // Class A

            // find data
          for (unsigned k = 0; k<script_data.size();k++)
          {
            if (msc_debug3) fprintf(mp_fp, "data[%d]:%s: %s (%lu.%08lu)\n", k, script_data[k].c_str(), address_data[k].c_str(), value_data[k] / COIN, value_data[k] % COIN);

            {
              string strSub = script_data[k].substr(2,16);
              seq = (ParseHex(script_data[k].substr(0,2)))[0];
  
                if (("0000000000000001" == strSub) || ("0000000000000002" == strSub))
                {
                  if (strScriptData.empty())
                  {
                    strScriptData = script_data[k].substr(2*1,2*PACKET_SIZE_CLASS_A);
                    strDataAddress = address_data[k];
                  }
  
                  if (msc_debug3) fprintf(mp_fp, "strScriptData #1:%s, seq = %x, value_data[%d]=%lu, %s marker_count= %d\n",
                   strScriptData.c_str(), seq, k, value_data[k], strDataAddress.c_str(), marker_count);

                  for (int exodus_idx=0;exodus_idx<marker_count;exodus_idx++)
                  {
                    if (msc_debug3) fprintf(mp_fp, "%s(); ExodusValues[%d]=%lu\n", __FUNCTION__, exodus_idx, ExodusValues[exodus_idx]);
                    if (value_data[k] == ExodusValues[exodus_idx])
                    {
                      if (msc_debug3) fprintf(mp_fp, "strScriptData(exodus_idx=%d) #2:%s, seq = %x\n", exodus_idx, strScriptData.c_str(), seq);
                      strScriptData = script_data[k].substr(2,2*PACKET_SIZE_CLASS_A);
                      strDataAddress = address_data[k];
                      break;
                    }
                  }
                  if (!strScriptData.empty()) break;
                }
              }
            }

            if (!strScriptData.empty())
            {
              ++seq;
              // look for reference using the seq #
              for (unsigned r = 0; r<script_data.size();r++)
              {
                if ((address_data[r] != strDataAddress) && (address_data[r] != exodus))
                {
                  if (seq == ParseHex(script_data[r].substr(0,2))[0])
                  {
                    strReference = address_data[r];
                  }
                }
              }

              // TODO: # on failure with 3 (non Exodus) outputs case, take non data/exodus to be the recipient
              if (strReference.empty())
              {
              int count = 0;

                fprintf(mp_fp, "%s() REF STILL EMPTY, data.size=%lu, line %d, file: %s\n", __FUNCTION__, script_data.size(), __LINE__, __FILE__);

//                if (4 == script_data.size())
                {
                  for (unsigned k = 0; k<script_data.size();k++)
                  {
                    fprintf(mp_fp, "%s():%s, line %d, file: %s\n", __FUNCTION__, address_data[k].c_str(), __LINE__, __FILE__);

                    // BUG HERE, FIXME
                    // strData is the script, not address !!!!!!!!!!!!!!!!!!!!
                    if ((address_data[k] != strDataAddress) && (address_data[k] != exodus))
                    {
                      if (ExodusHighestValue == value_data[k])
                      {
                        strReference = address_data[k];
                        ++count;
                      }
                    }
                  }
                }
                if (1 != count)
                {
                  fprintf(mp_fp, "%s() ERROR: MUST INVALIDATE HERE per Zathras 12 step algorithm, line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
                }
              }
            }

            if (strDataAddress.empty() || strReference.empty())
            {
            // this must be the BTC payment - validate (?)
            // TODO
            // ...
              if (msc_debug2 || msc_debug4) fprintf(mp_fp, "\n================BLOCK: %d======\ntxid: %s\n", nBlock, wtx.GetHash().GetHex().c_str());
              fprintf(mp_fp, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
              fprintf(mp_fp, "sender: %s , receiver: %s\n", strSender.c_str(), strReference.c_str());
              fprintf(mp_fp, "!!!!!!!!!!!!!!!!! this may be the BTC payment for an offer !!!!!!!!!!!!!!!!!!!!!!!!\n");

            // TODO collect all payments made to non-itself & non-exodus and their amounts -- these may be purchases!!!

              // go through the outputs, once again...
              {
                for (unsigned int i = 0; i < wtx.vout.size(); i++)
                {
                CTxDestination dest;
    
                  if (ExtractDestination(wtx.vout[i].scriptPubKey, dest))
                  {
                  const string strAddress = CBitcoinAddress(dest).ToString();

                    if (exodus == strAddress) continue;
                    if (strSender == strAddress) continue;
                    fprintf(mp_fp, "payment? %s %11.8lf\n", strAddress.c_str(), (double)wtx.vout[i].nValue/(double)COIN);

                    // check everything & pay BTC for the currency we are buying here...
                    matchBTCpayment(strAddress, strSender, wtx.vout[i].nValue, nBlock);
                    return 0;
                  }
                }
              }
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
            // gotta find the Reference
            BOOST_FOREACH(const string &addr, address_data)
            {
              if (msc_debug3) fprintf(mp_fp, "ref? data[%d]:%s: %s (%lu.%08lu)\n", k, script_data[k].c_str(), addr.c_str(), value_data[k] / COIN, value_data[k] % COIN);
              ++k;
              if ((addr != exodus) && (addr != strSender))
              {
                strReference = addr;
                break;
              }
            }

#if 0
            // if can't find the reference for a multisig tx -- assume the sender is sending MSC to itself !
            if (strReference.empty()) strReference = strSender;
#endif


          if (msc_debug0) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
          // multisig , Class B; get the data packets can be found here...
          for (unsigned k = 0; k<multisig_script_data.size();k++)
          {

          if (msc_debug0) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
            CPubKey key(ParseHex(multisig_script_data[k]));
            CKeyID keyID = key.GetID();
            string strAddress = CBitcoinAddress(keyID).ToString();
            char *c_addr_type = (char *)"";
            string strPacket;

          if (msc_debug0) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
            if (exodus == strAddress) c_addr_type = (char *)" (EXODUS)";
            else
            if (strAddress == strSender)
            {
              c_addr_type = (char *)" (SENDER)";
              // Are we the sender of BTC here?
              // TODO: do we care???
            }
            else
            {
              // this is a data packet, must deobfuscate now
              vector<unsigned char>hash = ParseHex(strObfuscatedHashes[mdata_count+1]);      
              vector<unsigned char>packet = ParseHex(multisig_script_data[k].substr(2*1,2*PACKET_SIZE));

              for (unsigned int i=0;i<packet.size();i++)
              {
                packet[i] ^= hash[i];
              }

              // ensure the first byte of the first packet is 01; is it the sequence number???
              if (03 >= packet[0])
              {
                memcpy(&packets[mdata_count], &packet[0], PACKET_SIZE);
                strPacket = HexStr(packet.begin(),packet.end(), false);
                ++mdata_count;
              }
            }

          if (msc_debug0) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
            if (msc_debug2)
            fprintf(mp_fp, "multisig_data[%d]:%s: %s%s\n", k, multisig_script_data[k].c_str(), strAddress.c_str(), c_addr_type);
            if (!strPacket.empty())
            {
              if (msc_debug) fprintf(mp_fp, "packet #%d: %s\n", mdata_count, strPacket.c_str());
            }
          if (msc_debug0) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
          }

            packet_size = mdata_count * (PACKET_SIZE - 1);

          if (msc_debug0) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
          }
          if (msc_debug0) fprintf(mp_fp, "%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);

            // now decode mastercoin packets
            for (int m=0;m<mdata_count;m++)
            {
              if (msc_debug2)
              fprintf(mp_fp, "m=%d: %s\n", m, HexStr(packets[m], PACKET_SIZE + packets[m], false).c_str());

              // ignoring sequence numbers for Class B packets -- TODO: revisit this
              memcpy(m*(PACKET_SIZE-1)+single_pkt, 1+packets[m], PACKET_SIZE-1);
            }

            if (msc_debug2) fprintf(mp_fp, "single_pkt: %s\n", HexStr(single_pkt, packet_size + single_pkt, false).c_str());

            mp_tx->set(strSender, strReference, 0, wtx.GetHash().GetHex(), nBlock, idx, single_pkt, packet_size, fMultisig, (inAll-outAll));  

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
        for(map<string, mp_tally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it)
        {
          // my_it->first = key
          // my_it->second = value

          printf("%34s => ", (my_it->first).c_str());
          (my_it->second).print();
        }
      break;

    case 1:
      // display the whole MP_txlist (leveldb)
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
      mastercore_handler_tx(tx, blockNum, tx_count);

      ++tx_count;
    }

    n_total += tx_count;
    if (msc_debug0) fprintf(mp_fp, "%4d:n_total= %d, n_found= %d\n", blockNum, n_total, n_found);

    mastercore_handler_block(blockNum, pblockindex);
  }

  for(map<string, mp_tally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it)
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
uint64_t  uValue = 0, uReserved = 0;
std::vector<std::string> vstr;
boost::split(vstr, s, boost::is_any_of(" ,="), token_compress_on);
int i = 0;
string strAddress = vstr[0];

//  if (msc_debug4) { BOOST_FOREACH(const string &debug_str, vstr) printf("%s\n", debug_str.c_str()); }

  ++i;

  uValue = boost::lexical_cast<boost::uint64_t>(vstr[i++]);
  uReserved = boost::lexical_cast<boost::uint64_t>(vstr[i++]);
  
  // want to bypass 0-value addresses...
  if ((0 == uValue) && (0 == uReserved)) return 0;

  // ignoring TMSC for now...
  update_tally_map(strAddress, MASTERCOIN_CURRENCY_MSC, uValue);
  update_tally_map(strAddress, MASTERCOIN_CURRENCY_MSC, uReserved, true);

  return 1;
}

// seller-address, offer_block, amount, currency, desired BTC , fee, blocktimelimit
// 13z1JFtDMGTYQvtMq5gs4LmCztK3rmEZga,299076,76375000,1,6415500,10000,6
int input_mp_offers_string(const string &s)
{
  int offerBlock;
  uint64_t amountRemaining, amountOriginal, amountReserved, btcDesired, minFee;
  unsigned int curr;
  unsigned char blocktimelimit;
  std::vector<std::string> vstr;
  boost::split(vstr, s, boost::is_any_of(" ,="), token_compress_on);
  string sellerAddr;
  int i = 0;

  if (9 != vstr.size()) return -1;


  sellerAddr = vstr[i++];
  offerBlock = atoi(vstr[i++]);
  amountRemaining = boost::lexical_cast<uint64_t>(vstr[i++]);
  amountOriginal = boost::lexical_cast<uint64_t>(vstr[i++]);
  amountReserved = boost::lexical_cast<uint64_t>(vstr[i++]);
  curr = boost::lexical_cast<unsigned int>(vstr[i++]);
  btcDesired = boost::lexical_cast<uint64_t>(vstr[i++]);
  minFee = boost::lexical_cast<uint64_t>(vstr[i++]);
  blocktimelimit = atoi(vstr[i++]);

  const string combo = STR_ADDR_CURR_COMBO(sellerAddr);
  CMPOffer newOffer(offerBlock, amountOriginal, amountRemaining, amountReserved, curr, btcDesired, minFee, blocktimelimit);
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
  std::vector<std::string> vstr;
  boost::split(vstr, s, boost::is_any_of(" ,="), token_compress_on);
  uint64_t amountRemaining, amountOriginal;
  uint64_t fee;
  unsigned int curr;
  string sellerAddr, buyerAddr;
  int i = 0;

  if (7 != vstr.size()) return -1;

  sellerAddr = vstr[i++];
  curr = boost::lexical_cast<unsigned int>(vstr[i++]);
  buyerAddr = vstr[i++];
  amountRemaining = boost::lexical_cast<uint64_t>(vstr[i++]);
  amountOriginal = boost::lexical_cast<uint64_t>(vstr[i++]);
  fee = boost::lexical_cast<uint64_t>(vstr[i++]);
  nBlock = atoi(vstr[i++]);

  const string combo = STR_ADDR_CURR_COMBO(sellerAddr);
  std::map<string,CMPOffer>::iterator iter = my_offers.find(combo);
  if (iter == my_offers.end()) {
    fprintf(mp_fp, "No offer from seller:%s found for accept from buyer:%s", sellerAddr.c_str(), buyerAddr.c_str());
    return -1;
  }

  return (*iter).second.loadAccept(buyerAddr, amountRemaining, amountOriginal, fee, nBlock);
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

  map<string, mp_tally>::const_iterator iter;
  for (iter = mp_tally_map.begin(); iter != mp_tally_map.end(); ++iter) {
    uint64_t mscBalance = (*iter).second.getMoney(MASTERCOIN_CURRENCY_MSC, false);
    uint64_t mscReserved = (*iter).second.getMoney(MASTERCOIN_CURRENCY_MSC, true);

    // we don't allow 0 balances to read in, so if we don't write them
    // it makes things match up better between peristed state and processed state
    if ( 0 == mscBalance && 0 == mscReserved ) {
      continue;
    }

    string lineOut = (boost::format("%s,%d,%d")
        % (*iter).first
        % mscBalance
        % mscReserved).str();

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
  map<string, CMPOffer>::const_iterator iter;
   for (iter = my_offers.begin(); iter != my_offers.end(); ++iter) {
     // decompose the key for address
     std::vector<std::string> vstr;
     boost::split(vstr, (*iter).first, boost::is_any_of("-"), token_compress_on);
     CMPOffer const &offer = (*iter).second;
     offer.saveAccepts(file, shaCtx, vstr[0]);
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
    if (false == boost::filesystem::is_regular_file(dIter->status())) {
      // skip funny business
      fprintf(mp_fp, "Non-regular file found in persistence directory : %s\n", dIter->path().filename().string().c_str());
      continue;
    }

    std::vector<std::string> vstr;
    boost::split(vstr, dIter->path().filename().string(), boost::is_any_of("-."), token_compress_on);
    if (  vstr.size() == 3 &&
          is_state_prefix(vstr[0]) &&
          boost::equals(vstr[2], "dat")) {
      uint256 blockHash;
      blockHash.SetHex(vstr[1]);
      statefulBlockHashes.insert(blockHash);
    } else {
      fprintf(mp_fp, "None state file found in persistence directory : %s\n", dIter->path().filename().string().c_str());
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
  mp_fp = fopen ("/tmp/mastercore.log", "a");
  fprintf(mp_fp, "\n%s MASTERCORE INIT\n\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()).c_str());

  if (bTestnet)
  {
    exodus = "n1eXodd53V4eQP96QmJPYTG2oBuFwbq6kL";
    ignore_all_but_MSC = 0;
  }

  p_txlistdb = new MP_txlist(GetDataDir() / "MP_txlist", 1<<20, false, fReindex);
  MPPersistencePath = GetDataDir() / "MP_persist";
  boost::filesystem::create_directories(MPPersistencePath);

  // this is the height of the data included in the preseeds
  static const int snapshotHeight = 290629;
  static const uint64_t snapshotDevMSC = 1743358325718;

  int loadedStateHeight = load_most_relevant_state();
  if (loadedStateHeight < snapshotHeight) {
    // the DEX block, using Zathras' msc_balances_290629.txt
    (void) msc_preseed_file_load(FILETYPE_BALANCES);
    (void) msc_preseed_file_load(FILETYPE_OFFERS);
    loadedStateHeight = snapshotHeight;
    exodus_prev=snapshotDevMSC;
  }

//  (void) msc_file_load(FILETYPE_ACCEPTS); // not needed per Zathras -- we are capturing blocks for which there are no outstanding accepts!

//  (void) msc_post_preseed(249497);  // Exodus block, dump for Zathras

  // collect the real Exodus balances available at the snapshot time
  exodus_balance = getMPbalance(exodus, MASTERCOIN_CURRENCY_MSC);
  printf("Exodus balance: %lu\n", exodus_balance);

  if (!bTestnet)
  {
//    (void) msc_post_preseed(290630);  // the DEX block, using Zathras' msc_balances_290629.txt
//    (void) msc_post_preseed(282083);  // Bart had an issue with this block
//    (void) msc_post_preseed(282080);  // Bart had an issue with this block

    (void) msc_post_preseed(loadedStateHeight + 1);
  }
  else
  {
    // testnet
    (void) msc_post_preseed(GetHeight()-1000); // sometimes testnet blocks get generated very fast, scan the last 1000 just for fun
  }

  // display Exodus balance
  exodus_balance = getMPbalance(exodus, MASTERCOIN_CURRENCY_MSC);
  printf("Exodus balance: %lu\n", exodus_balance);

  return 0;
}

int mastercore_shutdown()
{
  printf("%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);

  delete p_txlistdb; p_txlistdb = NULL;

  fprintf(mp_fp, "\n%s MASTERCORE SHUTDOWN\n\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()).c_str());
  if (mp_fp) { fclose(mp_fp); mp_fp = NULL; }

  return 0;
}

unsigned int msc_pos = 0;
unsigned int msc_neg = 0;
unsigned int msc_zero = 0;
unsigned int msc_total = 0; // position within the block, when available, 0-based

// this is called for every new transaction that comes in (actually in block parsing loop)
int mastercore_handler_tx(const CTransaction &tx, int nBlock, unsigned int idx)
{
int rc = 0;
CMPTransaction mp_obj;

  if (0 == msc_tx_populate(tx, nBlock, idx, &mp_obj))
  {
  // true MP transaction, validity (such as insufficient funds, or offer not found) is determined elsewhere

    mp_obj.print();

    fprintf(mp_fp, "%s(); rc = %d, line %d, file: %s\n", __FUNCTION__, rc, __LINE__, __FILE__);

    // TODO : this needs to be pulled into the refactored parsing engine since its validity is not know in this function !
    // FIXME: and of course only MP-related TXs will be recorded...
    p_txlistdb->recordTX(tx.GetHash(), false, nBlock);
  }

  return 0;
}

string mp_tally::getMSC()
{
  // FIXME: negative numbers -- do they work here?
  return strprintf("%d.%08d", moneys[MASTERCOIN_CURRENCY_MSC]/COIN, moneys[MASTERCOIN_CURRENCY_MSC]%COIN);
}

string mp_tally::getTMSC()
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
const uint64_t nAvailable = getMPbalance(FromAddress, CurrencyID);
CWallet *wallet = pwalletMain;
CCoinControl coinControl; // I am using coin control to send from
int rc = 0;
uint256 txid = 0;

  printf("%s(From: %s , To: %s , Currency= %u, Amount= %lu), line %d, file: %s\n", __FUNCTION__, FromAddress.c_str(), ToAddress.c_str(), CurrencyID, Amount, __LINE__, __FILE__);

  LOCK(wallet->cs_wallet);

  // make sure this address has enough MP currency available!
  if ((nAvailable < Amount) || (0 == Amount))
  {
    LogPrintf("%s(): aborted -- not enough MP currency (%lu < %lu)\n", __FUNCTION__, nAvailable, Amount);
    printf("%s(): aborted -- not enough MP currency (%lu < %lu)\n", __FUNCTION__, nAvailable, Amount);
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
              printf("----------------------------------------\n");

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
                printf("%s:IsMine()=%s:IsSpent()=%s:%s: i=%d, nValue= %lu\n", sAddress.c_str(), bIsMine ? "yes":"NO", bIsSpent ? "YES":"no", wtxid.ToString().c_str(), i, n);

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
  printf("ClassB_send returned %d\n", rc);

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
    int64_t tmpbal = getMPbalance(address, MASTERCOIN_CURRENCY_MSC);
    return ValueFromAmount(tmpbal);
}

void MP_txlist::recordTX(const uint256 &txid, bool fValid, int nBlock)
{
  if (!pdb) return;

const string key = txid.ToString();
const string value = strprintf("%u:%d", fValid ? 1:0, nBlock);
Status status;

  fprintf(mp_fp, "%s(%s, valid=%s, block= %d), line %d, file: %s\n", __FUNCTION__, txid.ToString().c_str(), fValid ? "YES":"NO", nBlock, __LINE__, __FILE__);

  if (pdb)
  {
    status = pdb->Put(writeoptions, key, value);
    ++nWritten;
    fprintf(mp_fp, "%s(): %s, line %d, file: %s\n", __FUNCTION__, status.ToString().c_str(), __LINE__, __FILE__);
  }
}

bool MP_txlist::exists(const uint256 &txid)
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

bool MP_txlist::getTX(const uint256 &txid, string &value)
{
Status status = pdb->Get(readoptions, txid.ToString(), &value);

  ++nRead;

  if (status.ok())
  {
    return true;
  }

  return false;
}

void MP_txlist::printAll()
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

bool IsMPTXvalid(const uint256 &txid)
{
string result;

  if (!p_txlistdb->getTX(txid, result)) return false;

  // parse the string returned, find the validity flag/bit
std::vector<std::string> vstr;
boost::split(vstr, result, boost::is_any_of(":"), token_compress_on);
int validity = atoi(vstr[0]);
int block = atoi(vstr[1]);

  printf("%s():%s;validity=%d, block= %d\n", __FUNCTION__, result.c_str(), validity, block);
  p_txlistdb->printStats();

  if (0 == validity) return false;

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

    Object entry;
    if (!pwalletMain->mapWallet.count(hash))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
    const CWalletTx& wtx = pwalletMain->mapWallet[hash];

/*
    int64_t nCredit = wtx.GetCredit();
    int64_t nDebit = wtx.GetDebit();
    int64_t nNet = nCredit - nDebit;
    int64_t nFee = (wtx.IsFromMe() ? wtx.GetValueOut() - nDebit : 0);
*/

    int64_t MP_Amount = 1337;

    entry.push_back(Pair("amount", ValueFromAmount(MP_Amount)));

    if (wtx.IsFromMe())
    {
//      entry.push_back(Pair("fee", ValueFromAmount(nFee)));
      // TODO: what here?
    }

    // check out leveldb KV store to see if the transaction is found and is valid per Master Protocol rules
    // perhaps I need to distinguish whether it is not found or not valid at this level and report via JSON (?)
    if (IsMPTXvalid(hash))
    {
      // TODO: JSON error - MP transaction is invalid !
      printf("%s() MP TX is VALID ! line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
    }
    else
    {
      printf("%s() MP TX is NOT valid ! line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);
    }

//    WalletTxToJSON(wtx, entry); // TODO: need MP JSON here

    Array details;
//    ListTransactions(wtx, "*", 0, false, details);  // TODO: need MP JSON here
    entry.push_back(Pair("details", details));

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << static_cast<CTransaction>(wtx);
    string strHex = HexStr(ssTx.begin(), ssTx.end());
    entry.push_back(Pair("hex", strHex));

    return entry;
}

// TODO: rename this function and the corresponding RPC call, once I understand better what it's supposed to do with Zathras' help
Value listtransactions_MP(const Array& params, bool fHelp)
{
CWallet *wallet = pwalletMain;
string sAddress = "";

    if (fHelp || params.size() != 1)
        throw runtime_error(
            "*** SOME *** HELP *** GOES *** HERE ***\n"
            + HelpExampleCli("*************_MP", "\"-------------\"")
            + HelpExampleRpc("*****************_MP", "\"-----------------\"")
        );

const string AddressGiven = params[0].get_str();

  LOCK(wallet->cs_wallet);

        // partially lifted from my send_MP function above -- perhaps refactor & merge common code
        for (map<uint256, CWalletTx>::iterator it = wallet->mapWallet.begin(); it != wallet->mapWallet.end(); ++it)
        {
        const uint256& wtxid = it->first;
        const CWalletTx* pcoin = &(*it).second;
        bool bIsMine;
        bool bIsSpent;

//            if (pcoin->IsTrusted())
            {
            const int64_t nAvailable = pcoin->GetAvailableCredit();

              if (!nAvailable) continue;
              printf("----------------------------------------\n");

     for (unsigned int i = 0; i < pcoin->vout.size(); i++)
        {
                CTxDestination dest;

                if(!ExtractDestination(pcoin->vout[i].scriptPubKey, dest))
                    continue;

                bIsMine = IsMine(*wallet, dest);
                bIsSpent = wallet->IsSpent(wtxid, i);

//                if (!bIsMine || bIsSpent) continue;

                int64_t nSatoshis = bIsSpent ? 0 : pcoin->vout[i].nValue;

                sAddress = CBitcoinAddress(dest).ToString();

                printf("%s ; txid= %s ; IsMine()=%s:IsSpent()=%s:i=%d, nValue= %lu\n",
                 sAddress.c_str(), wtxid.ToString().c_str(), bIsMine ? "yes":"NO", bIsSpent ? "YES":"no", i, nSatoshis);

            if (AddressGiven == sAddress)
            {
              printf(">>> MATCHES: %s\n", AddressGiven.c_str());
            }
        }
            }
        }

  return Value::null;   // TODO: what are we returning here??? Zathras?
}

