#pragma once

#include <glm/ext/vector_float3.hpp>
#include <filesystem>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

struct SubMaterialResource {
    std::string name;
    glm::vec3 ambientColor;
    glm::vec3 diffuseColor;
    glm::vec3 specularColor;
    float specularExponent;
    float transparency;
    glm::vec3 transmissionColor;
    float refractiveIndex;
    fs::path ambientTexture;
    fs::path diffuseTexture;
    fs::path normalTexture;
};

struct MaterialResource {
    std::vector<SubMaterialResource> subMaterials;
    std::unordered_map<fs::path, uint32_t> materialIndices;

    MaterialResource();
    bool Load(const fs::path& path);
    void Cleanup();
private:
    bool ProcessLine(std::istringstream& iss, const std::string& line, const fs::path& path);
};
