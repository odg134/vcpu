#pragma once

#include <cstdint>

namespace Vcpu
{
    typedef std::uint8_t UInt8;
    typedef std::uint16_t UInt16;
    typedef std::uint32_t UInt32;
    typedef std::uint64_t UInt64;

    typedef std::int8_t Int8;
    typedef std::int16_t Int16;
    typedef std::int32_t Int32;
    typedef std::int64_t Int64;

    typedef bool Boolean;

    static const Boolean True = true;
    static const Boolean False = false;
}
