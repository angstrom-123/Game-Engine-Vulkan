#pragma once

#include "event.h"
#include "scene.h"

class LoadingScene : public SceneBase {
public:
    void OnInit(const SceneConfig& config) override;
    void OnUpdate(double deltaTime) override;
    void OnSelect() override;
    void OnEvent(Event event) override;
    void OnCleanup() override;

private:
    double m_ElapsedTime{0.0};
    Entity m_Parent{INVALID_HANDLE};
    Entity m_BackgroundParent{INVALID_HANDLE};
    Entity m_SpinnerParent{INVALID_HANDLE};
};
