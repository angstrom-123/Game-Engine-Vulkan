#include "sandbox.h"
#include "Component/camera.h"
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

// NOTE: Explicitly constructing Scene
Sandbox::Sandbox(Engine *engine, Config& config) : Scene(engine)
{
    m_LastFrame = 0;
    m_LastTime = engine->GetTime();

    // For controlling the camera
    m_CameraSystem = ecs->RegisterSystem<DefaultCameraSystem>();
    ecs->SetSystemSignature<DefaultCameraSystem>(m_CameraSystem->GetSignature(ecs));
    m_CameraSystem->Init();

    // For rendering the scene
    m_RenderSystem = ecs->RegisterSystem<RenderSystem>();
    ecs->SetSystemSignature<RenderSystem>(m_RenderSystem->GetSignature(ecs));
    m_RenderSystem->Init(ecs);

    // Create camera and assign to renderer
    m_Camera = ecs->CreateEntity();
    ecs->AddComponent<Camera>(m_Camera, Camera(glm::vec3(0.0), glm::vec2(config.windowWidth, config.windowHeight), glm::radians(60.0), 0.01, 1000.0));
    m_RenderSystem->SetCamera(m_Camera);

    // Load sponza mesh. It is in many pieces so parent them all to one entity to inherit transform
    m_SponzaParent = ecs->CreateEntity();
    ecs->GetComponent<Transform>(m_SponzaParent).Scale(0.015).Translate(0.0, -2.0, 0.0).Rotate(glm::radians(-90.0), Y_AXIS);
    engine->CreateMesh(ecs, "src/Sandbox/Resource/Model/sponza.obj", "src/Sandbox/Resource/Model/sponza.mtl", m_SponzaParts);
    for (Entity e : m_SponzaParts) {
        ecs->GetComponent<Transform>(e).InheritFrom(m_SponzaParent);
    }

    // Add a lot of random lights
    m_LightsParent = ecs->CreateEntity();
    srand(time(NULL));
    for (uint32_t i = 0; i < 128; i++) {
        Entity point;
        LightCreateInfo pointLightInfo = {
            .position = glm::vec3(Random(-15.0, 15.0), Random(-5.0, 25.0), Random(-15.0, 15.0)),
            .color = glm::vec3(Random(0.0, 1.0), Random(0.0, 1.0), Random(0.0, 1.0)),
            .intensity = Random(0.5, 1.5),
            .radius = Random(1.0, 7.0)
        };
        engine->CreatePointLight(ecs, pointLightInfo, point);
        ecs->GetComponent<Transform>(point).InheritFrom(m_LightsParent);
    }

    // Directional shadowcasting light
    Entity sun;
    LightCreateInfo sunLightInfo = {
        .position           = glm::vec3(0.0),
        .color              = glm::vec3(1.0),
        .direction          = glm::vec3(0.6, -1.0, 0.2),
        .intensity          = 2.0,
        .radius             = 60.0,
        .distance           = 40.0,
        .shadowcaster       = true,
        .shadowBias         = 0.005,
        .projectionLeft     = -40.0,
        .projectionRight    = 40.0,
        .projectionBottom   = -40.0,
        .projectionTop      = 40.0
    };
    engine->CreateDirectionalLight(ecs, sunLightInfo, sun);

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
    engine->CreateSpotLight(ecs, spotLightInfo, spot);
}

void Sandbox::Update(double deltaTime)
{
    PROFILER_PROFILE_SCOPE("Sandbox::Frame");

    // FPS Counter
    double currTime = engine->GetTime();
    if (currTime - m_LastTime >= 1.0) {
        INFO(static_cast<float>(engine->GetFrameNumber() - m_LastFrame) << "fps");

        m_LastTime = currTime;
        m_LastFrame = engine->GetFrameNumber();
    }

    // Move all the point lights around
    float offsetX = std::sin(static_cast<float>(engine->GetFrameNumber()) / 60.0) * 5.0;
    float offsetY = std::cos(static_cast<float>(engine->GetFrameNumber()) / 60.0) * 5.0;
    float offsetZ = std::cos(static_cast<float>(engine->GetFrameNumber() + 47) / 45.0) * 5.0;
    ecs->GetComponent<Transform>(m_LightsParent).Translate(offsetX, offsetY, offsetZ);

    // Update our systems
    m_CameraSystem->Update(ecs, engine->GetKeysDown(), engine->GetFrameMouseDelta(), deltaTime);
    m_RenderSystem->Update(ecs, engine->GetRenderBackend());
}

void Sandbox::EventCallback(Event event)
{
    // Some simple logging
    switch (event.kind) {
        case EVENT_WINDOW_RESIZE:
            INFO("Resized to " << event.windowWidth << "x" << event.windowHeight);
            break;
        case EVENT_MOUSE_PRESS:
            INFO("Mouse pressed (button " << static_cast<int>(event.mouseButton) << ")");
            break;
        default:
            break;
    };
}

void Sandbox::Cleanup()
{
    // Nothing to clean up for now
}
