#include "mastercore_values.h"

#include <stdint.h>
#include <string>

#include <boost/test/unit_test.hpp>

using mastercore::IsEcosystem;
using mastercore::IsTokenAmount;
using mastercore::IsPropertyType;

BOOST_AUTO_TEST_SUITE(mastercore_value_tests)

BOOST_AUTO_TEST_CASE(mastercore_value_ecosystem_positive)
{
    BOOST_CHECK(IsEcosystem(1));
    BOOST_CHECK(IsEcosystem(2));
    BOOST_CHECK(IsEcosystem(1.0));
    BOOST_CHECK(IsEcosystem(2.0));
    BOOST_CHECK(IsEcosystem(1LL));
    BOOST_CHECK(IsEcosystem(2ULL));
    BOOST_CHECK(IsEcosystem(static_cast<uint8_t>(1)));
    BOOST_CHECK(IsEcosystem(static_cast<uint8_t>(2)));
    BOOST_CHECK(IsEcosystem(static_cast<int8_t>(1)));
    BOOST_CHECK(IsEcosystem(static_cast<int8_t>(2)));
    BOOST_CHECK(IsEcosystem(static_cast<uint64_t>(1)));
    BOOST_CHECK(IsEcosystem(static_cast<uint64_t>(2)));
    BOOST_CHECK(IsEcosystem(static_cast<int64_t>(1)));
    BOOST_CHECK(IsEcosystem(static_cast<int64_t>(2)));
}

BOOST_AUTO_TEST_CASE(mastercore_value_ecosystem_positive_str)
{
    BOOST_CHECK(IsEcosystem("1"));
    BOOST_CHECK(IsEcosystem("2"));
    BOOST_CHECK(IsEcosystem(std::string("1")));
    BOOST_CHECK(IsEcosystem(std::string("2")));
}

BOOST_AUTO_TEST_CASE(mastercore_value_ecosystem_negative)
{   
    BOOST_CHECK(!IsEcosystem(-256));
    BOOST_CHECK(!IsEcosystem(-1));
    BOOST_CHECK(!IsEcosystem(-0));
    BOOST_CHECK(!IsEcosystem(0));
    BOOST_CHECK(!IsEcosystem(3));
    BOOST_CHECK(!IsEcosystem(0.0));
    BOOST_CHECK(!IsEcosystem(3.0));
    BOOST_CHECK(!IsEcosystem(0LL));
    BOOST_CHECK(!IsEcosystem(3LL));
    BOOST_CHECK(!IsEcosystem(4294967294UL));
    BOOST_CHECK(!IsEcosystem(4294967295UL));
    BOOST_CHECK(!IsEcosystem(4294967296UL));
    BOOST_CHECK(!IsEcosystem(9223372036854775806LL));
    BOOST_CHECK(!IsEcosystem(9223372036854775807LL));
    BOOST_CHECK(!IsEcosystem(9223372036854775808ULL));
    BOOST_CHECK(!IsEcosystem(92233720368.54775807L));
}

BOOST_AUTO_TEST_CASE(mastercore_value_ecosystem_negative_str)
{
    BOOST_CHECK(!IsEcosystem('0'));
    BOOST_CHECK(!IsEcosystem("0"));
    BOOST_CHECK(!IsEcosystem("3"));
    BOOST_CHECK(!IsEcosystem("X"));
    BOOST_CHECK(!IsEcosystem(std::string("")));
    BOOST_CHECK(!IsEcosystem(std::string("0")));
    BOOST_CHECK(!IsEcosystem(std::string("3")));
    BOOST_CHECK(!IsEcosystem(std::string("X")));
}

BOOST_AUTO_TEST_CASE(mastercore_value_ecosystem_negative_null)
{
    BOOST_CHECK(!IsEcosystem("-0"));
}

