#pragma once
#include "Engine.h"
#include "Settings.h"
#include "runoff/Core.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <thread>
#include <unordered_map>

namespace runoff {
class Runtime {
public:
    static Runtime& Get();
    void Start();
    void OnGameReady();
    void BeforeLoad();
private:
    struct Outlet {
        RE::NiPoint3 origin;
        float clearance{};
        float length{};
        std::unique_ptr<RainRunoff::Engine::Effect> effect;
    };
    struct Roof {
        RE::ObjectRefHandle handle;
        RE::NiPointer<RE::NiNode> root;
        std::array<Outlet, 2> outlets;
    };
    void Tick();
    void Scan(RE::PlayerCharacter& player);
    void Discover(RE::TESObjectREFR& ref, float standingHeight);
    void Clear();
    Settings settings_;
    std::jthread worker_;
    std::atomic_bool pending_{};
    bool ready_{};
    bool failure_{};
    std::chrono::steady_clock::time_point previous_{};
    float nextScan_{};
    float elapsed_{};
    float lastHeight_{};
    FlowEnvelope flow_;
    std::deque<RE::ObjectRefHandle> queue_;
    // Only pointer identity is retained for failed candidates, never dereferenced.
    std::unordered_map<RE::FormID, const RE::NiAVObject*> examined_;
    std::unordered_map<RE::FormID, Roof> roofs_;
};
}
