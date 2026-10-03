#pragma once

#include <cstddef>
#include <vector>

#include "../PCH.h"
#include "LeashEnd.h"

namespace LeashFramework {
    struct LocomotionSettings {
        float forwardAssistance{1.0F};
        float backwardResistance{1.5F};
        float minimumForcedPullRatio{0.5F};
        float maximumCatchUpSpeed{3.0F};
        float movingFollowGap{0.4F};
        float distanceResponseRate{3.0F};
    };

    class LeashInstance;

    class PullController {
    public:
        PullController() = default;

        [[nodiscard]] LocomotionSettings GetSettings() const noexcept { return _settings; }
        void SetSettings(LocomotionSettings a_settings) noexcept;

        [[nodiscard]] bool DiagnosticsEnabled() const noexcept { return _diagnosticsEnabled; }
        void SetDiagnosticsEnabled(bool a_enabled) noexcept { _diagnosticsEnabled = a_enabled; }

    private:
        friend class LeashInstance;

        struct MotionState {
            RE::NiPoint3 previousGoal;
            RE::NiPoint3 velocity;
            float stationaryTime{};
            float movingBlend{};
            bool hasSample{};
            bool moving{};
        };

        struct State {
            MotionState motion;
            RE::MovementControllerNPC* nativeMovementBinding{};
            std::vector<RE::NiPoint3> path;
            std::size_t waypointIndex{};
            std::size_t stableDirectFrames{};
            RE::NiPoint3 lastGoal;
            float replanDelay{};
            float smoothedPlayerEffort{};
            float commandedSpeed{};
            float idleTime{};
            float retryDelay{};
            LeashSide follower{};
            bool active{};
            bool restorePlayerControls{};
        };

        // Drives the follower toward the leader's actor, or toward a world anchor when the leader has no actor
        void Update(State& a_state, LeashSide a_followerSide, const Roles<EndSample>& a_roles, float a_ropeLength, float a_minLength, float a_maxLength, float a_deltaTime);
        void ResetMotion(State& a_state);
        bool Release(State& a_state, RE::Actor* a_actor, bool a_keepMotion = false);

        LocomotionSettings _settings;
        bool _diagnosticsEnabled{};
    };
}  // namespace LeashFramework
