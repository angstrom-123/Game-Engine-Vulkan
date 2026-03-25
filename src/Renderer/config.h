#pragma once

enum SyncStrategy {
    SYNC_STRATEGY_VSYNC,
    SYNC_STRATEGY_UNCAPPED,
    SYNC_STRATEGY_MAX_ENUM,
};

struct Config {
    SyncStrategy syncStrategy;
};


