#pragma once

#include "ecsTypes.h"
#include <set>

class System {
public:
    virtual ~System() = default;
    virtual Signature GetSignature(class ECS *ecs) = 0;

public:
    Signature signature = 0;
    std::set<Entity> entities;
};
