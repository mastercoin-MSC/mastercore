#ifndef MASTERCORE_VALUES_H
#define MASTERCORE_VALUES_H

#include <stdlib.h>
#include <stdint.h>
#include <string>

#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_arithmetic.hpp>

namespace mastercore
{
const uint8_t ECOSYSTEM_MAIN = 1;                               ///< Main ecosystem value
const uint8_t ECOSYSTEM_TEST = 2;                               ///< Test ecosystem value

const uint8_t BLOCK_INTERVAL_MIN = 1;                           ///< Minimum interval measured in blocks
const uint8_t BLOCK_INTERVAL_MAX = 255;                         ///< Maximum interval measured in blocks

const uint8_t DEX_ACTION_NEW = 1;                               ///< New offer action value
const uint8_t DEX_ACTION_UPDATE = 2;                            ///< Update offer action value
const uint8_t DEX_ACTION_CANCEL = 3;                            ///< Cancel offer action value

const uint8_t MDEX_ACTION_ADD = 1;                              ///< Add offer action value
const uint8_t MDEX_ACTION_CANCEL_AT_PRICE = 2;                  ///< Cancel offers at price action value 
const uint8_t MDEX_ACTION_CANCEL_ALL_FOR_PAIR = 3;              ///< Cancel all offers of property pair action value
const uint8_t MDEX_ACTION_CANCEL_CANCEL_EVERYTHING = 4;         ///< Cancel every offer action value

const uint16_t PROPERTY_TYPE_INDIVISIBLE = 1;                   ///< Indivisible property type
const uint16_t PROPERTY_TYPE_DIVISIBLE = 2;                     ///< Divisible property type
const uint16_t PROPERTY_TYPE_INDIVISIBLE_REPLACING = 65;        ///< Property type for replacing indivisible properties
const uint16_t PROPERTY_TYPE_DIVISIBLE_REPLACING = 66;          ///< Property type for replacing divisible properties
const uint16_t PROPERTY_TYPE_INDIVISIBLE_APPENDING = 129;       ///< Property type for appending indivisible properties
const uint16_t PROPERTY_TYPE_DIVISIBLE_APPENDING = 130;         ///< Property type for appending divisible properties

const uint32_t PROPERTY_ID_MIN = 1;                             ///< Minimum property identifier value
const uint32_t PROPERTY_ID_MAX = 4294967295;                    ///< Maximum property identifier value

const uint64_t AMOUNT_INDIVISIBLE_MIN = 1;                      ///< Minimum indivisible amount
const uint64_t AMOUNT_INDIVISIBLE_MAX = 9223372036854775807;    ///< Maximum indivisible amount

const long double AMOUNT_DIVISIBLE_MIN = 0.00000001L;           ///< Minimum divisible amount
const long double AMOUNT_DIVISIBLE_MAX = 92233720368.54775807L; ///< Maximum divisible amount

///
/// Checks whether the value describes an ecosystem.
///
///@{
template<class T>
inline bool IsEcosystem(T value, typename boost::enable_if<boost::is_arithmetic<T> >::type* dummy = 0)
{
    uint8_t n = static_cast<uint8_t>(value);
    return (ECOSYSTEM_MAIN == n ||
            ECOSYSTEM_TEST == n);
}
inline bool IsEcosystem(const char* pzn)                  { return IsEcosystem(atoi(pzn)); }
inline bool IsEcosystem(const std::string& str)           { return IsEcosystem(str.c_str()); }
///@}

///
/// Checks whether the value describes an interval measured in blocks.
///
///@{
template<class T>
inline bool IsBlockInterval(T value, typename boost::enable_if<boost::is_arithmetic<T> >::type* dummy = 0)
{
    uint8_t n = static_cast<uint8_t>(value);
    return (BLOCK_INTERVAL_MIN <= n &&
            BLOCK_INTERVAL_MAX >= n);
}
inline bool IsBlockInterval(const char* pzn)              { return IsBlockInterval(atoi(pzn)); }
inline bool IsBlockInterval(const std::string& str)       { return IsBlockInterval(str.c_str()); }
///@}

///
/// Checks whether the value describes an action of the distributed exchange.
///
///@{
template<class T>
inline bool IsDexAction(T value, typename boost::enable_if<boost::is_arithmetic<T> >::type* dummy = 0)
{
    switch (static_cast<uint8_t>(value)) {
        case DEX_ACTION_NEW:
        case DEX_ACTION_UPDATE:
        case DEX_ACTION_CANCEL:
            return true;
    }
    return false;
}
inline bool IsDexAction(const char* pzn)                  { return IsDexAction(atoi(pzn)); }
inline bool IsDexAction(const std::string& str)           { return IsDexAction(str.c_str()); }
///@}

///
/// Checks whether the value describes an action of the distributed token exchange.
///
///@{
template<class T>
inline bool IsMetaDexAction(T value, typename boost::enable_if<boost::is_arithmetic<T> >::type* dummy = 0)
{
    switch (static_cast<uint8_t>(value)) {
        case MDEX_ACTION_ADD:
        case MDEX_ACTION_CANCEL_AT_PRICE:
        case MDEX_ACTION_CANCEL_ALL_FOR_PAIR:
        case MDEX_ACTION_CANCEL_CANCEL_EVERYTHING:
            return true;
    }
    return false;
}
inline bool IsMetaDexAction(const char* pzn)              { return IsMetaDexAction(atoi(pzn)); }
inline bool IsMetaDexAction(const std::string& str)       { return IsMetaDexAction(str.c_str()); }
///@}

///
/// Checks whether the value describes a subaction.
///
///@{
template<class T>
inline bool IsSubAction(T value, typename boost::enable_if<boost::is_arithmetic<T> >::type* dummy = 0)
{
    return (IsDexAction(value) ||
            IsMetaDexAction(value));
}
inline bool IsSubAction(const char* pzn)                  { return IsSubAction(atoi(pzn)); }
inline bool IsSubAction(const std::string& str)           { return IsSubAction(str.c_str()); }
///@}

///
/// Checks whether the value describes a property type.
///
///@{
template<class T>
inline bool IsPropertyType(T value, typename boost::enable_if<boost::is_arithmetic<T> >::type* dummy = 0)
{
    switch (static_cast<uint8_t>(value)) {
        case PROPERTY_TYPE_INDIVISIBLE:
        case PROPERTY_TYPE_DIVISIBLE:
        case PROPERTY_TYPE_INDIVISIBLE_REPLACING:
        case PROPERTY_TYPE_DIVISIBLE_REPLACING:
        case PROPERTY_TYPE_INDIVISIBLE_APPENDING:
        case PROPERTY_TYPE_DIVISIBLE_APPENDING:
            return true;
    }
    return false;
}
inline bool IsPropertyType(const char* pzn)               { return IsPropertyType(atoi(pzn)); }
inline bool IsPropertyType(const std::string& str)        { return IsPropertyType(str.c_str()); }
///@}

///
/// Checks whether the value describes a property identifier.
///
///@{
template<class T>
inline bool IsPropertyId(T value, typename boost::enable_if<boost::is_arithmetic<T> >::type* dummy = 0)
{
    uint32_t n = static_cast<uint32_t>(value);
    return (PROPERTY_ID_MIN <= n &&
            PROPERTY_ID_MAX >= n);
}
inline bool IsPropertyId(const char* pzn)                 { return IsPropertyId(atoi(pzn)); }
inline bool IsPropertyId(const std::string& str)          { return IsPropertyId(str.c_str()); }
///@}

///
/// Checks whether the value describes a divisible amount.
///
///@{
template<class T>
inline bool IsDivisibleAmount(T value, typename boost::enable_if<boost::is_arithmetic<T> >::type* dummy = 0)
{
    long double n = static_cast<long double>(value);
    return (AMOUNT_DIVISIBLE_MIN <= n &&
            AMOUNT_DIVISIBLE_MAX >= n);
}
inline bool IsDivisibleAmount(const char* pzn)            { return IsDivisibleAmount(strtold(pzn, NULL)); }
inline bool IsDivisibleAmount(const std::string& str)     { return IsDivisibleAmount(str.c_str()); }
///@}

///
/// Checks whether the value describes an indivisible amount.
///
///@{
template<class T>
inline bool IsIndivisibleAmount(T value, typename boost::enable_if<boost::is_arithmetic<T> >::type* dummy = 0)
{
    uint64_t n = static_cast<uint64_t>(value);
    return (AMOUNT_INDIVISIBLE_MIN <= n &&
            AMOUNT_INDIVISIBLE_MAX >= n);
}
inline bool IsIndivisibleAmount(const char* pzn)          { return IsIndivisibleAmount(strtoull(pzn, NULL, 10)); }
inline bool IsIndivisibleAmount(const std::string& str)   { return IsIndivisibleAmount(str.c_str()); }
///@}

///
/// Checks whether the value describes either a divisible or indivisible amount.
///
///@{
template<class T>
inline bool IsTokenAmount(T value, typename boost::enable_if<boost::is_arithmetic<T> >::type* dummy = 0)
{
    return (IsDivisibleAmount(value) ||
            IsIndivisibleAmount(value));
}
inline bool IsTokenAmount(const char* pzn)                { return IsTokenAmount(strtold(pzn, NULL)); }
inline bool IsTokenAmount(const std::string& str)         { return IsTokenAmount(str.c_str()); }
///@}
} // namespace mastercore

#endif // MASTERCORE_VALUES_H
