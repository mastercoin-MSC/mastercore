// global Master Protocol header file
// globals consts (DEFINEs for now) should be here
//
// for now (?) class declarations go here -- work in progress; probably will get pulled out into a separate file, note: some declarations are still in the .cpp file

#ifndef _MASTERCOIN
#define _MASTERCOIN 1

#include "netbase.h"
#include "protocol.h"

#define TEST_ECO_PROPERTY_1 (0x80000003UL)

// could probably also use: int64_t maxInt64 = std::numeric_limits<int64_t>::max();
// maximum numeric values from the spec: 
#define MAX_INT_8_BYTES (9223372036854775807)

// what should've been in the Exodus address for this block if none were spent
#define DEV_MSC_BLOCK_290629 (1743358325718)

#define SP_STRING_FIELD_LEN 256

// in Mastercoin Satoshis (Willetts)
#define TRANSFER_FEE_PER_OWNER  (1)

// some boost formats
#define FORMAT_BOOST_TXINDEXKEY "index-tx-%s"
#define FORMAT_BOOST_SPKEY      "sp-%d"

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
#define MAX_PACKETS         64

// Transaction types, from the spec
enum TransactionType {
  MSC_TYPE_SIMPLE_SEND              =  0,
  MSC_TYPE_RESTRICTED_SEND          =  2,
  MSC_TYPE_SEND_TO_OWNERS           =  3,
  MSC_TYPE_AUTOMATIC_DISPENSARY     = 15,
  MSC_TYPE_TRADE_OFFER              = 20,
  MSC_TYPE_METADEX                  = 21,
  MSC_TYPE_ACCEPT_OFFER_BTC         = 22,
  MSC_TYPE_CREATE_PROPERTY_FIXED    = 50,
  MSC_TYPE_CREATE_PROPERTY_VARIABLE = 51,
  MSC_TYPE_PROMOTE_PROPERTY         = 52,
  MSC_TYPE_CLOSE_CROWDSALE          = 53,
};

#define MSC_PROPERTY_TYPE_INDIVISIBLE             1
#define MSC_PROPERTY_TYPE_DIVISIBLE               2
#define MSC_PROPERTY_TYPE_INDIVISIBLE_REPLACING   65
#define MSC_PROPERTY_TYPE_DIVISIBLE_REPLACING     66
#define MSC_PROPERTY_TYPE_INDIVISIBLE_APPENDING   129
#define MSC_PROPERTY_TYPE_DIVISIBLE_APPENDING     130

// block height (MainNet) with which the corresponding transaction is considered valid, per spec
enum BLOCKHEIGHTRESTRICTIONS {
// starting block for parsing on TestNet
//  START_TESTNET_BLOCK= 253728,
  START_TESTNET_BLOCK=263000,
  START_REGTEST_BLOCK=5,
  MONEYMAN_TESTNET_BLOCK= 270775, // new address to assign MSC & TMSC on TestNet
  POST_EXODUS_BLOCK = 255366,
  MSC_DEX_BLOCK     = 290630,
  MSC_SP_BLOCK      = 297110,
  GENESIS_BLOCK     = 249498,
  LAST_EXODUS_BLOCK = 255365,
  MSC_STO_BLOCK     = 999999,
};

int txBlockRestrictions[][2] = {
  {MSC_TYPE_SIMPLE_SEND,              GENESIS_BLOCK},
  {MSC_TYPE_TRADE_OFFER,              MSC_DEX_BLOCK},
  {MSC_TYPE_ACCEPT_OFFER_BTC,         MSC_DEX_BLOCK},
  {MSC_TYPE_CREATE_PROPERTY_FIXED,    MSC_SP_BLOCK},
  {MSC_TYPE_CREATE_PROPERTY_VARIABLE, MSC_SP_BLOCK},
  {MSC_TYPE_CLOSE_CROWDSALE,          MSC_SP_BLOCK},
  {MSC_TYPE_SEND_TO_OWNERS,           MSC_STO_BLOCK},

// end of array marker, in addition to sizeof/sizeof
  {-1,-1},
};

enum FILETYPES {
  FILETYPE_BALANCES = 0,
  FILETYPE_OFFERS,
  FILETYPE_ACCEPTS,
  FILETYPE_GLOBALS,
  FILETYPE_CROWDSALES,
  NUM_FILETYPES
};

const char *mastercore_filenames[NUM_FILETYPES]={
"mastercoin_balances.txt",
"mastercoin_offers.txt",
"mastercoin_accepts.txt",
"mastercoin_globals.txt",
"mastercoin_crowdsales.txt",
};

#define PKT_RETURN_OFFER    (1000)
// #define PKT_RETURN_ACCEPT   (2000)

