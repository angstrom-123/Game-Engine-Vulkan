#pragma once 

#include "ECS/system.h"
#include "System/Render/renderBackend.h"
#include "System/lightSystem.h"
#include "System/shadowSystem.h"

class RenderSystem : public System {
public:
    void Init();
    void Update(RenderBackend *const backend);
    void SetCamera(Entity camera);

    Signature GetSignature() { return ECS::Get().GetBit<Transform>() | ECS::Get().GetBit<Mesh>() | ECS::Get().GetBit<Material>(); };

private:
    Entity m_Camera;

    // Subsystems
    LightSystem *m_LightSystem;
    ShadowSystem *m_ShadowSystem;
};
