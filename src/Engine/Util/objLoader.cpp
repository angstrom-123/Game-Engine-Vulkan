#include "objLoader.h"

#include "Util/logger.h"
#include "Util/myAssert.h"

#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

constexpr uint32_t HashString(const char *str) 
{
    uint32_t hash = 5138;
    while (*str) {
        hash = ((hash << 5) * hash) + *str;
        str++;
    }
    return hash;
}

constexpr uint32_t HASH_V = HashString("v");
constexpr uint32_t HASH_VT = HashString("vt");
constexpr uint32_t HASH_VN = HashString("vn");
constexpr uint32_t HASH_F = HashString("f");
constexpr uint32_t HASH_O = HashString("o");
constexpr uint32_t HASH_G = HashString("g");
constexpr uint32_t HASH_USEMTL = HashString("usemtl");
constexpr uint32_t HASH_NEWMTL = HashString("newmtl");
constexpr uint32_t HASH_KA = HashString("Ka");
constexpr uint32_t HASH_KD = HashString("Kd");
constexpr uint32_t HASH_KS = HashString("Ks");
constexpr uint32_t HASH_NS = HashString("Ns");
constexpr uint32_t HASH_NI = HashString("Ni");
constexpr uint32_t HASH_D = HashString("d");
constexpr uint32_t HASH_MAP_DISP = HashString("map_Disp");
constexpr uint32_t HASH_MAP_KA = HashString("map_Ka");
constexpr uint32_t HASH_MAP_KD = HashString("map_Kd");

bool ObjLoader::LoadObj(const fs::path& objFilePath, const fs::path& mtlFilePath, std::unordered_map<std::string, MtlData>& materialData, std::vector<Shape>& results)
{
    std::ios::sync_with_stdio(false);

    std::istringstream iss;
    std::string line;

    constexpr size_t BUFFER_SIZE = 1024 * 1024;
    char buffer[BUFFER_SIZE];

    std::ifstream mtlFile(mtlFilePath, std::ios::in | std::ios::binary);
    mtlFile.rdbuf()->pubsetbuf(buffer, BUFFER_SIZE);
    if (!mtlFile.is_open()) {
        ERROR("Failed to open mtl file: " << mtlFilePath);
        return false;
    }

    fs::path basePath(mtlFilePath);
    basePath = basePath.remove_filename();

    MtlData material;
    while (std::getline(mtlFile, line)) {
        if (line.empty()) continue;
        ProcessLineMtl(basePath, iss, line, &material, materialData);
    }

    mtlFile.close();

    if (!material.name.empty()) {
        materialData.insert({std::string(material.name), material});
    }

    std::ifstream objFile(objFilePath, std::ios::in | std::ios::binary);
    objFile.rdbuf()->pubsetbuf(buffer, BUFFER_SIZE);
    if (!objFile.is_open()) {
        ERROR("Failed to open obj file: " << objFilePath);
        return false;
    }

    objFile.seekg(std::ios::end);

    size_t fileSize = objFile.tellg();
    size_t reserveSize = fileSize / 3; // Overallocation is fine, I want to avoid resizing
    m_Positions.reserve(reserveSize);
    m_Normals.reserve(reserveSize);
    m_UVs.reserve(reserveSize);

    objFile.seekg(std::ios::beg);

    Shape shape = {};
    line = std::string();
    while (std::getline(objFile, line)) {
        if (line.empty()) continue;
        ProcessLineObj(iss, line, &shape, materialData, results);
    }

    objFile.close();

    if (!shape.indices.empty()) {
        results.push_back(shape);
    }

    m_Positions.shrink_to_fit();
    m_Normals.shrink_to_fit();
    m_UVs.shrink_to_fit();

    return true;
}

Vertex ObjLoader::GetVertex(IndexTriple index)
{
    glm::vec3 position = m_Positions[index.positionIndex - 1];
    glm::vec3 normal = m_Normals[index.normalIndex - 1];
    glm::vec2 uv = m_UVs[index.uvIndex - 1];

    return (Vertex) {
        .position = position,
        .normal = normal,
        .uv = uv,
    };
}

