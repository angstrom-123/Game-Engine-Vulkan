#pragma once

#include "System/Render/textureArrayHandler.h"
#include "Util/myAssert.h"
#include "glm/ext/vector_float2.hpp"
#include <filesystem>
#include <map>
#include <vector>

#include <stb/stb_truetype.h>

// Printable ascii
const char FIRST_CHAR = 32;
const char LAST_CHAR = 126;
const size_t FONT_ATLAS_RESOLUTION = 2048;

struct GlyphInfo {
    glm::vec2 uv0;
    glm::vec2 uv1;
    glm::vec2 offset;
    float advance;
};

struct GlyphSet {
    GlyphInfo glyphs[LAST_CHAR - FIRST_CHAR] = {};

    GlyphInfo GetGlyph(char c) const
    {
        ASSERT(c >= FIRST_CHAR && c <= LAST_CHAR && "Character out of range");
        return glyphs[c - FIRST_CHAR];
    }
};

using FontMetrics = std::map<int32_t, GlyphSet>;

struct FontInfo {
    TextureAllocation atlas;
    GlyphSet glyphSet;
};

class FontResource {
public:
    bool Load(const std::filesystem::path& path);
    FontMetrics Pack(std::initializer_list<int32_t> sizes);

public:
    uint8_t *bitmap = nullptr; // This is a raw pointer for interop with image resources that use stb image
    std::vector<uint8_t> ttfBuffer;
};
