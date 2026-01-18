#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace Serialisation
{

template<typename T>
struct EL
{
    static constexpr T load(std::span<const uint8_t, sizeof(T)> src)
    {
        return [&]<size_t... I>(std::index_sequence<I...>)
        {
            return (
                (static_cast<T>(src[I]) << (I * 8)) | ...
            );
        }(std::make_index_sequence<sizeof(T)>{});
    }
    template<typename A>
    static constexpr T load(A && a)
    {
        return load(std::span<const uint8_t, sizeof(T)>(std::forward<A>(a), std::forward<A>(a) + sizeof(T)));
    }

    static constexpr void store(std::span<uint8_t, sizeof(T)> dest, T t)
    {
        [&]<size_t... I>(std::index_sequence<I...>)
        {
            (
                (dest[I] = static_cast<uint8_t>(t >> (I * 8))), ...
            );
        }(std::make_index_sequence<sizeof(T)>{});
    }
    template<typename A>
    static constexpr void store(A && a, T t)
    {
        store(std::span<uint8_t, sizeof(T)>(std::forward<A>(a), std::forward<A>(a) + sizeof(T)), t);
    }
};

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
