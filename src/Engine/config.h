#pragma once

#include <cstdint>
#include <cstring>
#include "Util/logger.h"

struct Config {
    const char *appName;
    bool vsync;
    uint32_t windowWidth;
    uint32_t windowHeight;
    uint64_t msaaSamples;

    static bool VsyncEnabled(int argc, const char *argv[]);
};
