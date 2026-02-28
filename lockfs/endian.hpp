#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>
#include <utility>

namespace Serialisation
{

// Iterator so we can
//
//     out = std::ranges::copy(EndianIter{foo}, out).out;
//     out = std::ranges::copy(EndianIter{bar}, out).out;
//
// and even
//
//     in = std::ranges::copy(buf, buf + 4, EndianIter{baz}).in;
template<std::endian Endianness = std::endian::little>
struct Endian
{
    struct Stream
    {
        // Load a value by type
        template<typename T>
        constexpr T load()
        {
            T ret = Endian::load<T>(span.template first<sizeof(T)>());
            span = span.subspan(sizeof(T));
            return ret;
        }
        // Load a value into the reference (helps infer type)
        template<typename T>
        constexpr Stream & load(T & out)
        {
            out = load<T>();
            return *this;
        }
        // Store value
        template<typename T>
        constexpr Stream & store(T value)
        {
            Endian::store(span.template first<sizeof(T)>(), value);
            span = span.subspan(sizeof(T));
            return *this;
        }

        std::span<uint8_t> span;
    };

    // Simpler static load/store methods (and range helpers)
    template<typename T>
    static constexpr T load(std::span<const uint8_t, sizeof(T)> src)
    {
        return [&]<size_t... I>(std::index_sequence<I...>)
        {
            return (
                (static_cast<T>(src[I]) << shiftForByte<T>(I)) | ...
            );
        }(std::make_index_sequence<sizeof(T)>{});
    }

    template<typename T>
    static constexpr void store(std::span<uint8_t, sizeof(T)> dest, T t)
    {
        [&]<size_t... I>(std::index_sequence<I...>)
        {
            (
                (dest[I] = static_cast<uint8_t>(t >> shiftForByte<T>(I))), ...
            );
        }(std::make_index_sequence<sizeof(T)>{});
    }

    template<typename T>
    static constexpr uint8_t shiftForByte(uint8_t index)
    {
        if constexpr (Endianness == std::endian::little)
        {
            return index * 8;
        }
        else
        {
            return (sizeof(T) - 1 - index) * 8;
        }
    }
};

using EL = Endian<std::endian::little>;
using BE = Endian<std::endian::big>;

static_assert(
    []()
    {
        std::array<uint8_t, 4> data{1, 2, 3, 4};
        return EL::Stream{data}.load<uint32_t>();
    }() == 0x04030201
);
static_assert(
    []()
    {
        std::array<uint8_t, 4> ret{};
        EL::Stream{ret}.store(0x04030201);
        return ret;
    }() == std::array<uint8_t, 4>{1,2,3,4}
);

static_assert(
        []()
    {
        std::array<uint8_t, 4> data{1, 2, 3, 4};
        return BE::Stream{data}.load<uint32_t>();
    }() == 0x01020304
);
static_assert(
    []()
    {
        std::array<uint8_t, 4> ret{};
        BE::Stream{ret}.store<uint32_t>(0x01020304);
        return ret;
    }() == std::array<uint8_t, 4>{1,2,3,4}
);

// Initialise with repeating bytes
template<typename T>
constexpr T init(uint8_t byte)
{
    return [&]<size_t... I>(std::index_sequence<I...>) -> T
    {
        return (
            (static_cast<T>(byte) << (I * 8)) | ...
        );
    }(std::make_index_sequence<sizeof(T)>{});
}

};
