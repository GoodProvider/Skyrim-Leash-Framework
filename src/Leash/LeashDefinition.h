#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace LeashFramework {
    enum class ClosedHand : std::uint8_t { kNone, kRight, kLeft };

    struct HandAnchor {
        bool rightHand{true};
    };

    struct ActorBoneAnchor {
        std::string boneName{};
        float offsetX{};
        float offsetY{};
        float offsetZ{};
    };

    struct WorldPositionAnchor {
        std::uint32_t cellFormID{};
        float x{};
        float y{};
        float z{};
    };

    using LeashAnchorDefinition = std::variant<HandAnchor, ActorBoneAnchor, WorldPositionAnchor>;

    struct LeashedMesh {};

    struct HolderMesh {
        ClosedHand closedHand{ClosedHand::kNone};
    };

    struct StandaloneMesh {
        std::string modelPath;
        ActorBoneAnchor leashedAttachment{"NPC Neck [Neck]"};
    };

    using LeashMeshDefinition = std::variant<LeashedMesh, HolderMesh, StandaloneMesh>;

    struct LeashOverrides {
        std::optional<bool> ragdoll;
        std::optional<bool> teleport;
        std::optional<bool> preventOverstretch;
    };

    struct LeashDefinition {
        std::uint32_t holderFormID{};
        std::uint32_t leashedFormID{};
        LeashMeshDefinition mesh{};
        LeashAnchorDefinition anchor{};
        std::string parentBone{};
        std::string leashBoneMatch{};
        float minLength{};
        float maxLength{};
        bool persistent{};
        LeashOverrides overrides{};

        [[nodiscard]] bool HolderOwnsMesh() const { return std::holds_alternative<HolderMesh>(mesh); }
    };
}  // namespace LeashFramework
