#include "apesScene.h"
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

void Apes::OnInit(SceneConfig& config)
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
        .color1024 = 16,   // 1024*1024*16*4 = 64 MiB
        .color2048 = 4,    // 2048*2048*4*4  = 64 MiB
        .data1024 = 16,    // 1024*1024*16*4 = 64 MiB
        .data2048 = 4,     // 2048*2048*4*4  = 64 MiB
    };
    m_RenderSystem->Init(ecs, arraySizes, false, engine->graphicsBackend);

    // Create camera and assign to renderer
    m_Camera = ecs->CreateEntity();
    Camera camera = Camera(CAMERA_PERSPECTIVE, glm::vec3(0.0), config.windowSize, glm::radians(60.0), 0.01, 1000.0);
    ecs->AddComponent<Camera>(m_Camera, camera);
    m_RenderSystem->SetCamera(m_Camera);

    // ========================================= Load Apes =========================================

    // Set up transform parents
    m_LeftApeParent = ecs->CreateEntity();
    m_RightApeParent = ecs->CreateEntity();

    // Load left ape
    std::vector<Entity> leftApeParts;
    m_RenderSystem->CreateMesh(ecs, "src/Application/Resource/Model/ape.obj", "src/Application/Resource/Model/ape.mtl", leftApeParts, engine->graphicsBackend);

    std::vector<Entity> rightApeParts(leftApeParts.size());
    for (Entity e : leftApeParts) {
        Entity e2 = ecs->CreateEntity();

        // Clone components to right ape
        ecs->AddComponent<Mesh>(e2, ecs->GetComponent<Mesh>(e));
        ecs->AddComponent<Material>(e2, ecs->GetComponent<Material>(e));

        // Parent each ape to a transform parent
        ecs->GetComponent<Transform>(e).InheritFrom(m_LeftApeParent);
        ecs->GetComponent<Transform>(e2).InheritFrom(m_RightApeParent);
    }

    ecs->GetComponent<Transform>(m_LeftApeParent).Translate(-3.0, 0.0, -5.0);
    ecs->GetComponent<Transform>(m_RightApeParent).Translate(3.0, 0.0, -5.0);

    // ======================================== Create Light =======================================

    // Directional light
    Entity sun;
    LightCreateInfo sunLightInfo = {
        .position           = glm::vec3(0.0),
        .color              = glm::vec3(1.0),
        .direction          = glm::vec3(0.6, -1.0, 0.2),
        .intensity          = 2.0,
        .radius             = 60.0,
        .distance           = 40.0,
    };
    m_RenderSystem->CreateDirectionalLight(ecs, sunLightInfo, sun, engine->graphicsBackend);
}

void Apes::OnUpdate(double deltaTime)
{
    // FPS Counter
    double currTime = engine->GetTime();
    if (currTime - m_LastTime >= 1.0) {
        INFO(static_cast<float>(engine->GetFrameNumber() - m_LastFrame) << "fps");

        m_LastTime = currTime;
        m_LastFrame = engine->GetFrameNumber();
    }

    // Spin the apes
    ecs->GetComponent<Transform>(m_LeftApeParent).Rotate(glm::radians(static_cast<float>(engine->GetFrameNumber() % 360)), Y_AXIS);
    ecs->GetComponent<Transform>(m_RightApeParent).Rotate(glm::radians(static_cast<float>(engine->GetFrameNumber() % 360 + 180)), X_AXIS);

    // Update our systems
    m_CameraSystem->Update(ecs, engine->GetKeysDown(), engine->GetFrameMouseDelta(), deltaTime);
    m_RenderSystem->Update(ecs, engine->graphicsBackend);
}

void Apes::OnSelect()
{
    m_RenderSystem->RequestResize(engine->graphicsBackend);
    m_RenderSystem->Update(ecs, engine->graphicsBackend);
}

void Apes::OnEvent(Event event)
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
                INFO("Tab pressed - Switching to sandbox scene");
                engine->SetScene("sandbox");
            }
            break;
        default:
            break;
    };
}

void Apes::OnCleanup()
{
}
