#pragma once

#include "scene.h"
#include "Util/derived.h"

struct SceneContext {
    SceneBase *scene;
    void *userData;
};

class SceneManager {
public:
    void Init(uint32_t maxScenes);
    void Cleanup();
    void DispatchUpdates(double deltaTime);
    void DispatchEvents(Event event);
    void SwitchScene(Scene scene);
    // TODO: Async + callback for these
    void LoadScene(Engine *engine, Scene scene, void *sceneUserData);
    void UnloadScene(Scene scene);

    template<Derived<SceneBase> T> Scene RegisterScene()
    {
        ASSERT(m_Scenes.size() < m_MaxScenes && "Registering too many scenes");
        Scene handle = m_Scenes.size();
        m_Scenes.emplace_back(new T());
        return handle;
    }

private:
    uint32_t m_MaxScenes;
    Scene m_ActiveScene;
    std::set<Scene> m_LoadedScenes;
    std::vector<SceneBase *> m_Scenes;
};
