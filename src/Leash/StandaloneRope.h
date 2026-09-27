#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "../PCH.h"
#include "LeashAnchor.h"

namespace LeashFramework {
    class StandaloneRope {
    public:
        explicit StandaloneRope(StandaloneRopeSettings a_settings);
        ~StandaloneRope();
        StandaloneRope(const StandaloneRope&) = delete;
        StandaloneRope& operator=(const StandaloneRope&) = delete;

        [[nodiscard]] static bool IsSupported();
        [[nodiscard]] LeashAnchor::BindResult Bind(RE::Actor& a_leashed, std::string_view a_parent, std::string_view a_match);
        [[nodiscard]] RE::NiAVObject* GetRoot() const;
        [[nodiscard]] RE::NiAVObject* GetAttachment() const;
        [[nodiscard]] RE::NiPoint3 GetAttachmentPosition() const;
        [[nodiscard]] const std::vector<RE::NiPointer<RE::NiAVObject>>& GetBones() const;
        [[nodiscard]] const std::vector<float>& GetSegmentLengths() const;
        void SetFrame(const RE::NiPoint3& a_position, const RE::NiMatrix3& a_rotation);
        void ApplyFrame();
        void Hide();
        void Reset();

    private:
        struct NodePose {
            RE::NiPointer<RE::NiAVObject> object;
            RE::NiPoint3 position;
            RE::NiMatrix3 rotation;
        };

        [[nodiscard]] bool Load(std::string_view a_parent, std::string_view a_match);
        void CaptureNodes(RE::NiAVObject& a_object);
        void AttachTo(RE::NiNode& a_cellRoot, RE::NiNode& a_dynamicNode);
        void Detach();

        StandaloneRopeSettings _settings;
        RE::NiPointer<RE::NiNode> _root;
        RE::NiPointer<RE::NiNode> _cellRoot;
        RE::NiPointer<RE::NiNode> _dynamicNode;
        RE::NiPointer<RE::NiAVObject> _actorRoot;
        RE::NiPointer<RE::NiAVObject> _attachment;
        std::vector<RE::NiPointer<RE::NiAVObject>> _bones;
        std::vector<NodePose> _nodes;
        std::vector<float> _segmentLengths;
        RE::NiPoint3 _origin;
        RE::NiMatrix3 _basis;
        RE::NiPoint3 _framePosition;
        RE::NiMatrix3 _frameRotation;
        bool _hasFrame{};
        bool _loadFailed{};
    };
}
