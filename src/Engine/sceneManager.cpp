#include "sceneManager.h"
#include "engine.h"
#include <future>

SceneManager::~SceneManager()
{
    for (auto it = m_RegisteredScenes.begin(); it != m_RegisteredScenes.end(); it++) {
        const auto& [name, scene] = *it;
        if (m_LoadedScenes.contains(scene)) {
            m_Scenes[scene]->Cleanup();
        }
        delete m_Scenes[scene];
    }
}

void SceneManager::Init(uint32_t maxScenes)
{
    m_MaxScenes = maxScenes;
    m_Scenes.reserve(m_MaxScenes);
    activeScene = INVALID_HANDLE;
    for (uint32_t i = 0; i < maxScenes; i++) {
        m_FreeScenes.push(i);
    }
}

void SceneManager::DispatchUpdates(double deltaTime)
{
    if (activeScene != INVALID_HANDLE) {
        m_Scenes[activeScene]->OnUpdate(deltaTime);
    }
}

void SceneManager::DispatchEvents(Event event)
{
    if (activeScene != INVALID_HANDLE) {
        m_Scenes[activeScene]->OnEvent(event);
    }
}

void SceneManager::SwitchScene(Scene scene)
{
    ASSERT(m_LoadedScenes.contains(scene) && "Switching to unloaded scene");
    m_Scenes[scene]->OnSelect();
    activeScene = scene;
}

void SceneManager::LoadScene(Engine *engine, Scene scene)
{
    ASSERT(!m_LoadedScenes.contains(scene) && "Loading scene that is already loaded");
    m_Scenes[scene]->Init(engine, engine->GetWindowSize());
    m_LoadedScenes.insert(scene);
}

SceneFuture SceneManager::LoadSceneAsync(Engine *engine, Scene scene)
{
    ASSERT(!m_LoadedScenes.contains(scene) && "Loading scene that is already loaded");
    std::future<void> future = std::async(std::launch::async, &SceneBase::Init, m_Scenes[scene], engine, engine->GetWindowSize());
    return (SceneFuture) {
        .scene = scene,
        .future = std::move(future),
        .fulfilled = false
    };
}

bool SceneManager::PollSceneLoad(SceneFuture& sceneFuture)
{
    if (!sceneFuture.fulfilled && sceneFuture.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        sceneFuture.fulfilled = true;
        m_LoadedScenes.insert(sceneFuture.scene);
        return true;
    }
    return false;
}

void SceneManager::AwaitSceneLoad(SceneFuture& sceneFuture)
{
    sceneFuture.future.get();
    sceneFuture.fulfilled = true;
    m_LoadedScenes.insert(sceneFuture.scene);
}

void SceneManager::UnloadScene(Scene scene)
{
    ASSERT(m_LoadedScenes.contains(scene) && "Unloading scene that isn't loaded");
    m_LoadedScenes.erase(scene);
    m_Scenes[scene]->Cleanup();
    m_FreeScenes.push(scene);
}

Scene SceneManager::GetScene(const std::string& name)
{
    ASSERT(m_RegisteredScenes.contains(name) && "Switching to unloaded scene");
    return m_RegisteredScenes[name];
}
