#include "LeashTeleportController.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "../Actor/ActorRestrictions.h"
#include "../PCH.h"
#include "LeashInstance.h"

// Todo: Right now non-actor leash anchors are not handled (holderFormID == 0). Possibly do this in the future? Although
// I'm concerned about issues, and or false intentions.
namespace LeashFramework {
    namespace {
        constexpr float kFollowHandlingDisabledMinLength = 99999.0F;
        constexpr float kTeleportCooldown = 2.0F;
        constexpr float kTeleportSpacing = 128.0F;

        [[nodiscard]] bool AreInCompatibleSpaces(const RE::Actor& a_leader, const RE::Actor& a_follower) {
            const auto* leaderCell = a_leader.GetParentCell();
            const auto* followerCell = a_follower.GetParentCell();
            if (!leaderCell || !followerCell) {
                return false;
            }
            if (leaderCell == followerCell) {
                return true;
            }
            if (!leaderCell->IsExteriorCell() || !followerCell->IsExteriorCell()) {
                return false;
            }
            const auto* leaderWorldSpace = leaderCell->GetRuntimeData().worldSpace;
            return leaderWorldSpace && leaderWorldSpace == followerCell->GetRuntimeData().worldSpace;
        }

        // Player positioning is a deliberate move, so it always brings the other actor along whichever side the player is on
        [[nodiscard]] RE::FormID GetPlayerPartnerFormID(const LeashDefinition& a_definition) {
            const auto* player = RE::PlayerCharacter::GetSingleton();
            const auto playerSide = player ? a_definition.FindSide(player->GetFormID()) : std::nullopt;
            return playerSide ? a_definition.GetFormID(Opposite(*playerSide)) : 0;
        }

        [[nodiscard]] bool CanBeginRecovery(RE::Actor& a_actor) {
            const auto* actorState = a_actor.AsActorState();
            const auto* process = a_actor.GetActorRuntimeData().currentProcess;
            const auto* race = a_actor.GetRace();
            return !ActorRestrictions::IsRagdollOrTeleportBlocked(a_actor) && !a_actor.IsInRagdollState() && actorState->GetKnockState() == RE::KNOCK_STATE_ENUM::kNormal &&
                   race && !race->data.flags.any(RE::RACE_DATA::Flag::kImmobile) &&
                   !race->data.flags.any(RE::RACE_DATA::Flag::kNoKnockdowns) && actorState->GetLifeState() != RE::ACTOR_LIFE_STATE::kRestrained && process &&
                   (!process->high || static_cast<std::uint16_t>(process->high->animAction) != static_cast<std::uint16_t>(RE::CombatAnimation::ANIM::kActionActivate));
        }

    }  // namespace

    void LeashTeleportController::SetSettings(LeashTeleportSettings a_settings) noexcept {
        const LeashTeleportSettings defaults;
        a_settings.gracePeriod = std::isfinite(a_settings.gracePeriod) ? std::clamp(a_settings.gracePeriod, 0.0F, 30.0F) : defaults.gracePeriod;
        a_settings.playerDistance = std::isfinite(a_settings.playerDistance) ? a_settings.playerDistance : defaults.playerDistance;
        a_settings.npcDistance = std::isfinite(a_settings.npcDistance) ? a_settings.npcDistance : defaults.npcDistance;
        _settings = a_settings;
    }

    bool LeashTeleportController::HandlePlayerPositioned(LeashInstance& a_leash) {
        const auto& definition = a_leash.GetDefinition();
        auto& state = a_leash._teleportState;
        if (definition.holderFormID == 0) {
            state = {};
            return false;
        }
        if (!definition.overrides.teleport.value_or(true) || definition.minLength > kFollowHandlingDisabledMinLength) {
            state = {};
            return false;
        }
        const auto partnerFormID = GetPlayerPartnerFormID(definition);
        if (partnerFormID == 0) {
            return false;
        }
        auto* partner = RE::TESForm::LookupByID<RE::Actor>(partnerFormID);
        if (!partner || partner->IsDisabled()) {
            return false;
        }
        if (!CanBeginRecovery(*partner)) {
            state = State{.pendingPlayerPosition = true};
            return false;
        }
        const auto teleported = TeleportToPlayer(a_leash);
        if (teleported) {
            state.pendingPlayerPosition = false;
        }
        return teleported;
    }

