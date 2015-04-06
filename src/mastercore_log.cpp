#include "mastercore_log.h"

#include <assert.h>
#include <stdio.h>
#include <string>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/once.hpp>

/**
 * util.h/.cpp:
 *
 * std::string DateTimeStrFormat(const char* pszFormat, int64_t nTime);
 * const boost::filesystem::path &GetDataDir(bool fNetSpecific);
 * int64_t GetTime();
 *
 * bool fPrintToConsole = false;
 * bool fLogTimestamps = false;
 */
#include "util.h"

const char* LOG_FILENAME        = "mastercore.log";
const char* INFO_FILENAME       = "mastercore_crowdsales.log";
const char* OWNERS_FILENAME     = "mastercore_owners.log";

const int msc_debug_parser_data = 0;
const int msc_debug_parser      = 0;
const int msc_debug_verbose     = 0;
const int msc_debug_verbose2    = 0;
const int msc_debug_verbose3    = 0;
const int msc_debug_vin         = 0;
const int msc_debug_script      = 0;
const int msc_debug_dex         = 1;
const int msc_debug_send        = 1;
const int msc_debug_tokens      = 0;
const int msc_debug_spec        = 1;
const int msc_debug_exo         = 0;
const int msc_debug_tally       = 1;
const int msc_debug_sp          = 1;
const int msc_debug_sto         = 1;
const int msc_debug_txdb        = 0;
const int msc_debug_tradedb     = 1;
const int msc_debug_persistence = 0;

// disable all flags for metadex for the immediate tag, then turn back on 0 & 2 at least
const int msc_debug_metadex1    = 0;
const int msc_debug_metadex2    = 0;
// enable this to see the orderbook before and after each transaction
const int msc_debug_metadex3    = 0;

// LogPrintf() has been broken a couple of times now
// by well-meaning people adding mutexes in the most straightforward way.
// It breaks because it may be called by global destructors during shutdown.
// Since the order of destruction of static/global objects is undefined,
// defining a mutex as a global object doesn't work (the mutex gets
// destroyed, and then some later destructor calls OutputDebugStringF,
// maybe indirectly, and you get a core dump at shutdown trying to lock
// the mutex).
static boost::once_flag mp_debugPrintInitFlag = BOOST_ONCE_INIT;

// We use boost::call_once() to make sure these are initialized in
// in a thread-safe manner the first time it is called:
static FILE* fileout = NULL;
static boost::mutex* mutexDebugLog = NULL;

static void mp_DebugPrintInit() {
    assert(fileout == NULL);
    assert(mutexDebugLog == NULL);

    boost::filesystem::path pathDebug = GetDataDir() / LOG_FILENAME;
    fileout = fopen(pathDebug.c_str(), "a");
    if (fileout) setbuf(fileout, NULL); // unbuffered

    mutexDebugLog = new boost::mutex();
}

int mp_LogPrintStr(const std::string &str) {
    int ret = 0; // Returns total number of characters written
    if (fPrintToConsole) {
        // print to console
        ret = fwrite(str.data(), 1, str.size(), stdout);
    } else if (fPrintToDebugLog) {
        static bool fStartedNewLine = true;
        boost::call_once(&mp_DebugPrintInit, mp_debugPrintInitFlag);

        if (fileout == NULL)
            return ret;

        boost::mutex::scoped_lock scoped_lock(*mutexDebugLog);

        // reopen the log file, if requested
        if (fReopenDebugLog) {
            fReopenDebugLog = false;
            boost::filesystem::path pathDebug = GetDataDir() / LOG_FILENAME;
            if (freopen(pathDebug.c_str(), "a", fileout) != NULL)
                setbuf(fileout, NULL); // unbuffered
        }

        // Debug print useful for profiling
        if (fLogTimestamps && fStartedNewLine)
            ret += fprintf(fileout, "%s ", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()).c_str());
        if (!str.empty() && str[str.size() - 1] == '\n')
            fStartedNewLine = true;
        else
            fStartedNewLine = false;

        ret = fwrite(str.data(), 1, str.size(), fileout);
    }

    return ret;
}

void shrinkDebugFile() {
    // Scroll log if it's getting too big
    const int buffer_size = 8000000; // 8 MByte
    boost::filesystem::path pathLog = GetDataDir() / LOG_FILENAME;
    FILE* file = fopen(pathLog.c_str(), "r");

    if (file && boost::filesystem::file_size(pathLog) > 50000000) // 50 MByte
    {
        // Restart the file with some of the end
        char *pch = new char[buffer_size];
        if (NULL != pch) {
            fseek(file, -buffer_size, SEEK_END);
            int nBytes = fread(pch, 1, buffer_size, file);
            fclose(file);
            file = NULL;

            file = fopen(pathLog.c_str(), "w");
            if (file) {
                fwrite(pch, 1, nBytes, file);
                fclose(file);
                file = NULL;
            }
            delete [] pch;
        }
    } else {
        if (NULL != file) fclose(file);
    }
}

