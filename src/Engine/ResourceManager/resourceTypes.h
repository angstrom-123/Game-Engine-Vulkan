#pragma once

#include <cstdint>
#include <string>
#include "handle.h"

const int32_t MAX_RESOURCES = 4096;
const int32_t MAX_OF_EACH_RESOURCE = 256;

using PathString = std::string;
using Resource = int32_t;

struct DefaultTextures {
    Resource white;
    Resource gray;
    Resource black;
    Resource normal;
};

struct DefaultFonts {
    Resource robotoRegular;
};
