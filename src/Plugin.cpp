#include "Runtime.h"
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>

using namespace std::literals;
SKSEPluginInfo(
    .Version = {0, 1, 0, 0},
    .Name = "RainRunoff"sv,
    .Author = "RainRunoff contributors"sv,
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary
)
SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    if (auto path = SKSE::log::log_directory()) {
        *path /= "RainRunoff.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto logger = std::make_shared<spdlog::logger>("RainRunoff", std::move(sink));
        spdlog::set_default_logger(std::move(logger));
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::info);
    }
    SKSE::log::info("RainRunoff 0.1.0 experimental; runtime {}", skse->RuntimeVersion().string());
    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !SKSE::GetTaskInterface()) return false;
    return messaging->RegisterListener([](SKSE::MessagingInterface::Message* message) {
        auto& runtime = runoff::Runtime::Get();
        switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded: runtime.Start(); break;
        case SKSE::MessagingInterface::kPreLoadGame: runtime.BeforeLoad(); break;
        case SKSE::MessagingInterface::kNewGame:
        case SKSE::MessagingInterface::kPostLoadGame: runtime.OnGameReady(); break;
        default: break;
        }
    });
}
