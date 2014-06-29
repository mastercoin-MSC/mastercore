// global Master Protocol header file
// globals consts (DEFINEs for now) should be here
//
// for now (?) class declarations go here -- work in progress; probably will get pulled out into a separate file, note: some declarations are still in the .cpp file

#ifndef _MASTERCOIN
#define _MASTERCOIN 1

#include "netbase.h"
#include "protocol.h"

// what should've been in the Exodus address for this block if none were spent
#define DEV_MSC_BLOCK_290629 (1743358325718)

// the min amount to send to marker, reference, data outputs, used in send_MP() & related functions
#define MP_DUST_LIMIT 5678

// Master Protocol Transaction (Packet) Version
#define MP_TX_PKT_V0  0
#define MP_TX_PKT_V1  1

// Maximum outputs per BTC Transaction
#define MAX_BTC_OUTPUTS 16

// TODO: clean up is needed for pre-production #DEFINEs , consts & alike belong in header files (classes)
#define MAX_SHA256_OBFUSCATION_TIMES  255

#define PACKET_SIZE_CLASS_A 19
#define PACKET_SIZE         31
// #define MAX_PACKETS         5
#define MAX_PACKETS         64

#define MSC_TYPE_SIMPLE_SEND              0
#define MSC_TYPE_TRADE_OFFER              20
#define MSC_TYPE_ACCEPT_OFFER_BTC         22
#define MSC_TYPE_CREATE_PROPERTY_FIXED    50
#define MSC_TYPE_CREATE_PROPERTY_VARIABLE 51
#define MSC_TYPE_PROMOTE_PROPERTY         52

enum FILETYPES {
  FILETYPE_BALANCES = 0,
  FILETYPE_OFFERS,
  FILETYPE_ACCEPTS,
  FILETYPE_DEVMSC,
  NUM_FILETYPES
};

const char *mastercore_filenames[NUM_FILETYPES]={
"mastercoin_balances.txt",
"mastercoin_offers.txt",
"mastercoin_accepts.txt",
"mastercoin_devmsc.txt"
};

#define PKT_RETURN_OFFER    (1000)
#define PKT_RETURN_ACCEPT   (2000)

#define PKT_ERROR           ( -9000)
#define DEX_ERROR_SELLOFFER (-10000)
#define DEX_ERROR_ACCEPT    (-20000)
#define DEX_ERROR_PAYMENT   (-30000)

#define MASTERCOIN_CURRENCY_MSC   1
#define MASTERCOIN_CURRENCY_TMSC  2
#define MASTERCOIN_CURRENCY_SP1   3

// #define MSC_MAX_KNOWN_CURRENCIES  4
#define MSC_MAX_KNOWN_CURRENCIES  3

inline uint64_t rounduint64(double d)
{
  return (uint64_t)(abs(0.5 + d));
}

extern CCriticalSection cs_tally;
extern char *c_strMastercoinCurrency(int i);

enum TallyType { MONEY = 0, SELLOFFER_RESERVE = 1, ACCEPT_RESERVE = 2 };

class CMPTally
{
private:
  int64_t moneys[MSC_MAX_KNOWN_CURRENCIES];
  int64_t reserved[MSC_MAX_KNOWN_CURRENCIES];   // selloffer-reserved
  int64_t raccepted[MSC_MAX_KNOWN_CURRENCIES];  // accepted-reserved
  bool    divisible;	// mainly for human-interaction purposes; when divisible: multiply by COIN

  void print_old()
  {
    for (unsigned int i=1;i<MSC_MAX_KNOWN_CURRENCIES;i++) // not keeping track of BTC amounts, index=0.. for now?
    {
//      printf("%s = %+15.8lf [reserved= %+15.8lf] ", c_strMastercoinCurrency(i), (double)moneys[i]/(double)COIN, (double)reserved[i]/(double)COIN);
      // just print the money, not the name, getting too wide for my screen
      printf("%+15.8lf [reserved= %+15.8lf] ", (double)moneys[i]/(double)COIN, (double)reserved[i]/(double)COIN);
    }
    printf("\n");
  }

public:

  // when bSet is true -- overwrite the amount in the address, not just adjust it (+/-)
  bool msc_update_moneys(unsigned char which, int64_t amount, bool bSet = false)
  {
  LOCK(cs_tally);

    if (MSC_MAX_KNOWN_CURRENCIES > which)
    {
      if (bSet)
      {
        moneys[which] = amount;
        return true;
      }

//      fprintf(mp_fp, "%s(); FUNDS AVAILABLE= %lu\n", __FUNCTION__, moneys[which]);

      // check here if enough money is available for this address prior to update !!!
      if (0>(moneys[which] + amount))
      {
        fprintf(mp_fp, "%s(); FUNDS AVAILABLE: ONLY= %lu (INSUFFICIENT), line %d, file: %s\n", __FUNCTION__, moneys[which], __LINE__, __FILE__);
        return false;
      }
      moneys[which] += amount;
      return true;
    }

    return false;
  }

