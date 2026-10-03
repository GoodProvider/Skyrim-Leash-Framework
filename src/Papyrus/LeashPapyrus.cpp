#include "LeashPapyrus.h"

#include "../Leash/LeashManager.h"
#include "../PCH.h"

namespace LeashFramework::Papyrus {
    namespace {
        constexpr std::string_view kScriptName = "LeashFramework";

        bool ApplyLeash(RE::StaticFunctionTag*, RE::Actor* a_holder, RE::Actor* a_leashed, RE::BSFixedString a_parentBone, RE::BSFixedString a_leashBoneMatch, float a_minLength, float a_maxLength, bool a_persistent) {
            return LeashManager::GetSingleton().Apply(a_holder, a_leashed, a_parentBone, a_leashBoneMatch, a_minLength, a_maxLength, a_persistent);
        }

        bool ApplyLeashToHand(RE::StaticFunctionTag*, RE::Actor* a_holder, RE::Actor* a_leashed, RE::BSFixedString a_parentBone, RE::BSFixedString a_leashBoneMatch, float a_minLength, float a_maxLength,
            bool a_persistent, bool a_rightHand) {
            return LeashManager::GetSingleton().ApplyToHand(a_holder, a_leashed, a_parentBone, a_leashBoneMatch, a_minLength, a_maxLength, a_persistent, a_rightHand);
        }

        bool ApplyLeashToBone(RE::StaticFunctionTag*, RE::Actor* a_holder, RE::Actor* a_leashed, RE::BSFixedString a_holderBone, RE::BSFixedString a_parentBone, RE::BSFixedString a_leashBoneMatch, float a_minLength,
            float a_maxLength, bool a_persistent, float a_offsetX, float a_offsetY, float a_offsetZ) {
            return LeashManager::GetSingleton().ApplyToBone(a_holder, a_leashed, a_holderBone, a_offsetX, a_offsetY, a_offsetZ, a_parentBone, a_leashBoneMatch, a_minLength, a_maxLength, a_persistent);
        }

        bool ApplyHolderOwnedLeashToBone(RE::StaticFunctionTag*, RE::Actor* a_holder, RE::Actor* a_leashed, RE::BSFixedString a_leashedBone, RE::BSFixedString a_parentBone, RE::BSFixedString a_leashBoneMatch,
            float a_minLength, float a_maxLength, bool a_persistent, float a_offsetX, float a_offsetY, float a_offsetZ, std::int32_t a_closedHand) {
            return LeashManager::GetSingleton().ApplyHolderOwnedLeashToBone(
                a_holder, a_leashed, a_leashedBone, a_offsetX, a_offsetY, a_offsetZ, a_parentBone, a_leashBoneMatch, a_minLength, a_maxLength, a_persistent, a_closedHand);
        }

        bool ApplyLeashAtPosition(RE::StaticFunctionTag*, RE::Actor* a_leashed, RE::TESObjectCELL* a_anchorCell, float a_x, float a_y, float a_z, RE::BSFixedString a_parentBone, RE::BSFixedString a_leashBoneMatch,
            float a_minLength, float a_maxLength, bool a_persistent) {
            return LeashManager::GetSingleton().ApplyAtPosition(a_leashed, a_anchorCell, a_x, a_y, a_z, a_parentBone, a_leashBoneMatch, a_minLength, a_maxLength, a_persistent);
        }

        bool ApplyStandalone(std::string_view a_function, RE::Actor* a_holder, RE::Actor* a_leashed, LeashAnchorDefinition a_anchor, const RE::BSFixedString& a_modelPath,
            const RE::BSFixedString& a_leashedBone, float a_offsetX, float a_offsetY, float a_offsetZ, const RE::BSFixedString& a_parentBone, const RE::BSFixedString& a_leashBoneMatch, float a_minLength,
            float a_maxLength, bool a_persistent) {
            if (!a_leashed) {
                SKSE::log::warn("{} rejected null leashed actor", a_function);
                return false;
            }
            const auto toString = [](const RE::BSFixedString& a_text) { return std::string{std::string_view{a_text}}; };
            return LeashManager::GetSingleton().ApplyDefinition({.holderFormID = a_holder ? a_holder->GetFormID() : 0,
                .leashedFormID = a_leashed->GetFormID(),
                .mesh = StandaloneMesh{.modelPath = toString(a_modelPath),
                    .leashedAttachment = {.boneName = toString(a_leashedBone), .offsetX = a_offsetX, .offsetY = a_offsetY, .offsetZ = a_offsetZ}},
                .anchor = std::move(a_anchor),
                .parentBone = toString(a_parentBone),
                .leashBoneMatch = toString(a_leashBoneMatch),
                .minLength = a_minLength,
                .maxLength = a_maxLength,
                .persistent = a_persistent});
        }

