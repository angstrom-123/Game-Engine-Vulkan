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

Engine::Engine()
{
    m_Config = Config("src/CONFIG.yaml");

    // Window
    glfwInit();
    glfwSetErrorCallback(GLFWErrorCb);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    m_Window = glfwCreateWindow(m_Config.windowWidth, m_Config.windowHeight, m_Config.appName.c_str(), nullptr, nullptr);

    m_GraphicsBackend.Init(m_Window, m_Config);

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

void Engine::Run()
{
    PROFILER_BEGIN_SESSION("Profiling_Session");

    double lastTime = GetTime();

    m_SceneManager.SwitchScene(this, &m_GraphicsBackend, &m_ResourceManager, m_Config.startScene, false);

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

    PROFILER_END_SESSION();
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

double Engine::GetTime()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() / 1000000.0;
}

uint64_t Engine::GetFrameNumber()
{
    return m_GraphicsBackend.frameNumber;
}

glm::vec2 Engine::GetFrameMouseDelta()
{
    return m_EventManager.mousePos - m_EventManager.mousePosLastFrame;
}

glm::ivec2 Engine::GetWindowSize()
{
    glm::ivec2 res;
    glfwGetFramebufferSize(m_Window, &res.x, &res.y);
    return res;
}

bool *Engine::GetKeysDown()
{
    return m_EventManager.keysDown;
}
