#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "LeashSide.h"

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
        // The side that gets pulled; the other side leads. Procedural lean always stays on the leashed actor.
        LeashSide follower{LeashSide::kLeashed};

        [[nodiscard]] bool HolderOwnsMesh() const { return std::holds_alternative<HolderMesh>(mesh); }
        // The side that carries rope bone 0
        [[nodiscard]] LeashSide GetMeshSide() const { return HolderOwnsMesh() ? LeashSide::kHolder : LeashSide::kLeashed; }
        [[nodiscard]] std::uint32_t GetFormID(LeashSide a_side) const { return a_side == LeashSide::kHolder ? holderFormID : leashedFormID; }
        [[nodiscard]] std::optional<LeashSide> FindSide(std::uint32_t a_formID) const {
            for (const auto side : kLeashSides) {
                if (a_formID != 0 && GetFormID(side) == a_formID) {
                    return side;
                }
            }
            return std::nullopt;
        }
    };
}  // namespace LeashFramework
