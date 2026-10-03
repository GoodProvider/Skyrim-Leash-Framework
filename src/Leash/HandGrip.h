#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "../PCH.h"
#include "BindResult.h"

namespace LeashFramework {
    // A hand closed around the leash with the fixed grip pose
    class HandGrip {
    public:
        [[nodiscard]] BindResult Bind(RE::Actor* a_actor, bool a_rightHand, RE::FormID a_actorFormID, std::string_view a_role);
        void ApplyPose() const;
        void Reset();
        [[nodiscard]] RE::NiAVObject* GetHand() const { return _hand.get(); }
        // Where the rope passes through the closed fist
        [[nodiscard]] std::optional<RE::NiPoint3> GetGripPoint() const;

    private:
        RE::NiPointer<RE::NiAVObject> _root;
        RE::NiPointer<RE::NiAVObject> _hand;
        std::array<std::array<RE::NiPointer<RE::NiAVObject>, 3>, 5> _fingers{};
        bool _rightHand{true};
        bool _warningLogged{};
    };
}  // namespace LeashFramework
