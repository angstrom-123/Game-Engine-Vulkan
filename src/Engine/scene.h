#pragma once

#include "Component/camera.h"
#include "Component/light.h"
#include "Component/material.h"
#include "Component/mesh.h"
#include "Component/shadowcaster.h"
#include "ECS/ecs.h"
#include "event.h"

using SceneHandle = int32_t;

class Engine;

class Scene {
public:
    Scene(Engine *engine) 
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
    }

    virtual void Update(double deltaTime) = 0;
    virtual void EventCallback(Event event) = 0;
    void Destroy()
    {
        delete ecs;
        Cleanup();
    }

public:
    SceneHandle handle;
    Engine *engine;

protected:
    virtual void Cleanup() = 0;

protected:
    ECS *ecs;
};
