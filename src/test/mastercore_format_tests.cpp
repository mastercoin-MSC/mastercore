#include "mastercore_format.h"

#include <string>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(mastercore_format_tests)

BOOST_AUTO_TEST_CASE(mastercore_format_indivisible)
{
    // positive numbers
    BOOST_CHECK("0" == FormatIndivisibleAmount(0));
    BOOST_CHECK("1" == FormatIndivisibleAmount(1));
    BOOST_CHECK("10" == FormatIndivisibleAmount(10));
    BOOST_CHECK("100" == FormatIndivisibleAmount(100));
    BOOST_CHECK("1000" == FormatIndivisibleAmount(1000));
    BOOST_CHECK("10000" == FormatIndivisibleAmount(10000));
    BOOST_CHECK("100000" == FormatIndivisibleAmount(100000));
    BOOST_CHECK("1000000" == FormatIndivisibleAmount(1000000));
    BOOST_CHECK("10000000" == FormatIndivisibleAmount(10000000));
    BOOST_CHECK("100000000" == FormatIndivisibleAmount(100000000));
    BOOST_CHECK("1000000000" == FormatIndivisibleAmount(1000000000));
    BOOST_CHECK("10000000000" == FormatIndivisibleAmount(10000000000));
    BOOST_CHECK("100000000000" == FormatIndivisibleAmount(100000000000));
    BOOST_CHECK("1000000000000" == FormatIndivisibleAmount(1000000000000));
    BOOST_CHECK("10000000000000" == FormatIndivisibleAmount(10000000000000));
    BOOST_CHECK("100000000000000" == FormatIndivisibleAmount(100000000000000));
    BOOST_CHECK("1000000000000000" == FormatIndivisibleAmount(1000000000000000));
    BOOST_CHECK("10000000000000000" == FormatIndivisibleAmount(10000000000000000));
    BOOST_CHECK("100000000000000000" == FormatIndivisibleAmount(100000000000000000));
    BOOST_CHECK("1000000000000000000" == FormatIndivisibleAmount(1000000000000000000));

    // negative numbers
    BOOST_CHECK("-1" == FormatIndivisibleAmount(-1));
    BOOST_CHECK("-10" == FormatIndivisibleAmount(-10));
    BOOST_CHECK("-100" == FormatIndivisibleAmount(-100));
    BOOST_CHECK("-1000" == FormatIndivisibleAmount(-1000));
    BOOST_CHECK("-10000" == FormatIndivisibleAmount(-10000));
    BOOST_CHECK("-100000" == FormatIndivisibleAmount(-100000));
    BOOST_CHECK("-1000000" == FormatIndivisibleAmount(-1000000));
    BOOST_CHECK("-10000000" == FormatIndivisibleAmount(-10000000));
    BOOST_CHECK("-100000000" == FormatIndivisibleAmount(-100000000));
    BOOST_CHECK("-1000000000" == FormatIndivisibleAmount(-1000000000));
    BOOST_CHECK("-10000000000" == FormatIndivisibleAmount(-10000000000));
    BOOST_CHECK("-100000000000" == FormatIndivisibleAmount(-100000000000));
    BOOST_CHECK("-1000000000000" == FormatIndivisibleAmount(-1000000000000));
    BOOST_CHECK("-10000000000000" == FormatIndivisibleAmount(-10000000000000));
    BOOST_CHECK("-100000000000000" == FormatIndivisibleAmount(-100000000000000));
    BOOST_CHECK("-1000000000000000" == FormatIndivisibleAmount(-1000000000000000));
    BOOST_CHECK("-10000000000000000" == FormatIndivisibleAmount(-10000000000000000));
    BOOST_CHECK("-100000000000000000" == FormatIndivisibleAmount(-100000000000000000));
    BOOST_CHECK("-1000000000000000000" == FormatIndivisibleAmount(-1000000000000000000));
}

