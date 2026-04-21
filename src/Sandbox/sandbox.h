#pragma once

#include "System/Render/renderSystem.h"
#include <Engine/engine.h>
#include <Engine/System/defaultCameraSystem.h>

struct SandboxUserData {
    uint32_t windowWidth;
    uint32_t windowHeight;
};

class Sandbox : public SceneBase {
public: 
    void OnInit(void *userData) override;
    void OnUpdate(double deltaTime) override;
    void OnEvent(Event event) override;
    void OnCleanup() override;

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
