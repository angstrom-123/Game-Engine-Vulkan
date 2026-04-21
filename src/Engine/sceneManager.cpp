#include "sceneManager.h"

void SceneManager::Init(uint32_t maxScenes)
{
    m_MaxScenes = maxScenes;
    m_Scenes.reserve(m_MaxScenes);
    m_ActiveScene = INVALID_HANDLE;
}

void SceneManager::Cleanup()
{
    for (auto it = m_LoadedScenes.begin(); it != m_LoadedScenes.end(); it++) {
        m_Scenes[*it]->Cleanup();
        delete m_Scenes[*it];
    }
}

void SceneManager::DispatchUpdates(double deltaTime)
{
    if (m_ActiveScene != INVALID_HANDLE) {
        m_Scenes[m_ActiveScene]->OnUpdate(deltaTime);
    }
}

void SceneManager::DispatchEvents(Event event)
{
    if (m_ActiveScene != INVALID_HANDLE) {
        m_Scenes[m_ActiveScene]->OnEvent(event);
    }
}

void SceneManager::SwitchScene(Scene scene)
{
    ASSERT(m_LoadedScenes.contains(scene) && "Switching to unloaded scene");
    m_ActiveScene = scene;
}

void SceneManager::LoadScene(Engine *engine, Scene scene, void *sceneUserData)
{
    ASSERT(!m_LoadedScenes.contains(scene) && "Loading scene that is already loaded");
    m_Scenes[scene]->Init(engine, sceneUserData);
    m_LoadedScenes.insert(scene);
}

void SceneManager::UnloadScene(Scene scene)
{
    ASSERT(m_LoadedScenes.contains(scene) && "Unloading scene that isn't loaded");
    m_Scenes[scene]->Cleanup();
    m_LoadedScenes.erase(scene);
}
