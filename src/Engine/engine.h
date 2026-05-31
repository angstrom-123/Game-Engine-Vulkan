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
    Engine();
    ~Engine();
    void Run();
    void EventCallback(Event event);
    static void EventHook(Event event, void *data);
    void SetScene(const std::string& name, bool showLoadingScene);
    template<Derived<SceneBase> T> void RegisterScene(const fs::path& path)
    {
        m_SceneManager.RegisterScene<T>(path);
    }
    double GetTime();
    uint64_t GetFrameNumber();
    glm::vec2 GetFrameMouseDelta();
    glm::ivec2 GetWindowSize();
    bool *GetKeysDown();

private:
    Config m_Config;
    struct GLFWwindow *m_Window{nullptr};
    VulkanBackend m_GraphicsBackend{};
    EventManager m_EventManager{};
    SceneManager m_SceneManager{};
    ResourceManager m_ResourceManager{};
};
