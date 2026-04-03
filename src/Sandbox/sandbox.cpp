#include "sandbox.h"

#include <Engine/event.h>
#include <Engine/Util/logger.h>
#include <Engine/Component/transform.h>
#include <Engine/Component/material.h>

#include <glm/vec3.hpp>

void Sandbox::Init()
{
    m_LastFrame = 0;
    m_LastTime = m_Engine->GetTime();

    // For convenience
    ECS& ecs = m_Engine->GetECS();

    // Create camera control system (Camera is a default Entity in the engine) 
    // DefaultCameraSystem is built in to the engine, but must be enabled here
    // explicitly (in case you wish to use your own)
    m_CameraSystem = ecs.RegisterSystem<DefaultCameraSystem>();
    ecs.SetSystemSignature<DefaultCameraSystem>(m_CameraSystem->GetSignature(ecs));
    m_CameraSystem->Init();

    m_Parent = ecs.CreateEntity();
    ecs.GetComponent<Transform>(m_Parent).Scale(0.015)
                                         .Translate(0.0, -2.0, 0.0)
                                         .Rotate(glm::radians(90.0), Y_AXIS);

    // Load sponza mesh (made of several objects with different textures)
    // Sponza is a VERY heavy file, as it contains 20+ large image textures, which each need to 
    // be loaded individually, and applied to separate meshes (Could optimize with array textures)
    m_Engine->CreateMesh("src/Sandbox/Resource/Model/sponza.obj", "src/Sandbox/Resource/Model/sponza.mtl", m_SponzaParts);

    // Parent the sponza mesh to an entity to control its transform.
    // This is useful for meshes loaded from a file containing several objects / groups so they 
    // can all be transformed at once.
    for (Entity e : m_SponzaParts) {
        ecs.GetComponent<Transform>(e).InheritFrom(m_Parent);
    }

    // Load a monkey head mesh.
    // API demands a vector for the entities, but I know that this file contains only one object.
    std::vector<Entity> tmp;
    m_Engine->CreateMesh("src/Sandbox/Resource/Model/suzanne.obj", "src/Sandbox/Resource/Model/suzanne.mtl", tmp);
    m_Monkey = tmp.front();
    ecs.GetComponent<Transform>(m_Monkey).Translate(0.0, 0.0, -10.0);
}

void Sandbox::Frame(double deltaTime)
{
    // FPS Counter
    double currTime = m_Engine->GetTime();
    if (currTime - m_LastTime >= 1.0) {
        INFO(static_cast<float>(m_Engine->GetFrameNumber() - m_LastFrame) << "fps");

        m_LastTime = currTime;
        m_LastFrame = m_Engine->GetFrameNumber();
    }

    ECS& ecs = m_Engine->GetECS();

    // Spin the monkey!
    ecs.GetComponent<Transform>(m_Monkey).Rotate(glm::radians(90.0 * deltaTime), Y_AXIS);

    // Control the camera
    m_CameraSystem->Update(ecs, m_Engine->GetKeysDown(), m_Engine->GetFrameMouseDelta(), deltaTime);

    // The rendering system is handled by the engine, the scene is automatically 
    // drawn after this frame function is called.
}

void Sandbox::EventCallback(Event event)
{
    // Fundamental events are handled by the engine and then forwarded here.
    switch (event.kind) {
        case EVENT_WINDOW_RESIZE:
            INFO("Resized to " << event.windowWidth << "x" << event.windowHeight);
            break;
        case EVENT_MOUSE_PRESS:
            INFO("Mouse pressed (button " << (int) event.mouseButton << ")");
            break;
        default:
            break;
    };
}

void Sandbox::Cleanup()
{
    // Nothing to clean up for now
}
