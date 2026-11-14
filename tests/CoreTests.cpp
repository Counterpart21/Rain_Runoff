#include "runoff/Core.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

namespace
{
    int checks = 0;
    void expect(bool condition, const char* message)
    {
        ++checks;
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }
    bool near(float a, float b, float tolerance = 0.001F) { return std::abs(a - b) < tolerance; }
    runoff::Vec3 rotate(runoff::Vec3 p, float angle)
    {
        return {p.x * std::cos(angle) - p.y * std::sin(angle) + 500,
                p.x * std::sin(angle) + p.y * std::cos(angle) - 250, p.z + 300};
    }
    std::vector<runoff::Triangle> gable(float angle, int divisions)
    {
        std::vector<runoff::Triangle> result;
        // Roof footprint: 512 by 320. Both eaves are at z=0, ridge z=160.
        // Subdivide along the eave to check seam welding and edge stitching.
        for (int i = 0; i < divisions; ++i) {
            const float x0 = -256 + 512.0F * static_cast<float>(i) / static_cast<float>(divisions);
            const float x1 = -256 + 512.0F * static_cast<float>(i + 1) / static_cast<float>(divisions);
            const auto a = rotate({x0, -160, 0}, angle);
            const auto b = rotate({x1, -160, 0}, angle);
            const auto c = rotate({x1, 0, 160}, angle);
            const auto d = rotate({x0, 0, 160}, angle);
            const auto e = rotate({x0, 160, 0}, angle);
            const auto f = rotate({x1, 160, 0}, angle);
            result.push_back({a, b, c});
            result.push_back({a, c, d});
            result.push_back({d, c, f});
            result.push_back({d, f, e});
        }
        return result;
    }
    void expectSamePair(const std::optional<runoff::RoofPair>& actual, const runoff::RoofPair& expected,
                        float angle, int segments, int permutation)
    {
        ++checks;
        bool same = actual.has_value();
        for (std::size_t i = 0; same && i < expected.size(); ++i) {
            const auto& a = (*actual)[i];
            const auto& b = expected[i];
            same = near(a.position.x, b.position.x) && near(a.position.y, b.position.y) && near(a.position.z, b.position.z) &&
                   near(a.outward.x, b.outward.x) && near(a.outward.y, b.outward.y) && near(a.outward.z, b.outward.z);
        }
        if (!same) {
            std::cerr << "FAIL: eave/endpoint selection changed after permutation; angle=" << angle
                      << " segments=" << segments << " permutation=" << permutation << '\n';
            if (actual) {
                for (std::size_t i = 0; i < expected.size(); ++i) {
                    std::cerr << "  endpoint " << i << " actual=" << (*actual)[i].position.x << ',' << (*actual)[i].position.y
                              << ',' << (*actual)[i].position.z << " expected=" << expected[i].position.x << ','
                              << expected[i].position.y << ',' << expected[i].position.z << '\n';
                }
            }
            std::exit(1);
        }
    }
}

