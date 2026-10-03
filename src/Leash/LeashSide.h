#pragma once

#include <array>
#include <cstdint>

namespace LeashFramework {
    // The two ends of the holder/leashed relationship. Movement roles and rope ownership are expressed as sides, so
    // code that needs "the other end" asks for Opposite() instead of branching on which side it is.
    enum class LeashSide : std::uint8_t { kLeashed, kHolder };

    inline constexpr std::array kLeashSides{LeashSide::kLeashed, LeashSide::kHolder};

    [[nodiscard]] constexpr LeashSide Opposite(LeashSide a_side) noexcept { return a_side == LeashSide::kLeashed ? LeashSide::kHolder : LeashSide::kLeashed; }

    [[nodiscard]] constexpr const char* GetSideName(LeashSide a_side) noexcept { return a_side == LeashSide::kHolder ? "holder" : "leashed"; }

    template <class T>
    struct PerSide {
        T leashed{};
        T holder{};

        [[nodiscard]] constexpr T& operator[](LeashSide a_side) noexcept { return a_side == LeashSide::kLeashed ? leashed : holder; }
        [[nodiscard]] constexpr const T& operator[](LeashSide a_side) const noexcept { return a_side == LeashSide::kLeashed ? leashed : holder; }
    };

    template <class T>
    struct Roles {
        const T& follower;
        const T& leader;
    };

    // The only place a follower side becomes a follower/leader pair
    template <class T>
    [[nodiscard]] constexpr Roles<T> Orient(const PerSide<T>& a_values, LeashSide a_follower) noexcept {
        return {a_values[a_follower], a_values[Opposite(a_follower)]};
    }

    template <class T>
    Roles<T> Orient(const PerSide<T>&&, LeashSide) = delete;
}  // namespace LeashFramework
