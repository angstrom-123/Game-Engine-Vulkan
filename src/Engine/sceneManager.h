#pragma once

#include "scene.h"
#include "Util/derived.h"
#include <future>
#include <unordered_map>
#include <unordered_set>

struct SceneFuture {
    Scene scene;
    std::future<void> future;
    bool fulfilled;
};

class SceneManager {
public:
    ~SceneManager();
    void Init(uint32_t maxScenes);
    void DispatchUpdates(double deltaTime);
    void DispatchEvents(Event event);
    void SwitchScene(Scene scene);
    void LoadScene(Engine *engine, Scene scene);
    void UnloadScene(Scene scene);
    [[nodiscard]] SceneFuture LoadSceneAsync(Engine *engine, Scene scene);
    bool PollSceneLoad(SceneFuture& sceneFuture);
    void AwaitSceneLoad(SceneFuture& sceneFuture);
    Scene GetScene(const std::string& name);

    template<Derived<SceneBase> T> Scene RegisterScene(std::string name)
    {
        ASSERT(m_Scenes.size() < m_MaxScenes && "Registering too many scenes");

        Scene handle = m_FreeScenes.front();
        m_FreeScenes.pop();

        T *scene = new T();
        scene->name = name;

        m_Scenes[handle] = scene;
        m_RegisteredScenes.insert({name, handle});

        return handle;
    }

public:
    Scene activeScene;

private:
    uint32_t m_MaxScenes;
    std::unordered_set<Scene> m_LoadedScenes;
    std::unordered_map<std::string, Scene> m_RegisteredScenes;
    std::vector<SceneBase *> m_Scenes;
    std::queue<uint32_t> m_FreeScenes;
};
