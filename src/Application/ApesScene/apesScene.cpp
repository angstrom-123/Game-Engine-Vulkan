#include "apesScene.h"
#include "Component/text.h"
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

void ApesScene::OnInit(const SceneConfig& config)
{
    m_LastFrame = 0;
    m_LastTime = core.engine->GetTime();

    // For controlling the camera
    m_CameraController = core.ecs->RegisterSystem<DefaultCameraControlSystem>();
    m_CameraController->Init(core.ecs, core.camera);

    // Don't care about the individual parts of the meshes
    std::vector<Entity> ignore;
    std::tie(m_LeftApeParent, ignore) = core.renderSystem->GetModel(core.ecs, "Resource/ape.obj");
    std::tie(m_RightApeParent, ignore) = core.renderSystem->GetModel(core.ecs, "Resource/ape.obj");

    core.ecs->GetComponent<Transform>(m_LeftApeParent).Translate(-3.0, 0.0, -5.0);
    core.ecs->GetComponent<Transform>(m_RightApeParent).Translate(3.0, 0.0, -5.0);

    // Directional light
    LightCreateInfo sunLightInfo = {
        .position           = glm::vec3(0.0),
        .color              = glm::vec3(1.0),
        .direction          = glm::vec3(0.6, -1.0, 0.2),
        .intensity          = 2.0,
        .radius             = 60.0,
        .distance           = 40.0,
    };
    core.renderSystem->CreateDirectionalLight(core.ecs, sunLightInfo, core.graphicsBackend);

    // Text
    TextCreateInfo textInfo = {
        .fontSize = 68,
        .position = glm::vec3(0.0, 0.0, -5.0),
        .color = glm::vec3(1.0, 0.0, 0.0),
        .text = "Hello, World!",
        .align = TEXT_ALIGN_CENTRE
    };
    Entity text = core.renderSystem->CreateText(core.ecs, textInfo, core.graphicsBackend);
    core.ecs->GetComponent<Transform>(text).Scale(0.1);
}

void ApesScene::OnUpdate(double deltaTime)
{
    // FPS Counter
    double currTime = core.engine->GetTime();
    if (currTime - m_LastTime >= 1.0) {
        INFO(static_cast<float>(core.engine->GetFrameNumber() - m_LastFrame) << "fps");

        m_LastTime = currTime;
        m_LastFrame = core.engine->GetFrameNumber();
    }

    // Spin the apes
    core.ecs->GetComponent<Transform>(m_LeftApeParent).Rotate(glm::radians(static_cast<float>(core.engine->GetFrameNumber() % 360)), Y_AXIS);
    core.ecs->GetComponent<Transform>(m_RightApeParent).Rotate(glm::radians(static_cast<float>(core.engine->GetFrameNumber() % 360 + 180)), X_AXIS);

    // Update our systems
    m_CameraController->Update(core.ecs, core.engine->GetKeysDown(), core.engine->GetFrameMouseDelta(), deltaTime);
    core.renderSystem->Update(core.ecs, core.graphicsBackend);
}

void ApesScene::OnSelect()
{
    INFO("Selected Apes Scene");
}

void ApesScene::OnEvent(Event event)
{
    // Some simple logging
    switch (event.kind) {
        case EVENT_MOUSE_PRESS:
            INFO("Mouse pressed (button " << static_cast<int>(event.mouseButton) << ")");
            break;
        case EVENT_KEY_PRESS:
            if (event.key == GLFW_KEY_TAB) {
                INFO("Tab pressed - Switching to sponza scene");
                core.engine->SetScene("SponzaScene", true);
            }
            break;
        default:
            break;
    };
}

void ApesScene::OnCleanup()
{
}
