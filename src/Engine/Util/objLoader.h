#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Component/mesh.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

union IndexTriple {
    struct {
        int32_t positionIndex;
        int32_t uvIndex;
        int32_t normalIndex;
    };
    int32_t data[3];
};

struct Shape {
    std::vector<IndexTriple> indices;
    std::string materialName;
};

// Subset of actual mtl file data, more than enough for now
struct MtlData {
    std::string name;
    glm::vec3 ambientColor;            // Ka [0, 1]
    glm::vec3 diffuseColor;            // Kd [0, 1]
    glm::vec3 specularColor;           // Ks [0, 1]
    float specularExponent;            // Ns [0, 1000]
    float dissolve;                    // d  [0, 1]     (some implementations use Tr, d = 1 - Tr)
    glm::vec3 transmissionFilter;      // Tf [0, 1]     ("Tf xyz" = ciexyz colorspace, "Tf spectral" = file, I'll support rgb only)
    float refractiveIndex;             // Ni [0, ...]
    std::string ambientTexture;        // map_Ka
    std::string diffuseTexture;        // map_Kd
    std::string displacementTexture;   // map_Disp
    std::string alphaTexture;          // map_d
};

class ObjLoader {
public:
    bool LoadObj(const std::filesystem::path& objFilePath, const std::filesystem::path& mtlFilePath, std::unordered_map<std::string, MtlData>& materialData, std::vector<Shape>& results);
    Vertex GetVertex(IndexTriple index);

private:
    void ProcessLineMtl(std::istringstream& iss, const std::string& line, MtlData *material, std::unordered_map<std::string, MtlData>& results);
    void ProcessLineObj(std::istringstream& iss, const std::string& line, Shape *shape, std::unordered_map<std::string, MtlData>& materials, std::vector<Shape>& results);

private:
    std::vector<glm::vec3> m_Positions;
    std::vector<glm::vec2> m_UVs;
    std::vector<glm::vec3> m_Normals;
};
