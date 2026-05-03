#pragma once 

#include "ResourceManager/resourceManager.h"
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
    void Run(const std::string& startSceneName);
    void EventCallback(Event event);
    static void EventHook(Event event, void *data);
    void SetScene(const std::string& name, bool showLoadingScene);
    template<Derived<SceneBase> T> void RegisterScene(const fs::path& path) { m_SceneManager.RegisterScene<T>(path); }

    double GetTime() { return glfwGetTime(); }
    uint64_t GetFrameNumber() { return m_GraphicsBackend.GetFrameNumber(); }
    glm::vec2 GetFrameMouseDelta() { return m_EventManager.mousePos - m_EventManager.mousePosLastFrame; }
    glm::ivec2 GetWindowSize() { glm::ivec2 res; glfwGetFramebufferSize(m_Window, &res.x, &res.y); return res; }
    bool *GetKeysDown() { return m_EventManager.keysDown; }

private:
    struct GLFWwindow *m_Window = nullptr;
    VulkanBackend m_GraphicsBackend;
    EventManager m_EventManager;
    SceneManager m_SceneManager;
    ResourceManager m_ResourceManager;

private:
    void CalculateTangents(Vertex& v1, Vertex& v2, Vertex& v3);
};
