#include "sceneManager.h"
#include "Util/myAssert.h"
#include "engine.h"
#include "event.h"
#include "scene.h"
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

void SceneManager::Init()
{
    for (const auto& entry : fs::directory_iterator("src/Application")) {
        m_MaxScenes++;
    }
    for (const auto& entry : fs::directory_iterator("src/Engine/Scenes")) {
        m_MaxScenes++;
    }

    m_Scenes.reserve(m_MaxScenes);
    m_ActiveScene = INVALID_HANDLE;
    for (uint32_t i = 0; i < m_MaxScenes; i++) {
        m_FreeScenes.push(i);
    }
}

void SceneManager::DispatchEvents(Event event)
{
    if (m_ActiveScene != INVALID_HANDLE) {
        SceneBase *sceneBase = m_Scenes[m_ActiveScene];
        switch (event.kind) {
            case EVENT_WINDOW_RESIZE:
                sceneBase->core.renderSystem->RequestResize(sceneBase->core.graphicsBackend);
                sceneBase->core.renderSystem->Update(sceneBase->core.ecs, sceneBase->core.graphicsBackend);
                break;
            default:
                break;
        }

        sceneBase->OnEvent(event);
    }
}

void SceneManager::SwitchScene(Engine *engine, VulkanBackend *backend, ResourceManager *manager, const std::string& name, bool showLoadingScene)
{
    ASSERT(m_RegisteredScenes.contains(name) && "Switching to unregistered scene");
    Scene scene = m_RegisteredScenes[name];

    INFO("Switching to scene: " << name);

    if (scene == m_ActiveScene) {
        WARN("Switching to currently active scene: " << name);
        return;
    }

    m_PreviousScene = m_ActiveScene;

    if (showLoadingScene) {
        ASSERT(m_LoadingScene != INVALID_HANDLE && "Loading scene must be registered");
        m_ActiveScene = m_LoadingScene;
        SceneBase *sceneBase = m_Scenes[m_LoadingScene];
        sceneBase->core.renderSystem->RequestResize(sceneBase->core.graphicsBackend);
        sceneBase->core.renderSystem->Update(sceneBase->core.ecs, sceneBase->core.graphicsBackend);
        sceneBase->OnSelect();
    }

    m_SceneFuture = LoadSceneAsync(engine, backend, manager, scene);
}

void SceneManager::LoadScene(Engine *engine, VulkanBackend *backend, ResourceManager *manager, Scene scene)
{
    ASSERT(!m_LoadedScenes.contains(scene) && "Loading scene that is already loaded");
    SceneBase *sceneBase = m_Scenes[scene];
    std::vector<Resource> resources = manager->LoadAll(sceneBase->core.path);
    const ResourceManifest& manifest = manager->GetManifest(sceneBase->core.path);
    sceneBase->PreInit(engine, backend, manager, (SceneBaseConfig) {
        .windowSize = engine->GetWindowSize(),
        .manifest = manifest
    });
    sceneBase->core.renderSystem->Init(sceneBase->core.ecs, manager->GetArraySizes(manifest), manifest.shadowsEnabled, backend);
    sceneBase->core.renderSystem->UploadResources(resources, sceneBase->core.path, manager, backend);
    sceneBase->Init((SceneConfig) {
        .windowSize = engine->GetWindowSize()
    });
    m_LoadedScenes.insert(scene);
}

SceneFuture SceneManager::LoadSceneAsync(Engine *engine, VulkanBackend *backend, ResourceManager *manager, Scene scene)
{
    ASSERT(!m_LoadedScenes.contains(scene) && "Loading scene that is already loaded");
    INFO("Async load started");
    SceneBase *sceneBase = m_Scenes[scene];
    return (SceneFuture) {
        .scene = scene,
        .resourceFuture = manager->LoadAllAsync(sceneBase->core.path),
        .status = SCENE_LOAD_STATUS_LOADING_RESOURCES,
    };
}

void SceneManager::Update(Engine *engine, VulkanBackend *backend, ResourceManager *manager, double deltaTime)
{
    // Make progress on loading a scene if required
    switch (m_SceneFuture.status) {
        case SCENE_LOAD_STATUS_LOADING_RESOURCES: {
            if (m_SceneFuture.resourceFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                INFO("Async resource load complete");
                SceneBase *sceneBase = m_Scenes[m_SceneFuture.scene];
                m_SceneFuture.preInitFuture = std::async(std::launch::async, &SceneBase::PreInit, sceneBase, engine, backend, manager, (SceneBaseConfig) {
                    .windowSize = engine->GetWindowSize(),
                    .manifest = manager->GetManifest(sceneBase->core.path),
                });
                m_SceneFuture.status = SCENE_LOAD_STATUS_PRE_INITING_SCENE;
            }
            break;
        }
        case SCENE_LOAD_STATUS_PRE_INITING_SCENE: {
            if (m_SceneFuture.preInitFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                INFO("Async pre init complete");
                SceneBase *sceneBase = m_Scenes[m_SceneFuture.scene];

                const ResourceManifest& manifest = manager->GetManifest(sceneBase->core.path);
                sceneBase->core.renderSystem->Init(sceneBase->core.ecs, manager->GetArraySizes(manifest), manifest.shadowsEnabled, backend);
                sceneBase->core.renderSystem->UploadResources(m_SceneFuture.resourceFuture.get(), sceneBase->core.path, manager, backend);
                sceneBase->Init((SceneConfig) {
                    .windowSize = engine->GetWindowSize()
                });
                m_LoadedScenes.insert(m_SceneFuture.scene);

                if (m_PreviousScene != m_LoadingScene && m_PreviousScene != INVALID_HANDLE) {
                    manager->UnloadAll(m_Scenes[m_PreviousScene]->core.path);
                    UnloadScene(m_PreviousScene);
                }

                m_ActiveScene = m_SceneFuture.scene;
                sceneBase->core.renderSystem->RequestResize(sceneBase->core.graphicsBackend);
                sceneBase->core.renderSystem->Update(sceneBase->core.ecs, sceneBase->core.graphicsBackend);
                sceneBase->OnSelect();
                m_SceneFuture.status = SCENE_LOAD_STATUS_DONE;
            }
            break;
        }
        case SCENE_LOAD_STATUS_DONE:
        case SCENE_LOAD_STATUS_NONE:
            // This is fine
            break;
        default:
            UNREACHABLE("Invalid scene load status");
    }

    // Send update to active scene
    if (m_ActiveScene != INVALID_HANDLE) {
        ASSERT(m_LoadedScenes.contains(m_ActiveScene) && "Active scene is not loaded");
        m_Scenes[m_ActiveScene]->OnUpdate(deltaTime);
    }
}

void SceneManager::UnloadScene(Scene scene)
{
    ASSERT(m_LoadedScenes.contains(scene) && "Unloading scene that isn't loaded");
    m_LoadedScenes.erase(scene);
    m_Scenes[scene]->Cleanup();
    m_FreeScenes.push(scene);
    if (scene == m_ActiveScene) {
        m_ActiveScene = INVALID_HANDLE;
    }
}
