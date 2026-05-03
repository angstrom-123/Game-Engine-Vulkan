#pragma once

#include "Component/light.h"
#include "ECS/ecs.h"
#include "ECS/system.h"

class LightSystem : public System {
public:
    void Update(ECS *ecs);
    Signature GetSignature(ECS *ecs) override { return ecs->GetBit<Transform>() | ecs->GetBit<Light>(); };
};
