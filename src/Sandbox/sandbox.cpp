#include "sandbox.h"

#include <Engine/Util/logger.h>
#include <Engine/Component/transform.h>
#include <Engine/Component/material.h>
#include <glm/vec3.hpp>

void Sandbox::Init()
{
    ECS& ecs = m_Engine->GetECS();

    m_LastFrame = 0;
    m_LastTime = m_Engine->GetTime();

    // Create the materials (default shaders)
    Material defaultMaterial, checkerMaterial;
    m_Engine->CreateMaterial(defaultMaterial);
    m_Engine->CreateMaterial("src/Sandbox/Resource/Texture/checkerboard.png", checkerMaterial);

    Mesh suzanneMesh, suzanneSmoothMesh;
    m_Engine->CreateMesh("src/Sandbox/Resource/Model/suzanne.obj", suzanneMesh);
    m_Engine->CreateMesh("src/Sandbox/Resource/Model/suzanne_smooth.obj", suzanneSmoothMesh);

    m_Suzanne1 = ecs.CreateEntity();
    ecs.AddComponent<Mesh>(m_Suzanne1, suzanneMesh);
    ecs.AddComponent<Material>(m_Suzanne1, checkerMaterial);
    ecs.GetComponent<Transform>(m_Suzanne1).translation = glm::vec3(-3.0, 0.0, -5.0);

    m_Suzanne2 = ecs.CreateEntity();
    ecs.AddComponent<Mesh>(m_Suzanne2, suzanneMesh);
    ecs.AddComponent<Material>(m_Suzanne2, defaultMaterial);
    ecs.GetComponent<Transform>(m_Suzanne2).translation = glm::vec3(0.0, 0.0, -5.0);

    m_Suzanne3 = ecs.CreateEntity();
    ecs.AddComponent<Mesh>(m_Suzanne3, suzanneSmoothMesh);
    ecs.AddComponent<Material>(m_Suzanne3, checkerMaterial);
    ecs.GetComponent<Transform>(m_Suzanne3).translation = glm::vec3(3.0, 0.0, -5.0);
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

    // Spin the monkeys
    ECS& ecs = m_Engine->GetECS();
    ecs.GetComponent<Transform>(m_Suzanne1).Rotate(glm::radians(45.0f * deltaTime), glm::vec3(0.0, 1.0, 0.0));
    ecs.GetComponent<Transform>(m_Suzanne2).Rotate(glm::radians(45.0f * deltaTime), glm::vec3(0.0, 1.0, 0.0));
    ecs.GetComponent<Transform>(m_Suzanne3).Rotate(glm::radians(45.0f * deltaTime), glm::vec3(0.0, 1.0, 0.0));
}

void Sandbox::Event()
{
    INFO("Sandbox Event");
}

void Sandbox::Cleanup()
{
}

void Sandbox::Resized(int width, int height)
{
    INFO("Resized to " << width << "x" << height);
}

