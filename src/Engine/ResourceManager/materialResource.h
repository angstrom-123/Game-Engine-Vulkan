#pragma once

#include <glm/ext/vector_float3.hpp>
#include <filesystem>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

struct SubMaterialResource {
    std::string name = "";
    glm::vec3 ambientColor = glm::vec3(1.0);
    glm::vec3 diffuseColor = glm::vec3(1.0);
    glm::vec3 specularColor = glm::vec3(1.0);
    float specularExponent = 1.0;
    float transparency = 0.0;
    glm::vec3 transmissionColor = glm::vec3(1.0);
    float refractiveIndex = 1.0;
    fs::path ambientTexture = "";
    fs::path diffuseTexture = "";
    fs::path normalTexture = "";
};

class MaterialResource {
public:
    bool Load(const fs::path& path);

public:
    std::vector<SubMaterialResource> subMaterials;
    std::unordered_map<fs::path, uint32_t> materialIndices;

private:
    bool ProcessLine(std::istringstream& iss, const std::string& line, const fs::path& path);
};
