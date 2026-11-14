#include "Runtime.h"
#include <SKSE/SKSE.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <unordered_set>

namespace runoff {
namespace {
namespace engine = RainRunoff::Engine;
float distanceSquared(const RE::NiPoint3& a, const RE::NiPoint3& b) {
    const auto d = a - b;
    return d.x*d.x + d.y*d.y + d.z*d.z;
}
Vec3 vec(const RE::NiPoint3& p) { return {p.x, p.y, p.z}; }
RE::NiPoint3 point(const Vec3& p) { return {p.x, p.y, p.z}; }
float standingHeight(RE::PlayerCharacter& player) {
    // Use TESObjectREFR's standing bounds, not Actor::GetHeight's current
    // movement controller/crouching height. This includes race/reference scale.
    const auto height = static_cast<const RE::TESObjectREFR&>(player).GetHeight();
    return std::isfinite(height) && height > 16.0F ? height : 128.0F;
}
float rainValue() {
    const auto* sky = RE::Sky::GetSingleton();
    if (!sky || sky->mode != RE::Sky::Mode::kFull) return 0;
    auto rainy = [](const RE::TESWeather* weather) {
        return weather && weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy) &&
            !weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow) ? 1.0F : 0.0F;
    };
    // Respect precipitation start/end thresholds as well as the classification.
    // The on-disk fade bytes are unsigned even though RE headers expose int8_t.
    const float transition = std::clamp(sky->currentWeatherPct, 0.0F, 1.0F);
    const bool incoming = rainy(sky->currentWeather) > 0 && transition >
        static_cast<std::uint8_t>(sky->currentWeather->data.precipitationBeginFadeIn) / 255.0F;
    const bool outgoing = rainy(sky->lastWeather) > 0 && transition <
        static_cast<std::uint8_t>(sky->lastWeather->data.precipitationEndFadeOut) / 255.0F + .001F;
    if (!incoming && !outgoing) return 0;
    // Blend the two rainy classifications after the precipitation gate.
    return rainIntensity(rainy(sky->lastWeather ? sky->lastWeather : sky->currentWeather),
        rainy(sky->currentWeather), sky->currentWeatherPct);
}
std::vector<Triangle> portable(const std::vector<engine::Triangle>& triangles, bool roofOnly = false) {
    std::vector<Triangle> result;
    result.reserve(triangles.size());
    for (const auto& t : triangles) {
        if (!roofOnly || t.roofMaterial) result.push_back({vec(t.a),vec(t.b),vec(t.c)});
    }
    return result;
}
}
Runtime& Runtime::Get() {
    // SKSE plugins are process-lifetime modules. Avoid running engine-bound
    // destructors after Skyrim has already destroyed its allocator/scene graph.
    static auto* instance = new Runtime;
    return *instance;
}
void Runtime::Start() {
    settings_ = Settings::Load();
    auto* tasks = SKSE::GetTaskInterface();
    if (!tasks || worker_.joinable() || !settings_.enabled) return;
    SKSE::log::info("Radius {}, maximum roofs {}, build/drain {}/{} seconds", settings_.radius,
        settings_.maxRoofs, settings_.buildupSeconds, settings_.drainSeconds);
    worker_ = std::jthread([this, tasks](std::stop_token stop) {
        while (!stop.stop_requested()) {
            // The worker only posts tasks. All RE calls and scene/physics access
            // happen inside Tick on SKSE's main-thread task queue.
            if (!pending_.exchange(true)) {
                tasks->AddTask([this] {
                    try { Tick(); }
                    catch (const std::exception& e) {
                        if (!failure_) SKSE::log::error("Runoff disabled after exception: {}", e.what());
                        failure_ = true;
                        Clear();
                    }
                    pending_.store(false);
                });
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
}
void Runtime::OnGameReady() { Clear(); ready_ = true; previous_ = {}; }
void Runtime::BeforeLoad() { ready_ = false; Clear(); previous_ = {}; }
void Runtime::Clear() {
    roofs_.clear();
    queue_.clear();
    examined_.clear();
    flow_.reset();
    nextScan_ = 0;
    elapsed_ = 0;
    lastHeight_ = 0;
}
void Runtime::Tick() {
    const auto now = std::chrono::steady_clock::now();
    const float dt = previous_.time_since_epoch().count() == 0 ? 0.0F :
        std::clamp(std::chrono::duration<float>(now - previous_).count(), 0.0F, .25F);
    previous_ = now;
    if (!ready_ || failure_ || !settings_.enabled) return;
    auto* ui = RE::UI::GetSingleton();
    if (!ui || ui->GameIsPaused()) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* cell = player ? player->GetParentCell() : nullptr;
    if (!player || !player->Get3D() || !cell || cell->IsInteriorCell()) {
        Clear();
        return;
    }
    const auto height = standingHeight(*player);
    if (lastHeight_ > 0 && std::abs(height - lastHeight_) > 2) Clear();
    lastHeight_ = height;
    const auto rain = rainValue();
    flow_.update(rain, dt, settings_.buildupSeconds, settings_.drainSeconds);
    elapsed_ += dt;
    if (elapsed_ > 10000) elapsed_ = std::fmod(elapsed_, 1000.0F);
    const auto radius2 = settings_.radius * settings_.radius;
    std::erase_if(roofs_, [&](auto& entry) {
        auto& roof = entry.second;
        const auto ref = roof.handle.get();
        if (!ref || ref->IsDisabled() || !ref->GetParentCell() || ref->GetParentCell()->IsInteriorCell() ||
            ref->Get3D() != roof.root.get() || distanceSquared(ref->GetPosition(), player->GetPosition()) > radius2) return true;
        for (std::size_t i = 0; i < roof.outlets.size(); ++i) {
            auto& outlet = roof.outlets[i];
            if (outlet.effect) {
                const auto width = hasClearance(outlet.clearance, height) ? settings_.topWidth * flow_.value : 0.0F;
                // Slight phase difference prevents two synchronized water tapes.
                outlet.effect->Update(width, settings_.opacity, elapsed_ + static_cast<float>(i)*.37F, settings_.scrollSpeed);
            }
        }
        return false;
    });
    nextScan_ -= dt;
    if (nextScan_ <= 0) {
        Scan(*player);
        nextScan_ = settings_.scanSeconds;
    }
    // Discover geometry only while rain is building or residual runoff remains.
    if (rain <= 0 && flow_.value <= 0) return;
    for (unsigned budget = 0; budget < settings_.candidatesPerTick && !queue_.empty() && roofs_.size() < settings_.maxRoofs; ++budget) {
        const auto handle = queue_.front();
        queue_.pop_front();
        const auto ref = handle.get();
        if (ref && ref->Get3D() && !ref->IsDisabled() && ref->GetParentCell() &&
            ref->GetParentCell()->IsExteriorCell() && distanceSquared(ref->GetPosition(), player->GetPosition()) <= radius2)
            Discover(*ref, height);
    }
}
void Runtime::Scan(RE::PlayerCharacter& player) {
    auto* tes = RE::TES::GetSingleton();
    if (!tes) return;
    std::vector<std::pair<float, RE::ObjectRefHandle>> candidates;
    std::unordered_set<RE::FormID> nearby;
    tes->ForEachReferenceInRange(&player, settings_.radius, [&](RE::TESObjectREFR* ref) {
        if (!ref || ref->IsDisabled() || !ref->Get3D() || !ref->GetParentCell() || ref->GetParentCell()->IsInteriorCell())
            return RE::BSContainer::ForEachResult::kContinue;
        // Houses and roofs are normally statics. Never attach to actors,
        // inventory objects, movable furniture, or terrain/LOD geometry.
        const auto* base = ref->GetBaseObject();
        if (!base || !base->Is(RE::FormType::Static)) return RE::BSContainer::ForEachResult::kContinue;
        const auto id = ref->GetFormID();
        nearby.insert(id);
        const auto it = examined_.find(id);
        if ((it != examined_.end() && it->second == ref->Get3D()) || roofs_.contains(id))
            return RE::BSContainer::ForEachResult::kContinue;
        if (settings_.accepts(engine::ModelPath(*ref)))
            candidates.emplace_back(distanceSquared(ref->GetPosition(), player.GetPosition()), ref->GetHandle());
        return RE::BSContainer::ForEachResult::kContinue;
    });
    std::erase_if(examined_, [&](const auto& entry) { return !nearby.contains(entry.first); });
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    queue_.clear();
    for (const auto& [distance, handle] : candidates) { (void)distance; queue_.push_back(handle); }
}
void Runtime::Discover(RE::TESObjectREFR& ref, float height) {
    auto* object = ref.Get3D();
    auto* parent = object ? object->AsNode() : nullptr;
    auto* cell = ref.GetParentCell();
    if (!parent || !cell || !cell->IsExteriorCell()) return;
    const auto id = ref.GetFormID();
    examined_[id] = object;
    auto mesh = engine::CollectTriangles(ref, settings_.maxTriangles);
    DetectionSettings detection;
    detection.minFootprint = settings_.minimumSpan;
    detection.minEaveSpan = settings_.minimumSpan;
    detection.cornerOutset = settings_.cornerOutset;
    // Roof-named shapes/materials are stronger evidence than broad path hints.
    // Only fall back to other upward surfaces if they produce no plausible eave.
    auto triangles = portable(mesh.triangles, true);
    auto pair = mesh.budgetExceeded ? std::optional<RoofPair>{} : detectCorners(triangles, detection);
    if (!pair && !mesh.budgetExceeded) {
        triangles = portable(mesh.triangles);
        pair = detectCorners(triangles, detection);
    }
    bool sampled = false;
    if (!pair && settings_.collisionFallback) {
        triangles = portable(engine::CollectRoofSamples(ref, *cell, settings_.sampleResolution));
        pair = detectCorners(triangles, detection);
        sampled = true;
    }
    if (!pair) {
        if (settings_.debugLog) SKSE::log::debug("No eave {:08X}: {} CPU-missing shapes {}", id,
            engine::ModelPath(ref), mesh.shapesWithoutCPUData);
        return;
    }
    Roof roof;
    roof.handle = ref.GetHandle();
    roof.root = parent;
    unsigned valid = 0;
    for (std::size_t i = 0; i < pair->size(); ++i) {
        auto origin = point((*pair)[i].position);
        const auto outward = point((*pair)[i].outward);
        if (sampled) {
            // A coarse collision grid can leave its border inside an eave.
            // Walk outward to the actual collision perimeter before emitting.
            auto last = origin - outward * settings_.cornerOutset;
            bool leftRoof = false;
            for (unsigned step = 0; step < 32; ++step) {
                const auto probe = last + outward * 16.0F;
                const auto hit = engine::Raycast(*cell, probe + RE::NiPoint3{0,0,settings_.maxDrop},
                    probe - RE::NiPoint3{0,0,settings_.maxDrop});
                if (!hit || hit->ref != &ref || hit->normal.z < .2F || std::abs(hit->point.z - last.z) > 32.0F) {
                    origin = last + outward * settings_.cornerOutset;
                    leftRoof = true;
                    break;
                }
                last = hit->point;
            }
            if (!leftRoof) continue;
        }
        if (settings_.requireSky && engine::Raycast(*cell, origin + RE::NiPoint3{0,0,2},
            origin + RE::NiPoint3{0,0,8192})) continue;
        const auto ground = engine::Raycast(*cell, origin - RE::NiPoint3{0,0,2},
            origin - RE::NiPoint3{0,0,settings_.maxDrop});
        if (!ground) continue;  // No collision surface: never guess terrain Z.
        const auto clearance = origin.z - ground->point.z;
        if (!hasClearance(clearance, height)) continue;
        auto& outlet = roof.outlets[i];
        outlet.origin = origin;
        outlet.clearance = clearance;
        // Fade before reaching the first surface: no penetration/splash simulation.
        outlet.length = std::max(1.0F, clearance - 4.0F);
        outlet.effect = engine::Effect::Create(*parent, settings_.model.c_str(), origin, outlet.length);
        if (outlet.effect) {
            outlet.effect->Update(settings_.topWidth * flow_.value, settings_.opacity,
                elapsed_ + static_cast<float>(i)*.37F, settings_.scrollSpeed);
            ++valid;
        }
    }
    if (valid) {
        if (settings_.debugLog) SKSE::log::debug("Runoff {:08X}: {} outlets, fallback {}", id, valid, sampled);
        roofs_.emplace(id, std::move(roof));
    }
}
}
