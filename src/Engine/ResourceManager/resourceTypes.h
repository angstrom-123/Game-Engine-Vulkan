#pragma once

#include <cstdint>
#include "handle.h"

constexpr int32_t MAX_RESOURCES = 4096;
constexpr int32_t MAX_OF_EACH_RESOURCE = 256;

using Resource = int32_t;

struct DefaultTextures {
    Resource white;
    Resource gray;
    Resource black;
    Resource normal;
};

