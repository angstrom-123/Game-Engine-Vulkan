#pragma once

#include "Engine/engine.h"
#include "System/defaultCameraSystem.h"

class Sandbox: public App {
public: 
    void Init() override;
    void Frame(double deltaTime) override;
    void EventCallback(Event event) override;
    void Cleanup() override;

private:
    Entity m_Suzanne1;
    Entity m_Suzanne2;
    Entity m_Suzanne3;
    double m_LastTime;
    size_t m_LastFrame;
    DefaultCameraSystem *m_CameraSystem;
};
