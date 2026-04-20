#pragma once

#include "Component/light.h"
#include "ECS/ecs.h"
#include "ECS/system.h"

class LightSystem : public System {
public:
    void Update();
    Signature GetSignature() { return ECS::Get().GetBit<Transform>() | ECS::Get().GetBit<Light>(); };
};
