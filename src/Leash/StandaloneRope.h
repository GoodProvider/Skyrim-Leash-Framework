#pragma once

#include <vector>

#include "../PCH.h"
#include "LeashDefinition.h"
#include "RopeMesh.h"

namespace LeashFramework {
    // A rope NIF cloned into the leashed actor's cell. Nodes the solver doesn't drive move rigidly with bone 0.
    class StandaloneRope final : public RopeMesh {
    public:
        explicit StandaloneRope(const LeashDefinition& a_definition);
        ~StandaloneRope() override;

        [[nodiscard]] LeashAnchor::BindResult Bind(RE::Actor& a_leashed) override;
        void Reset() override;
        void PoseNeutral() override;
        void PoseFrame(const RE::NiPoint3& a_position, const RE::NiMatrix3& a_rotation) override;
        void Present() override;
        void Hide() override;
        void UpdateWorldBounds() override;
        [[nodiscard]] const RE::NiAVObject& GetPoseReference(std::size_t) const override { return *_attachment; }
        [[nodiscard]] RE::NiAVObject* GetRoot() const override { return _root.get(); }

    private:
        // Transform relative to bone 0 in the loaded model
        struct NodePose {
            RE::NiPointer<RE::NiAVObject> object;
            RE::NiPoint3 translate;
            RE::NiMatrix3 rotate;
        };

        [[nodiscard]] bool Load();
        void Place(const RE::NiPoint3& a_position, const RE::NiMatrix3& a_rotation);
        void AttachTo(RE::NiNode& a_cellRoot, RE::NiNode& a_dynamicNode);
        void Detach();

        const LeashDefinition& _definition;
        const StandaloneMesh& _settings;
        RE::NiPointer<RE::NiNode> _root;
        // Held while attached so the rope's parent chain stays valid until Detach, even if the cell unloads first
        RE::NiPointer<RE::NiNode> _cellRoot;
        RE::NiPointer<RE::NiNode> _dynamicNode;
        RE::NiPointer<RE::NiAVObject> _actorRoot;
        RE::NiPointer<RE::NiAVObject> _attachment;
        std::vector<NodePose> _nodes;
        RE::NiPoint3 _framePosition;
        RE::NiMatrix3 _frameRotation;
        bool _loadFailed{};
    };
}  // namespace LeashFramework
