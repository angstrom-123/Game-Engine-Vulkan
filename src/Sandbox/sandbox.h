#pragma once

#include "System/Render/renderSystem.h"
#include <Engine/engine.h>
#include <Engine/System/defaultCameraSystem.h>

struct SandboxUserData {
    uint32_t windowWidth;
    uint32_t windowHeight;
};

class Sandbox: public Scene {
public: 
    Sandbox(Engine *engine, Config& config);
    void Update(double deltaTime) override;
    void EventCallback(Event event) override;
    void Cleanup() override;

private:
    Entity m_Camera;
    Entity m_SponzaParent;
    std::vector<Entity> m_SponzaParts;
    Entity m_LightsParent;

    double m_LastTime;
    size_t m_LastFrame;

    DefaultCameraSystem *m_CameraSystem;
    RenderSystem *m_RenderSystem;
};
