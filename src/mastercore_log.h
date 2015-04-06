#ifndef OMNICORE_LOG_H
#define OMNICORE_LOG_H

#include "util.h"

// Log files
extern const char* LOG_FILENAME;
extern const char* INFO_FILENAME;
extern const char* OWNERS_FILENAME;

// Debug flags
extern const int msc_debug_parser_data;
extern const int msc_debug_parser;
extern const int msc_debug_verbose;
extern const int msc_debug_verbose2;
extern const int msc_debug_verbose3;
extern const int msc_debug_vin;
extern const int msc_debug_script;
extern const int msc_debug_dex;
extern const int msc_debug_send;
extern const int msc_debug_tokens;
extern const int msc_debug_spec;
extern const int msc_debug_exo;
extern const int msc_debug_tally;
extern const int msc_debug_sp;
extern const int msc_debug_sto;
extern const int msc_debug_txdb;
extern const int msc_debug_tradedb;
extern const int msc_debug_persistence;
extern const int msc_debug_metadex1;
extern const int msc_debug_metadex2;
extern const int msc_debug_metadex3;

int mp_LogPrintStr(const std::string& str);
void shrinkDebugFile();

/* When we switch to C++11, this can be switched to variadic templates instead
 * of this macro-based construction (see tinyformat.h).
 */
#define MP_MAKE_ERROR_AND_LOG_FUNC(n)                                         \
    /*   Print to debug.log if -debug=category switch is given OR category is NULL. */  \
    template<TINYFORMAT_ARGTYPES(n)>                                          \
    static inline int mp_category_log(const char* category, const char* format, TINYFORMAT_VARARGS(n))  \
    {                                                                         \
        if (!LogAcceptCategory(category)) return 0;                           \
        return mp_LogPrintStr(tfm::format(format, TINYFORMAT_PASSARGS(n)));   \
    }                                                                         \
    template<TINYFORMAT_ARGTYPES(n)>                                          \
    static inline int mp_log(TINYFORMAT_VARARGS(n))                           \
    {                                                                         \
        return mp_LogPrintStr(tfm::format("%s", TINYFORMAT_PASSARGS(n)));     \
    }                                                                         \
    template<TINYFORMAT_ARGTYPES(n)>                                          \
    static inline int mp_log(const char* format, TINYFORMAT_VARARGS(n))       \
    {                                                                         \
        return mp_LogPrintStr(tfm::format(format, TINYFORMAT_PASSARGS(n)));   \
    }                                                                         \
    template<TINYFORMAT_ARGTYPES(n)>                                          \
    static inline int file_log(const char* format, TINYFORMAT_VARARGS(n))     \
    {                                                                         \
        return mp_LogPrintStr(tfm::format(format, TINYFORMAT_PASSARGS(n)));   \
    }                                                                         \
    template<TINYFORMAT_ARGTYPES(n)>                                          \
    static inline int file_log(TINYFORMAT_VARARGS(n))                         \
    {                                                                         \
        return mp_LogPrintStr(tfm::format("%s", TINYFORMAT_PASSARGS(n)));     \
    }                                                                         \
    /*   Log error and return false */                                        \
    template<TINYFORMAT_ARGTYPES(n)>                                          \
    static inline bool mp_error(const char* format, TINYFORMAT_VARARGS(n))    \
    {                                                                         \
        mp_LogPrintStr("ERROR: " + tfm::format(format, TINYFORMAT_PASSARGS(n)) + "\n"); \
        return false;                                                         \
    }

TINYFORMAT_FOREACH_ARGNUM(MP_MAKE_ERROR_AND_LOG_FUNC)

#endif // OMNICORE_LOG_H
