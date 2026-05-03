#pragma once

#include <glm/vec2.hpp>

#include "Component/camera.h"
#include "ECS/ecs.h"
#include "ECS/system.h"

class DefaultCameraControlSystem : public System {
public:
    void Init(ECS *ecs, Entity camera, float sensitivity = 0.04, float speed = 4.0);
    void Update(ECS *ecs, bool *keysDown, glm::vec2 mouseDelta, double dt);
    Signature GetSignature(ECS *ecs) override { return ecs->GetBit<Transform>() | ecs->GetBit<Camera>(); };

private:
    Entity m_Camera = INVALID_HANDLE;
    float m_Sensitivity = 0.0;
    float m_Speed = 0.0;
};
