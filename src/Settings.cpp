#include "Settings.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace runoff {
namespace {
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
std::vector<std::string> tokens(const std::string& s) {
    std::vector<std::string> result;
    std::istringstream stream(s);
    for (std::string part; std::getline(stream, part, ',');) {
        part = lower(trim(part));
        if (!part.empty()) result.push_back(part);
    }
    return result;
}
}
Settings Settings::Load() {
    Settings result;
    std::ifstream file("Data/SKSE/Plugins/RainRunoff.ini");
    std::unordered_map<std::string, std::string> values;
    for (std::string line; std::getline(file, line);) {
        line = trim(line.substr(0, line.find_first_of(";#")));
        if (line.empty() || line.front() == '[') continue;
        const auto sep = line.find('=');
        if (sep != std::string::npos) values[lower(trim(line.substr(0, sep)))] = trim(line.substr(sep + 1));
    }
    auto number = [&](std::string_view key, float fallback, float low, float high) {
        const auto it = values.find(std::string(key));
        if (it == values.end()) return fallback;
        try {
            std::size_t used{};
            const float value = std::stof(it->second, &used);
            if (used != it->second.size() || !std::isfinite(value)) return fallback;
            return std::clamp(value, low, high);
        } catch (...) { return fallback; }
    };
    auto boolean = [&](std::string_view key, bool fallback) {
        const auto it = values.find(std::string(key));
        if (it == values.end()) return fallback;
        const auto s = lower(it->second);
        if (s == "true" || s == "1" || s == "yes") return true;
        if (s == "false" || s == "0" || s == "no") return false;
        return fallback;
    };
    result.enabled = boolean("enabled", result.enabled);
    result.radius = number("radius", result.radius, 512, 12000);
    result.maxRoofs = static_cast<unsigned>(number("maxroofs", static_cast<float>(result.maxRoofs), 1, 128));
    result.candidatesPerTick = static_cast<unsigned>(number("candidatespertick", static_cast<float>(result.candidatesPerTick), 1, 4));
    result.sampleResolution = static_cast<unsigned>(number("sampleresolution", static_cast<float>(result.sampleResolution), 5, 13));
    result.maxTriangles = static_cast<unsigned>(number("maxtriangles", static_cast<float>(result.maxTriangles), 100, 200000));
    result.scanSeconds = number("scanseconds", result.scanSeconds, .5F, 20);
    result.buildupSeconds = number("buildupseconds", result.buildupSeconds, .1F, 120);
    result.drainSeconds = number("drainseconds", result.drainSeconds, .1F, 120);
    result.topWidth = number("topwidth", result.topWidth, 1, 48);
    result.opacity = number("opacity", result.opacity, .01F, 1);
    result.scrollSpeed = number("scrollspeed", result.scrollSpeed, .1F, 10);
    result.maxDrop = number("maxdrop", result.maxDrop, 256, 8192);
    result.cornerOutset = number("corneroutset", result.cornerOutset, 1, 64);
    result.minimumSpan = number("minimumspan", result.minimumSpan, 64, 1024);
    result.requireSky = boolean("requiresky", result.requireSky);
    result.collisionFallback = boolean("collisionfallback", result.collisionFallback);
    result.debugLog = boolean("debuglog", result.debugLog);
    if (values.contains("includetokens")) result.includeTokens = tokens(values.at("includetokens"));
    if (values.contains("excludetokens")) result.excludeTokens = tokens(values.at("excludetokens"));
    if (values.contains("effectmodel") && !values.at("effectmodel").empty()) result.model = values.at("effectmodel");
    return result;
}
bool Settings::accepts(const std::string& modelPath) const {
    const auto s = lower(modelPath);
    if (s.empty() || s.find("rainrunoff") != std::string::npos) return false;
    for (const auto& token : excludeTokens) if (s.find(token) != std::string::npos) return false;
    if (includeTokens.empty()) return true;
    for (const auto& token : includeTokens) if (s.find(token) != std::string::npos) return true;
    return false;
}
}
