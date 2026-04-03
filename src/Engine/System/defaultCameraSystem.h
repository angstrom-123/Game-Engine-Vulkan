#pragma once

#include <glm/vec2.hpp>

#include "Component/camera.h"
#include "ECS/ecs.h"
#include "ECS/system.h"

class DefaultCameraSystem : public System {
public:
    void Init(float sensitivity = 0.04, float speed = 4.0);
    void Update(ECS& ecs, bool *keysDown, glm::vec2 mouseDelta, double dt);
    Signature GetSignature(ECS& ecs) { return ecs.GetBit<Transform>() | ecs.GetBit<Camera>(); };

private:
    float m_Sensitivity;
    float m_Speed;
};
