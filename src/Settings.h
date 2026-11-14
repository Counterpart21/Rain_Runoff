#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace runoff {
struct Settings {
    bool enabled{true};
    float radius{6000.0F};
    unsigned maxRoofs{24};
    unsigned candidatesPerTick{1};
    unsigned sampleResolution{7};
    unsigned maxTriangles{50000};
    float scanSeconds{2.0F};
    float buildupSeconds{8.0F};
    float drainSeconds{12.0F};
    float topWidth{10.0F};
    float opacity{0.48F};
    float scrollSpeed{1.8F};
    float maxDrop{2048.0F};
    float cornerOutset{12.0F};
    float minimumSpan{128.0F};
    bool requireSky{true};
    bool collisionFallback{true};
    bool debugLog{false};
    std::string model{"RainRunoff\\stream.nif"};
    std::vector<std::string> includeTokens{"architecture", "house", "roof", "farm", "building", "hut", "shack", "dwelling"};
    std::vector<std::string> excludeTokens{"interior", "dungeon", "cave", "furniture", "clutter", "door", "tree", "rock", "landscape", "statue"};
    static Settings Load();
    bool accepts(const std::string& modelPath) const;
};
}
