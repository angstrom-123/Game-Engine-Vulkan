#include "config.h"

#include "Util/enumIndex.h"
#include "Util/logger.h"

#include <fkYAML/node.hpp>
#include <fstream>

using Node = fkyaml::node;

static Node GetIfExists(const Node& node, const char *element)
{
    if (node.contains(element)) {
        return node[element];
    }
    FATAL("Failed to read field `" << element << "`");
}

Config::Config(const fs::path& path)
{
    std::ifstream file(path);
    try {
        Node rootNode = Node::deserialize(file);
        file.close();

        Node appNode = GetIfExists(rootNode, "app");
        Node windowNode = GetIfExists(rootNode, "window");
        Node graphicsNode = GetIfExists(rootNode, "graphics");

        appName = GetIfExists(appNode, "name").as_str();
        startScene = GetIfExists(appNode, "startScene").as_str();

        windowWidth = GetIfExists(windowNode, "width").as_int();
        windowHeight = GetIfExists(windowNode, "height").as_int();

        vsync = GetIfExists(graphicsNode, "vsync").as_bool();
        bloom = GetIfExists(graphicsNode, "bloom").as_bool();
        int rawQuality = GetIfExists(graphicsNode, "shadowQuality").as_int();
        if (rawQuality >= EnumBase(ShadowQuality::MAX_ENUM) || rawQuality < EnumBase(ShadowQuality::LOW)) {
            FATAL("Shadow quality (" << rawQuality << ") out of allowed range (" << EnumBase(ShadowQuality::LOW) << "-" << EnumBase(ShadowQuality::MAX_ENUM) - 1 <<")");
        }
        shadowQuality = static_cast<ShadowQuality>(rawQuality);
    } catch (fkyaml::exception e) {
        FATAL("FKYAML error: " << e.what());
    }
}