    LeashTeleportController::UpdateResult LeashTeleportController::Update(LeashInstance& a_leash, float a_deltaTime) {
        LF_PROFILE_SCOPE("Controller/Teleport");
        const auto& definition = a_leash.GetDefinition();
        auto& state = a_leash._teleportState;
        if (definition.holderFormID == 0) {
            state = {};
            return UpdateResult::kNone;
        }
        if (!definition.overrides.teleport.value_or(true) || definition.minLength > kFollowHandlingDisabledMinLength) {
            state = {};
            return UpdateResult::kNone;
        }

        auto* leader = RE::TESForm::LookupByID<RE::Actor>(definition.GetFormID(Opposite(definition.follower)));
        auto* follower = RE::TESForm::LookupByID<RE::Actor>(definition.GetFormID(definition.follower));
        if (!leader || !follower) {
            return UpdateResult::kNone;
        }
        if (leader->IsDisabled() || follower->IsDisabled()) {
            return UpdateResult::kNone;
        }
        if (leader->IsDead(true) || follower->IsDead(true)) {
            return UpdateResult::kNone;
        }
        if (state.pendingPlayerPosition) {
            if (!TeleportToPlayer(a_leash)) {
                return UpdateResult::kPending;
            }
            state.pendingPlayerPosition = false;
            return UpdateResult::kTeleported;
        }
        if (leader->IsPlayerRef()) {
            return UpdateResult::kNone;
        }
        auto teleportDistance = follower->IsPlayerRef() ? _settings.playerDistance : _settings.npcDistance;
        if (teleportDistance <= 0.0F) {
            if (!definition.overrides.teleport.value_or(false)) {
                state = {};
                return UpdateResult::kNone;
            }
            const LeashTeleportSettings defaults;
            teleportDistance = follower->IsPlayerRef() ? defaults.playerDistance : defaults.npcDistance;
        }
        state.cooldownRemaining = std::max(state.cooldownRemaining - a_deltaTime, 0.0F);

        SeparationReason reason{SeparationReason::kNone};
        if (!AreInCompatibleSpaces(*leader, *follower)) {
            reason = SeparationReason::kIncompatibleSpace;
        } else if (leader->GetPosition().GetDistance(follower->GetPosition()) > definition.maxLength + teleportDistance) {
            reason = SeparationReason::kExcessiveDistance;
        }

        if (reason == SeparationReason::kNone) {
            state.reason = SeparationReason::kNone;
            state.graceElapsed = 0.0F;
            return UpdateResult::kNone;
        }
        if (!CanBeginRecovery(*follower)) {
            state.reason = reason;
            state.graceElapsed = 0.0F;
            return UpdateResult::kPending;
        }
        if (state.cooldownRemaining > 0.0F) {
            return UpdateResult::kPending;
        }
        if (state.reason != reason) {
            state.reason = reason;
            state.graceElapsed = 0.0F;
        }
        state.graceElapsed += a_deltaTime;
        if (state.graceElapsed < _settings.gracePeriod) {
            return UpdateResult::kPending;
        }
        if (!leader->GetParentCell()) {
            return UpdateResult::kPending;
        }

        if (!Teleport(a_leash, reason)) {
            state.cooldownRemaining = 0.5F;
            return UpdateResult::kPending;
        }
        state.reason = SeparationReason::kNone;
        state.graceElapsed = 0.0F;
        state.cooldownRemaining = kTeleportCooldown;
        return UpdateResult::kTeleported;
    }

    bool LeashTeleportController::TeleportToPlayer(LeashInstance& a_leash) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto partnerFormID = GetPlayerPartnerFormID(a_leash.GetDefinition());
        auto* partner = RE::TESForm::LookupByID<RE::Actor>(partnerFormID);
        if (!player || !partner || partner->IsDisabled() || !CanBeginRecovery(*partner)) {
            return false;
        }
        partner->MoveTo(player);
        const auto movedToNavmesh = partner->MoveToNearestNavmesh();
        a_leash.ResetSimulation();
        SKSE::log::info("Teleported leash partner {:08X} to player after positioning; navmesh={}", partnerFormID, movedToNavmesh);
        return true;
    }

    bool LeashTeleportController::Teleport(LeashInstance& a_leash, SeparationReason a_reason) {
        const auto& definition = a_leash.GetDefinition();
        auto* leader = RE::TESForm::LookupByID<RE::Actor>(definition.GetFormID(Opposite(definition.follower)));
        auto* follower = RE::TESForm::LookupByID<RE::Actor>(definition.GetFormID(definition.follower));
        if (!leader || !follower || !leader->GetParentCell() || leader->IsDisabled() || follower->IsDisabled() || leader->IsDead(true) || follower->IsDead(true) || !CanBeginRecovery(*follower)) {
            return false;
        }

        auto destination = leader->GetPosition();
        if (const auto leaderRoot = leader->Get3D()) {
            auto backward = leaderRoot->world.rotate * RE::NiPoint3{0.0F, -kTeleportSpacing, 0.0F};
            backward.z = 0.0F;
            destination += backward;
        }
        follower->MoveTo(leader);
        bool movedToNavmesh{};
        if (!follower->IsPlayerRef() && leader->GetParentCell()->IsAttached()) {
            follower->SetPosition(destination, true);
            movedToNavmesh = follower->MoveToNearestNavmesh();
        }
        a_leash.ResetSimulation();
        const char* reasonName{};
        switch (a_reason) {
            case SeparationReason::kIncompatibleSpace:
                reasonName = "incompatible space";
                break;
            case SeparationReason::kExcessiveDistance:
                reasonName = "excessive distance";
                break;
            default:
                reasonName = "player positioning";
                break;
        }
        SKSE::log::info("Teleported follower {:08X} to leader {:08X}; reason={}; navmesh={}", definition.GetFormID(definition.follower), definition.GetFormID(Opposite(definition.follower)), reasonName, movedToNavmesh);
        return true;
    }
}  // namespace LeashFramework
