#pragma once

#include <memory>
#include <optional>

#include "../PCH.h"
#include "BindResult.h"
#include "LeashDefinition.h"
#include "LeashSide.h"

namespace LeashFramework {
    class LeashRope;

    // Where the rope meets one end this frame, before any procedural lean is applied
    struct EndSample {
        RE::Actor* actor{};               // Null for world anchors
        RE::NiPoint3 attachment;
        const RE::NiAVObject* node{};     // The node whose lean moves the attachment
        RE::TESObjectCELL* cell{};

        // The point an actor follows: the actor itself, or the fixed attachment of a world anchor
        [[nodiscard]] RE::NiPoint3 GetRoot() const { return actor ? actor->GetPosition() : attachment; }
    };

    // One end of a leash. The definition decides once, at construction, how each side attaches.
    class LeashEnd {
    public:
        LeashEnd() = default;
        LeashEnd(const LeashEnd&) = delete;
        LeashEnd& operator=(const LeashEnd&) = delete;
        virtual ~LeashEnd() = default;

        [[nodiscard]] virtual BindResult Bind(RE::Actor* a_actor) = 0;
        [[nodiscard]] virtual std::optional<EndSample> Sample(RE::Actor* a_actor) const = 0;
        [[nodiscard]] virtual RE::TESObjectCELL* GetCell(RE::Actor* a_actor) const { return a_actor ? a_actor->GetParentCell() : nullptr; }
        // Poses parts of the actor the end owns, such as a closed hand
        virtual void ApplyPose() {}
    };

    // Builds both ends. The side carrying the rope's bone 0 gets the rope; the other side gets the definition's anchor.
    [[nodiscard]] PerSide<std::unique_ptr<LeashEnd>> MakeLeashEnds(const LeashDefinition& a_definition, LeashRope& a_rope);
}  // namespace LeashFramework
