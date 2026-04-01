#pragma once 

#include <typeindex>
#include <unordered_map>

#include "componentArray.h"
#include "Util/myAssert.h"

class ComponentManager {
public:
    template<typename T> void RegisterComponent()
    {
        std::type_index index = std::type_index(typeid(T));
        VERIFY(m_ComponentArrays.find(index) == m_ComponentArrays.end() && "Registering component type twice");
        VERIFY(m_CurrentBit < 64 && "Registering too many components");

        m_ComponentArrays.insert({index, new ComponentArray<T>()});
        m_RegisteredTypeBits.insert({index, 1 << m_CurrentBit});
        m_CurrentBit++;
    }

    template<typename T> uint64_t GetBit()
    {
        std::type_index index = std::type_index(typeid(T));
        VERIFY(m_ComponentArrays.find(index) != m_ComponentArrays.end() && "Getting bit of unregistered component");

        return m_RegisteredTypeBits[index];
    }

    template<typename T> void AddComponent(Entity entity, T component)
    {
        std::type_index index = std::type_index(typeid(T));
        VERIFY(m_ComponentArrays.find(index) != m_ComponentArrays.end() && "Adding unregistered component");

        ComponentArray<T> *array = (ComponentArray<T> *) m_ComponentArrays[index];
        array->Insert(entity, component);
    }

    template<typename T> void RemoveComponent(Entity entity)
    {
        std::type_index index = std::type_index(typeid(T));
        VERIFY(m_ComponentArrays.find(index) != m_ComponentArrays.end() && "Removing unregistered component");

        ComponentArray<T> *array = (ComponentArray<T> *) m_ComponentArrays[index];
        array->Remove(entity);
    }

    template<typename T> T& GetComponent(Entity entity)
    {
        std::type_index index = std::type_index(typeid(T));
        VERIFY(m_ComponentArrays.find(index) != m_ComponentArrays.end() && "Getting unregistered component");

        ComponentArray<T> *array = (ComponentArray<T> *) m_ComponentArrays[index];
        return array->Get(entity);
    }

    // Notifying all component arrays that an entity is destroyed to clean up attached components
    void EntityDestroyed(Entity entity)
    {
        for (const auto& [index, componentArray] : m_ComponentArrays) {
            componentArray->EntityDestroyed(entity);
        }
    }

private:
    uint8_t m_CurrentBit = 0;
    std::unordered_map<std::type_index, ComponentArrayBase *> m_ComponentArrays;
    std::unordered_map<std::type_index, uint64_t> m_RegisteredTypeBits;
};
