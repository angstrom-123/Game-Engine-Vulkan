#pragma once

#include "Component/transform.h"
#include "Component/light.h"
#include "Component/shadowcaster.h"
#include "ECS/ecs.h"
#include "ECS/system.h"

class ShadowSystem : public System {
public:
    void Update();
    Signature GetSignature() { return ECS::Get().GetBit<Transform>() | ECS::Get().GetBit<Light>() | ECS::Get().GetBit<Shadowcaster>(); };
};
