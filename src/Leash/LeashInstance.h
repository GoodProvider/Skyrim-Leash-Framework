#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "../Animation/PullPoseController.h"
#include "../PCH.h"
#include "../Physics/RopeSolver.h"
#include "../Physics/SimulationSettings.h"
#include "../Recovery/ForcedRecoveryController.h"
#include "LeashAnchor.h"
#include "LeashDefinition.h"
#include "LeashTeleportController.h"
#include "PullController.h"
#include "RopeMesh.h"

namespace LeashFramework::Physics {
    class ActorBodyCollision;
}

namespace LeashFramework {
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
        [[nodiscard]] bool IsRagdollEnabled() const;
        [[nodiscard]] bool IsPreventOverstretchEnabled() const;
        bool ReleasePull();
        bool ReleaseRecovery();
        bool ReleaseControl();
        void Tick(float a_deltaTime, const Physics::SimulationSettings& a_settings, const Physics::ActorBodyCollision* a_actorCollision, bool a_allowForcedRecovery,
            const LeashInstance* a_holderPoseSource);
        void FreezeSimulation();
        void ResetSimulation();
        void ApplyDeferredPose();

    private:
        friend class LeashTeleportController;

        struct Frame {
            RE::NiPointer<RE::Actor> leashed;
            RE::NiPointer<RE::Actor> holder;
            LeashAnchor::Sample anchor;
            RE::TESObjectCELL* pullGoalCell{};
            bool meshChanged{};
            bool anchorChanged{};
        };

        [[nodiscard]] std::optional<Frame> Bind();
        void Invalidate();
        void ReadNeutralPose();
        void TransformPreparedPose(const RE::NiAVObject& a_object, RE::NiPoint3& a_position, RE::NiMatrix3& a_rotation) const;
        void ApplyPose(std::span<const RE::NiPoint3> a_neutralPositions, std::span<const RE::NiMatrix3> a_neutralRotations);

        LeashDefinition _definition;
        LeashAnchor _anchor;
        std::unique_ptr<RopeMesh> _mesh;
        PullController& _pullController;
        Recovery::ForcedRecoveryController& _recoveryController;
        Animation::PullPoseController& _pullPoseController;
        // Controller state lives with the leash so it can't get out of sync with separate actor ID maps.
        PullController::State _pullState;
        Recovery::ForcedRecoveryController::State _recoveryState;
        Animation::PullPoseController::State _pullPoseState;
        RE::MovementControllerNPC* _holderMovementBinding{};
        LeashTeleportController::State _teleportState;
        RE::ActorHandle _holder;
        RE::ActorHandle _leashed;
        std::vector<RE::NiPoint3> _neutralPositions;
        std::vector<RE::NiMatrix3> _neutralRotations;
        std::vector<float> _segmentLengths;
        std::vector<RE::NiPoint3> _deferredTranslations;
        std::vector<RE::NiMatrix3> _deferredRotations;
        Physics::RopeSolver _solver;
        bool _exceeded{};
    };
}  // namespace LeashFramework