int main()
{
    using namespace runoff;
    std::mt19937 shuffle(137);
    for (const auto angle : {0.0F, 0.37F, 1.5707953F, 1.5707963F, 1.5707973F, 2.31F,
                             3.1415916F, 3.1415927F, 3.1415937F, 4.7123880F, 4.7123890F, 4.7123900F}) {
        for (const int segments : {1, 2, 8, 32}) {
            auto roof = gable(angle, segments);
            auto corners = detectCorners(roof);
            expect(corners.has_value(), "gable detected at arbitrary orientation/subdivision");
            if (!corners) return 1;
            const auto& a = (*corners)[0];
            const auto& b = (*corners)[1];
            expect(near(std::hypot(a.position.x - b.position.x, a.position.y - b.position.y), 512, 0.01F), "full eave selected, not one triangle");
            expect(near(a.position.z, 300), "emission starts at low eave, not ridge");
            expect(near(b.position.z, 300), "both emitters at eave height");
            expect(near(std::hypot(a.outward.x, a.outward.y), 1), "outward vector is normalized");
            expect(near(a.outward.x, b.outward.x) && near(a.outward.y, b.outward.y), "emitters offset to same outside of eave");
            const auto previous = *corners;
            std::reverse(roof.begin(), roof.end());
            corners = detectCorners(roof);
            expectSamePair(corners, previous, angle, segments, -1);
            for (int permutation = 0; permutation < 16; ++permutation) {
                std::shuffle(roof.begin(), roof.end(), shuffle);
                for (std::size_t i = 0; i < roof.size(); ++i) {
                    auto& t = roof[i];
                    // Change each triangle's starting index while preserving
                    // winding, as equivalent NIF index buffers commonly do.
                    if ((i + static_cast<std::size_t>(permutation)) % 3 == 1) t = {t.b, t.c, t.a};
                    else if ((i + static_cast<std::size_t>(permutation)) % 3 == 2) t = {t.c, t.a, t.b};
                }
                expectSamePair(detectCorners(roof), previous, angle, segments, permutation);
            }
        }
    }
    const std::vector<Triangle> table{{{0,0,0},{50,0,0},{50,50,0}}, {{0,0,0},{50,50,0},{0,50,0}}};
    expect(!detectCorners(table), "small table does not qualify as roof");
    expect(!detectCorners({}), "empty geometry rejected");
    auto corrupt = gable(0, 1);
    corrupt.push_back({{NAN,0,0},{0,1,0},{0,0,1}});
    expect(detectCorners(corrupt).has_value(), "invalid triangle skipped without rejecting usable roof");
    for (auto& t : corrupt) std::swap(t.a, t.c);
    expect(!detectCorners(corrupt), "downward geometry rejected");
    DetectionSettings invalid;
    invalid.weldTolerance = 0;
    expect(!detectCorners(gable(0,1), invalid), "invalid detector settings rejected");
    std::vector<Triangle> flat{{{0,0,100},{512,0,100},{512,320,100}}, {{0,0,100},{512,320,100},{0,320,100}}};
    expect(detectCorners(flat).has_value(), "large flat roof supported");

    FlowEnvelope flow;
    expect(near(flow.update(1, 2, 8, 12), 0.25F), "rain grows width gradually");
    expect(near(flow.update(1, 6, 8, 12), 1), "buildup reaches full width at configured time");
    expect(near(flow.update(0, 3, 8, 12), 0.75F), "rain stop shrinks stream width");
    expect(near(flow.update(0, 9, 8, 12), 0), "drain reaches exact zero");
    expect(near(flow.update(1, 1.0e30F, 8, 12), 1), "large dt does not overshoot");
    expect(near(flow.update(0, NAN, 8, 12), 1), "invalid dt does not advance");
    expect(near(flow.update(0, -1, 8, 12), 1), "negative dt does not reverse flow");
    expect(near(flow.update(0, 1, 8, 0), 0), "zero drain time snaps to dry");
    flow.value = NAN;
    expect(near(flow.update(0, 1, 8, 12), 0), "invalid prior state recovered");
    expect(near(flow.update(0.5F, 100, 8, 12), 0.5F), "partial rain target limits width");
    expect(near(rainIntensity(0, 1, 0.25F), 0.25F), "dry to rainy transition interpolates");
    expect(near(rainIntensity(1, 0, 0.75F), 0.25F), "rain to dry transition interpolates");
    expect(near(rainIntensity(0, 0, 0.5F), 0), "snow or dry weather contributes no rain");
    expect(near(rainIntensity(1, 1, 0.5F), 1), "rain to rain stays wet");
    expect(near(rainIntensity(-1, 10, 2), 1), "invalid weather intensities clamped");

    const auto full = widthUV(1);
    expect(near(full.scale, 1) && near(full.offset, 0), "full flow has identity UVs");
    const auto half = widthUV(0.5F);
    expect(near(half.scale, 2) && near(half.offset, -0.5F), "half flow shrinks alpha mask around center");
    expect(near(0.5F * half.scale + half.offset, 0.5F), "UV center remains fixed");
    expect(std::isfinite(widthUV(0).scale) && std::isfinite(widthUV(NAN).scale), "dry and corrupt width cannot divide by zero");
    expect(!hasClearance(128, 128), "equal-to-player height rejected");
    expect(hasClearance(128.01F, 128), "strictly above player height accepted");
    expect(!hasClearance(NAN, 128) && !hasClearance(500, 0), "invalid clearance fails closed");
    std::cout << "Passed " << checks << " core checks.\n";
}
