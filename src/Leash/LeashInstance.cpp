#include "LeashInstance.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

#include "../Movement/DirectLocomotion.h"
#include "../Movement/LeashMovementConstraint.h"
#include "../PCH.h"
#include "../Recovery/ForcedRecoveryController.h"
#include "EquippedRope.h"
#include "PullController.h"
#include "StandaloneRope.h"

namespace LeashFramework {
    namespace {
        constexpr float kDirectionEpsilon = 0.0001F;

        [[nodiscard]] bool CanSimulateTogether(const RE::TESObjectCELL* a_anchorCell, const RE::Actor& a_leashed) {
            const auto* leashedCell = a_leashed.GetParentCell();
            if (!a_anchorCell || !leashedCell || !a_anchorCell->IsAttached() || !leashedCell->IsAttached()) {
                return false;
            }
            if (a_anchorCell == leashedCell) {
                return true;
            }
            if (!a_anchorCell->IsExteriorCell() || !leashedCell->IsExteriorCell()) {
                return false;
            }
            const auto* worldSpace = a_anchorCell->GetRuntimeData().worldSpace;
            return worldSpace && worldSpace == leashedCell->GetRuntimeData().worldSpace;
        }

        struct RopeEnd {
            RE::NiPoint3 position;
            const RE::NiAVObject* node{};
            const LeashInstance* poseSource{};
        };

        [[nodiscard]] std::unique_ptr<RopeMesh> MakeRopeMesh(const LeashDefinition& a_definition) {
            if (std::holds_alternative<StandaloneMesh>(a_definition.mesh)) {
                return std::make_unique<StandaloneRope>(a_definition);
            }
            return std::make_unique<EquippedRope>(a_definition);
        }

        [[nodiscard]] RE::NiMatrix3 AlignRotation(const RE::NiMatrix3& a_neutralRotation, RE::NiPoint3 a_from, RE::NiPoint3 a_to) {
            if (a_from.Unitize() <= kDirectionEpsilon || a_to.Unitize() <= kDirectionEpsilon) {
                return a_neutralRotation;
            }

            const auto dot = std::clamp(a_from.Dot(a_to), -1.0F, 1.0F);
            if (dot > 1.0F - kDirectionEpsilon) {
                return a_neutralRotation;
            }

            auto axis = a_to.Cross(a_from);
            if (axis.Unitize() <= kDirectionEpsilon) {
                axis = a_from.Cross({1.0F, 0.0F, 0.0F});
                if (axis.Unitize() <= kDirectionEpsilon) {
                    axis = a_from.Cross({0.0F, 1.0F, 0.0F});
                    axis.Unitize();
                }
            }

            RE::NiMatrix3 alignment;
            alignment.MakeRotation(std::acos(dot), axis);
            return alignment * a_neutralRotation;
        }
    }  // namespace

    LeashInstance::LeashInstance(LeashDefinition a_definition, PullController& a_pullController, Recovery::ForcedRecoveryController& a_recoveryController, Animation::PullPoseController& a_pullPoseController)
        : _definition(std::move(a_definition)), _anchor(_definition), _mesh(MakeRopeMesh(_definition)), _pullController(a_pullController), _recoveryController(a_recoveryController), _pullPoseController(a_pullPoseController) {
        if (auto* holder = RE::TESForm::LookupByID<RE::Actor>(_definition.holderFormID)) {
            _holder = holder->GetHandle();
        }
        if (auto* leashed = RE::TESForm::LookupByID<RE::Actor>(_definition.leashedFormID)) {
            _leashed = leashed->GetHandle();
        }
    }

    const LeashDefinition& LeashInstance::GetDefinition() const { return _definition; }

    RE::NiAVObject* LeashInstance::GetMeshRoot() const { return _mesh->GetRoot(); }

    bool LeashInstance::BindMesh() {
        const auto meshOwner = (_definition.HolderOwnsMesh() ? _holder : _leashed).get();
        return meshOwner && _mesh->Bind(*meshOwner) != LeashAnchor::BindResult::kFailed;
    }

    void LeashInstance::SetMinLength(float a_length) noexcept { _definition.minLength = a_length; }

    void LeashInstance::SetMaxLength(float a_length) noexcept { _definition.maxLength = a_length; }

    void LeashInstance::SetRagdollOverride(std::optional<bool> a_enabled) {
        _definition.overrides.ragdoll = a_enabled;
        if (!IsRagdollEnabled()) {
            ReleaseRecovery();
        }
    }

