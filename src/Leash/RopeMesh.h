#pragma once

#include <cstddef>
#include <vector>

#include "../PCH.h"
#include "LeashAnchor.h"

namespace LeashFramework {
    // The skinned rope a leash simulates. Bone 0 is the end carried by the mesh owner.
    class RopeMesh {
    public:
        using Bones = std::vector<RE::NiPointer<RE::NiAVObject>>;

        RopeMesh() = default;
        RopeMesh(const RopeMesh&) = delete;
        RopeMesh& operator=(const RopeMesh&) = delete;
        virtual ~RopeMesh() = default;

        // kChanged means the bones differ from the previous call, so anything computed for them is stale.
        [[nodiscard]] virtual LeashAnchor::BindResult Bind(RE::Actor& a_owner) = 0;
        virtual void Reset() = 0;
        // Poses the parts animation doesn't drive before the neutral pose is sampled.
        virtual void PoseNeutral() {}
        // Carries the parts the solver doesn't drive with bone 0's posed transform.
        virtual void PoseFrame(const RE::NiPoint3&, const RE::NiMatrix3&) {}
        // Restores the last frame after the engine's scene update overwrote it, and shows the rope.
        virtual void Present() {}
        virtual void Hide() {}
        virtual void UpdateWorldBounds() = 0;
        // The actor node whose procedural lean moves bone a_index.
        [[nodiscard]] virtual const RE::NiAVObject& GetPoseReference(std::size_t a_index) const = 0;
        [[nodiscard]] virtual RE::NiAVObject* GetRoot() const = 0;
        [[nodiscard]] const Bones& GetBones() const { return _bones; }

    protected:
        Bones _bones;
    };
}  // namespace LeashFramework
