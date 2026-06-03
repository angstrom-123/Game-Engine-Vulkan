#include "loadingScene.h"
#include <tuple>

void LoadingScene::OnInit(const SceneConfig& config)
{
    m_Parent = core.ecs->CreateEntity();

    std::vector<Entity> ignore;
    std::tie(m_BackgroundParent, ignore) = core.renderSystem->GetModel(core.ecs, "Resource/loadingBackground.obj");
    std::tie(m_SpinnerParent, ignore) = core.renderSystem->GetModel(core.ecs, "Resource/loadingSpinner.obj");

    core.ecs->GetComponent<Camera>(core.camera).scale = 0.005;

    core.ecs->GetComponent<Transform>(m_SpinnerParent).InheritFrom(m_Parent).Scale(1.0);
    core.ecs->GetComponent<Transform>(m_BackgroundParent).InheritFrom(m_Parent).Scale(3.0).Translate(0.0, 0.0, 2.0);
    core.ecs->GetComponent<Transform>(m_Parent).Translate(0.0, 0.0, -5.0).Rotate(glm::radians(180.0), Y_AXIS);
}

void LoadingScene::OnUpdate(double deltaTime)
{
    m_ElapsedTime += deltaTime;
    float rotate = 3.0 * m_ElapsedTime;
    rotate = std::fmod(rotate, 2.0 * glm::pi<float>());
    core.ecs->GetComponent<Transform>(m_SpinnerParent).Rotate(rotate, Z_AXIS);
    core.renderSystem->Update(core.ecs, core.graphicsBackend);
}

void LoadingScene::OnSelect()
{
    INFO("Selected Loading Scene");
}

void LoadingScene::OnEvent(Event event)
{
}

void LoadingScene::OnCleanup()
{
}
