#ifndef _MASTERCOIN_CONVERT
#define _MASTERCOIN_CONVERT

#include <string>
#include <stdint.h>

// Converts numbers into 64 bit wide integer and rounds 
// up.
uint64_t RoundToUInt64(long double d);

// Converts a string into an integer. Divisible as well as
// indivisible amounts are accepted. Invalid inputs result
// in 0. Any "-" invalidates the string.
// 1 indivisible unit equals 0.00000001 divisible units.
// Divisible amounts are truncated after the 8 digit, right
// behind the first and only decimal point ".".
// Indivisible amounts are truncated after a decimal point.
int64_t StrToInt64(const std::string& str, bool divisible);

#endif // _MASTERCOIN_CONVERT
