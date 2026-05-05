#include "fontResource.h"
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <ranges>
#include "Util/logger.h"
#include "Util/myAssert.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

bool FontResource::Load(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        ERROR("Failed to open font file: " << path);
        return false;
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    ttfBuffer.resize(fileSize);
    if (!file.read(reinterpret_cast<char *>(ttfBuffer.data()), fileSize)) {
        ERROR("Failed to read font file: " << path);
        return false;
    }

    return true;
}

FontMetrics FontResource::Pack(std::initializer_list<int32_t> sizes)
{
    ASSERT(!ttfBuffer.empty() && "Font resource not loaded");

    bitmap = static_cast<uint8_t *>(std::calloc(FONT_ATLAS_RESOLUTION * FONT_ATLAS_RESOLUTION, 1));

    std::vector<stbtt_pack_range> ranges(sizes.size());
    std::vector<stbtt_packedchar[LAST_CHAR - FIRST_CHAR]> metrics(sizes.size());
    for (const auto [i, size] : sizes | std::ranges::views::enumerate) {
        ranges[i] = { static_cast<float>(size), FIRST_CHAR, NULL, LAST_CHAR - FIRST_CHAR, metrics[i], 0, 0 };
    }

    // Pack font
    const uint8_t OVERSAMPLING = 2;
    stbtt_pack_context ctx;
    stbtt_PackBegin(&ctx, bitmap, FONT_ATLAS_RESOLUTION, FONT_ATLAS_RESOLUTION, 0, 1, NULL);
    stbtt_PackSetOversampling(&ctx, OVERSAMPLING, OVERSAMPLING);
    stbtt_PackFontRanges(&ctx, ttfBuffer.data(), 0, ranges.data(), sizes.size());
    stbtt_PackEnd(&ctx);

    // Glyph metrics
    FontMetrics results;
    for (const auto [i, size] : sizes | std::ranges::views::enumerate) {
        GlyphSet glyphSet;
        for (int32_t j = 0; j < LAST_CHAR - FIRST_CHAR; j++) {
            const stbtt_packedchar &pc = metrics[i][j];

            glyphSet.glyphs[j] = {
                .uv0 = glm::vec2(static_cast<float>(pc.x0) / static_cast<float>(FONT_ATLAS_RESOLUTION), 
                                 static_cast<float>(pc.y0) / static_cast<float>(FONT_ATLAS_RESOLUTION)),
                .uv1 = glm::vec2(static_cast<float>(pc.x1) / static_cast<float>(FONT_ATLAS_RESOLUTION), 
                                 static_cast<float>(pc.y1) / static_cast<float>(FONT_ATLAS_RESOLUTION)),
                .offset = glm::vec2(pc.xoff, pc.yoff) * glm::vec2(OVERSAMPLING),
                .advance = pc.xadvance * OVERSAMPLING
            };

            ASSERT(glyphSet.glyphs[j].uv0.x != 0.0 || glyphSet.glyphs[j].uv0.y != 0.0 || glyphSet.glyphs[j].uv1.x != 0.0 || glyphSet.glyphs[j].uv1.y != 0.0 && "UVs must not all be 0, atlas is probably too small to fit text");
        }
        results.insert({size, std::move(glyphSet)});
    }

    return results;
}
