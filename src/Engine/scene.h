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

class SceneBase {
public:
    SceneBase() = default;
    virtual ~SceneBase() = default;

    void Init(Engine *engine, void *userData)
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

        OnInit(userData);
    }

    void Cleanup()
    {
        delete ecs;
        OnCleanup();
    }

    virtual void OnEvent(Event event) = 0;
    virtual void OnUpdate(double deltaTime) = 0;

public:
    Engine *engine;

protected:
    virtual void OnInit(void *userData) = 0;
    virtual void OnCleanup() = 0;

protected:
    ECS *ecs;
};
