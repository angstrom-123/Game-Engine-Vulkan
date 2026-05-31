#pragma once

#include <cstdint>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

enum ShadowQuality {
    NONE,
    LOW,
    MEDIUM,
    HIGH,
    ULTRA,
    MAX_ENUM
};

struct Config {
    Config() = default;
    Config(const fs::path& path);

    std::string appName{""};
    std::string startScene{""};
    uint32_t windowWidth{0};
    uint32_t windowHeight{0};
    bool vsync{false};
    ShadowQuality shadowQuality{ShadowQuality::NONE};
    bool bloom{false};

};
