#include "shadowTestScene.h"
#include <cstdlib>

#include <Engine/ECS/ecsTypes.h>
#include <Engine/event.h>
#include <Engine/Util/logger.h>
#include <Engine/Util/profiler.h>
#include <Engine/Component/transform.h>
#include <Engine/Component/material.h>
#include <Engine/Component/light.h>

#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

void ShadowTestScene::OnInit(const SceneConfig& config)
{
    m_LastFrame = 0;
    m_LastTime = core.engine->GetTime();

    // For controlling the camera
    m_CameraController = core.ecs->RegisterSystem<DefaultCameraControlSystem>();
    m_CameraController->Init(core.ecs, core.camera);

    m_SceneParent = core.renderSystem->GetModel(core.ecs, "Resource/shadowTest.obj").first;
    core.ecs->GetComponent<Transform>(m_SceneParent).Scale(0.4).Translate(0.0, -4.0, 0.0);

    // Directional light
    LightCreateInfo sunLightInfo = {
        .position           = glm::vec3(0.0),
        .color              = glm::vec3(1.0),
        .direction          = glm::vec3(1.0, -1.0, 0.0),
        .intensity          = 2.0,
        .radius             = 60.0,
        .shadowcaster       = true,
    };
    core.renderSystem->CreateDirectionalLight(core.ecs, sunLightInfo, core.graphicsBackend);
}

void ShadowTestScene::OnUpdate(double deltaTime)
{
    // FPS Counter
    double currTime = core.engine->GetTime();
    if (currTime - m_LastTime >= 1.0) {
        INFO(static_cast<float>(core.engine->GetFrameNumber() - m_LastFrame) << "fps");

        m_LastTime = currTime;
        m_LastFrame = core.engine->GetFrameNumber();
    }

    // Update our systems
    m_CameraController->Update(core.ecs, core.engine->GetKeysDown(), core.engine->GetFrameMouseDelta(), deltaTime);
    core.renderSystem->Update(core.ecs, core.graphicsBackend);
}

void ShadowTestScene::OnSelect()
{
    INFO("Selected Shadow Test Scene");
}

void ShadowTestScene::OnEvent(Event event)
{
    // Some simple logging
    switch (event.kind) {
        case EVENT_MOUSE_PRESS:
            INFO("Mouse pressed (button " << static_cast<int>(event.mouseButton) << ")");
            break;
        default:
            break;
    };
}

void ShadowTestScene::OnCleanup()
{
}
