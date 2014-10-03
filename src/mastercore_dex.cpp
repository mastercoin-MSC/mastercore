// DEx & MetaDEx

#include "base58.h"
#include "rpcserver.h"
#include "init.h"
#include "util.h"
#include "wallet.h"

#include <stdint.h>
#include <string.h>
#include <map>

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

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;
using namespace leveldb;

#include "mastercore.h"

using namespace mastercore;

#include "mastercore_dex.h"
#include "mastercore_tx.h"

extern int msc_debug_dex, msc_debug_metadex;

static uint64_t getGoodFractionalPartPrecision(uint64_t n1, uint64_t n2)
{
  if (!n2) return 0;

  const uint64_t remainder = n1 % n2;
  const double frac = (double)remainder / (double)n2;

  return (GOOD_PRECISION * frac);
}

CMPMetaDEx *mastercore::getMetaDEx(const string &sender_addr, unsigned int curr)
{
  if (msc_debug_metadex) fprintf(mp_fp, "%s()\n", __FUNCTION__);

const string combo = STR_SELLOFFER_ADDR_CURR_COMBO(sender_addr);
  MetaDExMap::iterator it = metadex.find(combo);

  if (it != metadex.end()) return &(it->second);

  return (CMPMetaDEx *) NULL;
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

// check to see if such a sell offer exists
bool mastercore::DEx_offerExists(const string &seller_addr, unsigned int curr)
{
//  if (msc_debug_dex) fprintf(mp_fp, "%s()\n", __FUNCTION__);
const string combo = STR_SELLOFFER_ADDR_CURR_COMBO(seller_addr);
OfferMap::iterator my_it = my_offers.find(combo);

  return !(my_it == my_offers.end());
}

// getOffer may replace DEx_offerExists() in the near future
// TODO: locks are needed around map's insert & erase
CMPOffer *mastercore::DEx_getOffer(const string &seller_addr, unsigned int curr)
{
  if (msc_debug_dex) fprintf(mp_fp, "%s(%s, %u)\n", __FUNCTION__, seller_addr.c_str(), curr);
const string combo = STR_SELLOFFER_ADDR_CURR_COMBO(seller_addr);
OfferMap::iterator my_it = my_offers.find(combo);

  if (my_it != my_offers.end()) return &(my_it->second);

  return (CMPOffer *) NULL;
}

// TODO: locks are needed around map's insert & erase
CMPAccept *mastercore::DEx_getAccept(const string &seller_addr, unsigned int curr, const string &buyer_addr)
{
  if (msc_debug_dex) fprintf(mp_fp, "%s(%s, %u, %s)\n", __FUNCTION__, seller_addr.c_str(), curr, buyer_addr.c_str());
const string combo = STR_ACCEPT_ADDR_CURR_ADDR_COMBO(seller_addr, buyer_addr);
AcceptMap::iterator my_it = my_accepts.find(combo);

  if (my_it != my_accepts.end()) return &(my_it->second);

  return (CMPAccept *) NULL;
}

// returns 0 if everything is OK
int mastercore::DEx_offerCreate(string seller_addr, unsigned int curr, uint64_t nValue, int block, uint64_t amount_desired, uint64_t fee, unsigned char btl, const uint256 &txid, uint64_t *nAmended)
{
//  if (msc_debug_dex) fprintf(mp_fp, "%s()\n", __FUNCTION__);
int rc = DEX_ERROR_SELLOFFER;

  // sanity check our params are OK
  if ((!btl) || (!amount_desired)) return (DEX_ERROR_SELLOFFER -101); // time limit or amount desired empty

  if (DEx_getOffer(seller_addr, curr)) return (DEX_ERROR_SELLOFFER -10);  // offer already exists

  const string combo = STR_SELLOFFER_ADDR_CURR_COMBO(seller_addr);

  if (msc_debug_dex)
   fprintf(mp_fp, "%s(%s|%s), nValue=%lu)\n", __FUNCTION__, seller_addr.c_str(), combo.c_str(), nValue);

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
int mastercore::DEx_offerDestroy(const string &seller_addr, unsigned int curr)
{
//  if (msc_debug_dex) fprintf(mp_fp, "%s()\n", __FUNCTION__);
const uint64_t amount = getMPbalance(seller_addr, curr, SELLOFFER_RESERVE);

  if (!DEx_offerExists(seller_addr, curr)) return (DEX_ERROR_SELLOFFER -11); // offer does not exist

  const string combo = STR_SELLOFFER_ADDR_CURR_COMBO(seller_addr);

  OfferMap::iterator my_it;

  my_it = my_offers.find(combo);

  if (amount)
  {
    update_tally_map(seller_addr, curr, amount, MONEY);   // give money back to the seller from SellOffer-Reserve
    update_tally_map(seller_addr, curr, - amount, SELLOFFER_RESERVE);
  }

  // delete the offer
  my_offers.erase(my_it);

  if (msc_debug_dex)
   fprintf(mp_fp, "%s(%s|%s)\n", __FUNCTION__, seller_addr.c_str(), combo.c_str());

  return 0;
}

// returns 0 if everything is OK
int mastercore::DEx_offerUpdate(const string &seller_addr, unsigned int curr, uint64_t nValue, int block, uint64_t desired, uint64_t fee, unsigned char btl, const uint256 &txid, uint64_t *nAmended)
{
//  if (msc_debug_dex) fprintf(mp_fp, "%s()\n", __FUNCTION__);
int rc = DEX_ERROR_SELLOFFER;

  fprintf(mp_fp, "%s(%s, %d)\n", __FUNCTION__, seller_addr.c_str(), curr);

  if (!DEx_offerExists(seller_addr, curr)) return (DEX_ERROR_SELLOFFER -12); // offer does not exist

  rc = DEx_offerDestroy(seller_addr, curr);

  if (!rc)
  {
    rc = DEx_offerCreate(seller_addr, curr, nValue, block, desired, fee, btl, txid, nAmended);
  }

  return rc;
}

// returns 0 if everything is OK
int mastercore::DEx_acceptCreate(const string &buyer, const string &seller, int curr, uint64_t nValue, int block, uint64_t fee_paid, uint64_t *nAmended)
{
int rc = DEX_ERROR_ACCEPT - 10;
OfferMap::iterator my_it;
const string selloffer_combo = STR_SELLOFFER_ADDR_CURR_COMBO(seller);
const string accept_combo = STR_ACCEPT_ADDR_CURR_ADDR_COMBO(seller, buyer);
uint64_t nActualAmount = getMPbalance(seller, curr, SELLOFFER_RESERVE);

  my_it = my_offers.find(selloffer_combo);

  if (my_it == my_offers.end()) return DEX_ERROR_ACCEPT -15;

  CMPOffer &offer = my_it->second;

  if (msc_debug_dex) fprintf(mp_fp, "%s(offer: %s)\n", __FUNCTION__, offer.getHash().GetHex().c_str());

  // here we ensure the correct BTC fee was paid in this acceptance message, per spec
  if (fee_paid < offer.getMinFee())
  {
    fprintf(mp_fp, "ERROR: fee too small -- the ACCEPT is rejected! (%lu is smaller than %lu)\n", fee_paid, offer.getMinFee());
    return DEX_ERROR_ACCEPT -105;
  }

  fprintf(mp_fp, "%s(%s) OFFER FOUND\n", __FUNCTION__, selloffer_combo.c_str());

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
int mastercore::DEx_acceptDestroy(const string &buyer, const string &seller, int curr, bool bForceErase)
{
int rc = DEX_ERROR_ACCEPT - 20;
CMPOffer *p_offer = DEx_getOffer(seller, curr);
CMPAccept *p_accept = DEx_getAccept(seller, curr, buyer);
bool bReturnToMoney; // return to MONEY of the seller, otherwise return to SELLOFFER_RESERVE
const string accept_combo = STR_ACCEPT_ADDR_CURR_ADDR_COMBO(seller, buyer);

  if (!p_accept) return rc; // sanity check

  const uint64_t nActualAmount = p_accept->getAcceptAmountRemaining();

  // if the offer is gone ACCEPT_RESERVE should go back to MONEY
  if (!p_offer)
  {
    bReturnToMoney = true;
  }
  else
  {
    fprintf(mp_fp, "%s() HASHES: offer=%s, accept=%s\n", __FUNCTION__, p_offer->getHash().GetHex().c_str(), p_accept->getHash().GetHex().c_str());

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
  const AcceptMap::iterator my_it = my_accepts.find(accept_combo);

    if (my_accepts.end() !=my_it) my_accepts.erase(my_it);
  }

  return rc;
}

// incoming BTC payment for the offer
// TODO: verify proper partial payment handling
int mastercore::DEx_payment(uint256 txid, unsigned int vout, string seller, string buyer, uint64_t BTC_paid, int blockNow, uint64_t *nAmended)
{
//  if (msc_debug_dex) fprintf(mp_fp, "%s()\n", __FUNCTION__);

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

  if (msc_debug_dex) fprintf(mp_fp, "%s(%s, %s)\n", __FUNCTION__, seller.c_str(), buyer.c_str());

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
AcceptMap::iterator my_it = my_accepts.begin();

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
      boost::split(vstr, my_it->first, boost::is_any_of("-+"), boost::token_compress_on);
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

int mastercore::MetaDEx_Trade(const string &customer, unsigned int currency, unsigned int currency_desired, uint64_t amount_desired,
 uint64_t price_int, uint64_t price_frac)
{
  return 0;
}

// bSell = true when selling property for MSC/TMSC
int mastercore::MetaDEx_Phase1(const string &addr, unsigned int property, bool bSell, const uint256 &txid, unsigned int idx)
{
  fprintf(mp_fp, "%s(%s, bSell=%s), line %d, file: %s\n", __FUNCTION__, addr.c_str(), bSell ? "YES":"NO", __LINE__, __FILE__);

  return 0;
}

int mastercore::MetaDEx_Create(const string &sender_addr, unsigned int curr, uint64_t amount, int block, unsigned int currency_desired, uint64_t amount_desired, const uint256 &txid, unsigned int idx)
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
int mastercore::MetaDEx_Destroy(const string &sender_addr, unsigned int curr)
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

int mastercore::MetaDEx_Update(const string &sender_addr, unsigned int curr, uint64_t nValue, int block, unsigned int currency_desired, uint64_t amount_desired, const uint256 &txid, unsigned int idx)
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

