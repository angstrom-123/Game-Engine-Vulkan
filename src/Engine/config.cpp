#include "config.h"

Config::Config(int argc, const char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "vsync:off") == 0) syncStrategy = SYNC_STRATEGY_UNCAPPED;
    }
}
