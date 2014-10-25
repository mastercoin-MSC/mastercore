#ifndef _MASTERCOIN_PARSE_STRING
#define _MASTERCOIN_PARSE_STRING

#include <stdint.h>
#include <string>

namespace mastercore
{
// TODO: add FormatMP() -- currently depends on isPropertyDivisible()

// Formats a value as divisible token amount with 8 digits.
// Per default a minus sign is prepended, if n is negative.
// A positive or negative sign can be enforced.
std::string FormatDivisibleAmount(int64_t n);
std::string FormatDivisibleAmount(int64_t n, bool sign);

// Formats a value as indivisible token amount with leading
// minus sign, if n is negative.
std::string FormatIndivisibleAmount(int64_t n);

// Formats a value as token amount and prepends a minus sign,
// if the value is negative. Divisible amounts have 8 digits.
// Per default n is formatted as indivisible amount.
std::string FormatTokenAmount(int64_t n);
std::string FormatTokenAmount(int64_t n, bool divisible);

// Converts strings to 64 bit wide interger.
// Divisible and indivisible amounts are accepted.
// 1 indivisible unit equals 0.00000001 divisible units.
// If input string is not a accepted number, 0 is returned.
// Divisible amounts are truncated after 8 decimal places.
// Characters after decimal mark are ignored for indivisible
// amounts.
// Any minus sign invalidates.
int64_t StrToInt64(const std::string& str, bool divisible);
}

#endif // _MASTERCOIN_PARSE_STRING