        bool ApplyStandaloneLeash(RE::StaticFunctionTag*, RE::Actor* a_holder, RE::Actor* a_leashed, RE::BSFixedString a_modelPath, RE::BSFixedString a_leashedBone, RE::BSFixedString a_parentBone,
            RE::BSFixedString a_leashBoneMatch, float a_minLength, float a_maxLength, bool a_persistent, bool a_rightHand, float a_offsetX, float a_offsetY, float a_offsetZ) {
            if (!a_holder) {
                SKSE::log::warn("ApplyStandaloneLeash rejected null holder");
                return false;
            }
            return ApplyStandalone("ApplyStandaloneLeash", a_holder, a_leashed, HandAnchor{.rightHand = a_rightHand}, a_modelPath, a_leashedBone, a_offsetX, a_offsetY, a_offsetZ, a_parentBone,
                a_leashBoneMatch, a_minLength, a_maxLength, a_persistent);
        }

        bool ApplyStandaloneLeashToBone(RE::StaticFunctionTag*, RE::Actor* a_holder, RE::Actor* a_leashed, RE::BSFixedString a_modelPath, RE::BSFixedString a_leashedBone, RE::BSFixedString a_holderBone,
            RE::BSFixedString a_parentBone, RE::BSFixedString a_leashBoneMatch, float a_minLength, float a_maxLength, bool a_persistent, float a_leashedOffsetX, float a_leashedOffsetY, float a_leashedOffsetZ,
            float a_holderOffsetX, float a_holderOffsetY, float a_holderOffsetZ) {
            if (!a_holder) {
                SKSE::log::warn("ApplyStandaloneLeashToBone rejected null holder");
                return false;
            }
            const ActorBoneAnchor anchor{.boneName = std::string{std::string_view{a_holderBone}}, .offsetX = a_holderOffsetX, .offsetY = a_holderOffsetY, .offsetZ = a_holderOffsetZ};
            return ApplyStandalone("ApplyStandaloneLeashToBone", a_holder, a_leashed, anchor, a_modelPath, a_leashedBone, a_leashedOffsetX, a_leashedOffsetY, a_leashedOffsetZ, a_parentBone,
                a_leashBoneMatch, a_minLength, a_maxLength, a_persistent);
        }

        bool ApplyStandaloneLeashAtPosition(RE::StaticFunctionTag*, RE::Actor* a_leashed, RE::TESObjectCELL* a_anchorCell, float a_x, float a_y, float a_z, RE::BSFixedString a_modelPath,
            RE::BSFixedString a_leashedBone, RE::BSFixedString a_parentBone, RE::BSFixedString a_leashBoneMatch, float a_minLength, float a_maxLength, bool a_persistent, float a_offsetX, float a_offsetY,
            float a_offsetZ) {
            if (!a_anchorCell) {
                SKSE::log::warn("ApplyStandaloneLeashAtPosition rejected null cell");
                return false;
            }
            return ApplyStandalone("ApplyStandaloneLeashAtPosition", nullptr, a_leashed, WorldPositionAnchor{.cellFormID = a_anchorCell->GetFormID(), .x = a_x, .y = a_y, .z = a_z}, a_modelPath,
                a_leashedBone, a_offsetX, a_offsetY, a_offsetZ, a_parentBone, a_leashBoneMatch, a_minLength, a_maxLength, a_persistent);
        }

        bool DisconnectLeash(RE::StaticFunctionTag*, RE::Actor* a_holder, RE::Actor* a_leashed) { return LeashManager::GetSingleton().Disconnect(a_holder, a_leashed); }

        bool UnleashAll(RE::StaticFunctionTag*, RE::Actor* a_actor) { return LeashManager::GetSingleton().UnleashAll(a_actor); }

        bool IsLeashed(RE::StaticFunctionTag*, RE::Actor* a_actor) { return LeashManager::GetSingleton().IsLeashed(a_actor); }

        bool IsLeashHolder(RE::StaticFunctionTag*, RE::Actor* a_actor) { return LeashManager::GetSingleton().IsLeashHolder(a_actor); }

        RE::Actor* GetLeashHolder(RE::StaticFunctionTag*, RE::Actor* a_leashed) { return LeashManager::GetSingleton().GetLeashHolder(a_leashed); }

        std::vector<RE::Actor*> GetLeashedActors(RE::StaticFunctionTag*, RE::Actor* a_holder) { return LeashManager::GetSingleton().GetLeashedActors(a_holder); }

