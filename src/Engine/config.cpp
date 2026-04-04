#include "config.h"

bool Config::VsyncEnabled(int argc, const char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "vsync:=off") == 0) return false;
    }
    return true;
}
