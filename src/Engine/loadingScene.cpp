#include "loadingScene.h"

#include "Component/camera.h"
#include "engine.h"

void LoadingScene::OnInit(SceneConfig& config)
{
    m_RenderSystem = ecs->RegisterSystem<RenderSystem>();
    ecs->SetSystemSignature<RenderSystem>(m_RenderSystem->GetSignature(ecs));
    TextureArraySizes arraySizes = {
        .color1024 = 2
    };
    m_RenderSystem->Init(ecs, arraySizes, true, engine->graphicsBackend);

    Entity cameraEntity = ecs->CreateEntity();
    Camera camera = Camera(CAMERA_ORTHOGRAPHIC, glm::vec3(0.0), config.windowSize, 0.001, 0.01, 100.0);
    ecs->AddComponent<Camera>(cameraEntity, camera);
    m_RenderSystem->SetCamera(cameraEntity);

    m_Parent = ecs->CreateEntity();
    m_BackgroundParent = ecs->CreateEntity();
    m_SpinnerParent = ecs->CreateEntity();
    ecs->GetComponent<Transform>(m_SpinnerParent).Scale(0.4).InheritFrom(m_Parent);
    ecs->GetComponent<Transform>(m_BackgroundParent).InheritFrom(m_Parent);

    std::vector<Entity> backgroundEntities;
    m_RenderSystem->CreateMesh(ecs, "src/Engine/Resource/Model/loadingBackground.obj", "src/Engine/Resource/Model/loadingBackground.mtl", backgroundEntities, engine->graphicsBackend);
    for (Entity e : backgroundEntities) {
        ecs->GetComponent<Transform>(e).InheritFrom(m_BackgroundParent);
    }

    std::vector<Entity> spinnerEntities;
    m_RenderSystem->CreateMesh(ecs, "src/Engine/Resource/Model/loadingSpinner.obj", "src/Engine/Resource/Model/loadingSpinner.mtl", spinnerEntities, engine->graphicsBackend);
    for (Entity e : spinnerEntities) {
        ecs->GetComponent<Transform>(e).InheritFrom(m_SpinnerParent);
    }

    ecs->GetComponent<Transform>(m_Parent).Translate(0.0, 0.0, -5.0).Rotate(glm::radians(180.0), Y_AXIS);
}

void LoadingScene::OnUpdate(double deltaTime)
{
    float rotate = 360.0 * deltaTime;
    m_CumulativeRotation = m_CumulativeRotation + rotate;
    if (m_CumulativeRotation >= 360.0) {
        m_CumulativeRotation -= 360.0;
    }

    ecs->GetComponent<Transform>(m_SpinnerParent).Rotate(glm::radians(m_CumulativeRotation), Z_AXIS);
    m_RenderSystem->Update(ecs, engine->graphicsBackend);
}

void LoadingScene::OnSelect()
{
    INFO("Selected Loading Scene");
    m_RenderSystem->RequestResize(engine->graphicsBackend);
    m_RenderSystem->Update(ecs, engine->graphicsBackend);
}

void LoadingScene::OnEvent(Event event)
{
    if (event.kind == EVENT_WINDOW_RESIZE) {
        m_RenderSystem->RequestResize(engine->graphicsBackend);
        m_RenderSystem->Update(ecs, engine->graphicsBackend);
    }
}

void LoadingScene::OnCleanup()
{

}