void ObjLoader::ProcessLineMtl(const fs::path& basePath, std::istringstream& iss, const std::string& line, MtlData *mtl, std::unordered_map<std::string, MtlData>& results)
{
    iss.clear();
    iss.str(line);
    std::string tokenString;
    uint32_t token;

    if (!(iss >> tokenString)) return;
    token = HashString(tokenString.data());
    switch (token) {
        case HASH_NEWMTL: {
            std::string materialName = {};
            if (iss >> materialName) {
                if (!mtl->name.empty()) {
                    results.insert({mtl->name, *mtl});
                    *mtl = (MtlData) {
                        .ambientTexture = "",
                        .diffuseTexture = "",
                        .normalTexture = ""
                    };
                }
                mtl->name = materialName;
            } else {
                ERROR("Failed to read material name");
                abort();
            }
            break;
        } 
        case HASH_KA: 
            if (!(iss >> mtl->ambientColor[0] >> mtl->ambientColor[1] >> mtl->ambientColor[2])) {
                ERROR("Failed to read ambient color");
                abort();
            }
            break;
        case HASH_KD: 
            if (!(iss >> mtl->diffuseColor[0] >> mtl->diffuseColor[1] >> mtl->diffuseColor[2])) {
                ERROR("Failed to read diffuse color");
                abort();
            }
            break;
        case HASH_KS:
            if (!(iss >> mtl->specularColor[0] >> mtl->specularColor[1] >> mtl->specularColor[2])) {
                ERROR("Failed to read diffuse color");
                abort();
            }
            break;
        case HASH_NS: 
            if (!(iss >> mtl->specularExponent)) {
                ERROR("Failed to read specular exponent value");
                abort();
            }
            break;
        case HASH_NI: 
            if (!(iss >> mtl->refractiveIndex)) {
                ERROR("Failed to read refractive index value");
                abort();
            }
            break;
        case HASH_D:
            if (!(iss >> mtl->dissolve)) {
                ERROR("Failed to read dissolve value");
                abort();
            }
            break;
        case HASH_MAP_DISP: {
            std::string path;
            if (!(iss >> path)) {
                ERROR("Failed to read normal texture path");
                abort();
            }
            mtl->normalTexture = fs::path(basePath).append(path);
            break;
        }
        case HASH_MAP_KA: {
            std::string path;
            if (!(iss >> path)) {
                ERROR("Failed to read ambient texture path");
                abort();
            }
            mtl->ambientTexture = fs::path(basePath).append(path);
            break;
        }
        case HASH_MAP_KD: {
            std::string path;
            if (!(iss >> path)) {
                ERROR("Failed to read diffuse texture path");
                abort();
            }
            mtl->diffuseTexture = fs::path(basePath).append(path);
            break;
        }
    };
}

void ObjLoader::ProcessLineObj(std::istringstream& iss, const std::string& line, Shape *shape, std::unordered_map<std::string, MtlData>& materials, std::vector<Shape>& results)
{
    iss.clear();
    iss.str(line);
    std::string tokenString;
    uint32_t token;

    if (!(iss >> tokenString)) return;
    token = HashString(tokenString.data());
    switch (token) {
        case HASH_V: {
            glm::vec3 position = {};
            if (!(iss >> position[0] >> position[1] >> position[2])) {
                ERROR("Failed to read vertex position");
                abort();
            }
            m_Positions.push_back(position);
            break;
        }
        case HASH_VT: {
            glm::vec2 uv = {};
            if (!(iss >> uv[0] >> uv[1])) {
                ERROR("Failed to read vertex texture coordinate");
                abort();
            }
            m_UVs.push_back(uv);
            break;
        }
        case HASH_VN: {
            glm::vec3 normal = {}; 
            if (!(iss >> normal[0] >> normal[1] >> normal[2])) {
                ERROR("Failed to read vertex normal");
                abort();
            }
            m_Normals.push_back(normal);
            break;
        }
        case HASH_USEMTL: {
            std::string materialName = {};
            if (!(iss >> materialName)) {
                ERROR("Failed to read material name");
                abort();
            }
            ASSERT(materials.find(materialName) != materials.end() && "Invalid material");
            shape->materialName = materialName;
            break;
        }
        case HASH_O: case HASH_G: // Was just HASH_O
            if (!shape->indices.empty()) {
                results.push_back(*shape);
                *shape = (Shape) {};
            }
            break;
        case HASH_F:
            IndexTriple indices = {};
            std::string facePart = {};
            std::string indexString = {};
            std::istringstream vertexStream = {};
            // Face parts are index triples separated by '/', so I use a new stream for that
            while (iss >> facePart) {
                size_t partIndex = 0;
                indexString.clear();
                vertexStream.clear();
                vertexStream.str(facePart);
                while (std::getline(vertexStream, indexString, '/')) {
                    std::from_chars_result res = std::from_chars(indexString.data(), indexString.data() + indexString.size(), indices.data[partIndex]);
                    ASSERT(res.ec == std::errc() && "Failed to read face index");
                    partIndex++;
                }
                shape->indices.push_back(indices);
            }
            break;
    };
}
