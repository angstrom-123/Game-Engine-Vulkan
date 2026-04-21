#include "renderSystem.h"

void RenderSystem::Init()
{
    ECS& ecs = ECS::Get();

    m_LightSystem = ecs.RegisterSystem<LightSystem>();
    m_ShadowSystem = ecs.RegisterSystem<ShadowSystem>();

    ecs.SetSystemSignature<LightSystem>(m_LightSystem->GetSignature());
    ecs.SetSystemSignature<ShadowSystem>(m_ShadowSystem->GetSignature());
}

void RenderSystem::Update(RenderBackend *const backend)
{
    m_LightSystem->Update();
    m_ShadowSystem->Update();
    backend->Draw(m_Camera, entities);
}

void RenderSystem::SetCamera(Entity camera)
{
    m_Camera = camera;
}
