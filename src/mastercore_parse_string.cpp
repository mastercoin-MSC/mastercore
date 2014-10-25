#include "mastercore_parse_string.h"

#include "util.h"

#include <stdint.h>
#include <algorithm>
#include <string>

#include <boost/lexical_cast.hpp>

namespace mastercore
{
std::string FormatDivisibleAmount(int64_t n)
{
    // Prepend sign only for negative numbers
    bool is_negative = (n < 0);

    return FormatDivisibleAmount(n, is_negative);
}

std::string FormatDivisibleAmount(int64_t n, bool sign)
{
    // Note: not using straight sprintf here because we do NOT want
    // localized number formatting.
    int64_t n_abs = (n > 0 ? n : -n);
    int64_t quotient = n_abs / COIN;
    int64_t remainder = n_abs % COIN;
    std::string str = tfm::format("%ld.%08d", quotient, remainder);

    if (!sign)
        return str;

    if (n < 0)
        str.insert((unsigned int) 0, 1, '-');
    else
        str.insert((unsigned int) 0, 1, '+');

    return str;
}

std::string FormatIndivisibleAmount(int64_t n)
{
    return tfm::format("%ld", n);
}

std::string FormatTokenAmount(int64_t n)
{
    // Default token amounts are formatted as indivisible
    // token which aligns well with an integer input
    return FormatIndivisibleAmount(n);
}

std::string FormatTokenAmount(int64_t n, bool divisible)
{
    if (divisible)
        return FormatDivisibleAmount(n);

    return FormatIndivisibleAmount(n);
}

int64_t StrToInt64(const std::string& str, bool divisible)
{    
    // copy original, so it remains unchanged
    std::string strAmount (str);
    int64_t nAmount = 0;
    
    // check for a negative (minus sign) and invalidate if present
    size_t negSignPos = strAmount.find("-");
    if (negSignPos != std::string::npos) return 0;

    // convert the string into a usable int64
    if (divisible) {
        // check for existance of decimal point
        size_t pos = strAmount.find(".");
        if (pos == std::string::npos) {
            // no decimal point but divisible so pad 8 zeros on right
            strAmount += "00000000";
        } else {
            // check for existence of second decimal point, if so invalidate amount
            size_t posSecond = strAmount.find(".", pos + 1);
            if (posSecond != std::string::npos) return 0;
            
            if ((strAmount.size() - pos) < 9) {
                // there are decimals either exact or not enough, pad as needed
                std::string strRightOfDecimal = strAmount.substr(pos + 1);
                unsigned int zerosToPad = 8 - strRightOfDecimal.size();
                
                // do we need to pad?
                if (zerosToPad > 0) 
                {
                    for (unsigned int it = 0; it != zerosToPad; it++) {
                        strAmount += "0";
                    }
                }
            } else {
                // there are too many decimals, truncate after 8
                strAmount = strAmount.substr(0, pos + 9);
            }
        }
        strAmount.erase(std::remove(strAmount.begin(), strAmount.end(), '.'), strAmount.end());
        try {
            nAmount = boost::lexical_cast<int64_t>(strAmount);
        } catch (const boost::bad_lexical_cast &e) {}
    } else {
        size_t pos = strAmount.find(".");
        std::string newStrAmount = strAmount.substr(0, pos);
        try {
            nAmount = boost::lexical_cast<int64_t>(newStrAmount);
        } catch (const boost::bad_lexical_cast &e) {}
    }

    return nAmount;
}
} // namespace mastercore
