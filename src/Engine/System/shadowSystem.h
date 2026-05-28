#pragma once

#include "Component/transform.h"
#include "Component/light.h"
#include "Component/shadowcaster.h"
#include "ECS/ecs.h"
#include "ECS/system.h"

class ShadowSystem : public System {
public:
    void Update(ECS *ecs, Entity camera);
    Signature GetSignature(ECS *ecs) override { return ecs->GetBit<Transform>() | ecs->GetBit<Light>() | ecs->GetBit<Shadowcaster>(); };

private:
    float m_ShadowDistance{0.0};
};
