#include "objLoader.h"
#include "logger.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <ranges>
#include <string>

glm::ivec3& OBJFaceIndices::operator[](int index)
{
    if (index == 0) return this->x;
    else if (index == 1) return this->y;
    else if (index == 2) return this->z;
    else {
        ERROR("Indexing ObjFace out of bounds");
        abort();
    }
};

OBJData::OBJData(const std::filesystem::path& filePath)
{
    INFO("Loading OBJ file: " << filePath);

    std::ifstream file(filePath);
    std::string line;
    while (std::getline(file, line)) {
        if (!ParseLine(line)) {
            corrupted = true;
            file.close();
            return;
        }
    }
    file.close();
}

bool OBJData::ParseLine(const std::string& line)
{
    std::vector<std::string_view> tokens;

    size_t i = 0;
    for (size_t j = 0; j < line.size(); j++) {
        if (j == line.size() - 1) {
            tokens.push_back(std::string_view(line).substr(i, j - i + 1));
            i = j + 1;
        } else if (line[j] == ' ' || line[j] == '/') {
            tokens.push_back(std::string_view(line).substr(i, j - i));
            i = j + 1;
        }
    }

    if (tokens[0] == "vt") {
        if (tokens.size() != 3) return false;

        glm::vec2 tmp;
        for (auto const [index, token] : tokens | std::views::drop(1) | std::ranges::views::enumerate) {
            if (!ReadFloat(token, &tmp[index])) return false;
        }
        textureCoords.push_back(tmp);
    } else if (tokens[0] == "vn") {
        if (tokens.size() != 4) return false;

        glm::vec3 tmp;
        for (auto const [index, token] : tokens | std::views::drop(1) | std::ranges::views::enumerate) {
            if (!ReadFloat(token, &tmp[index])) return false;
        }
        normals.push_back(tmp);
    } else if (tokens[0] == "v") {
        if (tokens.size() != 4) return false;

        glm::vec3 tmp;
        for (auto const [index, token] : tokens | std::views::drop(1) | std::ranges::views::enumerate) {
            if (!ReadFloat(token, &tmp[index])) return false;
        }
        vertices.push_back(tmp);
    } else if (tokens[0] == "f") {
        if (tokens.size() != 10) return false;

        OBJFaceIndices tmp;
        int subIndex = -1;
        for (auto const [index, token] : tokens | std::views::drop(1) | std::ranges::views::enumerate) {
            if (index % 3 == 0) {
                subIndex++;
            }
            if (!ReadInt(token, &tmp[subIndex][index % 3])) return false;
        }
        faces.push_back(tmp);
    }

    return true;
}

// strtol won't work for me, because I need the function to operate on a string_view
bool OBJData::ReadInt(std::string_view view, int32_t *res)
{
    bool negative = false;
    float tmp = 0.0;
    int32_t mul = std::pow(10, view.size() - 1);
    for (const auto [i, c] : view | std::views::enumerate) {
        if (c == '-') {
            if (i == 0) {
                negative = true; 
                continue;
            }
            else return false;
        }

        int digit = c - '0';
        if (digit < 0 || digit > 9) return false;

        tmp += digit * mul;
        mul /= 10;
    }
    if (negative) tmp /= -10;
    *res = tmp;
    return true;
}

// strtof won't work for me, because I need the function to operate on a string_view
bool OBJData::ReadFloat(std::string_view view, float *res)
{
    float mul = -1.0;
    for (const auto [i, c] : view | std::views::enumerate) {
        if (c == '.') {
            mul = std::pow(10.0f, static_cast<float>(i - 1));
            break;
        }
    }

    // No decimal point
    if (mul < 0.0) {
        int32_t tmp;
        if (!ReadInt(view, &tmp)) return false;
        *res = static_cast<float>(tmp);
        return true;
    }

    bool negative = false;
    float tmp = 0.0;
    for (const auto [i, c] : view | std::views::enumerate) {
        if (c == '.') continue;
        if (c == '-') {
            if (i == 0) {
                negative = true; 
                continue;
            }
            else return false;
        }

        int digit = c - '0';
        if (digit < 0 || digit > 9) return false;

        tmp += static_cast<float>(digit) * mul;
        mul /= 10.0;
    }
    if (negative) tmp *= -0.1;
    *res = tmp;
    return true;
}