#define PKT_ERROR             ( -9000)
#define DEX_ERROR_SELLOFFER   (-10000)
#define DEX_ERROR_ACCEPT      (-20000)
#define DEX_ERROR_PAYMENT     (-30000)
// Smart Properties
#define PKT_ERROR_SP          (-40000)
// Send To Owners
#define PKT_ERROR_STO         (-50000)
#define PKT_ERROR_SEND        (-60000)
#define PKT_ERROR_TRADEOFFER  (-70000)

#define MASTERCOIN_CURRENCY_BTC   0
#define MASTERCOIN_CURRENCY_MSC   1
#define MASTERCOIN_CURRENCY_TMSC  2

#define MSC_MAX_KNOWN_CURRENCIES  64  // TODO, FIXME: take this away, used to write persistent files

inline uint64_t rounduint64(double d)
{
  return (uint64_t)(abs(0.5 + d));
}

extern CCriticalSection cs_tally;
extern char *c_strMastercoinCurrency(int i);

enum TallyType { MONEY = 0, SELLOFFER_RESERVE = 1, ACCEPT_RESERVE = 2, TALLY_TYPE_COUNT };

class CMPTally
{
private:
typedef struct
{
  uint64_t balance[TALLY_TYPE_COUNT];
} BalanceRecord;

  typedef std::map<unsigned int, BalanceRecord> TokenMap;
  TokenMap mp_token;
  TokenMap::iterator my_it;

//  bool    divisible;	// mainly for human-interaction purposes; when divisible: multiply by COIN

  bool propertyExists(unsigned int which_currency) const
  {
  const TokenMap::const_iterator it = mp_token.find(which_currency);

    return (it != mp_token.end());
  }

public:
//  bool isDivisible() const { return divisible; }

  unsigned int init()
  {
  unsigned int ret = 0;

//    printf("%s();size = %lu, line %d, file: %s\n", __FUNCTION__, mp_token.size(), __LINE__, __FILE__);
    my_it = mp_token.begin();
    if (my_it != mp_token.end()) ret = my_it->first;
//    printf("%s();size = %lu, ret= %u, line %d, file: %s\n", __FUNCTION__, mp_token.size(), ret, __LINE__, __FILE__);

    return ret;
  }

  unsigned int next()
  {
  unsigned int ret;

//    printf("%s(), line %d, file: %s\n", __FUNCTION__, __LINE__, __FILE__);

    if (my_it == mp_token.end()) return 0;

    ret = my_it->first;

//    printf("%s();ret =%u, line %d, file: %s\n", __FUNCTION__, ret, __LINE__, __FILE__);

    ++my_it;

    return ret;
  }

  bool updateMoney(unsigned int which_currency, int64_t amount, TallyType ttype)
  {
  bool bRet = false;
  int64_t now64;

    LOCK(cs_tally);

    now64 = mp_token[which_currency].balance[ttype];

    if (0>(now64 + amount))
    {
    }
    else
    {
      now64 += amount;
      mp_token[which_currency].balance[ttype] = now64;

      bRet = true;
    }

    return bRet;
  }

  // the constructor -- create an empty tally for an address
  CMPTally()
  {
//    divisible = true; // TODO: re-think, but currently hard-coded
    my_it = mp_token.begin();
  }

  void print(int which_currency = MASTERCOIN_CURRENCY_MSC, bool bDivisible = true)
  {
  uint64_t money = 0;
  uint64_t so_r = 0;
  uint64_t a_r = 0;

    if (propertyExists(which_currency))
    {
      money = mp_token[which_currency].balance[MONEY];
      so_r = mp_token[which_currency].balance[SELLOFFER_RESERVE];
      a_r = mp_token[which_currency].balance[ACCEPT_RESERVE];
    }

    if (bDivisible)
    {
      printf("%+20.8lf [SO_RESERVE= %+20.8lf , ACCEPT_RESERVE= %+20.8lf ]\n",
       (double)money/(double)COIN, (double)so_r/(double)COIN, (double)a_r/(double)COIN);
    }
    else
    {
      printf("%14lu [SO_RESERVE= %14lu , ACCEPT_RESERVE= %14lu ]\n", money, so_r, a_r);
    }
  }

  uint64_t getMoney(unsigned int which_currency, TallyType ttype)
  {
  uint64_t ret64 = 0;

    LOCK(cs_tally);

    if (propertyExists(which_currency)) ret64 = mp_token[which_currency].balance[ttype];

    return ret64;
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

    bool isMPinBlockRange(int, int, bool);
};

// extern map<string, CMPTally> mp_tally_map;
extern uint64_t global_MSC_total;
extern uint64_t global_MSC_RESERVED_total;

int mastercore_init(void);

uint64_t getMPbalance(const string &Address, unsigned int currency, TallyType ttype);
bool IsMyAddress(const std::string &address);

string getLabel(const string &address);

int mastercore_handler_tx(const CTransaction &tx, int nBlock, unsigned int idx, CBlockIndex const *pBlockIndex );
int mastercore_save_state( CBlockIndex const *pBlockIndex );

#endif

