#include "mastercore_convert.h"

#include <endian.h>

#include <boost/test/unit_test.hpp>

using namespace mastercore;

BOOST_AUTO_TEST_SUITE(mastercore_swapbyteorder_tests)

BOOST_AUTO_TEST_CASE(mastercore_swapbyteorder_should_not_cycle)
{
    uint16_t a1 = 41959, a2 = a1;

    swapByteOrder16(a2);
    swapByteOrder16(a2);
    BOOST_CHECK_EQUAL(a1, a2); // Should be equal, swapping twice brings us back to the original value

    uint32_t b1 = 16909060, b2 = b1;
    swapByteOrder32(b2);
    swapByteOrder32(b2);    
    BOOST_CHECK_EQUAL(b1, b2); // Should be equal, swapping twice brings us back to the original value

    uint64_t c1 = 722385979038285, c2 = c1;
    swapByteOrder64(c2);
    swapByteOrder64(c2);
    BOOST_CHECK_EQUAL(c1, c2); // Should be equal, swapping twice brings us back to the original value
}

BOOST_AUTO_TEST_SUITE_END()