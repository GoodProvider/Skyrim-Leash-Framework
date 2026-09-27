#pragma once

#include <array>
#include <glaze/glaze.hpp>
#include <string>
#include <vector>

#include "../PCH.h"
#include "../Animation/PullPoseController.h"
#include "../Hooks/FrameHook.h"
#include "../Leash/LeashTeleportController.h"
#include "../Leash/PullController.h"
#include "../Movement/LeashMovementConstraint.h"
#include "../Physics/SimulationSettings.h"
#include "../Recovery/ForcedRecoveryController.h"

template <>
struct glz::meta<RE::NiPoint3> {
    using T = RE::NiPoint3;
    static constexpr auto value = glz::object(&T::x, &T::y, &T::z);
};

namespace LeashFramework::UI {
    struct ArmorEntry {
        char modName[128];
        char formID[64];

        struct glaze {
            using T = ArmorEntry;
            static constexpr auto value = glz::object(&T::modName, &T::formID);
        };
    };

    struct TestLeashPreset {
        std::string name;
        bool holderOwnsLeash{};
        bool standaloneRope{};
        int anchorType{};
        std::string attachmentBone;
        RE::NiPoint3 attachmentOffset{};
        int closedHand{};
        std::string ropeModelPath;
        std::string leashedAttachmentBone;
        RE::NiPoint3 leashedAttachmentOffset{};
        std::string parentBone;
        std::string leashBoneMatch;
        float minLength{};
        float maxLength{};
        bool persistent{};
    };

    struct DebugSettings {
        char parentBone[128]{"NPC Spine2 [Spn2]"};
        char leashBoneMatch[128]{"Leash1_1"};
        float minLength{200.0F};
        float maxLength{300.0F};
        RE::NiPoint3 attachmentOffset{};
        bool holderOwnsLeash{};
        bool standaloneRope{};
        char ropeModelPath[260]{};
        char leashedAttachmentBone[128]{"NPC Neck [Neck]"};
        RE::NiPoint3 leashedAttachmentOffset{};
        int closedHand{};
        bool persistent{true};
        bool enablePullDiagnostics{};
        std::array<ArmorEntry, 5> armorEntries{ArmorEntry{"Leash.esm", "800 #Body Rope"}, ArmorEntry{"Leash.esm", "804 #Neck Rope"}, ArmorEntry{"Leash.esm", "806 #Neck Chain"},
            ArmorEntry{"Leash.esm", "32ce #Magic Rope"}, ArmorEntry{"Leash.esm", "d69 #Leasher-held shield Leash"}};
        std::vector<TestLeashPreset> testLeashPresets;

        struct glaze {
            using T = DebugSettings;
            static constexpr auto value = glz::object(
                &T::parentBone, &T::leashBoneMatch, &T::minLength, &T::maxLength, &T::attachmentOffset, &T::holderOwnsLeash, &T::standaloneRope, &T::ropeModelPath,
                &T::leashedAttachmentBone, &T::leashedAttachmentOffset, &T::closedHand, &T::persistent, &T::enablePullDiagnostics, &T::armorEntries,
                &T::testLeashPresets);
        };
    };

    struct ModMenuSettings {
        Hooks::FrameHookSettings frameHook;
        Physics::SimulationSettings simulation;
        Animation::PullPoseSettings pullPose;
        LocomotionSettings locomotion;
        Movement::HolderMovementSettings holderMovement;
        Recovery::ForcedRecoverySettings recovery;
        LeashTeleportSettings teleport;
        DebugSettings debug;
    };
}
