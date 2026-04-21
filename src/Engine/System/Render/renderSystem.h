#pragma once 

#include "ECS/system.h"
#include "System/Render/renderBackend.h"
#include "System/lightSystem.h"
#include "System/shadowSystem.h"

class RenderSystem : public System {
public:
    void Init(ECS *ecs);
    void Update(ECS *ecs, RenderBackend *const backend);
    void SetCamera(Entity camera);

    Signature GetSignature(ECS *ecs) { return ecs->GetBit<Transform>() | ecs->GetBit<Mesh>() | ecs->GetBit<Material>(); };

private:
    Entity m_Camera;

    // Subsystems
    LightSystem *m_LightSystem;
    ShadowSystem *m_ShadowSystem;
};
