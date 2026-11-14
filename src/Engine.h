#pragma once

#include <RE/Skyrim.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Every function in this file is for the Skyrim main thread only.
namespace RainRunoff::Engine
{
    struct Triangle
    {
        RE::NiPoint3 a;
        RE::NiPoint3 b;
        RE::NiPoint3 c;
        bool roofMaterial{};
    };

    struct MeshReadResult
    {
        std::vector<Triangle> triangles;
        std::size_t shapesRead{};
        std::size_t shapesWithoutCPUData{};
        bool budgetExceeded{};
    };

    struct Hit
    {
        RE::NiPoint3 point;
        RE::NiPoint3 normal;
        RE::TESObjectREFR* ref{};  // Borrowed; do not retain between tasks.
        RE::COL_LAYER layer{ RE::COL_LAYER::kUnidentified };
    };

    // Raw CPU data can be discarded after upload. Do not map the GPU or assume
    // rawVertexData is present; use collision sampling for those references.
    MeshReadResult CollectTriangles(RE::TESObjectREFR& ref, std::size_t maxTriangles = 60000);

    // Returns the first static/terrain/water hit. Transient actor and clutter
    // collisions are skipped so a passing actor cannot become "ground".
    std::optional<Hit> Raycast(RE::TESObjectCELL& cell,
        const RE::NiPoint3& from, const RE::NiPoint3& to);

    // Automatic fallback when the NIF's CPU vertex buffer was discarded.
    // Samples the transformed object bounds, accepts only this ref's upward
    // collision faces, and triangulates complete grid cells.
    std::vector<Triangle> CollectRoofSamples(RE::TESObjectREFR& ref,
        RE::TESObjectCELL& cell, std::size_t resolution = 9);

    std::string ModelPath(RE::TESObjectREFR& ref);
    bool HasRoofMaterialName(RE::BSGeometry& geometry);

    class Effect
    {
    public:
        static std::unique_ptr<Effect> Create(RE::NiNode& parent,
            const char* modelPath, const RE::NiPoint3& worldPosition,
            float length, float nativeLength = 1.0F, float nativeWidth = 0.1F);

        Effect(const Effect&) = delete;
        Effect& operator=(const Effect&) = delete;
        ~Effect();

        // Width is in world game units. Source texture borders must have zero
        // alpha and clamp in U, wrap in V. The shader stays an ordinary effect
        // shader; no custom HLSL or Community Shaders API is required.
        void Update(float width, float opacity, float elapsedSeconds, float scrollSpeed);
        void Detach();
        RE::NiAVObject* Get3D() const { return root_.get(); }

    private:
        struct Material
        {
            RE::BSShaderProperty* property{};
            RE::BSEffectShaderMaterial* material{};
            float originalAlpha{ 1.0F };
            float vScale{ 1.0F };
        };

        Effect() = default;
        RE::NiPointer<RE::NiNode> parent_;
        RE::NiPointer<RE::NiAVObject> root_;
        std::vector<Material> materials_;
        RE::NiTransform worldTransform_;
        float fullWidth_{};
    };
}
