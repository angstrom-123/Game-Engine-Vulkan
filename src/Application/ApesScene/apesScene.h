#pragma once

#include "ECS/ecsTypes.h"
#include <Engine/engine.h>
#include <Engine/System/defaultCameraControlSystem.h>

class ApesScene : public SceneBase {
public: 
    void OnInit(const SceneConfig& config) override;
    void OnUpdate(double deltaTime) override;
    void OnSelect() override;
    void OnEvent(Event event) override;
    void OnCleanup() override;

private:
    Entity m_LeftApeParent = INVALID_HANDLE;
    Entity m_RightApeParent = INVALID_HANDLE;

    double m_LastTime = 0.0;
    uint64_t m_LastFrame = 0;

    DefaultCameraControlSystem *m_CameraController = nullptr;
};
