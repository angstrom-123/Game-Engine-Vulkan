#include "config.h"

bool Config::VsyncEnabled(int argc, const char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "vsync:=on") == 0) return true;
    }
    return false;
}
