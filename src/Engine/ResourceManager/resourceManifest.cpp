#include "resourceManifest.h"
#include "Util/myAssert.h"

#include <fkYAML/node.hpp>
#include <fstream>

bool ResourceManifest::Load(const fs::path& path)
{
    sceneDir = fs::path(path);
    sceneDir.remove_filename();

    std::ifstream file(path);

    // I wish this library didn't use exceptions
    try {
        fkyaml::node rootNode = fkyaml::node::deserialize(file);
        file.close();
    
        if (rootNode.contains("manifest")) {
            sceneName = rootNode["manifest"].as_str();
        } else {
            return false;
        }

        if (rootNode.contains("camera")) {
            const auto& cameraNode = rootNode["camera"];

            if (cameraNode.contains("shadows")) {
                shadowsEnabled = cameraNode["shadows"].as_bool();
            }

            if (cameraNode.contains("projection")) {
                std::string projectionString = cameraNode["projection"].as_str();
                if (projectionString == "perspective") {
                    perspectiveProjection = true;
                } else if (projectionString == "orthographic") {
                    perspectiveProjection = false;
                } else {
                    return false;
                }
            }
        }

        if (rootNode.contains("models")) {
            for (const auto& modelNode : rootNode["models"]) {
                models.emplace_back(modelNode["path"].as_str());
            }
        }

        if (rootNode.contains("materials")) {
            for (const auto& materialNode : rootNode["materials"]) {
                materials.emplace_back(materialNode["path"].as_str());
            }
        }
    } catch (fkyaml::exception e) {
        ERROR("FKYAML error: " << e.what());
        file.close();
        return false;
    }

    return true;
}

std::string ResourceManifest::ReadName(const fs::path& path)
{
    std::ifstream file(path);

    // I wish this library didn't use exceptions
    try {
        fkyaml::node rootNode = fkyaml::node::deserialize(file);
        file.close();
    
        if (rootNode.contains("manifest")) {
            return rootNode["manifest"].as_str();
        } else {
            return "";
        }
    } catch (fkyaml::exception e) {
        ERROR("FKYAML error: " << e.what());
        file.close();
        return "";
    }
}

