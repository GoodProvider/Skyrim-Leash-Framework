#pragma once

#include <memory>
#include <optional>

#include "../Animation/PullPoseController.h"
#include "../PCH.h"
#include "../Physics/SimulationSettings.h"
#include "../Recovery/ForcedRecoveryController.h"
#include "LeashDefinition.h"
#include "LeashEnd.h"
#include "LeashRope.h"
#include "LeashSide.h"
#include "LeashTeleportController.h"
#include "PoseRegistry.h"
#include "PullController.h"

namespace LeashFramework::Physics {
    class ActorBodyCollision;
}

namespace LeashFramework {
    struct TickContext {
        float deltaTime{};
        const Physics::SimulationSettings& settings;
        const Physics::ActorBodyCollision* actorCollision{};
        const PoseRegistry& poses;
        bool allowForcedRecovery{};
        // False while another leash is driving this leash's follower
        bool allowFollowerControl{};
    };

    class LeashInstance {
    public:
        LeashInstance(LeashDefinition a_definition, PullController& a_pullController, Recovery::ForcedRecoveryController& a_recoveryController, Animation::PullPoseController& a_pullPoseController);

        [[nodiscard]] const LeashDefinition& GetDefinition() const;
        [[nodiscard]] RE::NiAVObject* GetMeshRoot() const;
        [[nodiscard]] bool BindMesh();
        void ResetBinding();
        void SetMinLength(float a_length) noexcept;
        void SetMaxLength(float a_length) noexcept;
        void SetRagdollOverride(std::optional<bool> a_enabled);
        void SetTeleportOverride(std::optional<bool> a_enabled);
        void SetPreventOverstretchOverride(std::optional<bool> a_enabled);
        void SetFollower(LeashSide a_follower);
        [[nodiscard]] bool IsRagdollEnabled() const;
        [[nodiscard]] bool IsPreventOverstretchEnabled() const;
        [[nodiscard]] bool IsControllingFollower() const;
        bool ReleasePull();
        bool ReleaseRecovery();
        bool ReleaseControl();
        void Tick(const TickContext& a_context);
        void FreezeSimulation();
        void ResetSimulation();
        void ApplyDeferredPose();
        // Moves a_object with the lean this leash prepared for its leashed actor, if any
        void TransformPreparedPose(const RE::NiAVObject& a_object, RE::NiPoint3& a_position, RE::NiMatrix3& a_rotation) const;

    private:
        friend class LeashTeleportController;
        friend class PreparedLean;

        using Actors = PerSide<RE::NiPointer<RE::Actor>>;

        struct Binding {
            Actors actors;
            bool changed{};
        };

        [[nodiscard]] std::optional<Binding> Bind();
        [[nodiscard]] std::optional<PerSide<EndSample>> Sample(const Actors& a_actors) const;
        void Invalidate();
        void LogExceeded(float a_distance);
        [[nodiscard]] bool UpdateFollower(const Roles<EndSample>& a_roles, const TickContext& a_context);
        void UpdateLeaderConstraint(const PerSide<EndSample>& a_ends, const PreparedLean& a_lean, const RE::NiPoint3& a_holderAnchor, bool a_forcedRecoveryActive);
        [[nodiscard]] RE::NiPoint3 GetLeanReachLimit(const EndSample& a_leashedEnd, const RE::NiPoint3& a_anchor) const;

        LeashDefinition _definition;
        PerSide<RE::ActorHandle> _actors;
        LeashRope _rope;
        PerSide<std::unique_ptr<LeashEnd>> _ends;
        PullController& _pullController;
        Recovery::ForcedRecoveryController& _recoveryController;
        Animation::PullPoseController& _pullPoseController;
        // Controller state lives with the leash so it can't get out of sync with separate actor ID maps.
        PullController::State _pullState;
        Recovery::ForcedRecoveryController::State _recoveryState;
        Animation::PullPoseController::State _pullPoseState;
        RE::MovementControllerNPC* _leaderMovementBinding{};
        LeashTeleportController::State _teleportState;
        bool _exceeded{};
    };
}  // namespace LeashFramework
