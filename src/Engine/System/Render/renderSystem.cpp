#include "renderSystem.h"

void RenderSystem::Init(ECS *ecs)
{
    m_LightSystem = ecs->RegisterSystem<LightSystem>();
    m_ShadowSystem = ecs->RegisterSystem<ShadowSystem>();

    ecs->SetSystemSignature<LightSystem>(m_LightSystem->GetSignature(ecs));
    ecs->SetSystemSignature<ShadowSystem>(m_ShadowSystem->GetSignature(ecs));
}

void RenderSystem::Update(ECS *ecs, RenderBackend *const backend)
{
    m_LightSystem->Update(ecs);
    m_ShadowSystem->Update(ecs);
    backend->Draw(ecs, m_Camera, entities);
}

void RenderSystem::SetCamera(Entity camera)
{
    m_Camera = camera;
}
