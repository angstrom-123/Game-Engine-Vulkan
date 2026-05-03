#include "materialResource.h"

#include <fstream>

#include "Util/myAssert.h"

constexpr uint32_t HashString(const char *str) 
{
    uint32_t hash = 5138;
    while (*str) {
        hash = ((hash << 5) * hash) + *str;
        str++;
    }
    return hash;
}

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

MaterialResource::MaterialResource()
{
    subMaterials.reserve(5);
}

bool MaterialResource::Load(const fs::path& path)
{
    std::ios::sync_with_stdio(false);

    std::istringstream iss;
    std::string line;

    constexpr size_t BUFFER_SIZE = 1024 * 1024;
    char buffer[BUFFER_SIZE];

    std::ifstream file(path, std::ios::in | std::ios::binary);
    file.rdbuf()->pubsetbuf(buffer, BUFFER_SIZE);
    if (!file.is_open()) {
        ERROR("Failed to open mtl file: " << path);
        return false;
    }

    fs::path basePath(path);
    basePath = basePath.remove_filename();

    while (std::getline(file, line)) {
        bool res = ProcessLine(iss, line, basePath);
        if (!res) {
            ERROR("Failed to process mtl line: " << line);
            return false;
        }
    }

    file.close();

    return true;
}

void MaterialResource::Cleanup()
{

}

bool MaterialResource::ProcessLine(std::istringstream& iss, const std::string& line, const fs::path& path)
{
    if (line.empty()) return true;

    iss.clear();
    iss.str(line);
    std::string tokenString;
    uint32_t token;

    if (!(iss >> tokenString)) {
        ERROR("Failed to read mtl line token");
        return false;
    }
    token = HashString(tokenString.data());
    switch (token) {
        case HASH_NEWMTL: {
            std::string materialName = {};
            if (iss >> materialName) {
                materialIndices.insert({materialName, subMaterials.size()});
                SubMaterialResource material = {};
                material.name = materialName;
                subMaterials.push_back(material);
            } else {
                ERROR("Failed to read material name");
                return false;
            }
            break;
        } 
        case HASH_KA: {
            SubMaterialResource &material = subMaterials.back();
            if (!(iss >> material.ambientColor[0] >> material.ambientColor[1] >> material.ambientColor[2])) {
                ERROR("Failed to read ambient color");
                return false;
            }
            break;
        }
        case HASH_KD: {
            SubMaterialResource &material = subMaterials.back();
            if (!(iss >> material.diffuseColor[0] >> material.diffuseColor[1] >> material.diffuseColor[2])) {
                ERROR("Failed to read diffuse color");
                return false;
            }
            break;
        }
        case HASH_KS: {
            SubMaterialResource &material = subMaterials.back();
            if (!(iss >> material.specularColor[0] >> material.specularColor[1] >> material.specularColor[2])) {
                ERROR("Failed to read diffuse color");
                return false;
            }
            break;
        }
        case HASH_NS: {
            SubMaterialResource &material = subMaterials.back();
            if (!(iss >> material.specularExponent)) {
                ERROR("Failed to read specular exponent value");
                return false;
            }
            break;
        }
        case HASH_NI: {
            if (!(iss >> subMaterials.back().refractiveIndex)) {
                ERROR("Failed to read refractive index value");
                return false;
            }
            break;
        }
        case HASH_D: {
            if (!(iss >> subMaterials.back().transparency)) {
                ERROR("Failed to read dissolve value");
                return false;
            }
            break;
        }
        case HASH_MAP_DISP: {
            fs::path filePath = "";
            if (!(iss >> filePath)) {
                ERROR("Failed to read normal texture path");
                return false;
            }
            subMaterials.back().normalTexture = path / filePath;
            break;
        }
        case HASH_MAP_KA: {
            fs::path filePath = "";
            if (!(iss >> filePath)) {
                ERROR("Failed to read ambient texture path");
                return false;
            }
            subMaterials.back().ambientTexture = path / filePath;
            break;
        }
        case HASH_MAP_KD: {
            fs::path filePath = "";
            if (!(iss >> filePath)) {
                ERROR("Failed to read diffuse texture path");
                return false;
            }
            subMaterials.back().diffuseTexture = path / filePath;
            break;
        }
        default:
            break;
    };

    return true;
}
