#include "ModMenu.h"

#include <filesystem>
#include <glaze/glaze.hpp>
#include <string>
#include <system_error>

#include "../PCH.h"
#include "../../include/SKSEMenuFramework.h"
#include "../Hooks/FrameHook.h"
#include "../Leash/LeashManager.h"
#include "DebugPage.h"
#include "Locale.h"
#include "MenuSettings.h"
#include "SettingsPage.h"

namespace LeashFramework::UI::ModMenu {
    namespace {
        constexpr auto kSettingsPath = "Data/SKSE/Plugins/LeashFramework.json";

        std::string settingsJson;
        bool modMenuOpen{};
        SKSEMenuFramework::Model::Event* menuEvent{};

        [[nodiscard]] ModMenuSettings ReadSettings() {
            auto& manager = LeashManager::GetSingleton();
            return {.frameHook = Hooks::FrameHook::GetSettings(),
                .simulation = manager.GetSimulationSettings(),
                .pullPose = manager.GetPullPoseSettings(),
                .locomotion = manager.GetLocomotionSettings(),
                .holderMovement = Movement::GetHolderMovementSettings(),
                .recovery = manager.GetRecoverySettings(),
                .teleport = manager.GetTeleportSettings(),
                .debug = DebugPage::GetSettings()};
        }

        void ApplySettings(const ModMenuSettings& a_settings) {
            auto& manager = LeashManager::GetSingleton();
            manager.SetSimulationSettings(a_settings.simulation);
            manager.SetPullPoseSettings(a_settings.pullPose);
            manager.SetLocomotionSettings(a_settings.locomotion);
            Movement::SetHolderMovementSettings(a_settings.holderMovement);
            manager.SetRecoverySettings(a_settings.recovery);
            manager.SetTeleportSettings(a_settings.teleport);
            Hooks::FrameHook::SetSettings(a_settings.frameHook);
            DebugPage::SetSettings(a_settings.debug);
        }

        void LoadSettings() {
            std::error_code fileError;
            if (!std::filesystem::exists(kSettingsPath, fileError)) {
                if (fileError) {
                    SKSE::log::error("Could not inspect menu settings file: {}", fileError.message());
                }
                return;
            }

            ModMenuSettings settings;
            settingsJson.clear();
            if (const auto error = glz::read_file_json(settings, kSettingsPath, settingsJson); error) {
                SKSE::log::error("Failed to load menu settings: {}", glz::format_error(error, settingsJson));
                return;
            }

            ApplySettings(settings);
            SKSE::log::info("Loaded menu settings");
        }

        void SaveSettings() {
            const auto settings = ReadSettings();
            settingsJson.clear();
            if (const auto error = glz::write_file_json(settings, kSettingsPath, settingsJson); error) {
                SKSE::log::error("Failed to save menu settings: {}", glz::format_error(error, settingsJson));
                return;
            }
            SKSE::log::info("Saved menu settings");
        }

        void __stdcall OnMenuEvent(SKSEMenuFramework::Model::EventType a_eventType) {
            if (a_eventType == SKSEMenuFramework::Model::kOpenMenu) {
                modMenuOpen = true;
            } else if (a_eventType == SKSEMenuFramework::Model::kCloseMenu && modMenuOpen) {
                modMenuOpen = false;
                SaveSettings();
            }
        }

        void __stdcall RenderSettingsPage() {
            auto settings = ReadSettings();
            const bool resetAll = SettingsPage::Render(settings);
            ApplySettings(resetAll ? ModMenuSettings{} : settings);
            if (resetAll) {
                SaveSettings();
            }
        }

        void __stdcall RenderDebugPage() { DebugPage::Render(); }
    }  // namespace

    bool IsActorCollisionDebugEnabled() { return DebugPage::IsActorCollisionDebugEnabled(); }

    void Register() {
        LoadSettings();
        Locale::Load();
        if (!SKSEMenuFramework::IsInstalled()) {
            SKSE::log::info("SKSE Menu Framework is not installed; mod menu disabled");
            return;
        }

        SKSEMenuFramework::SetSection(Locale::Text("Leash Framework"));
        SKSEMenuFramework::AddSectionItem(Locale::Text("Settings"), RenderSettingsPage);
        SKSEMenuFramework::AddSectionItem(Locale::Text("Debug"), RenderDebugPage);
        menuEvent = new SKSEMenuFramework::Model::Event(OnMenuEvent);
        SKSE::log::info("Registered Leash Framework mod menu");
    }
}  // namespace LeashFramework::UI::ModMenu
