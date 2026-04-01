#pragma once

#include "Engine/engine.h"

class Sandbox: public App {
public: 
    void Init() override;
    void Frame(double deltaTime) override;
    void Event() override;
    void Cleanup() override;
    void Resized(int width, int height) override;

private:
    Entity m_Suzanne1;
    Entity m_Suzanne2;
    Entity m_Suzanne3;
    double m_LastTime;
    size_t m_LastFrame;
};