    void LeashInstance::SetTeleportOverride(std::optional<bool> a_enabled) {
        if (_definition.overrides.teleport != a_enabled) {
            _definition.overrides.teleport = a_enabled;
            _teleportState = {};
        }
    }

    void LeashInstance::SetPreventOverstretchOverride(std::optional<bool> a_enabled) {
        _definition.overrides.preventOverstretch = a_enabled;
        if (!IsPreventOverstretchEnabled()) {
            Movement::ClearLeashMovementConstraint(_holderMovementBinding);
        }
    }

    bool LeashInstance::IsPreventOverstretchEnabled() const {
        return _definition.overrides.preventOverstretch.value_or(Movement::GetHolderMovementSettings().preventOverstretch);
    }

    bool LeashInstance::IsRagdollEnabled() const {
        const auto leashed = _leashed.get();
        if (!leashed) {
            return false;
        }
        const auto settings = _recoveryController.GetSettings();
        return _definition.overrides.ragdoll.value_or(leashed->IsPlayerRef() ? settings.enablePlayer : settings.enableNPCs);
    }

    bool LeashInstance::ReleasePull() {
        auto leashed = _leashed.get();
        return _pullController.Release(_pullState, leashed.get());
    }

    bool LeashInstance::ReleaseRecovery() { return _recoveryController.Release(_recoveryState); }

    bool LeashInstance::ReleaseControl() {
        Movement::ClearLeashMovementConstraint(_holderMovementBinding);
        const auto releasedPull = ReleasePull();
        const auto releasedRecovery = ReleaseRecovery();
        _pullPoseController.Reset(_pullPoseState);
        return releasedPull || releasedRecovery;
    }

