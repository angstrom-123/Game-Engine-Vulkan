#pragma once

#include <Engine/engine.h>
#include <Engine/System/defaultCameraSystem.h>

class Sandbox: public App {
public: 
    void Init() override;
    void Frame(double deltaTime) override;
    void EventCallback(Event event) override;
    void Cleanup() override;

private:
    Entity m_SponzaParent;
    std::vector<Entity> m_SponzaParts;
    Entity m_LightsParent;

    double m_LastTime;
    size_t m_LastFrame;
    DefaultCameraSystem *m_CameraSystem;
};
