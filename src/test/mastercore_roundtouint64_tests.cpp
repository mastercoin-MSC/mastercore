#include "mastercore_convert.h"

#include <string>
#include <stdint.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(mastercore_roundtouint64_tests)

BOOST_AUTO_TEST_CASE(mastercore_roundtouint64_abs)
{
    uint64_t ul = 1UL;
    long double ld = 1.0L;    
    
    while (ul < UINT64_MAX && ld < UINT64_MAX) {
        BOOST_CHECK(ul == RoundToUInt64(ld));
        ul = ul * 2UL;
        ld = ld * 2.0L;
    }
}

BOOST_AUTO_TEST_CASE(mastercore_roundtouint64_limits)
{
    BOOST_CHECK(9223372036854775807UL == RoundToUInt64(-9223372036854775807.5L)); 
    BOOST_CHECK(9223372036854775807UL == RoundToUInt64(9223372036854775807.0L));    
    BOOST_CHECK(0UL == RoundToUInt64(0.0L));
    BOOST_CHECK(1UL == RoundToUInt64(1.49999999L));
    BOOST_CHECK(2UL == RoundToUInt64(1.50000000L));    
    BOOST_CHECK(0UL == RoundToUInt64(-0.0L));
    BOOST_CHECK(0UL == RoundToUInt64(-1.49999999L));
    BOOST_CHECK(1UL == RoundToUInt64(-1.50000000L));    
    
    // TODO: more testing. Is even -1.499 -> 0 the desired outcome?
}

BOOST_AUTO_TEST_SUITE_END()
