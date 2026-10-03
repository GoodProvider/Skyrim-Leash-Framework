#pragma once

#include <unordered_map>

#include "../PCH.h"
#include "LeashEnd.h"

namespace LeashFramework {
    class LeashInstance;

    // Leans that are final for this frame, by leashed actor. The manager publishes each leash after it ticks, so a
    // holder's lean is only visible to leashes sorted after its own.
    class PoseRegistry {
    public:
        void Clear() { _sources.clear(); }
        void Reserve(std::size_t a_count) { _sources.reserve(a_count); }
        void Publish(RE::FormID a_actorFormID, const LeashInstance& a_source) { _sources.insert_or_assign(a_actorFormID, &a_source); }
        [[nodiscard]] const LeashInstance* Find(const RE::Actor* a_actor) const;

        [[nodiscard]] RE::NiPoint3 Pose(const EndSample& a_end) const;

    private:
        std::unordered_map<RE::FormID, const LeashInstance*> _sources;
    };

    // Proof that a leash prepared its leashed actor's lean this frame. Posing that actor, or anything it carries,
    // goes through this so it can't be read before Prepare.
    class PreparedLean {
    public:
        [[nodiscard]] RE::NiPoint3 Pose(const EndSample& a_end) const;
        void Pose(const RE::Actor* a_actor, const RE::NiAVObject& a_node, RE::NiPoint3& a_position, RE::NiMatrix3& a_rotation) const;
        // The furthest a_end's attachment can lean toward a_anchor. a_end must be the prepared actor's end.
        [[nodiscard]] RE::NiPoint3 GetReachLimit(const EndSample& a_end, const RE::NiPoint3& a_anchor) const;

    private:
        friend class LeashInstance;

        PreparedLean(const PoseRegistry& a_registry, const RE::Actor& a_actor, const LeashInstance& a_source) : _registry(a_registry), _actor(&a_actor), _source(&a_source) {}

        [[nodiscard]] const LeashInstance* Find(const RE::Actor* a_actor) const { return a_actor == _actor ? _source : _registry.Find(a_actor); }

        const PoseRegistry& _registry;
        const RE::Actor* _actor;
        const LeashInstance* _source;
    };
}  // namespace LeashFramework
