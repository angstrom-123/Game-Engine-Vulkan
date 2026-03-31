#pragma once

#include "ecsTypes.h"

#include <set>

class System {
public:
    Signature signature = {0};
    std::set<Entity> entities;
};