BOOST_AUTO_TEST_CASE(mastercore_format_divisible_signed)
{
    // positive numbers
    BOOST_CHECK("+0.00000000" == FormatDivisibleAmount(0, true));
    BOOST_CHECK("+0.00000001" == FormatDivisibleAmount(1, true));
    BOOST_CHECK("+0.00000010" == FormatDivisibleAmount(10, true));
    BOOST_CHECK("+0.00000100" == FormatDivisibleAmount(100, true));
    BOOST_CHECK("+0.00001000" == FormatDivisibleAmount(1000, true));
    BOOST_CHECK("+0.00010000" == FormatDivisibleAmount(10000, true));
    BOOST_CHECK("+0.00100000" == FormatDivisibleAmount(100000, true));
    BOOST_CHECK("+0.01000000" == FormatDivisibleAmount(1000000, true));
    BOOST_CHECK("+0.10000000" == FormatDivisibleAmount(10000000, true));
    BOOST_CHECK("+1.00000000" == FormatDivisibleAmount(100000000, true));
    BOOST_CHECK("+10.00000000" == FormatDivisibleAmount(1000000000, true));
    BOOST_CHECK("+100.00000000" == FormatDivisibleAmount(10000000000, true));
    BOOST_CHECK("+1000.00000000" == FormatDivisibleAmount(100000000000, true));
    BOOST_CHECK("+10000.00000000" == FormatDivisibleAmount(1000000000000, true));
    BOOST_CHECK("+100000.00000000" == FormatDivisibleAmount(10000000000000, true));
    BOOST_CHECK("+1000000.00000000" == FormatDivisibleAmount(100000000000000, true));
    BOOST_CHECK("+10000000.00000000" == FormatDivisibleAmount(1000000000000000, true));
    BOOST_CHECK("+100000000.00000000" == FormatDivisibleAmount(10000000000000000, true));
    BOOST_CHECK("+1000000000.00000000" == FormatDivisibleAmount(100000000000000000, true));
    BOOST_CHECK("+10000000000.00000000" == FormatDivisibleAmount(1000000000000000000, true));

    // negative numbers
    BOOST_CHECK("-0.00000001" == FormatDivisibleAmount(-1, true));
    BOOST_CHECK("-0.00000010" == FormatDivisibleAmount(-10, true));
    BOOST_CHECK("-0.00000100" == FormatDivisibleAmount(-100, true));
    BOOST_CHECK("-0.00001000" == FormatDivisibleAmount(-1000, true));
    BOOST_CHECK("-0.00010000" == FormatDivisibleAmount(-10000, true));
    BOOST_CHECK("-0.00100000" == FormatDivisibleAmount(-100000, true));
    BOOST_CHECK("-0.01000000" == FormatDivisibleAmount(-1000000, true));
    BOOST_CHECK("-0.10000000" == FormatDivisibleAmount(-10000000, true));
    BOOST_CHECK("-1.00000000" == FormatDivisibleAmount(-100000000, true));
    BOOST_CHECK("-10.00000000" == FormatDivisibleAmount(-1000000000, true));
    BOOST_CHECK("-100.00000000" == FormatDivisibleAmount(-10000000000, true));
    BOOST_CHECK("-1000.00000000" == FormatDivisibleAmount(-100000000000, true));
    BOOST_CHECK("-10000.00000000" == FormatDivisibleAmount(-1000000000000, true));
    BOOST_CHECK("-100000.00000000" == FormatDivisibleAmount(-10000000000000, true));
    BOOST_CHECK("-1000000.00000000" == FormatDivisibleAmount(-100000000000000, true));
    BOOST_CHECK("-10000000.00000000" == FormatDivisibleAmount(-1000000000000000, true));
    BOOST_CHECK("-100000000.00000000" == FormatDivisibleAmount(-10000000000000000, true));
    BOOST_CHECK("-1000000000.00000000" == FormatDivisibleAmount(-100000000000000000, true));
    BOOST_CHECK("-10000000000.00000000" == FormatDivisibleAmount(-1000000000000000000, true));
}

