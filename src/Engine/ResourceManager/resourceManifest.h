#pragma once

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

/*  Example MANIFEST.yaml
    manifest: scene name
    camera:
      maxShadows: 4
      perspective: true
    models:
      - relative/path/to/model1.obj
      - relative/path/to/model2.obj
    materials:
      - relative/path/to/material1.mtl
      - relative/path/to/material2.mtl
 */

// TODO: Add fonts
struct ResourceManifest {
    std::string sceneName = "";
    uint32_t maxShadows = 0;
    bool perspectiveProjection = true;
    std::vector<fs::path> models;
    std::vector<fs::path> materials;
    fs::path sceneDir = "";

    bool Load(const fs::path& path);
    static std::string ReadName(const fs::path& path);
};