BOOST_AUTO_TEST_CASE(mastercore_value_amount_positive)
{
    BOOST_CHECK(IsTokenAmount(1));
    BOOST_CHECK(IsTokenAmount(2147483647UL));
    BOOST_CHECK(IsTokenAmount(4294967295UL));
    BOOST_CHECK(IsTokenAmount(9223372036854775807LL));
    BOOST_CHECK(IsTokenAmount(1.0));
    BOOST_CHECK(IsTokenAmount(1.00000000L));
    BOOST_CHECK(IsTokenAmount(9223372036854775807.0L));
    
}

BOOST_AUTO_TEST_CASE(mastercore_value_amount_negative)
{
    BOOST_CHECK(!IsTokenAmount(0));
    BOOST_CHECK(!IsTokenAmount(-1));
    BOOST_CHECK(!IsTokenAmount(-9223372036854775807LL));
    BOOST_CHECK(!IsTokenAmount(18446744073709551615ULL));
    BOOST_CHECK(!IsTokenAmount(-9223372036854775807.0L));
    BOOST_CHECK(!IsTokenAmount(-9223372036854775806.99999999L));
}

BOOST_AUTO_TEST_CASE(mastercore_value_amount_negative_null)
{
    BOOST_CHECK(!IsTokenAmount("-0"));
}

BOOST_AUTO_TEST_CASE(mastercore_value_amount_positive_char)
{
    BOOST_CHECK(IsTokenAmount("1"));
    BOOST_CHECK(IsTokenAmount("2147483647"));
    BOOST_CHECK(IsTokenAmount("4294967295"));    
    BOOST_CHECK(IsTokenAmount("9223372036854775807"));
    BOOST_CHECK(IsTokenAmount("1.0"));
    BOOST_CHECK(IsTokenAmount("1.00000000"));
    BOOST_CHECK(IsTokenAmount("9223372036854775807.0"));
}

BOOST_AUTO_TEST_CASE(mastercore_value_amount_negative_char)
{
    BOOST_CHECK(!IsTokenAmount("-0"));
    BOOST_CHECK(!IsTokenAmount("X"));
    BOOST_CHECK(!IsTokenAmount("0"));
    BOOST_CHECK(!IsTokenAmount("-1"));
    BOOST_CHECK(!IsTokenAmount("-9223372036854775807"));
    BOOST_CHECK(!IsTokenAmount("18446744073709551615"));
    BOOST_CHECK(!IsTokenAmount("-9223372036854775807.0"));
    BOOST_CHECK(!IsTokenAmount("-9223372036854775806.99999999"));
}

BOOST_AUTO_TEST_CASE(mastercore_value_amount_positive_string)
{
    BOOST_CHECK(IsTokenAmount(std::string("1")));
    BOOST_CHECK(IsTokenAmount(std::string("2147483647")));
    BOOST_CHECK(IsTokenAmount(std::string("4294967295")));    
    BOOST_CHECK(IsTokenAmount(std::string("9223372036854775807")));
    BOOST_CHECK(IsTokenAmount(std::string("1.0")));
    BOOST_CHECK(IsTokenAmount(std::string("1.00000000")));
    BOOST_CHECK(IsTokenAmount(std::string("9223372036854775807.0")));
}

BOOST_AUTO_TEST_CASE(mastercore_value_amount_negative_string)
{
    BOOST_CHECK(!IsTokenAmount(std::string("-0")));
    BOOST_CHECK(!IsTokenAmount(std::string("X")));
    BOOST_CHECK(!IsTokenAmount(std::string("0")));
    BOOST_CHECK(!IsTokenAmount(std::string("-1")));
    BOOST_CHECK(!IsTokenAmount(std::string("-9223372036854775807")));
    BOOST_CHECK(!IsTokenAmount(std::string("18446744073709551615")));
    BOOST_CHECK(!IsTokenAmount(std::string("-9223372036854775807.0")));
    BOOST_CHECK(!IsTokenAmount(std::string("-9223372036854775806.99999999")));
}

BOOST_AUTO_TEST_CASE(mastercore_value_property_type)
{
    BOOST_CHECK(IsPropertyType("1"));
    BOOST_CHECK(IsPropertyType(1));
    BOOST_CHECK(IsPropertyType(1.0));
}

BOOST_AUTO_TEST_SUITE_END()
