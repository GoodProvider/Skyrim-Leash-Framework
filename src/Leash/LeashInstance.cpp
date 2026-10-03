#include "LeashInstance.h"

#include <cmath>
#include <utility>

#include "../Movement/DirectLocomotion.h"
#include "../Movement/LeashMovementConstraint.h"
#include "../PCH.h"

namespace LeashFramework {
    namespace {
        [[nodiscard]] bool CanSimulateTogether(const RE::TESObjectCELL* a_first, const RE::TESObjectCELL* a_second) {
            if (!a_first || !a_second || !a_first->IsAttached() || !a_second->IsAttached()) {
                return false;
            }
            if (a_first == a_second) {
                return true;
            }
            if (!a_first->IsExteriorCell() || !a_second->IsExteriorCell()) {
                return false;
            }
            const auto* worldSpace = a_first->GetRuntimeData().worldSpace;
            return worldSpace && worldSpace == a_second->GetRuntimeData().worldSpace;
        }
    }  // namespace

    LeashInstance::LeashInstance(LeashDefinition a_definition, PullController& a_pullController, Recovery::ForcedRecoveryController& a_recoveryController, Animation::PullPoseController& a_pullPoseController)
        : _definition(std::move(a_definition)), _rope(_definition), _ends(MakeLeashEnds(_definition, _rope)), _pullController(a_pullController), _recoveryController(a_recoveryController),
          _pullPoseController(a_pullPoseController) {
        for (const auto side : kLeashSides) {
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(_definition.GetFormID(side))) {
                _actors[side] = actor->GetHandle();
            }
        }
    }

    const LeashDefinition& LeashInstance::GetDefinition() const { return _definition; }

    RE::NiAVObject* LeashInstance::GetMeshRoot() const { return _rope.GetMeshRoot(); }

    bool LeashInstance::BindMesh() {
        const auto owner = _actors[_rope.GetRootSide()].get();
        return owner && _rope.Bind(*owner) != BindResult::kFailed;
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
            Movement::ClearLeashMovementConstraint(_leaderMovementBinding);
        }
    }

    bool LeashInstance::IsPreventOverstretchEnabled() const {
        return _definition.overrides.preventOverstretch.value_or(Movement::GetHolderMovementSettings().preventOverstretch);
    }

    void LeashInstance::SetFollower(LeashSide a_follower) {
        if (_definition.follower == a_follower) {
            return;
        }
        // Release against the current follower before the roles flip, or its locomotion and player controls are never restored
        ReleaseControl();
        _teleportState = {};
        _definition.follower = a_follower;
    }

    bool LeashInstance::IsRagdollEnabled() const {
        // Only the leashed actor is ever ragdolled. A following holder relies on pathing and teleport recovery instead.
        const auto leashed = _actors.leashed.get();
        if (_definition.follower != LeashSide::kLeashed || !leashed) {
            return false;
        }
        const auto settings = _recoveryController.GetSettings();
        return _definition.overrides.ragdoll.value_or(leashed->IsPlayerRef() ? settings.enablePlayer : settings.enableNPCs);
    }

    bool LeashInstance::IsControllingFollower() const { return _pullState.active || Recovery::ForcedRecoveryController::IsControlling(_recoveryState); }

    bool LeashInstance::ReleasePull() {
        const auto follower = _actors[_definition.follower].get();
        return _pullController.Release(_pullState, follower.get());
    }

    bool LeashInstance::ReleaseRecovery() { return _recoveryController.Release(_recoveryState); }

    bool LeashInstance::ReleaseControl() {
        Movement::ClearLeashMovementConstraint(_leaderMovementBinding);
        const auto releasedPull = ReleasePull();
        const auto releasedRecovery = ReleaseRecovery();
        _pullPoseController.Reset(_pullPoseState);
        return releasedPull || releasedRecovery;
    }

    void LeashInstance::Tick(const TickContext& a_context) {
        LF_PROFILE_SCOPE("Leash/Tick");
        if (!std::isfinite(a_context.deltaTime) || a_context.deltaTime <= 0.0F) {
            return;
        }

        const auto binding = Bind();
        if (!binding) {
            Invalidate();
            return;
        }
        // A new or replaced scene binding (equipment changed, etc.) makes the cached simulation meaningless
        if (binding->changed) {
            ReleaseControl();
            ResetSimulation();
        }
        auto& leashed = *binding->actors.leashed;
        const auto logBinding = binding->changed && _pullController.DiagnosticsEnabled();
        if (logBinding) {
            Movement::LogDirectLocomotionState(leashed, "leash tick after bind");
        }

        _rope.ReadNeutralPose();
        const auto ends = Sample(binding->actors);
        if (!ends) {
            Invalidate();
            return;
        }
        LogExceeded(ends->leashed.attachment.GetDistance(ends->holder.attachment));

        const auto forcedRecoveryActive = UpdateFollower(Orient(*ends, _definition.follower), a_context);

        // The holder's lean is already final because its own leash ticks before this one
        const auto holderAnchor = a_context.poses.Pose(ends->holder);
        _pullPoseController.Prepare(_pullPoseState, leashed, ends->leashed.node, ends->leashed.attachment, holderAnchor, _rope.GetLength(), a_context.deltaTime, !forcedRecoveryActive);
        const PreparedLean lean{a_context.poses, leashed, *this};

        UpdateLeaderConstraint(*ends, lean, holderAnchor, forcedRecoveryActive);
        auto* cell = leashed.GetParentCell();
        _rope.Solve(*ends, lean, a_context.deltaTime, cell ? cell->GetbhkWorld() : nullptr, a_context.actorCollision, a_context.settings);
        if (const auto collar = _rope.GetEndSegment(LeashSide::kLeashed); collar && !forcedRecoveryActive) {
            _pullPoseController.Capture(_pullPoseState, collar->first, collar->second);
        }
        if (logBinding) {
            Movement::LogDirectLocomotionState(leashed, "leash tick complete");
        }
    }

    bool LeashInstance::UpdateFollower(const Roles<EndSample>& a_roles, const TickContext& a_context) {
        // Bind fails when the follower side has no actor
        auto& follower = *a_roles.follower.actor;
        const auto allowForcedRecovery = a_context.allowForcedRecovery && a_context.allowFollowerControl;
        if (!allowForcedRecovery) {
            ReleaseRecovery();
        }
        const auto forcedRecoveryActive = allowForcedRecovery &&
                                          _recoveryController.Update(_recoveryState, _definition.follower, a_roles, _definition.maxLength, a_context.deltaTime, IsRagdollEnabled());
        if (forcedRecoveryActive || !a_context.allowFollowerControl) {
            _pullController.Release(_pullState, &follower);
        } else {
            _pullController.Update(_pullState, _definition.follower, a_roles, _rope.GetLength(), _definition.minLength, _definition.maxLength, a_context.deltaTime);
        }
        return forcedRecoveryActive;
    }

    void LeashInstance::UpdateLeaderConstraint(const PerSide<EndSample>& a_ends, const PreparedLean& a_lean, const RE::NiPoint3& a_holderAnchor, bool a_forcedRecoveryActive) {
        auto* leader = Orient(a_ends, _definition.follower).leader.actor;
        if (!leader || a_forcedRecoveryActive || !IsPreventOverstretchEnabled()) {
            Movement::ClearLeashMovementConstraint(_leaderMovementBinding);
            return;
        }
        // Only the leashed actor leans, so only its end can reach past its attachment
        const PerSide<RE::NiPoint3> reach{.leashed = a_lean.GetReachLimit(a_ends.leashed, a_holderAnchor), .holder = a_holderAnchor};
        const auto reachRoles = Orient(reach, _definition.follower);
        Movement::UpdateLeaderMovementConstraint(_leaderMovementBinding, *leader, reachRoles.leader, reachRoles.follower, _rope.GetLength(), _definition.overrides.preventOverstretch);
    }

    RE::NiPoint3 LeashInstance::GetLeanReachLimit(const EndSample& a_leashedEnd, const RE::NiPoint3& a_anchor) const {
        return _pullPoseController.GetLeanLimitAttachment(_pullPoseState, a_leashedEnd.node, a_leashedEnd.attachment, a_anchor);
    }

    void LeashInstance::LogExceeded(float a_distance) {
        if (a_distance > _definition.maxLength) {
            if (!_exceeded) {
                SKSE::log::info("Exceeded by {}", a_distance - _definition.maxLength);
                _exceeded = true;
            }
        } else {
            _exceeded = false;
        }
    }

    void LeashInstance::FreezeSimulation() {
        Movement::ClearLeashMovementConstraint(_leaderMovementBinding);
        _rope.Freeze();
        _pullController.ResetMotion(_pullState);
        _pullPoseController.Freeze(_pullPoseState);
    }

    void LeashInstance::ResetSimulation() {
        Movement::ClearLeashMovementConstraint(_leaderMovementBinding);
        _rope.ResetSimulation();
        _pullController.ResetMotion(_pullState);
        _pullPoseController.Reset(_pullPoseState);
    }

    void LeashInstance::ResetBinding() {
        ReleaseControl();
        ResetSimulation();
        _rope.Unbind();
    }

    void LeashInstance::Invalidate() {
        ReleaseControl();
        ResetSimulation();
        _exceeded = false;
    }

    void LeashInstance::ApplyDeferredPose() {
        LF_PROFILE_SCOPE("Leash/ApplyDeferredPose");
        const auto binding = Bind();
        if (!binding) {
            Invalidate();
            return;
        }
        // Never restore a pose solved for the previous binding
        if (binding->changed) {
            ReleaseControl();
            ResetSimulation();
            return;
        }

        _pullPoseController.Apply(_pullPoseState, *binding->actors.leashed);
        _rope.ApplyDeferredPose();
    }

    std::optional<LeashInstance::Binding> LeashInstance::Bind() {
        Binding binding{.actors = {.leashed = _actors.leashed.get(), .holder = _actors.holder.get()}};
        const auto& actors = binding.actors;
        const auto ropeSide = _rope.GetRootSide();
        if (!actors.leashed || !actors[ropeSide]) {
            // Nothing will rebind the rope while its owner is gone, so release a standalone clone from its cell now
            _rope.Unbind();
            return std::nullopt;
        }
        if (!actors[_definition.follower]) {
            return std::nullopt;
        }
        for (const auto side : {ropeSide, Opposite(ropeSide)}) {
            const auto result = _ends[side]->Bind(actors[side].get());
            if (result == BindResult::kFailed) {
                return std::nullopt;
            }
            binding.changed |= result == BindResult::kChanged;
        }
        for (const auto side : kLeashSides) {
            _ends[side]->ApplyPose();
        }
        if (!CanSimulateTogether(_ends.leashed->GetCell(actors.leashed.get()), _ends.holder->GetCell(actors.holder.get()))) {
            return std::nullopt;
        }
        return binding;
    }

    std::optional<PerSide<EndSample>> LeashInstance::Sample(const Actors& a_actors) const {
        PerSide<EndSample> ends;
        for (const auto side : kLeashSides) {
            const auto sample = _ends[side]->Sample(a_actors[side].get());
            if (!sample) {
                return std::nullopt;
            }
            ends[side] = *sample;
        }
        return ends;
    }

    void LeashInstance::TransformPreparedPose(const RE::NiAVObject& a_object, RE::NiPoint3& a_position, RE::NiMatrix3& a_rotation) const {
        _pullPoseController.Transform(_pullPoseState, a_object, a_position, a_rotation);
    }
}  // namespace LeashFramework
