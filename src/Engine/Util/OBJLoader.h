#pragma once

#include <filesystem>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

class OBJFaceIndices {
public:
    ~OBJFaceIndices() = default;
    glm::ivec3& operator[](int index);

    // OBJ files are 1-indexed, so we subtract one here.
    static int32_t GetVertexIndex(const glm::ivec3& set) { return set[0] - 1; }
    static int32_t GetTexCoordIndex(const glm::ivec3& set) { return set[1] - 1; }
    static int32_t GetNormalIndex(const glm::ivec3& set) { return set[2] - 1; }

public:
    glm::ivec3 x;
    glm::ivec3 y;
    glm::ivec3 z;
};

class OBJData {
public:
    ~OBJData() = default;
    OBJData(const std::filesystem::path& filePath);

public:
    bool corrupted = false;
    std::vector<glm::vec3> vertices;
    std::vector<glm::vec2> textureCoords;
    std::vector<glm::vec3> normals;
    std::vector<OBJFaceIndices> faces;

private:
    bool ParseLine(const std::string& line);
    bool ReadInt(std::string_view view, int32_t *res);
    bool ReadFloat(std::string_view view, float *res);
};
