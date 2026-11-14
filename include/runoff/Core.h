#pragma once

#include <array>
#include <optional>
#include <span>

namespace runoff
{
    struct Vec3
    {
        float x{}, y{}, z{};
    };

    struct Triangle
    {
        Vec3 a, b, c;
    };

    struct Corner
    {
        Vec3 position;
        Vec3 outward;  // Unit horizontal vector, away from the selected eave.
    };

    using RoofPair = std::array<Corner, 2>;

    struct DetectionSettings
    {
        float minFootprint = 128.0F;
        float minEaveSpan = 128.0F;
        float minNormalZ = 0.2F;
        float maxHeightSpan = 2048.0F;
        float weldTolerance = 0.5F;
        float maxEaveSlope = 0.08F;
        float cornerOutset = 12.0F;
    };

    // Heuristic: find a long, low, horizontal boundary of an upward-facing
    // surface. Collinear, vertex-connected edge subdivisions are joined.
    // Normals use triangle winding; callers must preserve the NIF winding.
    // This does not determine sky exposure or ground clearance.
    [[nodiscard]] std::optional<RoofPair> detectCorners(
        std::span<const Triangle> triangles,
        const DetectionSettings& settings = {});

    struct FlowEnvelope
    {
        float value = 0.0F;

        // Times specify a full 0-to-1 / 1-to-0 transition in real seconds.
        // A zero duration is immediate. Invalid/nonpositive dt does not advance.
        float update(float target, float dt, float buildupSeconds, float drainSeconds);
        void reset() noexcept { value = 0.0F; }
    };

    [[nodiscard]] float rainIntensity(float outgoingRain, float currentRain, float transition);

    struct WidthUV
    {
        float scale;
        float offset;
    };

    // For an alpha ribbon using clamped U coordinates, centered at U=0.5.
    [[nodiscard]] WidthUV widthUV(float flow);
    [[nodiscard]] bool hasClearance(float clearance, float playerHeight);
}
