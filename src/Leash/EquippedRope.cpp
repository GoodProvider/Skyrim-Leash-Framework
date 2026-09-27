#include "EquippedRope.h"

#include <algorithm>
#include <span>
#include <utility>

#include "SceneGraph.h"

namespace LeashFramework {
    EquippedRope::EquippedRope(const LeashDefinition& a_definition) : _definition(a_definition) {}

    template <class... Args>
    LeashAnchor::BindResult EquippedRope::Fail(std::format_string<Args...> a_reason, Args&&... a_args) {
        if (!_warningLogged) {
            SKSE::log::warn("Unable to bind leash {:08X}->{:08X}: {}", _definition.holderFormID, _definition.leashedFormID, std::format(a_reason, std::forward<Args>(a_args)...));
            _warningLogged = true;
        }
        return LeashAnchor::BindResult::kFailed;
    }

    LeashAnchor::BindResult EquippedRope::Bind(RE::Actor& a_owner) {
        auto* root = a_owner.Get3D(false);
        // Equipment changes can detach cached nodes without replacing the actor root.
        if (root && _root.get() == root && _bones.size() >= 2 && std::ranges::all_of(_bones, [&](const auto& a_bone) { return SceneGraph::IsDescendantOf(a_bone.get(), root); })) {
            return LeashAnchor::BindResult::kUnchanged;
        }

        Reset();
        if (!root) {
            return Fail("mesh owner {:08X} has no third-person 3D", a_owner.GetFormID());
        }
        const auto& parentBone = _definition.parentBone;
        auto* parent = root->GetObjectByName(RE::BSFixedString(parentBone));
        auto* parentNode = parent ? parent->AsNode() : nullptr;
        if (!parentNode) {
            return parent ? Fail("parent bone '{}' is not a node", parentBone) : Fail("parent bone '{}' was not found", parentBone);
        }
        auto bones = SceneGraph::CollectBones(*parentNode, _definition.leashBoneMatch);
        if (bones.size() < 2) {
            return Fail("found {} child bone(s) containing '{}' under '{}'", bones.size(), _definition.leashBoneMatch, parentBone);
        }

        _root.reset(root);
        _bones = std::move(bones);
        _warningLogged = false;
        SKSE::log::info("Bound {} leash bones containing '{}' under '{}' for {:08X}->{:08X}", _bones.size(), _definition.leashBoneMatch, parentBone, _definition.holderFormID, _definition.leashedFormID);
        return LeashAnchor::BindResult::kChanged;
    }

    void EquippedRope::Reset() {
        _bones.clear();
        _root.reset();
    }

    void EquippedRope::UpdateWorldBounds() {
        SceneGraph::UpdateGeometryWorldBounds(_root.get(), [&](RE::BSGeometry& a_geometry) {
            const auto& skin = a_geometry.GetGeometryRuntimeData().skinInstance;
            if (!skin || !skin->skinData || !skin->bones || !skin->boneWorldTransforms) {
                return false;
            }
            return std::ranges::any_of(std::span{skin->bones, skin->skinData->GetBoneCount()}, [&](const RE::NiAVObject* a_bone) {
                return std::ranges::contains(_bones, a_bone, [](const auto& a_ropeBone) { return a_ropeBone.get(); });
            });
        });
    }
}  // namespace LeashFramework
