#include "engine.h"

#include "Scenes/LoadingScene/loadingScene.h"
#include "Util/profiler.h"
#include "scene.h"

#include <GLFW/glfw3.h>
#include <filesystem>

void GLFWErrorCb(int error, const char *desc) 
{
    (void) error;
    ERROR(desc);
}

Engine::Engine(Config& config)
{
    ASSERT(config.windowWidth > 0 && "Window width must not be 0");
    ASSERT(config.windowHeight > 0 && "Window height must not be 0");

    // Window
    glfwInit();
    glfwSetErrorCallback(GLFWErrorCb);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    m_Window = glfwCreateWindow(config.windowWidth, config.windowHeight, config.appName, nullptr, nullptr);

    m_GraphicsBackend.Init(m_Window, config);

    m_ResourceManager.Init();

    m_EventManager.Init(m_Window);
    m_EventManager.SetEventCallback(Engine::EventHook, this);

    m_SceneManager.Init();
    glfwPollEvents();
    m_SceneManager.RegisterLoadingScene<LoadingScene>(this, &m_GraphicsBackend, &m_ResourceManager, "src/Engine/Scenes/LoadingScene");
}

Engine::~Engine()
{
    m_GraphicsBackend.WaitForIdle();
    glfwDestroyWindow(m_Window);
    glfwTerminate();
}

void Engine::Run(const std::string& startSceneName)
{
    double lastTime = GetTime();

    m_SceneManager.SwitchScene(this, &m_GraphicsBackend, &m_ResourceManager, startSceneName, false);

    // Main loop
    while (!glfwWindowShouldClose(m_Window)) {
        PROFILER_PROFILE_SCOPE("Frame");

        double currTime = GetTime();
        double deltaTime = currTime - lastTime;
        lastTime = currTime;

        glfwPollEvents();
        m_SceneManager.Update(this, &m_GraphicsBackend, &m_ResourceManager, deltaTime);
        m_EventManager.Update();
    }

    INFO("Done");
}

void Engine::EventCallback(Event event)
{
    m_SceneManager.DispatchEvents(event);
}

void Engine::EventHook(Event event, void *data)
{
    Engine *engine = static_cast<Engine *>(data);
    engine->EventCallback(event);
}

void Engine::SetScene(const std::string& name, bool showLoadingScene)
{
    m_SceneManager.SwitchScene(this, &m_GraphicsBackend, &m_ResourceManager, name, showLoadingScene);
}
