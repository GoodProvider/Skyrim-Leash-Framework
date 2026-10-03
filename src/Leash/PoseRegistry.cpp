#include "PoseRegistry.h"

#include "LeashInstance.h"

namespace LeashFramework {
    namespace {
        [[nodiscard]] RE::NiPoint3 PoseWith(const LeashInstance* a_source, const EndSample& a_end) {
            auto position = a_end.attachment;
            if (a_source && a_end.node) {
                auto rotation = a_end.node->world.rotate;
                a_source->TransformPreparedPose(*a_end.node, position, rotation);
            }
            return position;
        }
    }  // namespace

    const LeashInstance* PoseRegistry::Find(const RE::Actor* a_actor) const {
        if (!a_actor) {
            return nullptr;
        }
        const auto source = _sources.find(a_actor->GetFormID());
        return source != _sources.end() ? source->second : nullptr;
    }

    RE::NiPoint3 PoseRegistry::Pose(const EndSample& a_end) const { return PoseWith(Find(a_end.actor), a_end); }

    RE::NiPoint3 PreparedLean::Pose(const EndSample& a_end) const { return PoseWith(Find(a_end.actor), a_end); }

    void PreparedLean::Pose(const RE::Actor* a_actor, const RE::NiAVObject& a_node, RE::NiPoint3& a_position, RE::NiMatrix3& a_rotation) const {
        if (const auto* source = Find(a_actor)) {
            source->TransformPreparedPose(a_node, a_position, a_rotation);
        }
    }

    RE::NiPoint3 PreparedLean::GetReachLimit(const EndSample& a_end, const RE::NiPoint3& a_anchor) const { return _source->GetLeanReachLimit(a_end, a_anchor); }
}  // namespace LeashFramework
