#ifndef _MASTERCOIN_FORMAT
#define _MASTERCOIN_FORMAT

#include <string>
#include <stdint.h>

// Formats a value as token amount and prepends a minus sign,
// if the value is negative. Divisible amounts have 8 digits.
// Per default n is formatted as indivisible amount.
std::string FormatTokenAmount(int64_t n);
std::string FormatTokenAmount(int64_t n, bool divisible);

// Formats a value as indivisible token amount with leading
// minus sign, if n is negative.
std::string FormatIndivisibleAmount(int64_t n);

// Formats a value as divisible token amount with 8 digits.
// Per default a minus sign is prepended, if n is negative.
// A positive or negative sign can be enforced.
std::string FormatDivisibleAmount(int64_t n);
std::string FormatDivisibleAmount(int64_t n, bool sign);

#endif // _MASTERCOIN_FORMAT
