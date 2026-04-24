#pragma once 

#include "System/Render/vulkanBackend.h"
#include "config.h"
#include "eventManager.h"
#include "scene.h"
#include "sceneManager.h"
#include <GLFW/glfw3.h>

class Engine {
public:
    Engine(Config& config);
    ~Engine();
    void Run(Scene startScene);
    void EventCallback(Event event);
    static void EventHook(Event event, void *data);
    void SetScene(const std::string& name);

    double GetTime() { return glfwGetTime(); }
    uint64_t GetFrameNumber() { return graphicsBackend.GetFrameNumber(); }
    glm::vec2 GetFrameMouseDelta() { return eventManager.mousePos - eventManager.mousePosLastFrame; }
    glm::ivec2 GetWindowSize() { glm::ivec2 res; glfwGetFramebufferSize(window, &res.x, &res.y); return res; }
    bool *GetKeysDown() { return eventManager.keysDown; }

public:
    struct GLFWwindow *window;
    VulkanBackend graphicsBackend;
    EventManager eventManager;
    SceneManager sceneManager;

private:
    uint64_t m_SplashScreenFrames = 120;
    Scene m_LoadingScene = INVALID_HANDLE;

private:
    void CalculateTangents(Vertex& v1, Vertex& v2, Vertex& v3);
};