    void LeashInstance::Tick(float a_deltaTime, const Physics::SimulationSettings& a_settings, const Physics::ActorBodyCollision* a_actorCollision, bool a_allowForcedRecovery,
        const LeashInstance* a_holderPoseSource) {
        LF_PROFILE_SCOPE("Leash/Tick");
        if (!std::isfinite(a_deltaTime) || a_deltaTime <= 0.0F) {
            return;
        }

        const auto frame = Bind();
        if (!frame) {
            Invalidate();
            return;
        }
        // A new or replaced scene binding (equipment changed, etc.) makes the cached simulation meaningless
        if (frame->meshChanged || frame->anchorChanged) {
            ReleaseControl();
            ResetSimulation();
        }
        auto& leashed = *frame->leashed;
        auto* holder = frame->holder.get();
        const auto& anchor = frame->anchor;
        if (frame->meshChanged && _pullController.DiagnosticsEnabled()) {
            Movement::LogDirectLocomotionState(leashed, "leash tick after bind");
        }

        ReadNeutralPose();
        const auto ropeLength = std::accumulate(_segmentLengths.begin(), _segmentLengths.end(), 0.0F);
        // Bone 0 sits on the mesh owner, so a holder-owned rope reaches the collar at its free end
        const auto holderOwnsMesh = _definition.HolderOwnsMesh();
        const RopeEnd meshEnd{_neutralPositions.front(), &_mesh->GetPoseReference(0), holderOwnsMesh ? a_holderPoseSource : this};
        const RopeEnd freeEnd{anchor.position, anchor.poseReference, holderOwnsMesh ? this : a_holderPoseSource};
        const auto [collar, leasher] = holderOwnsMesh ? std::pair{freeEnd, meshEnd} : std::pair{meshEnd, freeEnd};
        const auto posed = [](const RopeEnd& a_end) {
            auto position = a_end.position;
            if (a_end.poseSource && a_end.node) {
                auto rotation = a_end.node->world.rotate;
                a_end.poseSource->TransformPreparedPose(*a_end.node, position, rotation);
            }
            return position;
        };

        const auto anchorDistance = collar.position.GetDistance(leasher.position);
        if (anchorDistance > _definition.maxLength) {
            if (!_exceeded) {
                SKSE::log::info("Exceeded by {}", anchorDistance - _definition.maxLength);
                _exceeded = true;
            }
        } else {
            _exceeded = false;
        }
        if (!a_allowForcedRecovery) {
            ReleaseRecovery();
        }
        const auto pullGoal = holder ? holder->GetPosition() : anchor.position;
        const auto forcedRecoveryActive = a_allowForcedRecovery &&
                                          _recoveryController.Update(_recoveryState, leashed, collar.position, leasher.position, pullGoal, _definition.maxLength, a_deltaTime, IsRagdollEnabled());
        if (forcedRecoveryActive) {
            _pullController.Release(_pullState, &leashed);
        } else {
            _pullController.Update(_pullState, leashed, collar.position, leasher.position, ropeLength, pullGoal, frame->pullGoalCell, holder != nullptr, _definition.minLength, _definition.maxLength,
                a_deltaTime);
        }

        // The leasher end only follows the holder's lean, which is prepared before this leash ticks
        const auto poseLeasherAnchor = posed(leasher);
        _pullPoseController.Prepare(_pullPoseState, leashed, collar.node, collar.position, poseLeasherAnchor, ropeLength, a_deltaTime, !forcedRecoveryActive);
        if (holder && !forcedRecoveryActive && IsPreventOverstretchEnabled()) {
            const auto leanLimitAttachment = _pullPoseController.GetLeanLimitAttachment(_pullPoseState, collar.node, collar.position, poseLeasherAnchor);
            Movement::UpdateHolderMovementConstraint(_holderMovementBinding, *holder, poseLeasherAnchor, leanLimitAttachment, ropeLength, _definition.overrides.preventOverstretch);
        } else {
            Movement::ClearLeashMovementConstraint(_holderMovementBinding);
        }
        auto posedNeutralPositions = _neutralPositions;
        auto posedNeutralRotations = _neutralRotations;
        // The actor wearing the leash can also be getting leaned by their own leash, so use that pose instead of pretending the mesh stayed where it was
        if (meshEnd.poseSource) {
            for (std::size_t index = 0; index < posedNeutralPositions.size(); ++index) {
                meshEnd.poseSource->TransformPreparedPose(_mesh->GetPoseReference(index), posedNeutralPositions[index], posedNeutralRotations[index]);
            }
        }
        // Same thing for the other end. This is what keeps a leash attached to the neck when that actor's leash makes them lean
        const auto posedEndAnchor = posed(freeEnd);
        RE::bhkWorld* world{};
        if (auto* cell = leashed.GetParentCell()) {
            world = cell->GetbhkWorld();
        }

        const auto& positions = _solver.Solve(posedNeutralPositions, _segmentLengths, posedEndAnchor, a_deltaTime, world, a_actorCollision, a_settings);
        if (!forcedRecoveryActive && positions.size() >= 2) {
            const auto collarIndex = holderOwnsMesh ? positions.size() - 1 : 0;
            const auto nextIndex = holderOwnsMesh ? collarIndex - 1 : 1;
            _pullPoseController.Capture(_pullPoseState, positions[collarIndex], positions[nextIndex]);
        }
        if (positions.size() == _mesh->GetBones().size()) {
            _mesh->PoseFrame(posedNeutralPositions.front(), posedNeutralRotations.front());
            ApplyPose(posedNeutralPositions, posedNeutralRotations);
        }
        if (frame->meshChanged && _pullController.DiagnosticsEnabled()) {
            Movement::LogDirectLocomotionState(leashed, "leash tick complete");
        }
    }

    void LeashInstance::FreezeSimulation() {
        Movement::ClearLeashMovementConstraint(_holderMovementBinding);
        _solver.Freeze();
        _pullController.ResetMotion(_pullState);
        _pullPoseController.Freeze(_pullPoseState);
    }

    void LeashInstance::ResetSimulation() {
        Movement::ClearLeashMovementConstraint(_holderMovementBinding);
        _solver.Reset();
        _pullController.ResetMotion(_pullState);
        _pullPoseController.Reset(_pullPoseState);
        _deferredTranslations.clear();
        _deferredRotations.clear();
        _mesh->Hide();
    }

    void LeashInstance::ResetBinding() {
        ReleaseControl();
        ResetSimulation();
        _mesh->Reset();
    }

    void LeashInstance::Invalidate() {
        ReleaseControl();
        ResetSimulation();
        _exceeded = false;
    }

