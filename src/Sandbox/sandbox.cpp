#include "sandbox.h"

#include <Engine/Util/logger.h>
#include <Engine/Component/transform.h>
#include <Engine/Component/material.h>
#include <glm/vec3.hpp>

void Sandbox::Init()
{
    m_LastFrame = 0;
    m_LastTime = m_Engine->GetTime();

    // Create the monkeys
    m_Suzanne = m_Engine->CreateGameObject(glm::vec3(-2.0, 0.0, -5.0), "src/Sandbox/Resource/Model/suzanne.obj");
    m_SuzanneSmooth = m_Engine->CreateGameObject(glm::vec3(2.0, 0.0, -5.0), "src/Sandbox/Resource/Model/suzanne_smooth.obj");
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
    Transform& transform = m_Engine->GetECS().GetComponent<Transform>(m_Suzanne);
    transform.Rotate(glm::radians(45.0f * deltaTime), glm::vec3(0.0, 1.0, 0.0));

    Transform& transform2 = m_Engine->GetECS().GetComponent<Transform>(m_SuzanneSmooth);
    transform2.Rotate(glm::radians(-45.0f * deltaTime), glm::vec3(0.0, 1.0, 0.0));
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
    (void) width; (void) height;
}

