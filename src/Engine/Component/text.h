#pragma once 

#include "System/Render/backendTypes.h"
#include "glm/ext/vector_float3.hpp"
#include <glm/vec4.hpp>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

enum class TextAlign : uint32_t {
    LEFT,
    RIGHT,
    CENTRE,
};

struct TextCreateInfo {
    int32_t fontSize = 0;
    glm::vec3 position = glm::vec3(0.0);
    glm::vec3 color = glm::vec3(1.0);
    std::string text = "Text";
    fs::path fontPath = "";    
    float maxWidth = 0.0;                   // 0 = no wrapping
    float lineHeight = 1.2;                 // wrapping only
    TextAlign align = TextAlign::LEFT;
};

struct Text {
    int32_t fontSize = 0;
    glm::vec3 color = glm::vec3(1.0);
    std::string text = "";
    AllocatedTexture atlasTexture;
    fs::path fontPath = "";
};
