#pragma once

#include "../PCH.h"

#include <optional>

namespace LeashFramework::Movement {
    struct HolderMovementSettings {
        bool preventOverstretch{true};
        float stretchAllowance{};
    };

    [[nodiscard]] HolderMovementSettings GetHolderMovementSettings();
    void SetHolderMovementSettings(HolderMovementSettings a_settings);
    void InstallLeashMovementConstraint();
    [[nodiscard]] bool CanConstrainNativeMovement(const RE::Actor& a_actor);
    void UpdateLeashMovementConstraint(RE::MovementControllerNPC*& a_binding, RE::Actor& a_actor, const RE::NiPoint3& a_collar, const RE::NiPoint3& a_anchor,
        const RE::NiPoint3& a_goal, float a_ropeLength, float a_maxLength);
    // Uses the holder overstretch settings for whichever actor leads. a_attachment is on a_leader; a_anchor is the follower's rope end.
    void UpdateLeaderMovementConstraint(RE::MovementControllerNPC*& a_binding, RE::Actor& a_leader, const RE::NiPoint3& a_attachment,
        const RE::NiPoint3& a_anchor, float a_ropeLength, std::optional<bool> a_preventOverstretch);
    void ClearLeashMovementConstraint(RE::MovementControllerNPC*& a_binding);
}  // namespace LeashFramework::Movement
