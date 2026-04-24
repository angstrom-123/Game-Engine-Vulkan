#pragma once

#include "System/Render/renderSystem.h"
#include "event.h"
#include "scene.h"

class LoadingScene : public SceneBase {
public:
    void OnInit(SceneConfig& config) override;
    void OnUpdate(double deltaTime) override;
    void OnSelect() override;
    void OnEvent(Event event) override;
    void OnCleanup() override;

private:
    Entity m_Parent = INVALID_HANDLE;
    Entity m_BackgroundParent = INVALID_HANDLE;
    Entity m_SpinnerParent = INVALID_HANDLE;

    float m_CumulativeRotation = 0.0;

    RenderSystem *m_RenderSystem = nullptr;
};
