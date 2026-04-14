#pragma once

#include "ecsTypes.h"

#include <set>

class System {
public:
    virtual ~System() = default;
    Signature signature = 0;
    std::set<Entity> entities;
};