        float GetMinLeashLength(RE::StaticFunctionTag*, RE::Actor* a_leashed) { return LeashManager::GetSingleton().GetMinLength(a_leashed); }

        float GetMaxLeashLength(RE::StaticFunctionTag*, RE::Actor* a_leashed) { return LeashManager::GetSingleton().GetMaxLength(a_leashed); }

        bool SetMinLeashLength(RE::StaticFunctionTag*, RE::Actor* a_leashed, float a_length) { return LeashManager::GetSingleton().SetMinLength(a_leashed, a_length); }

        bool SetMaxLeashLength(RE::StaticFunctionTag*, RE::Actor* a_leashed, float a_length) { return LeashManager::GetSingleton().SetMaxLength(a_leashed, a_length); }

        bool SetRagdollOverride(RE::StaticFunctionTag*, RE::Actor* a_leashed, std::int32_t a_mode) { return LeashManager::GetSingleton().SetRagdollOverride(a_leashed, a_mode); }

        bool SetTeleportOverride(RE::StaticFunctionTag*, RE::Actor* a_leashed, std::int32_t a_mode) { return LeashManager::GetSingleton().SetTeleportOverride(a_leashed, a_mode); }

        bool SetPreventOverstretchOverride(RE::StaticFunctionTag*, RE::Actor* a_leashed, std::int32_t a_mode) { return LeashManager::GetSingleton().SetPreventOverstretchOverride(a_leashed, a_mode); }

        std::int32_t GetLeashFollower(RE::StaticFunctionTag*, RE::Actor* a_leashed) { return LeashManager::GetSingleton().GetFollower(a_leashed); }

        bool SetLeashFollower(RE::StaticFunctionTag*, RE::Actor* a_leashed, std::int32_t a_follower) { return LeashManager::GetSingleton().SetFollower(a_leashed, a_follower); }
    }  // namespace

    bool Register(RE::BSScript::IVirtualMachine* a_vm) {
        if (!a_vm) {
            return false;
        }

        a_vm->RegisterFunction("ApplyLeash", kScriptName, ApplyLeash);
        a_vm->RegisterFunction("ApplyLeashToHand", kScriptName, ApplyLeashToHand);
        a_vm->RegisterFunction("ApplyLeashToBone", kScriptName, ApplyLeashToBone);
        a_vm->RegisterFunction("ApplyHolderOwnedLeashToBone", kScriptName, ApplyHolderOwnedLeashToBone);
        a_vm->RegisterFunction("ApplyLeashAtPosition", kScriptName, ApplyLeashAtPosition);
        a_vm->RegisterFunction("ApplyStandaloneLeash", kScriptName, ApplyStandaloneLeash);
        a_vm->RegisterFunction("ApplyStandaloneLeashToBone", kScriptName, ApplyStandaloneLeashToBone);
        a_vm->RegisterFunction("ApplyStandaloneLeashAtPosition", kScriptName, ApplyStandaloneLeashAtPosition);
        a_vm->RegisterFunction("DisconnectLeash", kScriptName, DisconnectLeash);
        a_vm->RegisterFunction("UnleashAll", kScriptName, UnleashAll);
        a_vm->RegisterFunction("IsLeashed", kScriptName, IsLeashed);
        a_vm->RegisterFunction("IsLeashHolder", kScriptName, IsLeashHolder);
        a_vm->RegisterFunction("GetLeashHolder", kScriptName, GetLeashHolder);
        a_vm->RegisterFunction("GetLeashedActors", kScriptName, GetLeashedActors);
        a_vm->RegisterFunction("GetMinLeashLength", kScriptName, GetMinLeashLength);
        a_vm->RegisterFunction("GetMaxLeashLength", kScriptName, GetMaxLeashLength);
        a_vm->RegisterFunction("SetMinLeashLength", kScriptName, SetMinLeashLength);
        a_vm->RegisterFunction("SetMaxLeashLength", kScriptName, SetMaxLeashLength);
        a_vm->RegisterFunction("SetRagdollOverride", kScriptName, SetRagdollOverride);
        a_vm->RegisterFunction("SetTeleportOverride", kScriptName, SetTeleportOverride);
        a_vm->RegisterFunction("SetPreventOverstretchOverride", kScriptName, SetPreventOverstretchOverride);
        a_vm->RegisterFunction("GetLeashFollower", kScriptName, GetLeashFollower);
        a_vm->RegisterFunction("SetLeashFollower", kScriptName, SetLeashFollower);
        SKSE::log::info("Registered {} Papyrus API", kScriptName);
        return true;
    }
}  // namespace LeashFramework::Papyrus
