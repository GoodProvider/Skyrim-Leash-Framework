#pragma once

#include "MenuSettings.h"

namespace LeashFramework::UI::DebugPage {
    [[nodiscard]] DebugSettings GetSettings();
    void SetSettings(const DebugSettings& a_settings);
    [[nodiscard]] bool IsActorCollisionDebugEnabled();
    void Render();
}  // namespace LeashFramework::UI::DebugPage
