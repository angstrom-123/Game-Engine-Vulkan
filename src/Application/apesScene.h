#pragma once

#include "ECS/ecsTypes.h"
#include "System/Render/renderSystem.h"
#include <Engine/engine.h>
#include <Engine/System/defaultCameraSystem.h>

class Apes : public SceneBase {
public: 
    void OnInit(SceneConfig& config) override;
    void OnUpdate(double deltaTime) override;
    void OnSelect() override;
    void OnEvent(Event event) override;
    void OnCleanup() override;

private:
    Entity m_Camera = INVALID_HANDLE;
    Entity m_LeftApeParent = INVALID_HANDLE;
    Entity m_RightApeParent = INVALID_HANDLE;

    double m_LastTime = 0.0;
    uint64_t m_LastFrame = 0;

    DefaultCameraSystem *m_CameraSystem = nullptr;
    RenderSystem *m_RenderSystem = nullptr;
};
