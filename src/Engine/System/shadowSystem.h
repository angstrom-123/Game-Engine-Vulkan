#pragma once

#include "Component/transform.h"
#include "Component/light.h"
#include "Component/shadowcaster.h"
#include "ECS/ecs.h"
#include "ECS/system.h"

class ShadowSystem : public System {
public:
    void Update(ECS *ecs);
    Signature GetSignature(ECS *ecs) { return ecs->GetBit<Transform>() | ecs->GetBit<Light>() | ecs->GetBit<Shadowcaster>(); };
};
