#include "config.h"

Config::Config()
{
    syncStrategy = SYNC_STRATEGY_VSYNC;
}

Config::Config(int argc, const char *argv[])
{
    Config();

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "vsync:off") == 0) syncStrategy = SYNC_STRATEGY_UNCAPPED;
    }
}
