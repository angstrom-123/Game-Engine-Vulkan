#pragma once 

#include "System/Render/textureArrayHandler.h"
#include "glm/ext/vector_float3.hpp"
#include <glm/vec4.hpp>
#include <string>

enum TextFlag {
    TEXT_FLAG_NONE = 0x0,
    TEXT_FLAG_DIRTY = 0x1,
};

namespace fs = std::filesystem;

enum TextAlign {
    TEXT_ALIGN_LEFT,
    TEXT_ALIGN_RIGHT,
    TEXT_ALIGN_CENTRE,
};

struct TextCreateInfo {
    int32_t      fontSize    = 0;
    glm::vec3    position    = glm::vec3(0.0);
    glm::vec3    color       = glm::vec3(1.0);
    std::string  text        = "Text";
    fs::path     fontPath    = "";    
    float        maxWidth    = 0.0;            // 0 = no wrapping
    float        lineHeight  = 1.2;            // wrapping only
    TextAlign    align       = TEXT_ALIGN_LEFT;
};

struct Text {
    int32_t           fontSize      = 0;
    uint32_t          flags         = TEXT_FLAG_NONE;
    glm::vec3         color         = glm::vec3(1.0);
    std::string       text          = "";
    TextureAllocation atlasTexture;
    fs::path          fontPath      = "";
};
