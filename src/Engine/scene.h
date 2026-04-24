#pragma once

#include "Component/camera.h"
#include "Component/light.h"
#include "Component/material.h"
#include "Component/mesh.h"
#include "Component/shadowcaster.h"
#include "ECS/ecs.h"
#include "event.h"

using Scene = int32_t;

class Engine;

struct SceneConfig {
    glm::ivec2 windowSize;
};

class SceneBase {
public:
    virtual ~SceneBase() = default;
    SceneBase() = default;

    void Init(Engine *engine, glm::ivec2 windowSize)
    {
        this->engine = engine;
        this->ecs = new ECS();

        // Register default ecs components
        ecs->RegisterComponent<Transform>();
        ecs->RegisterComponent<Camera>();
        ecs->RegisterComponent<Mesh>();
        ecs->RegisterComponent<Material>();
        ecs->RegisterComponent<Light>();
        ecs->RegisterComponent<Shadowcaster>();

        SceneConfig config = {
            .windowSize = windowSize,
        };
        OnInit(config);
    }

    void Cleanup()
    {
        delete ecs;
        OnCleanup();
    }

    virtual void OnSelect() = 0;
    virtual void OnEvent(Event event) = 0;
    virtual void OnUpdate(double deltaTime) = 0;

public:
    Engine *engine = nullptr;
    std::string name = "";

protected:
    virtual void OnInit(SceneConfig& userData) = 0;
    virtual void OnCleanup() = 0;

protected:
    ECS *ecs;
};
