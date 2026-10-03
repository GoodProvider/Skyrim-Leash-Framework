#pragma once

#include "../Leash/LeashEnd.h"
#include "../PCH.h"
#include "RagdollHold.h"

namespace LeashFramework {
    class LeashInstance;
}

namespace LeashFramework::Recovery {
    struct ForcedRecoverySettings {
        bool enableNPCs{true};
        bool enablePlayer{};
        float distanceMultiplier{2.0F};
    };

    class ForcedRecoveryController {
    public:
        ForcedRecoveryController() = default;

        [[nodiscard]] ForcedRecoverySettings GetSettings() const noexcept { return _settings; }
        void SetSettings(ForcedRecoverySettings a_settings) noexcept;

    private:
        friend class LeashFramework::LeashInstance;

        enum class Mode { kInactive, kRequestingRagdoll, kPulling, kPullingCorpse, kRecovering, kCooldown };

        struct State {
            Mode mode{Mode::kInactive};
            std::unique_ptr<RagdollHold> ragdollHold;
            float insideDistanceTime{};
            float actionRetryDelay{};
            float modeElapsed{};
            bool requestIssued{};
            bool knockdownObserved{};
            bool pullEventSent{};
            bool interruptingGetUp{};
        };

        // Ragdolls and drags the follower toward the leader's end once it strays too far
        [[nodiscard]] bool Update(State& a_state, LeashSide a_followerSide, const Roles<EndSample>& a_roles, float a_maxLength, float a_deltaTime, bool a_enabled);
        [[nodiscard]] static bool IsControlling(const State& a_state) noexcept { return a_state.mode != Mode::kInactive && a_state.mode != Mode::kCooldown; }
        bool Release(State& a_state);
        void BeginRecovery(State& a_state);
        void BeginCooldown(State& a_state);
        [[nodiscard]] bool UpdateRecovery(State& a_state, RE::Actor& a_actor, float a_deltaTime);

        ForcedRecoverySettings _settings;
    };
}  // namespace LeashFramework::Recovery
