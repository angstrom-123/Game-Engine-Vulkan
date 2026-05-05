#pragma once

#include "Component/camera.h"
#include "Component/light.h"
#include "Component/material.h"
#include "Component/mesh.h"
#include "Component/screenSpace.h"
#include "Component/shadowcaster.h"
#include "ECS/ecs.h"
#include "ResourceManager/resourceManifest.h"
#include "System/Render/renderSystem.h"
#include "event.h"

using Scene = int32_t;

class Engine;

struct SceneConfig {
    glm::ivec2 windowSize;
};

struct SceneBaseConfig {
    glm::ivec2 windowSize;
    ResourceManifest manifest;
};

class SceneBase {
public:
    virtual ~SceneBase() = default;
    SceneBase() = default;

    void PreInit(Engine *engine, VulkanBackend *backend, ResourceManager *manager, const SceneBaseConfig&& baseConfig)
    {
        core.engine = engine;
        core.graphicsBackend = backend;

        core.ecs = new ECS();
        core.ecs->RegisterComponent<Transform>();
        core.ecs->RegisterComponent<Camera>();
        core.ecs->RegisterComponent<Mesh>();
        core.ecs->RegisterComponent<Material>();
        core.ecs->RegisterComponent<Light>();
        core.ecs->RegisterComponent<Shadowcaster>();
        core.ecs->RegisterComponent<Text>();
        core.ecs->RegisterComponent<ScreenSpace>();

        core.renderSystem = core.ecs->RegisterSystem<RenderSystem>();

        core.camera = core.ecs->CreateEntity();
        if (baseConfig.manifest.perspectiveProjection) {
            core.ecs->AddComponent<Camera>(core.camera, Camera(CAMERA_PERSPECTIVE, glm::vec3(0.0), baseConfig.windowSize));
        } else {
            core.ecs->AddComponent<Camera>(core.camera, Camera(CAMERA_ORTHOGRAPHIC, glm::vec3(0.0), baseConfig.windowSize));
        }
        core.renderSystem->SetCamera(core.camera);
    }

    void Init(const SceneConfig&& config)
    {
        OnInit(config);
    }

    void Cleanup()
    {
        delete core.ecs;
        OnCleanup();
    }

    virtual void OnSelect() = 0;
    virtual void OnEvent(Event event) = 0;
    virtual void OnUpdate(double deltaTime) = 0;

public:
    struct {
        // External
        Engine                *engine           = nullptr;
        ECS                   *ecs              = nullptr;
        VulkanBackend         *graphicsBackend  = nullptr;
        // Local
        std::filesystem::path path              = "";
        RenderSystem          *renderSystem     = nullptr;
        Entity                camera            = INVALID_HANDLE;
    } core;

protected:
    virtual void OnInit(const SceneConfig& userData) = 0;
    virtual void OnCleanup() = 0;
};
