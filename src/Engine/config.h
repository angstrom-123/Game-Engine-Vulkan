#pragma once

#include <cstdint>
#include <cstring>
#include "Util/logger.h"

enum SyncStrategy {
    SYNC_STRATEGY_VSYNC,
    SYNC_STRATEGY_UNCAPPED,
    SYNC_STRATEGY_MAX_ENUM,
};

struct Config {
    const char *appName = "app";
    SyncStrategy syncStrategy = SYNC_STRATEGY_VSYNC;
    uint32_t windowWidth = 1600;
    uint32_t windowHeight = 900;

    Config();
    Config(int argc, const char *argv[]);
};