    void LeashInstance::ApplyDeferredPose() {
        LF_PROFILE_SCOPE("Leash/ApplyDeferredPose");
        const auto frame = Bind();
        if (!frame) {
            Invalidate();
            return;
        }
        // Never restore a pose solved for the previous binding
        if (frame->meshChanged || frame->anchorChanged) {
            ReleaseControl();
            ResetSimulation();
            return;
        }

        _pullPoseController.Apply(_pullPoseState, *frame->leashed);
        const auto& bones = _mesh->GetBones();
        if (_deferredTranslations.empty() || _deferredTranslations.size() != bones.size() || _deferredRotations.size() != bones.size()) {
            return;
        }

        _mesh->Present();
        for (std::size_t index = 0; index < bones.size(); ++index) {
            bones[index]->world.translate = _deferredTranslations[index];
            bones[index]->world.rotate = _deferredRotations[index];
        }
        _mesh->UpdateWorldBounds();
    }

    std::optional<LeashInstance::Frame> LeashInstance::Bind() {
        Frame frame{.leashed = _leashed.get(), .holder = _holder.get()};
        const auto holderOwnsMesh = _definition.HolderOwnsMesh();
        auto* meshOwner = (holderOwnsMesh ? frame.holder : frame.leashed).get();
        auto* attachmentActor = (holderOwnsMesh ? frame.leashed : frame.holder).get();
        if (!frame.leashed || !meshOwner) {
            // Nothing will rebind the mesh while its owner is gone, so release a standalone clone from its cell now
            _mesh->Reset();
            return std::nullopt;
        }
        // Bind the mesh first so a missing rope doesn't close the holder's hand around nothing
        const auto meshResult = _mesh->Bind(*meshOwner);
        if (meshResult == LeashAnchor::BindResult::kFailed) {
            return std::nullopt;
        }
        const auto anchorResult = _anchor.Bind(attachmentActor, frame.holder.get());
        if (anchorResult == LeashAnchor::BindResult::kFailed) {
            return std::nullopt;
        }
        _anchor.ApplyPose();
        const auto sample = _anchor.GetSample(attachmentActor);
        if (!sample) {
            return std::nullopt;
        }
        frame.anchor = *sample;
        frame.pullGoalCell = frame.holder ? frame.holder->GetParentCell() : sample->cell;
        if (!CanSimulateTogether(frame.pullGoalCell, *frame.leashed)) {
            return std::nullopt;
        }
        frame.meshChanged = meshResult == LeashAnchor::BindResult::kChanged;
        frame.anchorChanged = anchorResult == LeashAnchor::BindResult::kChanged;
        return frame;
    }

    void LeashInstance::ReadNeutralPose() {
        _mesh->PoseNeutral();
        const auto& bones = _mesh->GetBones();
        _neutralPositions.resize(bones.size());
        _neutralRotations.resize(bones.size());
        _segmentLengths.resize(bones.size() - 1);

        for (std::size_t index = 0; index < bones.size(); ++index) {
            _neutralPositions[index] = bones[index]->world.translate;
            _neutralRotations[index] = bones[index]->world.rotate;
            if (index > 0) {
                _segmentLengths[index - 1] = _neutralPositions[index - 1].GetDistance(_neutralPositions[index]);
            }
        }
    }

    void LeashInstance::TransformPreparedPose(const RE::NiAVObject& a_object, RE::NiPoint3& a_position, RE::NiMatrix3& a_rotation) const {
        _pullPoseController.Transform(_pullPoseState, a_object, a_position, a_rotation);
    }

    void LeashInstance::ApplyPose(std::span<const RE::NiPoint3> a_neutralPositions, std::span<const RE::NiMatrix3> a_neutralRotations) {
        const auto& positions = _solver.GetPositions();
        const auto& bones = _mesh->GetBones();
        _deferredTranslations.resize(bones.size());
        _deferredRotations.resize(bones.size());
        for (std::size_t index = 0; index < bones.size(); ++index) {
            RE::NiPoint3 neutralDirection;
            RE::NiPoint3 solvedDirection;
            if (index + 1 < bones.size()) {
                neutralDirection = a_neutralPositions[index + 1] - a_neutralPositions[index];
                solvedDirection = positions[index + 1] - positions[index];
            } else {
                neutralDirection = a_neutralPositions[index] - a_neutralPositions[index - 1];
                solvedDirection = positions[index] - positions[index - 1];
            }

            bones[index]->world.translate = positions[index];
            bones[index]->world.rotate = AlignRotation(a_neutralRotations[index], neutralDirection, solvedDirection);
            _deferredTranslations[index] = bones[index]->world.translate;
            _deferredRotations[index] = bones[index]->world.rotate;
        }
    }
}  // namespace LeashFramework
