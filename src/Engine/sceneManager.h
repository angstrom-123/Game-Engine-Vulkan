#pragma once

#include "ResourceManager/resourceTypes.h"
#include "handle.h"
#include "scene.h"
#include "Util/derived.h"
#include <future>
#include <unordered_map>
#include <unordered_set>

enum SceneLoadStatus {
    SCENE_LOAD_STATUS_NONE,
    SCENE_LOAD_STATUS_LOADING_RESOURCES,
    SCENE_LOAD_STATUS_PRE_INITING_SCENE,
    SCENE_LOAD_STATUS_DONE
};

struct SceneFuture {
    Scene scene = INVALID_HANDLE;
    std::future<std::vector<Resource>> resourceFuture;
    std::future<void> preInitFuture;
    SceneLoadStatus status = SCENE_LOAD_STATUS_NONE;
};

class SceneManager {
public:
    ~SceneManager();
    void Init();
    void DispatchEvents(Event event);
    void Update(Engine *engine, VulkanBackend *backend, ResourceManager *manager, double deltaTime);

    // In the future, have a flag for showing a loading indicator instead of the loading screen
    void SwitchScene(Engine *engine, VulkanBackend *backend, ResourceManager *manager, const std::string& name, bool showLoadingScene);

    template<Derived<SceneBase> T> void RegisterLoadingScene(Engine *engine, VulkanBackend *backend, ResourceManager *manager, const std::filesystem::path& path)
    {
        m_LoadingScene = RegisterScene<T>(path);
        LoadScene(engine, backend, manager, m_LoadingScene);
        m_ActiveScene = m_LoadingScene;
        m_Scenes[m_LoadingScene]->OnSelect();
    }

    template<Derived<SceneBase> T> Scene RegisterScene(const std::filesystem::path& path)
    {
        ASSERT(m_Scenes.size() < m_MaxScenes && "Registering too many scenes");

        Scene handle = m_FreeScenes.front();
        m_FreeScenes.pop();

        T *scene = new T();
        std::string name = ResourceManifest::ReadName(path / "MANIFEST.yaml");
        ASSERT(!name.empty() && "Failed to read scene name from manifest");
        INFO("Registered scene: " << name);
        scene->core.path = std::filesystem::path(path);

        m_Scenes[handle] = scene;
        m_RegisteredScenes.insert({name, handle});

        return handle;
    }

private:
    void LoadScene(Engine *engine, VulkanBackend *backend, ResourceManager *manager, Scene scene);
    void UnloadScene(Scene scene);
    [[nodiscard]] SceneFuture LoadSceneAsync(Engine *engine, VulkanBackend *backend, ResourceManager *manager, Scene scene);

private:
    Scene m_ActiveScene = INVALID_HANDLE;
    Scene m_PreviousScene = INVALID_HANDLE;
    Scene m_LoadingScene = INVALID_HANDLE;
    SceneFuture m_SceneFuture;
    uint32_t m_MaxScenes = 0;
    std::unordered_set<Scene> m_LoadedScenes;
    std::unordered_map<std::string, Scene> m_RegisteredScenes;
    std::vector<SceneBase *> m_Scenes;
    std::queue<uint32_t> m_FreeScenes;
};
