#include "engine.h"

#include "ECS/ecsTypes.h"
#include "Util/profiler.h"
#include "loadingScene.h"
#include "scene.h"

#include <GLFW/glfw3.h>

void GLFWErrorCb(int error, const char *desc) 
{
    (void) error;
    ERROR(desc);
}

Engine::Engine(Config& config)
{
    ASSERT(config.windowWidth > 0 && "Window width must not be 0");
    ASSERT(config.windowHeight > 0 && "Window height must not be 0");
    ASSERT(config.msaaSamples > 0 && "MSAA samples must not be 0");
    ASSERT(std::floor(std::log2(config.msaaSamples)) == std::log2(config.msaaSamples) && "MSAA sample count must be a power of 2");
    ASSERT(config.maxScenes > 0 && "Maximum scenes must not be 0");

    // Window
    glfwInit();
    glfwSetErrorCallback(GLFWErrorCb);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    window = glfwCreateWindow(config.windowWidth, config.windowHeight, config.appName, nullptr, nullptr);

    graphicsBackend.Init(window, config);

    eventManager.Init(window);
    eventManager.SetEventCallback(Engine::EventHook, this);

    sceneManager.Init(config.maxScenes);
    m_LoadingScene = sceneManager.RegisterScene<LoadingScene>("INTERNAL_LOADING_SCENE");
    sceneManager.LoadScene(this, m_LoadingScene);
    sceneManager.SwitchScene(m_LoadingScene);
}

Engine::~Engine()
{
    VulkanBackend::WaitForIdle(graphicsBackend.device);
    glfwDestroyWindow(window);
    glfwTerminate();
}

void Engine::Run(Scene startScene)
{
    // Mini initial loop for the splashscreen to receive resize events
    double lastTime = GetTime();
    for (uint64_t i = 0; i < m_SplashScreenFrames; i++) {
        double currTime = GetTime();
        double deltaTime = currTime - lastTime;
        lastTime = currTime;

        glfwPollEvents();

        sceneManager.DispatchUpdates(deltaTime);
        eventManager.RecordFrame();
    }

    // Load start scene
    ASSERT(startScene != INVALID_HANDLE && "Start scene is invalid");
    VulkanBackend::WaitForIdle(graphicsBackend.device);
    sceneManager.LoadScene(this, startScene);
    sceneManager.SwitchScene(startScene);

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        PROFILER_PROFILE_SCOPE("Frame");

        double currTime = GetTime();
        double deltaTime = currTime - lastTime;
        lastTime = currTime;

        glfwPollEvents();

        sceneManager.DispatchUpdates(deltaTime);
        eventManager.RecordFrame();
    }
}

void Engine::EventCallback(Event event)
{
    sceneManager.DispatchEvents(event);
}

void Engine::EventHook(Event event, void *data)
{
    Engine *engine = static_cast<Engine *>(data);
    engine->EventCallback(event);
}

void Engine::SetScene(const std::string& name)
{
    ASSERT(sceneManager.activeScene != sceneManager.GetScene(name) && "Loading scene that is already active");

    Scene oldScene = sceneManager.activeScene;
    if (oldScene != m_LoadingScene && oldScene != INVALID_HANDLE) {
        sceneManager.SwitchScene(m_LoadingScene);
    }

    VulkanBackend::WaitForIdle(graphicsBackend.device);

    if (oldScene != m_LoadingScene && oldScene != INVALID_HANDLE) {
        sceneManager.UnloadScene(oldScene);
    }

    Scene scene = sceneManager.GetScene(name);
    sceneManager.LoadScene(this, scene);
    sceneManager.SwitchScene(scene);
}
