#pragma once

#include <cstdlib>
#include <typeindex>
#include <unordered_map>

#include "system.h"
#include "Util/myAssert.h"

class SystemManager {
public:
    template<typename T> T *RegisterSystem()
    {
        std::type_index index = std::type_index(typeid(T));
        VERIFY(m_Systems.find(index) == m_Systems.end() && "Registering system twice");

        T *system = new T();
        m_Systems.insert({index, system});
        return system;
    }

    template<typename T> void SetSignature(Signature signature)
    {
        std::type_index index = std::type_index(typeid(T));
        VERIFY(m_Systems.find(index) != m_Systems.end() && "Setting signature of unregistered system");

        m_Systems[index]->signature = signature;
    }

    void EntityDestroyed(Entity entity)
    {
        for (const auto& [index, system] : m_Systems) {
            system->entities.erase(entity);
        }
    }

    void EntitySignatureChanged(Entity entity, Signature signature)
    {
        for (const auto& [index, system] : m_Systems) {
            if ((signature & system->signature) == system->signature) {
                // Entity signature matches system signature, so we add it
                system->entities.insert(entity);
            } else {
                // Entity signature does not match the system, so we remove it
                system->entities.erase(entity);
            }
        }
    }

private:
    std::unordered_map<std::type_index, System *> m_Systems;
};
