#include "sandboxScene.h"
#include "Component/camera.h"
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

void Sandbox::OnInit(SceneConfig& config)
{
    m_LastFrame = 0;
    m_LastTime = engine->GetTime();

    // ==================================== Initialize Systems =====================================

    // For controlling the camera
    m_CameraSystem = ecs->RegisterSystem<DefaultCameraSystem>();
    ecs->SetSystemSignature<DefaultCameraSystem>(m_CameraSystem->GetSignature(ecs));
    m_CameraSystem->Init(ecs);

    // For rendering the scene
    m_RenderSystem = ecs->RegisterSystem<RenderSystem>();
    ecs->SetSystemSignature<RenderSystem>(m_RenderSystem->GetSignature(ecs));
    TextureArraySizes arraySizes = {
        .color1024 = 128,   // 1024*1024*128*4 = 512 MiB
        .color2048 = 32,    // 2048*2048*32*4  = 512 MiB
        .data1024 = 128,    // 1024*1024*128*4 = 512 MiB
        .data2048 = 32,     // 2048*2048*32*4  = 512 MiB
    };
    m_RenderSystem->Init(ecs, arraySizes, true, engine->graphicsBackend);

    // Create camera and assign to renderer
    m_Camera = ecs->CreateEntity();
    Camera camera = Camera(CAMERA_PERSPECTIVE, glm::vec3(0.0), config.windowSize, glm::radians(60.0), 0.01, 1000.0);
    ecs->AddComponent<Camera>(m_Camera, camera);
    m_RenderSystem->SetCamera(m_Camera);

    // ===================================== Load Sponza Mesh ======================================

    // Load sponza mesh. It is in many pieces so parent them all to one entity to inherit transform
    m_SponzaParent = ecs->CreateEntity();
    ecs->GetComponent<Transform>(m_SponzaParent).Scale(0.015).Translate(0.0, -2.0, 0.0).Rotate(glm::radians(-90.0), Y_AXIS);
    std::vector<Entity> sponzaParts;
    m_RenderSystem->CreateMesh(ecs, "src/Application/Resource/Model/sponza.obj", "src/Application/Resource/Model/sponza.mtl", sponzaParts, engine->graphicsBackend);
    for (Entity e : sponzaParts) {
        ecs->GetComponent<Transform>(e).InheritFrom(m_SponzaParent);
    }

    // ======================================= Create Lights =======================================

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
        m_RenderSystem->CreatePointLight(ecs, pointLightInfo, point);
        ecs->GetComponent<Transform>(point).InheritFrom(m_LightsParent);
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
    m_RenderSystem->CreateDirectionalLight(ecs, sunLightInfo, sun, engine->graphicsBackend);

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
    m_RenderSystem->CreateSpotLight(ecs, spotLightInfo, spot, engine->graphicsBackend);
}

void Sandbox::OnUpdate(double deltaTime)
{
    // FPS Counter
    double currTime = engine->GetTime();
    if (currTime - m_LastTime >= 1.0) {
        INFO(static_cast<float>(engine->GetFrameNumber() - m_LastFrame) << "fps");

        m_LastTime = currTime;
        m_LastFrame = engine->GetFrameNumber();
    }

    // Move all the point lights around
    glm::vec3 lightOffset = glm::vec3(
        std::sin(static_cast<float>(engine->GetFrameNumber()) / 60.0) * 5.0,
        std::cos(static_cast<float>(engine->GetFrameNumber()) / 60.0) * 5.0,
        std::cos(static_cast<float>(engine->GetFrameNumber() + 47) / 45.0) * 5.0
    );
    ecs->GetComponent<Transform>(m_LightsParent).Translate(lightOffset);

    // Update our systems
    m_CameraSystem->Update(ecs, engine->GetKeysDown(), engine->GetFrameMouseDelta(), deltaTime);
    m_RenderSystem->Update(ecs, engine->graphicsBackend);
}

void Sandbox::OnSelect()
{
    m_RenderSystem->RequestResize(engine->graphicsBackend);
    m_RenderSystem->Update(ecs, engine->graphicsBackend);
}

void Sandbox::OnEvent(Event event)
{
    // Some simple logging
    switch (event.kind) {
        case EVENT_WINDOW_RESIZE:
            m_RenderSystem->RequestResize(engine->graphicsBackend);
            m_RenderSystem->Update(ecs, engine->graphicsBackend);
            break;
        case EVENT_MOUSE_PRESS:
            INFO("Mouse pressed (button " << static_cast<int>(event.mouseButton) << ")");
            break;
        case EVENT_KEY_PRESS:
            if (event.key == GLFW_KEY_TAB) {
                INFO("Tab pressed - Switching to apes scene");
                engine->SetScene("apes");
            }
            break;
        default:
            break;
    };
}

void Sandbox::OnCleanup()
{
}
