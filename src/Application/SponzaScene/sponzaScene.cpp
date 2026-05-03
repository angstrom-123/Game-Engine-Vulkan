#include "sponzaScene.h"
#include <GLFW/glfw3.h>
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

float Random(float min, float max)
{
    return min + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX) / (max - min));
}

void SponzaScene::OnInit(const SceneConfig& config)
{
    m_LastFrame = 0;
    m_LastTime = core.engine->GetTime();

    // Add a camera controller
    m_CameraController = core.ecs->RegisterSystem<DefaultCameraControlSystem>();
    m_CameraController->Init(core.ecs, core.camera);

    // Get the sponza 3d model from this scene's resources
    const auto [origin, _] = core.renderSystem->GetModel(core.ecs, "Resource/sponza.obj");
    core.ecs->GetComponent<Transform>(origin).Scale(0.015).Translate(0.0, -2.0, 0.0);

    // Add a lot of random lights
    m_LightsParent = core.ecs->CreateEntity();
    srand(time(NULL));
    for (uint32_t i = 0; i < 128; i++) {
        Entity point;
        LightCreateInfo pointLightInfo = {
            .position = glm::vec3(Random(-15.0, 15.0), Random(-5.0, 25.0), Random(-15.0, 15.0)),
            .color = glm::vec3(Random(0.0, 1.0), Random(0.0, 1.0), Random(0.0, 1.0)),
            .intensity = Random(0.5, 1.5),
            .radius = Random(1.0, 7.0)
        };
        core.renderSystem->CreatePointLight(core.ecs, pointLightInfo, point);
        core.ecs->GetComponent<Transform>(point).InheritFrom(m_LightsParent);
    }

    // Directional shadowcasting light
    Entity sun;
    LightCreateInfo sunLightInfo = {
        .position           = glm::vec3(0.0),
        .color              = glm::vec3(1.0),
        .direction          = glm::vec3(0.6, -1.0, 0.2),
        .intensity          = 2.0,
        .radius             = 50.0,
        .distance           = 40.0,
        .shadowcaster       = true,
        .shadowBias         = 0.005,
        .projectionLeft     = -40.0,
        .projectionRight    = 40.0,
        .projectionBottom   = -40.0,
        .projectionTop      = 40.0
    };
    core.renderSystem->CreateDirectionalLight(core.ecs, sunLightInfo, sun, core.graphicsBackend);

    // Spot shadowcasting light
    Entity spot;
    LightCreateInfo spotLightInfo = {
        .position           = glm::vec3(0.0, 6.0, 0.0),
        .color              = glm::vec3(1.0, 0.0, 1.0),
        .direction          = glm::vec3(-1.0, 0.0, 0.3),
        .intensity          = 1.0,
        .radius             = 20.0,
        .innerConeRadians   = glm::radians(20.0),
        .outerConeRadians   = glm::radians(45.0),
        .shadowcaster       = true,
        .shadowBias         = 0.005,
    };
    core.renderSystem->CreateSpotLight(core.ecs, spotLightInfo, spot, core.graphicsBackend);
}

void SponzaScene::OnUpdate(double deltaTime)
{
    // FPS Counter
    double currTime = core.engine->GetTime();
    if (currTime - m_LastTime >= 1.0) {
        INFO(static_cast<float>(core.engine->GetFrameNumber() - m_LastFrame) << "fps");

        m_LastTime = currTime;
        m_LastFrame = core.engine->GetFrameNumber();
    }

    // Move all the point lights around
    glm::vec3 lightOffset = glm::vec3(
        std::sin(static_cast<float>(core.engine->GetFrameNumber()) / 60.0) * 5.0,
        std::cos(static_cast<float>(core.engine->GetFrameNumber()) / 60.0) * 5.0,
        std::cos(static_cast<float>(core.engine->GetFrameNumber() + 47) / 45.0) * 5.0
    );
    core.ecs->GetComponent<Transform>(m_LightsParent).Translate(lightOffset);

    // Update our systems
    m_CameraController->Update(core.ecs, core.engine->GetKeysDown(), core.engine->GetFrameMouseDelta(), deltaTime);
    core.renderSystem->Update(core.ecs, core.graphicsBackend);
}

void SponzaScene::OnSelect()
{
    INFO("Selected Sandbox Scene");
}

void SponzaScene::OnEvent(Event event)
{
    // Some simple logging
    switch (event.kind) {
        case EVENT_MOUSE_PRESS:
            INFO("Mouse pressed (button " << static_cast<int>(event.mouseButton) << ")");
            break;
        case EVENT_KEY_PRESS:
            if (event.key == GLFW_KEY_TAB) {
                INFO("Tab pressed - Switching to apes scene");
                core.engine->SetScene("ApesScene", false);
            }
            break;
        default:
            break;
    };
}

void SponzaScene::OnCleanup()
{
}
