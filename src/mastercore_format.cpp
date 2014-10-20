#include "mastercore_format.h"

// TODO: find a way to use strprintf/tfm::format directly
// It currently relies on util.h and tinyformat.h, but has heavy dependencies
#include "util.h"

#include <string>
#include <stdint.h>

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

std::string FormatIndivisibleAmount(int64_t n)
{
    return tfm::format("%d", n);
}

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
    std::string str = tfm::format("%d.%08d", quotient, remainder);

    if (!sign)
        return str;

    if (n < 0)
        str.insert((unsigned int) 0, 1, '-');
    else
        str.insert((unsigned int) 0, 1, '+');
    
    return str;
}
