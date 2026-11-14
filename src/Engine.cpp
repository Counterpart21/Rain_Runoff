#include "Engine.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace RainRunoff::Engine
{
    namespace
    {
        std::string Lower(std::string_view text)
        {
            std::string result(text);
            for (char& c : result) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c + ('a' - 'A'));
                }
            }
            return result;
        }

        bool RoofName(std::string_view name)
        {
            auto lower = Lower(name);
            constexpr std::array words{ "roof", "thatch", "shingle", "slate" };
            return std::ranges::any_of(words, [&](auto word) {
                return lower.find(word) != std::string::npos;
            });
        }

        float HalfToFloat(std::uint16_t h)
        {
            const auto sign = static_cast<std::uint32_t>(h & 0x8000U) << 16U;
            std::uint32_t exponent = (h >> 10U) & 0x1FU;
            std::uint32_t mantissa = h & 0x3FFU;
            if (exponent == 0) {
                if (mantissa == 0) {
                    return std::bit_cast<float>(sign);
                }
                int e = -14;
                while ((mantissa & 0x400U) == 0) {
                    mantissa <<= 1U;
                    --e;
                }
                mantissa &= 0x3FFU;
                return std::bit_cast<float>(sign |
                    (static_cast<std::uint32_t>(e + 127) << 23U) | (mantissa << 13U));
            }
            if (exponent == 31) {
                return std::bit_cast<float>(sign | 0x7F800000U | (mantissa << 13U));
            }
            return std::bit_cast<float>(sign | ((exponent + 112U) << 23U) | (mantissa << 13U));
        }

        bool Finite(const RE::NiPoint3& p)
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }

        RE::NiPoint3 Position(const std::uint8_t* data, bool fullPrecision)
        {
            RE::NiPoint3 result;
            if (fullPrecision) {
                std::memcpy(&result, data, sizeof(result));
            } else {
                std::array<std::uint16_t, 3> packed;
                std::memcpy(packed.data(), data, sizeof(packed));
                result = { HalfToFloat(packed[0]), HalfToFloat(packed[1]), HalfToFloat(packed[2]) };
            }
            return result;
        }

        bool EligibleLayer(RE::COL_LAYER layer)
        {
            switch (layer) {
            case RE::COL_LAYER::kStatic:
            case RE::COL_LAYER::kAnimStatic:
            case RE::COL_LAYER::kTerrain:
            case RE::COL_LAYER::kGround:
            case RE::COL_LAYER::kWater:
                return true;
            default:
                return false;
            }
        }

        bool IsUpward(const Triangle& triangle, float minimumZ = 0.05F)
        {
            auto normal = (triangle.b - triangle.a).Cross(triangle.c - triangle.a);
            const auto length = normal.Length();
            return length > 0.01F && normal.z / length > minimumZ;
        }
    }

    std::string ModelPath(RE::TESObjectREFR& ref)
    {
        auto* base = ref.GetBaseObject();
        if (!base) {
            return {};
        }
        if (auto* stat = base->As<RE::TESObjectSTAT>()) {
            const auto* path = stat->GetModel();
            return path ? path : "";
        }
        return {};
    }

    bool HasRoofMaterialName(RE::BSGeometry& geometry)
    {
        if (RoofName(geometry.name.c_str())) {
            return true;
        }
        const auto& data = geometry.GetGeometryRuntimeData();
        auto* property = data.properties[RE::BSGeometry::States::kEffect].get();
        auto* shader = property ? netimmerse_cast<RE::BSShaderProperty*>(property) : nullptr;
        if (!shader || !shader->material ||
            shader->material->GetType() != RE::BSShaderMaterial::Type::kLighting) {
            return false;
        }
        auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(shader->material);
        if (!material->textureSet) {
            return false;
        }
        const auto* diffuse = material->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
        return diffuse && RoofName(diffuse);
    }

    MeshReadResult CollectTriangles(RE::TESObjectREFR& ref, std::size_t maxTriangles)
    {
        MeshReadResult result;
        auto* root = ref.Get3D();
        if (!root) {
            return result;
        }
        RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* geometry) {
            auto* shape = geometry->AsTriShape();
            if (!shape || shape->AsDynamicTriShape() || geometry->GetAppCulled() ||
                Lower(geometry->name.c_str()).starts_with("rainrunoff")) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }
            const auto& data = geometry->GetGeometryRuntimeData();
            if (data.skinInstance) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }
            const auto* buffers = data.rendererData;
            if (!buffers || !buffers->rawVertexData || !buffers->rawIndexData) {
                ++result.shapesWithoutCPUData;
                return RE::BSVisit::BSVisitControl::kContinue;
            }
            const auto& counts = shape->GetTrishapeRuntimeData();
            if (counts.vertexCount == 0 || counts.triangleCount == 0 ||
                !buffers->vertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX)) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }
            if (result.triangles.size() + counts.triangleCount > maxTriangles) {
                result.budgetExceeded = true;
                return RE::BSVisit::BSVisitControl::kStop;
            }

            // The low nibble is the packed vertex stride in four-byte units.
            // VertexDesc::GetSize() assumes float positions and must not be
            // used for half-precision layouts. Positions always start at zero.
            std::uint64_t descriptor{};
            std::memcpy(&descriptor, &buffers->vertexDesc, sizeof(descriptor));
            const auto stride = static_cast<std::size_t>(descriptor & 0xFU) * 4U;
            // Skyrim SE's BS stream version 100 stores float positions even
            // when the FULLPREC descriptor bit is not set (unlike FO4).
            const bool fullPrecision = true;
            if (stride < (fullPrecision ? 12U : 6U) || stride > 60U) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }
            const bool roof = HasRoofMaterialName(*geometry);
            for (std::size_t i = 0; i < counts.triangleCount; ++i) {
                const auto a = buffers->rawIndexData[i * 3U];
                const auto b = buffers->rawIndexData[i * 3U + 1U];
                const auto c = buffers->rawIndexData[i * 3U + 2U];
                if (a >= counts.vertexCount || b >= counts.vertexCount || c >= counts.vertexCount) {
                    continue;
                }
                Triangle triangle{
                    geometry->world * Position(buffers->rawVertexData + a * stride, fullPrecision),
                    geometry->world * Position(buffers->rawVertexData + b * stride, fullPrecision),
                    geometry->world * Position(buffers->rawVertexData + c * stride, fullPrecision),
                    roof
                };
                if (Finite(triangle.a) && Finite(triangle.b) && Finite(triangle.c) && IsUpward(triangle)) {
                    result.triangles.push_back(triangle);
                }
            }
            ++result.shapesRead;
            return RE::BSVisit::BSVisitControl::kContinue;
        });
        return result;
    }

    std::optional<Hit> Raycast(RE::TESObjectCELL& cell,
        const RE::NiPoint3& from, const RE::NiPoint3& to)
    {
        auto* world = cell.GetbhkWorld();
        if (!world || !Finite(from) || !Finite(to)) {
            return std::nullopt;
        }
        const auto direction = to - from;
        const auto length = direction.Length();
        if (length < 0.1F) {
            return std::nullopt;
        }
        const auto unitDirection = direction / length;
        const auto scale = RE::bhkWorld::GetWorldScale();
        RE::NiPoint3 start = from;

        // PickObject is a game virtual and accepts Havok-space endpoints.
        // Lock here, on the game thread, while resolving hit owners.
        const RE::BSReadLockGuard lock(world->worldLock);
        for (int skipped = 0; skipped < 12; ++skipped) {
            RE::bhkPickData pick{};
            pick.rayInput.from = RE::hkVector4(start * scale);
            pick.rayInput.to = RE::hkVector4(to * scale);
            pick.rayInput.filterInfo = static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);
            pick.rayInput.enableShapeCollectionFilter = true;
            world->PickObject(pick);
            if (!pick.rayOutput.HasHit()) {
                return std::nullopt;
            }
            const auto fraction = std::clamp(pick.rayOutput.hitFraction, 0.0F, 1.0F);
            const auto point = start + (to - start) * fraction;
            const auto* collidable = pick.rayOutput.rootCollidable;
            if (!collidable) {
                return std::nullopt;
            }
            const auto layer = collidable->GetCollisionLayer();
            if (EligibleLayer(layer)) {
                alignas(16) std::array<float, 4> normal;
                _mm_store_ps(normal.data(), pick.rayOutput.normal.quad);
                return Hit{ point, { normal[0], normal[1], normal[2] },
                    RE::TESHavokUtilities::FindCollidableRef(*collidable), layer };
            }
            // A static-only ground query must not stop on temporary actors or
            // dropped objects. Move beyond rejected hits and query again.
            start = point + unitDirection * 2.0F;
            if ((to - start).Dot(unitDirection) <= 0.0F) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::vector<Triangle> CollectRoofSamples(RE::TESObjectREFR& ref,
        RE::TESObjectCELL& cell, std::size_t resolution)
    {
        std::vector<Triangle> triangles;
        auto* root = ref.Get3D();
        auto* base = ref.GetBaseObject();
        if (!root || !base) {
            return triangles;
        }
        resolution = std::clamp<std::size_t>(resolution, 3U, 17U);
        const auto& bounds = base->boundData;
        RE::NiPoint3 minimum{ (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)() };
        RE::NiPoint3 maximum{ -(std::numeric_limits<float>::max)(),
            -(std::numeric_limits<float>::max)(), -(std::numeric_limits<float>::max)() };
        for (unsigned i = 0; i < 8; ++i) {
            const RE::NiPoint3 local{
                static_cast<float>((i & 1U) ? bounds.boundMax.x : bounds.boundMin.x),
                static_cast<float>((i & 2U) ? bounds.boundMax.y : bounds.boundMin.y),
                static_cast<float>((i & 4U) ? bounds.boundMax.z : bounds.boundMin.z)
            };
            const auto world = root->world * local;
            minimum.x = (std::min)(minimum.x, world.x);
            minimum.y = (std::min)(minimum.y, world.y);
            minimum.z = (std::min)(minimum.z, world.z);
            maximum.x = (std::max)(maximum.x, world.x);
            maximum.y = (std::max)(maximum.y, world.y);
            maximum.z = (std::max)(maximum.z, world.z);
        }
        if (!Finite(minimum) || !Finite(maximum) ||
            maximum.x - minimum.x < 32.0F || maximum.y - minimum.y < 32.0F) {
            return triangles;
        }
        bool namedRoof = RoofName(ModelPath(ref));
        RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* geometry) {
            namedRoof = namedRoof || HasRoofMaterialName(*geometry);
            return RE::BSVisit::BSVisitControl::kContinue;
        });
        std::vector<std::optional<RE::NiPoint3>> points(resolution * resolution);
        for (std::size_t y = 0; y < resolution; ++y) {
            for (std::size_t x = 0; x < resolution; ++x) {
                // Keep the grid aligned to the model, rather than the world
                // AABB. Otherwise rotated eaves turn into stair-step borders
                // whose edge heights vary and cannot be recognized as eaves.
                const auto localX = std::lerp(static_cast<float>(bounds.boundMin.x) + 1.0F,
                    static_cast<float>(bounds.boundMax.x) - 1.0F,
                    static_cast<float>(x) / static_cast<float>(resolution - 1U));
                const auto localY = std::lerp(static_cast<float>(bounds.boundMin.y) + 1.0F,
                    static_cast<float>(bounds.boundMax.y) - 1.0F,
                    static_cast<float>(y) / static_cast<float>(resolution - 1U));
                const auto localZ = 0.5F * (static_cast<float>(bounds.boundMin.z) +
                    static_cast<float>(bounds.boundMax.z));
                const auto footprint = root->world * RE::NiPoint3{ localX, localY, localZ };
                auto hit = Raycast(cell, { footprint.x, footprint.y, maximum.z + 64.0F },
                    { footprint.x, footprint.y, minimum.z - 16.0F });
                if (hit && hit->ref == &ref && hit->normal.z > 0.15F) {
                    points[y * resolution + x] = hit->point;
                }
            }
        }
        const float maximumStep = (std::max)(maximum.x - minimum.x, maximum.y - minimum.y) /
            static_cast<float>(resolution) * 2.5F;
        auto add = [&](const auto& a, const auto& b, const auto& c) {
            if (!a || !b || !c) {
                return;
            }
            if (std::abs(a->z - b->z) > maximumStep ||
                std::abs(b->z - c->z) > maximumStep ||
                std::abs(c->z - a->z) > maximumStep) {
                return;
            }
            Triangle triangle{ *a, *b, *c, namedRoof };
            if (IsUpward(triangle, 0.15F)) {
                triangles.push_back(triangle);
            }
        };
        for (std::size_t y = 0; y + 1 < resolution; ++y) {
            for (std::size_t x = 0; x + 1 < resolution; ++x) {
                const auto& a = points[y * resolution + x];
                const auto& b = points[y * resolution + x + 1];
                const auto& c = points[(y + 1) * resolution + x];
                const auto& d = points[(y + 1) * resolution + x + 1];
                add(a, b, c);
                add(b, d, c);
            }
        }
        return triangles;
    }

    std::unique_ptr<Effect> Effect::Create(RE::NiNode& parent,
        const char* modelPath, const RE::NiPoint3& worldPosition,
        float length, float nativeLength, float nativeWidth)
    {
        if (!modelPath || !std::isfinite(length) || !std::isfinite(nativeLength) ||
            !std::isfinite(nativeWidth) || !std::isfinite(parent.world.scale) ||
            length <= 0.0F || nativeLength <= 0.0F || nativeWidth <= 0.0F ||
            !Finite(worldPosition) || parent.world.scale <= 0.0F) {
            return nullptr;
        }
        RE::NiPointer<RE::NiNode> prototype;
        if (RE::BSModelDB::Demand(modelPath, prototype, {}) != RE::BSResource::ErrorCode::kNone || !prototype) {
            return nullptr;
        }
        std::unordered_set<RE::NiProperty*> prototypeProperties;
        RE::BSVisit::TraverseScenegraphGeometries(prototype.get(), [&](RE::BSGeometry* geometry) {
            for (auto& property : geometry->GetGeometryRuntimeData().properties) {
                if (property) {
                    prototypeProperties.insert(property.get());
                }
            }
            return RE::BSVisit::BSVisitControl::kContinue;
        });

        // A normal NiAVObject clone may share shader properties. Deep copy is
        // used for this tiny two-ribbon asset so mutations never change the
        // cached model or another runoff instance.
        RE::NiPointer<RE::NiObject> copiedObject;
        prototype->CreateDeepCopy(copiedObject);
        auto* copiedRoot = copiedObject ? copiedObject->AsNode() : nullptr;
        if (!copiedRoot) {
            return nullptr;
        }
        auto effect = std::unique_ptr<Effect>(new Effect);
        effect->parent_.reset(&parent);
        effect->root_.reset(copiedRoot);
        bool valid = true;
        RE::BSVisit::TraverseScenegraphGeometries(copiedRoot, [&](RE::BSGeometry* geometry) {
            auto* property = geometry->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect].get();
            auto* shader = property ? netimmerse_cast<RE::BSShaderProperty*>(property) : nullptr;
            if (!shader || !shader->material || prototypeProperties.contains(property) ||
                shader->material->GetType() != RE::BSShaderMaterial::Type::kEffect) {
                valid = false;
                return RE::BSVisit::BSVisitControl::kStop;
            }
            // true requests a material copy, as used with temporary source
            // materials by the game's shader property API.
            std::unique_ptr<RE::BSShaderMaterial> independent(shader->material->Create());
            if (!independent) {
                valid = false;
                return RE::BSVisit::BSVisitControl::kStop;
            }
            independent->CopyMembers(shader->material);
            shader->SetMaterial(independent.get(), true);
            auto* material = static_cast<RE::BSEffectShaderMaterial*>(shader->material);
            material->effectClampMode = RE::BSGraphics::TextureAddressMode::kClampSWrapT;
            effect->materials_.push_back({ shader, material, material->baseColor.alpha,
                material->texCoordScale[0].y });
            shader->SetupGeometry(geometry);
            return RE::BSVisit::BSVisitControl::kContinue;
        });
        if (!valid || effect->materials_.empty()) {
            return nullptr;
        }
        effect->worldTransform_.translate = worldPosition;
        effect->worldTransform_.scale = length / nativeLength;
        effect->fullWidth_ = nativeWidth * effect->worldTransform_.scale;
        effect->root_->name = "RainRunoff_Runtime";
        effect->root_->local = parent.world.Invert() * effect->worldTransform_;
        effect->root_->SetAppCulled(true);
        parent.AttachChild(effect->root_.get(), true);
        return effect;
    }

    Effect::~Effect()
    {
        Detach();
    }

    void Effect::Detach()
    {
        if (root_ && parent_ && root_->parent == parent_.get()) {
            parent_->DetachChild(root_.get());
        }
        materials_.clear();
        root_.reset();
        parent_.reset();
    }

    void Effect::Update(float width, float opacity, float elapsedSeconds, float scrollSpeed)
    {
        if (!root_ || !parent_) {
            return;
        }
        const bool visible = std::isfinite(width) && std::isfinite(opacity) && width > 0.01F && opacity > 0.001F;
        root_->SetAppCulled(!visible);
        if (!visible) {
            return;
        }
        const auto uScale = std::clamp(fullWidth_ / width, 1.0F, 10000.0F);
        const auto phase = std::isfinite(elapsedSeconds) && std::isfinite(scrollSpeed)
            ? std::fmod(elapsedSeconds * scrollSpeed, 1.0F) : 0.0F;
        for (auto& entry : materials_) {
            entry.material->baseColor.alpha = entry.originalAlpha * std::clamp(opacity, 0.0F, 1.0F);
            for (unsigned i = 0; i < 2; ++i) {
                entry.material->texCoordScale[i].x = uScale;
                entry.material->texCoordScale[i].y = entry.vScale;
                entry.material->texCoordOffset[i].x = 0.5F - 0.5F * uScale;
                // U increases horizontally, V increases toward the bottom of
                // the ribbon. A negative sample offset moves features down.
                entry.material->texCoordOffset[i].y = -phase;
            }
        }
        root_->local = parent_->world.Invert() * worldTransform_;
        RE::NiUpdateData update{};
        update.time = elapsedSeconds;
        update.flags = RE::NiUpdateData::Flag::kDirty;
        root_->Update(update);
    }
}
