#include "mastercore_convert.h"

#include <string>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(mastercore_strtoint64_tests)

BOOST_AUTO_TEST_CASE(mastercore_strtoint64_invidisible_positive)
{
    BOOST_CHECK(9223372036854775807 == StrToInt64("9223372036854775807", false)); // max int64
    BOOST_CHECK(4000000000000000 == StrToInt64("4000000000000000", false)); // big num
    BOOST_CHECK(0 == StrToInt64("0", false)); // zero amount
}

BOOST_AUTO_TEST_CASE(mastercore_strtoint64_invidisible_negative)
{
    BOOST_CHECK(0 == StrToInt64("9223372036854775808", false)); // over max int64
    BOOST_CHECK(0 == StrToInt64("-4", false)); // negative
}

BOOST_AUTO_TEST_CASE(mastercore_strtoint64_divisible_positive)
{
    BOOST_CHECK(0 == StrToInt64("0.000", true));
    BOOST_CHECK(9223372036854775807 == StrToInt64("92233720368.54775807", true)); // max int64
    BOOST_CHECK(9223372036854775807 == StrToInt64("92233720368.547758070", true)); // 8 digits
    BOOST_CHECK(4 == StrToInt64("0.00000004", true)); // check padding
    BOOST_CHECK(40 == StrToInt64("0.0000004", true)); // check padding
    BOOST_CHECK(40000 == StrToInt64("0.0004", true)); // check padding
    BOOST_CHECK(40000000 == StrToInt64("0.4", true)); // check padding
    BOOST_CHECK(400000000 == StrToInt64("4.0", true)); // check padding    
    BOOST_CHECK(4000000000 == StrToInt64("40.00000000000099", true)); // over 8 digits
}

BOOST_AUTO_TEST_CASE(mastercore_strtoint64_divisible_negative)
{
    BOOST_CHECK(0 == StrToInt64("92233720368.54775808", true)); // over max int64
    BOOST_CHECK(0 == StrToInt64("1234..12345678", true)); // more than one decimal in string
    BOOST_CHECK(0 == StrToInt64("1234.12345A", true)); // alpha chars in string
    BOOST_CHECK(0 == StrToInt64("-4.0", true)); // negative
}

BOOST_AUTO_TEST_CASE(mastercore_strtoint64_other_chars)
{
    BOOST_CHECK(0 == StrToInt64("8.765432101.2345687890", true));
    BOOST_CHECK(876543210 == StrToInt64("8.7654321012345687890", true));
        
    BOOST_CHECK(8 == StrToInt64("8.76543210123456878901", false));
    BOOST_CHECK(8 == StrToInt64("8.765432101.2345687890", false));
    BOOST_CHECK(2345 == StrToInt64("2345.AbCdEhf71z1.23", false));
    BOOST_CHECK(0 == StrToInt64("2345.AbCdEFG71z88-1.23", false));
}

BOOST_AUTO_TEST_SUITE_END()
