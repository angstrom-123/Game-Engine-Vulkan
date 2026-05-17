#pragma once 

#include <cstdint>
#include <initializer_list>

static constexpr bool _FLAGS_CONTAIN_ANY(uint32_t flags, std::initializer_list<uint32_t> bits) noexcept 
{
    uint32_t combined = 0;
    for (const uint32_t bit : bits) combined |= bit;
    return (flags & combined) != 0;
}

#define FLAGS_CONTAIN_ANY(flags, ...) (_FLAGS_CONTAIN_ANY(flags, { __VA_ARGS__ }))
#define FLAGS_CONTAIN(flags, bits) (((flags) & (bits)) == (bits))
