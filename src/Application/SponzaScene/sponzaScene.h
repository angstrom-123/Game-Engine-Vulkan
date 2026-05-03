#pragma once

#include "ECS/ecsTypes.h"
#include <Engine/engine.h>
#include <Engine/System/defaultCameraControlSystem.h>

class SponzaScene : public SceneBase {
public: 
    void OnInit(const SceneConfig& config) override;
    void OnUpdate(double deltaTime) override;
    void OnSelect() override;
    void OnEvent(Event event) override;
    void OnCleanup() override;

private:
    double m_LastTime = 0.0;
    uint64_t m_LastFrame = 0;

    DefaultCameraControlSystem *m_CameraController = nullptr;
    Entity m_LightsParent = INVALID_HANDLE;
};