BOOST_AUTO_TEST_CASE(mastercore_format_divisible_unsigned)
{
    // positive numbers
    BOOST_CHECK("0.00000000" == FormatDivisibleAmount(0, false));
    BOOST_CHECK("0.00000001" == FormatDivisibleAmount(1, false));
    BOOST_CHECK("0.00000010" == FormatDivisibleAmount(10, false));
    BOOST_CHECK("0.00000100" == FormatDivisibleAmount(100, false));
    BOOST_CHECK("0.00001000" == FormatDivisibleAmount(1000, false));
    BOOST_CHECK("0.00010000" == FormatDivisibleAmount(10000, false));
    BOOST_CHECK("0.00100000" == FormatDivisibleAmount(100000, false));
    BOOST_CHECK("0.01000000" == FormatDivisibleAmount(1000000, false));
    BOOST_CHECK("0.10000000" == FormatDivisibleAmount(10000000, false));
    BOOST_CHECK("1.00000000" == FormatDivisibleAmount(100000000, false));
    BOOST_CHECK("10.00000000" == FormatDivisibleAmount(1000000000, false));
    BOOST_CHECK("100.00000000" == FormatDivisibleAmount(10000000000, false));
    BOOST_CHECK("1000.00000000" == FormatDivisibleAmount(100000000000, false));
    BOOST_CHECK("10000.00000000" == FormatDivisibleAmount(1000000000000, false));
    BOOST_CHECK("100000.00000000" == FormatDivisibleAmount(10000000000000, false));
    BOOST_CHECK("1000000.00000000" == FormatDivisibleAmount(100000000000000, false));
    BOOST_CHECK("10000000.00000000" == FormatDivisibleAmount(1000000000000000, false));
    BOOST_CHECK("100000000.00000000" == FormatDivisibleAmount(10000000000000000, false));
    BOOST_CHECK("1000000000.00000000" == FormatDivisibleAmount(100000000000000000, false));
    BOOST_CHECK("10000000000.00000000" == FormatDivisibleAmount(1000000000000000000, false));

    // negative numbers
    BOOST_CHECK("0.00000001" == FormatDivisibleAmount(-1, false));
    BOOST_CHECK("0.00000010" == FormatDivisibleAmount(-10, false));
    BOOST_CHECK("0.00000100" == FormatDivisibleAmount(-100, false));
    BOOST_CHECK("0.00001000" == FormatDivisibleAmount(-1000, false));
    BOOST_CHECK("0.00010000" == FormatDivisibleAmount(-10000, false));
    BOOST_CHECK("0.00100000" == FormatDivisibleAmount(-100000, false));
    BOOST_CHECK("0.01000000" == FormatDivisibleAmount(-1000000, false));
    BOOST_CHECK("0.10000000" == FormatDivisibleAmount(-10000000, false));
    BOOST_CHECK("1.00000000" == FormatDivisibleAmount(-100000000, false));
    BOOST_CHECK("10.00000000" == FormatDivisibleAmount(-1000000000, false));
    BOOST_CHECK("100.00000000" == FormatDivisibleAmount(-10000000000, false));
    BOOST_CHECK("1000.00000000" == FormatDivisibleAmount(-100000000000, false));
    BOOST_CHECK("10000.00000000" == FormatDivisibleAmount(-1000000000000, false));
    BOOST_CHECK("100000.00000000" == FormatDivisibleAmount(-10000000000000, false));
    BOOST_CHECK("1000000.00000000" == FormatDivisibleAmount(-100000000000000, false));
    BOOST_CHECK("10000000.00000000" == FormatDivisibleAmount(-1000000000000000, false));
    BOOST_CHECK("100000000.00000000" == FormatDivisibleAmount(-10000000000000000, false));
    BOOST_CHECK("1000000000.00000000" == FormatDivisibleAmount(-100000000000000000, false));
    BOOST_CHECK("10000000000.00000000" == FormatDivisibleAmount(-1000000000000000000, false));
}

BOOST_AUTO_TEST_CASE(mastercore_format_indivisible_limits)
{
    BOOST_CHECK("9223372036854775807" == FormatIndivisibleAmount(9223372036854775807));
    BOOST_CHECK("-9223372036854775807" == FormatIndivisibleAmount(-9223372036854775807));
}

BOOST_AUTO_TEST_CASE(mastercore_format_divisible_unsigned_limits)
{
    BOOST_CHECK("92233720368.54775807" == FormatDivisibleAmount(9223372036854775807));
    BOOST_CHECK("-92233720368.54775807" == FormatDivisibleAmount(-9223372036854775807));
}

BOOST_AUTO_TEST_CASE(mastercore_format_divisible_signed_limits)
{
    BOOST_CHECK("+92233720368.54775807" == FormatDivisibleAmount(9223372036854775807, true));
    BOOST_CHECK("-92233720368.54775807" == FormatDivisibleAmount(-9223372036854775807, true));
}

BOOST_AUTO_TEST_CASE(mastercore_format_token_amount_divisible)
{
    BOOST_CHECK("9223372036854775807" == FormatTokenAmount(9223372036854775807, false));
    BOOST_CHECK("-9223372036854775807" == FormatTokenAmount(-9223372036854775807, false));
}

BOOST_AUTO_TEST_CASE(mastercore_format_token_amount_indivisible)
{
    BOOST_CHECK("92233720368.54775807" == FormatTokenAmount(9223372036854775807, true));
    BOOST_CHECK("-92233720368.54775807" == FormatTokenAmount(-9223372036854775807, true));
}

BOOST_AUTO_TEST_CASE(mastercore_format_amounts_auto_sign)
{
    BOOST_CHECK("1" == FormatTokenAmount(1));
    BOOST_CHECK("-1" == FormatTokenAmount(-1));

    BOOST_CHECK("0.00000001" == FormatDivisibleAmount(1));
    BOOST_CHECK("-0.00000001" == FormatDivisibleAmount(-1));
}

BOOST_AUTO_TEST_SUITE_END()
