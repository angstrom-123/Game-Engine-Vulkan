#pragma once

#include <cstdint>
#include <cstring>
#include "Engine/Util/logger.h"

enum SyncStrategy {
    SYNC_STRATEGY_VSYNC,
    SYNC_STRATEGY_UNCAPPED,
    SYNC_STRATEGY_MAX_ENUM,
};

struct Config {
    const char *appName;
    SyncStrategy syncStrategy;
    uint32_t windowWidth;
    uint32_t windowHeight;

    Config();
    Config(int argc, const char *argv[]);
};
