#pragma once

#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <filesystem>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

struct SubModelResource {
    std::string name;
    std::string materialName;
    std::vector<glm::ivec3> indices;
};

struct ModelResource {
    fs::path materialFilePath;
    std::vector<SubModelResource> subModels;
    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> normals;

    ModelResource();
    bool Load(const fs::path& path);
    void Cleanup();

private:
    bool ProcessLine(std::istringstream& iss, const std::string& line, const fs::path& path, SubModelResource *subModel);
};
