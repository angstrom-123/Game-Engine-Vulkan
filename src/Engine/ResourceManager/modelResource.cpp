#include "modelResource.h"

#include "Util/myAssert.h"
#include <charconv>
#include <fstream>

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
constexpr uint32_t HASH_MTLLIB = HashString("mtllib");

ModelResource::ModelResource()
{
    subModels.reserve(5);
}

bool ModelResource::Load(const fs::path& path)
{
    std::ios::sync_with_stdio(false);

    constexpr size_t BUFFER_SIZE = 1024 * 1024;
    char buffer[BUFFER_SIZE];

    std::ifstream file(path, std::ios::in | std::ios::binary);
    file.rdbuf()->pubsetbuf(buffer, BUFFER_SIZE);
    if (!file.is_open()) {
        ERROR("Failed to open file: " << path);
        return false;
    }

    file.seekg(std::ios::end);

    size_t fileSize = file.tellg();
    size_t reserveSize = fileSize / 3; // Overallocation is fine, I want to avoid resizing

    file.seekg(std::ios::beg);

    positions.reserve(reserveSize);
    uvs.reserve(reserveSize);
    normals.reserve(reserveSize);

    SubModelResource subModel = {};
    std::string line;
    std::istringstream iss;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        ProcessLine(iss, line, path, &subModel);
    }

    file.close();

    if (!subModel.indices.empty()) {
        subModels.push_back(subModel);
    }

    return true;
}

void ModelResource::Cleanup()
{
}

bool ModelResource::ProcessLine(std::istringstream& iss, const std::string& line, const fs::path& path, SubModelResource *subModel)
{
    iss.clear();
    iss.str(line);
    std::string tokenString;
    uint32_t token;

    if (!(iss >> tokenString)) return false;
    token = HashString(tokenString.data());
    switch (token) {
        case HASH_V: {
            glm::vec3 position = {};
            if (!(iss >> position[0] >> position[1] >> position[2])) {
                ERROR("Failed to read vertex position");
                return false;
            }
            positions.push_back(position);
            break;
        }
        case HASH_VT: {
            glm::vec2 uv = {};
            if (!(iss >> uv[0] >> uv[1])) {
                ERROR("Failed to read vertex texture coordinate");
                return false;
            }
            uvs.push_back(uv);
            break;
        }
        case HASH_VN: {
            glm::vec3 normal = {}; 
            if (!(iss >> normal[0] >> normal[1] >> normal[2])) {
                ERROR("Failed to read vertex normal");
                return false;
            }
            normals.push_back(normal);
            break;
        }
        case HASH_USEMTL: {
            if (!(iss >> subModel->materialName)) {
                ERROR("Failed to read material name");
                return false;
            }
            break;
        }
        case HASH_MTLLIB: {
            std::string materialFileName = {};
            if (!(iss >> materialFileName)) {
                ERROR("Failed to read material file name");
                return false;
            }
            materialFilePath = fs::path(path);
            materialFilePath.replace_filename(materialFileName);
            break;
        }
        case HASH_O: 
        case HASH_G:
            if (!(iss >> subModel->name)) {
                ERROR("Failed to read submodel name");
                return false;
            }

            if (!subModel->indices.empty()) {
                subModels.push_back(*subModel);
                *subModel = {};
            }
            break;
        case HASH_F:
            glm::ivec3 indices = {};
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
                    std::from_chars_result res = std::from_chars(indexString.data(), indexString.data() + indexString.size(), indices[partIndex]);
                    ASSERT(res.ec == std::errc() && "Failed to read face index");
                    partIndex++;
                }
                subModel->indices.push_back(indices);
            }
            break;
    };

    return true;
}
