#include "sandbox.h"

#include <Engine/event.h>
#include <Engine/Util/logger.h>
#include <Engine/Util/profiler.h>
#include <Engine/Component/transform.h>
#include <Engine/Component/material.h>
#include <Engine/Component/light.h>

#include <cstdlib>
#include <glm/vec3.hpp>

Entity PointLight(glm::vec3 position, glm::vec3 color, float intensity, float radius)
{
    ECS& ecs = ECS::Get();
    Entity e = ecs.CreateEntity();

    ecs.GetComponent<Transform>(e).Translate(position);
    ecs.AddComponent(e, Light(POINT, color, intensity, radius));

    return e;
}

Entity SpotLight(glm::vec3 position, glm::vec3 color, float intensity, float radius, glm::vec3 direction, float inner, float outer)
{
    ECS& ecs = ECS::Get();
    Entity e = ecs.CreateEntity();

    direction = glm::normalize(direction);
    glm::vec3 base = glm::vec3(0.0, 0.0, 1.0);
    glm::vec3 axis = glm::cross(base, direction);
    float angle = glm::acos(glm::dot(base, direction));

    ecs.GetComponent<Transform>(e).Translate(position).Rotate(angle, axis);
    ecs.AddComponent(e, Light(SPOT, color, intensity, radius, inner, outer));

    return e;
}

Entity DirectionalLight(glm::vec3 color, float intensity, glm::vec3 direction)
{
    ECS& ecs = ECS::Get();
    Entity e = ecs.CreateEntity();

    direction = glm::normalize(direction);
    glm::vec3 base = glm::vec3(0.0, 0.0, 1.0);
    glm::vec3 axis = glm::cross(base, direction);
    float angle = glm::acos(glm::dot(base, direction));

    ecs.GetComponent<Transform>(e).Rotate(angle, axis);
    ecs.AddComponent(e, Light(DIRECTIONAL, color, intensity));

    return e;
}

float Random(float min, float max)
{
    return min + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX) / (max - min));
}

void Sandbox::Init()
{
    m_LastFrame = 0;
    m_LastTime = m_Engine->GetTime();

    // For convenience
    ECS& ecs = ECS::Get();

    // Create camera control system (Camera is a default Entity in the engine) 
    // DefaultCameraSystem is built in to the engine, but must be enabled here
    // explicitly (in case you wish to use your own)
    m_CameraSystem = ecs.RegisterSystem<DefaultCameraSystem>();
    ecs.SetSystemSignature<DefaultCameraSystem>(m_CameraSystem->GetSignature());
    m_CameraSystem->Init();

    m_SponzaParent = ecs.CreateEntity();
    ecs.GetComponent<Transform>(m_SponzaParent).Scale(0.015).Translate(0.0, -2.0, 0.0).Rotate(glm::radians(-90.0), Y_AXIS);

    // Load sponza mesh (made of several objects with different textures)
    // Sponza is a VERY heavy file, as it contains 40+ large image textures, which each need to 
    // be loaded individually, and applied to separate meshes (Could optimize with array textures)
    m_Engine->CreateMesh("src/Sandbox/Resource/Model/sponza.obj", "src/Sandbox/Resource/Model/sponza.mtl", m_SponzaParts);

    // Add a lot of random lights
    m_LightsParent = ecs.CreateEntity();

    srand(time(NULL));
    for (uint32_t i = 0; i < 256; i++) {

        glm::vec3 randomPosition = glm::vec3(Random(-15.0, 15.0), Random(-5.0, 25.0), Random(-15.0, 15.0));
        glm::vec3 randomColor = glm::vec3(Random(0.0, 1.0), Random(0.0, 1.0), Random(0.0, 1.0));
        float randomIntensity = Random(0.5, 1.5);
        float randomRadius = Random(1.0, 7.0);

        Entity light = PointLight(randomPosition, randomColor, randomIntensity, randomRadius);
        ecs.GetComponent<Transform>(light).InheritFrom(m_LightsParent);
    }

    DirectionalLight(glm::vec3(1.0), 1.0, glm::vec3(0.1, -1.0, 0.1));

    // Parent the sponza mesh to an entity to control its transform.
    // This is useful for meshes loaded from a file containing several objects / groups so they 
    // can all be transformed at once.
    for (Entity e : m_SponzaParts) {
        ecs.GetComponent<Transform>(e).InheritFrom(m_SponzaParent);
    }
}

void Sandbox::Frame(double deltaTime)
{
    PROFILER_PROFILE_SCOPE("Sandbox::Frame");

    // FPS Counter
    double currTime = m_Engine->GetTime();
    if (currTime - m_LastTime >= 1.0) {
        INFO(static_cast<float>(m_Engine->GetFrameNumber() - m_LastFrame) << "fps");

        m_LastTime = currTime;
        m_LastFrame = m_Engine->GetFrameNumber();
    }

    // For convenience
    ECS& ecs = ECS::Get();

    // Control the camera
    m_CameraSystem->Update(m_Engine->GetKeysDown(), m_Engine->GetFrameMouseDelta(), deltaTime);

    // Move all the lights around
    float factorX = std::sin(static_cast<float>(m_Engine->GetFrameNumber()) / 60.0) * 5.0;
    float factorY = std::cos(static_cast<float>(m_Engine->GetFrameNumber()) / 60.0) * 5.0;
    ecs.GetComponent<Transform>(m_LightsParent).Translate(factorX, factorY, factorY);
}

void Sandbox::EventCallback(Event event)
{
    // Fundamental events are handled by the engine and then forwarded here.
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
