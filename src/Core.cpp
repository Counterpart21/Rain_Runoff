#include "runoff/Core.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace runoff
{
    namespace
    {
        constexpr float epsilon = 0.0001F;
        float unit(float value)
        {
            return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
        }

        Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
        Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
        Vec3 mul(Vec3 a, float scale) { return {a.x * scale, a.y * scale, a.z * scale}; }
        float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
        Vec3 cross(Vec3 a, Vec3 b)
        {
            return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        }
        float xyLength(Vec3 v) { return std::hypot(v.x, v.y); }
        bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

        struct Key
        {
            std::int64_t x, y, z;
            bool operator==(const Key&) const = default;
            bool operator<(const Key& other) const noexcept
            {
                return std::tie(x, y, z) < std::tie(other.x, other.y, other.z);
            }
        };
        struct KeyHash
        {
            std::size_t operator()(const Key& p) const noexcept
            {
                auto result = std::hash<std::int64_t>{}(p.x);
                result ^= std::hash<std::int64_t>{}(p.y) + 0x9e3779b9U + (result << 6U) + (result >> 2U);
                result ^= std::hash<std::int64_t>{}(p.z) + 0x9e3779b9U + (result << 6U) + (result >> 2U);
                return result;
            }
        };
        struct EdgeKey
        {
            std::size_t a, b;
            bool operator==(const EdgeKey&) const = default;
        };
        struct EdgeHash
        {
            std::size_t operator()(EdgeKey edge) const noexcept
            {
                return edge.a ^ (edge.b + 0x9e3779b9U + (edge.a << 6U) + (edge.a >> 2U));
            }
        };
        struct Edge
        {
            std::size_t a{}, b{};
            unsigned count{};
            bool sloped{};
        };
        struct DisjointSet
        {
            explicit DisjointSet(std::size_t count) : parent(count), rank(count)
            {
                std::iota(parent.begin(), parent.end(), 0U);
            }
            std::size_t find(std::size_t value)
            {
                while (parent[value] != value) {
                    parent[value] = parent[parent[value]];
                    value = parent[value];
                }
                return value;
            }
            void join(std::size_t a, std::size_t b)
            {
                a = find(a);
                b = find(b);
                if (a == b) return;
                if (rank[a] < rank[b]) std::swap(a, b);
                parent[b] = a;
                if (rank[a] == rank[b]) ++rank[a];
            }
            std::vector<std::size_t> parent;
            std::vector<unsigned> rank;
        };
    }

    std::optional<RoofPair> detectCorners(std::span<const Triangle> triangles, const DetectionSettings& cfg)
    {
        if (!std::isfinite(cfg.minFootprint) || cfg.minFootprint <= 0 ||
            !std::isfinite(cfg.minEaveSpan) || cfg.minEaveSpan <= 0 ||
            !std::isfinite(cfg.minNormalZ) || cfg.minNormalZ <= 0 || cfg.minNormalZ > 1 ||
            !std::isfinite(cfg.maxHeightSpan) || cfg.maxHeightSpan <= 0 ||
            !std::isfinite(cfg.weldTolerance) || cfg.weldTolerance < 0.001F ||
            !std::isfinite(cfg.maxEaveSlope) || cfg.maxEaveSlope < 0 ||
            !std::isfinite(cfg.cornerOutset) || cfg.cornerOutset < 0) return std::nullopt;

        const auto keyFor = [&](Vec3 point) -> Key {
            return {std::llround(static_cast<double>(point.x) / cfg.weldTolerance),
                    std::llround(static_cast<double>(point.y) / cfg.weldTolerance),
                    std::llround(static_cast<double>(point.z) / cfg.weldTolerance)};
        };
        // Exact comparisons make this a strict order, including coordinates
        // falling into the same weld cell. Never use an epsilon in std::sort.
        const auto pointBefore = [&](Vec3 a, Vec3 b) {
            const auto ak = keyFor(a), bk = keyFor(b);
            return ak == bk ? std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z) : ak < bk;
        };
        std::vector<Triangle> ordered;
        ordered.reserve(triangles.size());
        for (auto tri : triangles) {
            if (!finite(tri.a) || !finite(tri.b) || !finite(tri.c)) continue;
            // Protect quantization and cross products from corrupt mesh coordinates.
            if (std::max({std::abs(tri.a.x), std::abs(tri.a.y), std::abs(tri.a.z),
                          std::abs(tri.b.x), std::abs(tri.b.y), std::abs(tri.b.z),
                          std::abs(tri.c.x), std::abs(tri.c.y), std::abs(tri.c.z)}) > 1.0e8F) continue;
            // A cyclic permutation preserves winding and fixes the arithmetic
            // order even when a NIF stores the same triangle with another start.
            if (pointBefore(tri.b, tri.a) && pointBefore(tri.b, tri.c))
                tri = {tri.b, tri.c, tri.a};
            else if (pointBefore(tri.c, tri.a) && pointBefore(tri.c, tri.b))
                tri = {tri.c, tri.a, tri.b};
            ordered.push_back(tri);
        }
        std::sort(ordered.begin(), ordered.end(), [&](const Triangle& a, const Triangle& b) {
            for (const auto& [ap, bp] : {std::pair{a.a, b.a}, std::pair{a.b, b.b}, std::pair{a.c, b.c}}) {
                if (pointBefore(ap, bp)) return true;
                if (pointBefore(bp, ap)) return false;
            }
            return false;
        });

        std::vector<Vec3> points;
        std::unordered_map<Key, std::size_t, KeyHash> welded;
        std::unordered_map<EdgeKey, Edge, EdgeHash> edges;
        Vec3 minimum{INFINITY, INFINITY, INFINITY};
        Vec3 maximum{-INFINITY, -INFINITY, -INFINITY};
        double centerX = 0, centerY = 0, centerZ = 0;
        double totalArea = 0;
        const auto indexFor = [&](Vec3 point) -> std::size_t {
            const auto [iter, inserted] = welded.try_emplace(keyFor(point), points.size());
            if (inserted) points.push_back(point);
            return iter->second;
        };

        for (const auto& tri : ordered) {
            const auto normal = cross(sub(tri.b, tri.a), sub(tri.c, tri.a));
            const auto area2 = std::sqrt(dot(normal, normal));
            if (!std::isfinite(area2) || area2 < epsilon || normal.z / area2 < cfg.minNormalZ) continue;
            const bool sloped = normal.z / area2 < 0.98F;
            const auto indices = std::array{indexFor(tri.a), indexFor(tri.b), indexFor(tri.c)};
            for (auto p : {tri.a, tri.b, tri.c}) {
                minimum.x = std::min(minimum.x, p.x);
                minimum.y = std::min(minimum.y, p.y);
                minimum.z = std::min(minimum.z, p.z);
                maximum.x = std::max(maximum.x, p.x);
                maximum.y = std::max(maximum.y, p.y);
                maximum.z = std::max(maximum.z, p.z);
            }
            centerX += (static_cast<double>(tri.a.x) + tri.b.x + tri.c.x) * area2 / 3.0;
            centerY += (static_cast<double>(tri.a.y) + tri.b.y + tri.c.y) * area2 / 3.0;
            centerZ += (static_cast<double>(tri.a.z) + tri.b.z + tri.c.z) * area2 / 3.0;
            totalArea += area2;
            for (std::size_t i = 0; i < 3; ++i) {
                auto a = indices[i], b = indices[(i + 1) % 3];
                if (a == b) continue;
                if (a > b) std::swap(a, b);
                auto [it, inserted] = edges.try_emplace({a, b}, Edge{a, b, 0, false});
                ++it->second.count;
                it->second.sloped = it->second.sloped || sloped;
            }
        }
        if (totalArea < epsilon || !std::isfinite(centerX) || !std::isfinite(centerY) || !std::isfinite(centerZ) ||
            maximum.x - minimum.x < cfg.minFootprint ||
            maximum.y - minimum.y < cfg.minFootprint ||
            maximum.z - minimum.z > cfg.maxHeightSpan) return std::nullopt;
        const Vec3 center{static_cast<float>(centerX / totalArea), static_cast<float>(centerY / totalArea),
                          static_cast<float>(centerZ / totalArea)};

        std::vector<Edge> boundary;
        std::vector<Vec3> directions;
        std::vector<std::vector<std::size_t>> incident(points.size());
        for (const auto& [_, edge] : edges) {
            if (edge.count != 1) continue;
            const auto delta = sub(points[edge.b], points[edge.a]);
            const auto span = xyLength(delta);
            if (span < epsilon || std::abs(delta.z) / span > cfg.maxEaveSlope) continue;
            boundary.push_back(edge);
        }
        // IDs come from the canonical triangle traversal, so this removes the
        // implementation-dependent traversal order of unordered_map as well.
        std::sort(boundary.begin(), boundary.end(), [](const Edge& a, const Edge& b) {
            return std::tie(a.a, a.b) < std::tie(b.a, b.b);
        });
        for (std::size_t id = 0; id < boundary.size(); ++id) {
            const auto& edge = boundary[id];
            const auto delta = sub(points[edge.b], points[edge.a]);
            const auto span = xyLength(delta);
            directions.push_back({delta.x / span, delta.y / span, 0});
            incident[edge.a].push_back(id);
            incident[edge.b].push_back(id);
        }
        if (boundary.empty()) return std::nullopt;
        DisjointSet groups(boundary.size());
        for (const auto& neighbors : incident) {
            for (std::size_t i = 0; i < neighbors.size(); ++i) {
                for (std::size_t j = i + 1; j < neighbors.size(); ++j) {
                    if (std::abs(dot(directions[neighbors[i]], directions[neighbors[j]])) >= 0.998F)
                        groups.join(neighbors[i], neighbors[j]);
                }
            }
        }
        struct Line
        {
            bool populated = false;
            Vec3 origin{}, dir{}, first{}, last{};
            double low{}, high{}, score{};
            bool sloped = false;
        };
        std::vector<Line> lines(boundary.size());
        for (std::size_t i = 0; i < boundary.size(); ++i) {
            auto& line = lines[groups.find(i)];
            if (!line.populated) {
                line.populated = true;
                line.origin = points[boundary[i].a];
                line.dir = directions[i];
                line.first = line.last = line.origin;
            }
            line.sloped = line.sloped || boundary[i].sloped;
            for (auto p : {points[boundary[i].a], points[boundary[i].b]}) {
                const auto projection = (static_cast<double>(p.x) - line.origin.x) * line.dir.x +
                                        (static_cast<double>(p.y) - line.origin.y) * line.dir.y;
                if (projection < line.low) { line.low = projection; line.first = p; }
                if (projection > line.high) { line.high = projection; line.last = p; }
            }
        }
        const Line* selected = nullptr;
        double bestScore = -1;
        for (auto& line : lines) {
            if (!line.populated) continue;
            const auto span = std::hypot(static_cast<double>(line.last.x) - line.first.x,
                                         static_cast<double>(line.last.y) - line.first.y);
            if (span < cfg.minEaveSpan) continue;
            // The score favors eaves of sloped roofs over horizontal decks.
            // A ridge open at a material seam gets a penalty for elevation.
            const auto midpointZ = (static_cast<double>(line.first.z) + line.last.z) * 0.5;
            const auto relief = static_cast<double>(maximum.z) - minimum.z;
            const auto lowEave = relief > epsilon ? std::clamp((maximum.z - midpointZ) / relief, 0.0, 1.0) : 0.0;
            line.score = span * (line.sloped ? 2.0 : 1.0) * (1.0 + 0.5 * lowEave);
            bestScore = std::max(bestScore, line.score);
        }
        // Differences below the weld resolution do not indicate a meaningfully
        // longer eave. Find the global maximum first, then choose the earliest
        // spatial key among tied candidates. A pairwise epsilon comparator can
        // be nontransitive and used to depend on unordered traversal.
        const auto scoreTolerance = std::max(static_cast<double>(cfg.weldTolerance) * 0.5, bestScore * 1.0e-6);
        const auto lineKey = [&](const Line& line) {
            const auto mid = keyFor(mul(add(line.first, line.last), 0.5F));
            auto first = keyFor(line.first), last = keyFor(line.last);
            if (last < first) std::swap(first, last);
            return std::tuple{mid.x, mid.y, mid.z, first.x, first.y, first.z, last.x, last.y, last.z};
        };
        for (const auto& line : lines) {
            if (!line.populated || line.score <= 0 || bestScore - line.score > scoreTolerance) continue;
            if (!selected || lineKey(line) < lineKey(*selected)) selected = &line;
        }
        if (!selected) return std::nullopt;
        const auto mid = mul(add(selected->first, selected->last), 0.5F);
        const auto eave = sub(selected->last, selected->first);
        const auto span = xyLength(eave);
        Vec3 outward{-eave.y / span, eave.x / span, 0};
        if (dot(outward, sub(mid, center)) < 0) outward = mul(outward, -1.0F);
        auto first = selected->first, last = selected->last;
        // Stable endpoint order aids deterministic attachment/revalidation.
        if (pointBefore(last, first)) std::swap(first, last);
        return RoofPair{{{add(first, mul(outward, cfg.cornerOutset)), outward},
                         {add(last, mul(outward, cfg.cornerOutset)), outward}}};
    }

    float FlowEnvelope::update(float target, float dt, float buildupSeconds, float drainSeconds)
    {
        value = unit(value);
        target = unit(target);
        if (!std::isfinite(dt) || dt <= 0.0F) return value;
        const auto duration = target > value ? buildupSeconds : drainSeconds;
        if (!std::isfinite(duration) || duration <= 0.0F) return value = target;
        const auto step = dt / duration;
        value = target > value ? std::min(target, value + step) : std::max(target, value - step);
        return value;
    }

    float rainIntensity(float outgoingRain, float currentRain, float transition)
    {
        const auto blend = unit(transition);
        return unit(outgoingRain) * (1.0F - blend) + unit(currentRain) * blend;
    }

    WidthUV widthUV(float flow)
    {
        const auto scale = 1.0F / std::max(unit(flow), 0.001F);
        return {scale, (1.0F - scale) * 0.5F};
    }

    bool hasClearance(float clearance, float playerHeight)
    {
        return std::isfinite(clearance) && std::isfinite(playerHeight) &&
            playerHeight > 0.0F && clearance > playerHeight;
    }
}