  bool msc_update_reserved(unsigned char which, int64_t amount)  
  {
  LOCK(cs_tally);

    if (MSC_MAX_KNOWN_CURRENCIES > which)
    {
//      fprintf(mp_fp, "%s(); SELLOFFER-RESERVED FUNDS AVAILABLE= %lu\n", __FUNCTION__, reserved[which]);

      // check here if enough money is available for this address prior to update !!!
      if (0>(reserved[which] + amount))
      {
        fprintf(mp_fp, "%s(); FUNDS AVAILABLE: ONLY= %lu (INSUFFICIENT), line %d, file: %s\n", __FUNCTION__, reserved[which], __LINE__, __FILE__);
        return false;
      }
      reserved[which] += amount;
      return true;
    }

    return false;
  }

  bool msc_update_raccepted(unsigned char which, int64_t amount)  
  {
  LOCK(cs_tally);

    if (MSC_MAX_KNOWN_CURRENCIES > which)
    {
//      fprintf(mp_fp, "%s(); ACCEPT-RESERVED FUNDS AVAILABLE= %lu\n", __FUNCTION__, reserved[which]);

      // check here if enough money is available for this address prior to update !!!
      if (0>(raccepted[which] + amount))
      {
        fprintf(mp_fp, "%s(); FUNDS AVAILABLE: ONLY= %lu (INSUFFICIENT), line %d, file: %s\n", __FUNCTION__, reserved[which], __LINE__, __FILE__);
        return false;
      }
      raccepted[which] += amount;
      return true;
    }

    return false;
  }

  // the constructor -- create an empty tally for an address
  CMPTally(unsigned char which)
  {
    for (unsigned int i=0;i<MSC_MAX_KNOWN_CURRENCIES;i++)
    {
      moneys[i] = 0;
      reserved[i] = 0;
      raccepted[i] = 0;
      divisible = true;
    }
  }

  void print()
  {
  int i = MASTERCOIN_CURRENCY_MSC;  // Hard-coded for MSC

    printf("%+20.8lf [SO_RESERVE= %+20.8lf , ACCEPT_RESERVE= %+20.8lf ]\n",
     (double)moneys[i]/(double)COIN, (double)reserved[i]/(double)COIN, (double)raccepted[i]/(double)COIN);
  }

  string getMSC();  // this function was created for QT only -- hard-coded internally, TODO: use getMoney()
  string getTMSC(); // this function was created for QT only -- hard-coded internally, TODO: use getMoney()

  uint64_t getMoney(unsigned int which_currency, TallyType ttype) const
  {
    if (MSC_MAX_KNOWN_CURRENCIES <= which_currency) return 0;

    switch (ttype)
    {
      case MONEY:             return moneys[which_currency];
      case SELLOFFER_RESERVE: return reserved[which_currency];
      case ACCEPT_RESERVE:    return raccepted[which_currency];
    }

    return 0;
  }
};

/* leveldb-based storage for the list of ALL Master Protocol TXIDs (key) with validity bit & other misc data as value */
class CMPTxList
{
protected:
    // database options used
    leveldb::Options options;

    // options used when reading from the database
    leveldb::ReadOptions readoptions;

    // options used when iterating over values of the database
    leveldb::ReadOptions iteroptions;

    // options used when writing to the database
    leveldb::WriteOptions writeoptions;

    // options used when sync writing to the database
    leveldb::WriteOptions syncoptions;

    // the database itself
    leveldb::DB *pdb;

    // statistics
    unsigned int nWritten;
    unsigned int nRead;

public:
    CMPTxList(const boost::filesystem::path &path, size_t nCacheSize, bool fMemory, bool fWipe):nWritten(0),nRead(0)
    {
      options.paranoid_checks = true;
      options.create_if_missing = true;

      readoptions.verify_checksums = true;
      iteroptions.verify_checksums = true;
      iteroptions.fill_cache = false;
      syncoptions.sync = true;

      leveldb::Status status = leveldb::DB::Open(options, path.string(), &pdb);

      printf("%s(): %s, line %d, file: %s\n", __FUNCTION__, status.ToString().c_str(), __LINE__, __FILE__);
    }

    ~CMPTxList()
    {
      delete pdb;
      pdb = NULL;
    }

    void recordTX(const uint256 &txid, bool fValid, int nBlock, unsigned int type, uint64_t nValue);
    bool exists(const uint256 &txid);
    bool getTX(const uint256 &txid, string &value);

    void printStats()
    {
      fprintf(mp_fp, "CMPTxList stats: nWritten= %d , nRead= %d\n", nWritten, nRead);
    }

    void printAll();
};

extern map<string, CMPTally> mp_tally_map;
extern uint64_t global_MSC_total;
extern uint64_t global_MSC_RESERVED_total;

uint64_t getMPbalance(const string &Address, unsigned int currency, TallyType ttype);
bool IsMyAddress(const std::string &address);

string getLabel(const string &address);

int mastercore_handler_tx(const CTransaction &tx, int nBlock, unsigned int idx, CBlockHeader pBlockHeader );
int mastercore_save_state( CBlockIndex const *pBlockIndex );

#endif

