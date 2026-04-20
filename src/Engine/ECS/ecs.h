#pragma once

#include "Component/transform.h"

#include "systemManager.h"
#include "entityManager.h"
#include "componentManager.h"

class ECS {
public:
    static ECS& Get()
    {
        static ECS instance;
        return instance;
    }

    [[nodiscard]] Entity CreateEntity()
    {
        // All entities get a transform component
        Entity e = m_EntityManager.CreateEntity();
        Transform t = Transform();
        AddComponent<Transform>(e, t);
        return e;
    }

    // Note: Specifically NOT returning a reference, since we don't want the user to edit the signature directly
    Signature GetSignature(Entity entity)
    {
        return m_EntityManager.GetSignature(entity);
    }

    void DestroyEntity(Entity entity)
    {
        m_EntityManager.DestroyEntity(entity);
        m_ComponentManager.EntityDestroyed(entity);
        m_SystemManager.EntityDestroyed(entity);
    }

    template<typename T> bool HasComponent(Entity entity)
    {
        uint64_t bit = GetBit<T>();
        return (m_EntityManager.GetSignature(entity) & bit) == bit;
    }

    template<typename T> void RegisterComponent()
    {
        m_ComponentManager.RegisterComponent<T>();
    }

    template<typename T> void AddComponent(Entity entity, T component)
    {
        m_ComponentManager.AddComponent(entity, component);

        // Set the signature bit for the given component
        Signature& signature = m_EntityManager.GetSignature(entity);
        signature |= m_ComponentManager.GetBit<T>();

        m_EntityManager.SetSignature(entity, signature);
        m_SystemManager.EntitySignatureChanged(entity, signature);
    }

    template<typename T> void RemoveComponent(Entity entity)
    {
        m_ComponentManager.RemoveComponent<T>(entity);

        // Unset the signature bit for the given component
        Signature& signature = m_EntityManager.GetSignature(entity);
        signature ^= m_ComponentManager.GetBit<T>();

        m_EntityManager.SetSignature(entity, signature);
        m_SystemManager.EntitySignatureChanged(entity, signature);
    }

    template<typename T> T& GetComponent(Entity entity)
    {
        return m_ComponentManager.GetComponent<T>(entity);
    }

    template<typename T> uint64_t GetBit()
    {
        return m_ComponentManager.GetBit<T>();
    }

    template<typename T> std::pair<uint32_t, T *> GetData()
    {
        return m_ComponentManager.GetData<T>();
    }

    template<typename T> T* RegisterSystem()
    {
        return m_SystemManager.RegisterSystem<T>();
    }

    template<typename T> void SetSystemSignature(Signature signature)
    {
        m_SystemManager.SetSignature<T>(signature);
    }

private:
    ECS()
    {
        m_ComponentManager = ComponentManager();
        m_EntityManager = EntityManager();
        m_SystemManager = SystemManager();
    }
    ECS(const ECS&) = delete;
    void operator=(const ECS&) = delete;

private:
    ComponentManager m_ComponentManager;
    EntityManager m_EntityManager;
    SystemManager m_SystemManager;
};
