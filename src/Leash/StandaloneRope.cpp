#include "StandaloneRope.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace LeashFramework {
    namespace {
        bool IsDescendantOf(const RE::NiAVObject* a_object, const RE::NiAVObject* a_root) {
            for (auto* object = a_object; object; object = object->parent) {
                if (object == a_root) {
                    return true;
                }
            }
            return false;
        }

        void CollectBones(RE::NiAVObject& a_object, std::string_view a_match, std::vector<RE::NiPointer<RE::NiAVObject>>& a_bones) {
            if (a_object.AsNode() && std::string_view(a_object.name).contains(a_match)) {
                a_bones.emplace_back(&a_object);
            }
            if (auto* node = a_object.AsNode()) {
                for (const auto& child : node->GetChildren()) {
                    if (child) {
                        CollectBones(*child, a_match, a_bones);
                    }
                }
            }
        }

        RE::NiNode* GetDynamicNode(RE::TESObjectCELL& a_cell) {
            using func_t = RE::NiAVObject*(RE::TESObjectCELL*);
            static REL::Relocation<func_t> func{REL::VariantID(18916, 19339, 0x28BF00)};
            auto* node = func(&a_cell);
            return node ? node->AsNode() : nullptr;
        }
    }

    StandaloneRope::StandaloneRope(StandaloneRopeSettings a_settings) : _settings(std::move(a_settings)) {}

    StandaloneRope::~StandaloneRope() { Reset(); }

    bool StandaloneRope::Load(std::string_view a_parent, std::string_view a_match) {
        if (_loadFailed) {
            return false;
        }
        _loadFailed = true;
        RE::NiPointer<RE::NiNode> model;
        const RE::BSModelDB::DBTraits::ArgsType args{};
        if (RE::BSModelDB::Demand(_settings.modelPath.c_str(), model, args) != RE::BSResource::ErrorCode::kNone || !model) {
            SKSE::log::warn("Standalone rope: could not load '{}'", _settings.modelPath);
            return false;
        }
        RE::NiPointer<RE::NiObject> clone{model->Clone()};
        auto* root = clone ? clone->AsNode() : nullptr;
        if (!root || root == model.get()) {
            SKSE::log::warn("Standalone rope: '{}' did not produce an independent node clone", _settings.modelPath);
            return false;
        }
        _root.reset(root);
        auto* parent = a_parent.empty() ? root : root->GetObjectByName(RE::BSFixedString(a_parent));
        auto* parentNode = parent ? parent->AsNode() : nullptr;
        if (parentNode) {
            for (const auto& child : parentNode->GetChildren()) {
                if (child) {
                    CollectBones(*child, a_match, _bones);
                }
            }
        }
        if (_bones.size() < 2) {
            SKSE::log::warn("Standalone rope: '{}' needs at least two bones matching '{}' below '{}'", _settings.modelPath, a_match, a_parent.empty() ? "<NIF root>" : a_parent);
            Reset();
            return false;
        }

        bool validSkin = true;
        bool ropeSkinned{};
        RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geometry) {
            const auto skin = a_geometry->GetGeometryRuntimeData().skinInstance;
            if (!skin) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }
            if (!skin->skinData || !skin->bones || !skin->boneWorldTransforms || !IsDescendantOf(skin->rootParent, root)) {
                validSkin = false;
                return RE::BSVisit::BSVisitControl::kContinue;
            }
            const auto count = skin->skinData->GetBoneCount();
            for (std::uint32_t index = 0; index < count; ++index) {
                const auto* bone = skin->bones[index];
                if (!IsDescendantOf(bone, root) || skin->boneWorldTransforms[index] != &bone->world) {
                    validSkin = false;
                }
                ropeSkinned |= std::ranges::any_of(_bones, [&](const auto& a_bone) { return a_bone.get() == bone; });
            }
            return RE::BSVisit::BSVisitControl::kContinue;
        });
        if (!validSkin || !ropeSkinned) {
            SKSE::log::warn("Standalone rope: '{}' must contain its own skeleton and rope skin bindings", _settings.modelPath);
            Reset();
            return false;
        }

        RE::NiUpdateData update{};
        update.flags.set(RE::NiUpdateData::Flag::kDirty);
        update.flags.set(RE::NiUpdateData::Flag::kDisableCollision);
        root->Update(update);
        CaptureNodes(*root);
        if (std::ranges::any_of(_nodes, [](const auto& a_pose) { return a_pose.object->collisionObject != nullptr; })) {
            SKSE::log::warn("Standalone rope: '{}' contains collision objects; use a visual rope NIF with collision handled by RopeSolver", _settings.modelPath);
            Reset();
            return false;
        }
        for (std::size_t index = 1; index < _bones.size(); ++index) {
            const auto length = _bones[index - 1]->world.translate.GetDistance(_bones[index]->world.translate);
            if (!std::isfinite(length) || length <= 0.0001F) {
                SKSE::log::warn("Standalone rope: '{}' has an invalid neutral segment length", _settings.modelPath);
                Reset();
                return false;
            }
            _segmentLengths.push_back(length);
        }
        _origin = _bones.front()->world.translate;
        _basis = _bones.front()->world.rotate.Transpose();
        _root->SetAppCulled(true);
        _loadFailed = false;
        SKSE::log::info("Standalone rope: cloned '{}' with {} rope bones", _settings.modelPath, _bones.size());
        return true;
    }

    void StandaloneRope::CaptureNodes(RE::NiAVObject& a_object) {
        _nodes.push_back({RE::NiPointer<RE::NiAVObject>{&a_object}, a_object.world.translate, a_object.world.rotate});
        if (auto* node = a_object.AsNode()) {
            for (const auto& child : node->GetChildren()) {
                if (child) {
                    CaptureNodes(*child);
                }
            }
        }
    }

    LeashAnchor::BindResult StandaloneRope::Bind(RE::Actor& a_leashed, std::string_view a_parent, std::string_view a_match) {
        auto* actorRoot = a_leashed.Get3D(false);
        auto* cell = a_leashed.GetParentCell();
        auto* loaded = cell && cell->IsAttached() ? cell->GetRuntimeData().loadedData : nullptr;
        auto* cellRoot = loaded ? loaded->cell3D.get() : nullptr;
        auto* dynamicNode = cellRoot ? GetDynamicNode(*cell) : nullptr;
        if (!actorRoot || !dynamicNode) {
            Reset();
            return LeashAnchor::BindResult::kFailed;
        }
        auto* attachment = actorRoot->GetObjectByName(RE::BSFixedString(_settings.leashedAttachment.boneName));
        if (!attachment || !IsDescendantOf(attachment, actorRoot)) {
            Reset();
            return LeashAnchor::BindResult::kFailed;
        }
        if (_root && _actorRoot.get() == actorRoot && _attachment.get() == attachment &&
            std::ranges::all_of(_nodes, [&](const auto& a_pose) { return IsDescendantOf(a_pose.object.get(), _root.get()); })) {
            // Crossing a cell seam only changes the scene parent; the rope writes world transforms directly, so the
            // existing clone and its simulation state carry over.
            if (_root->parent != dynamicNode) {
                AttachTo(*cellRoot, *dynamicNode);
            }
            return LeashAnchor::BindResult::kUnchanged;
        }
        Reset();
        if (!Load(a_parent, a_match)) {
            return LeashAnchor::BindResult::kFailed;
        }
        _actorRoot.reset(actorRoot);
        _attachment.reset(attachment);
        AttachTo(*cellRoot, *dynamicNode);
        return LeashAnchor::BindResult::kChanged;
    }

    void StandaloneRope::AttachTo(RE::NiNode& a_cellRoot, RE::NiNode& a_dynamicNode) {
        Detach();
        _cellRoot.reset(&a_cellRoot);
        _dynamicNode.reset(&a_dynamicNode);
        a_dynamicNode.AttachChild(_root.get(), true);
    }

    void StandaloneRope::Detach() {
        if (_root && _root->parent) {
            auto* parent = _root->parent;
            parent->DetachChild(_root.get());
            for (auto* node = parent; node; node = node->parent) {
                node->UpdateWorldBound();
            }
        }
        _dynamicNode.reset();
        _cellRoot.reset();
    }

    RE::NiAVObject* StandaloneRope::GetRoot() const { return _root.get(); }
    RE::NiAVObject* StandaloneRope::GetAttachment() const { return _attachment.get(); }
    const std::vector<RE::NiPointer<RE::NiAVObject>>& StandaloneRope::GetBones() const { return _bones; }
    const std::vector<float>& StandaloneRope::GetSegmentLengths() const { return _segmentLengths; }

    RE::NiPoint3 StandaloneRope::GetAttachmentPosition() const {
        const auto& anchor = _settings.leashedAttachment;
        const RE::NiPoint3 offset{anchor.offsetX, anchor.offsetY, anchor.offsetZ};
        return _attachment->world.translate + _attachment->world.rotate * offset * _attachment->world.scale;
    }

    void StandaloneRope::SetFrame(const RE::NiPoint3& a_position, const RE::NiMatrix3& a_rotation) {
        _framePosition = a_position;
        _frameRotation = a_rotation * _basis;
        _hasFrame = true;
    }

    void StandaloneRope::ApplyFrame() {
        if (!_hasFrame || !_root) {
            return;
        }
        for (const auto& pose : _nodes) {
            pose.object->world.translate = _framePosition + _frameRotation * (pose.position - _origin);
            pose.object->world.rotate = _frameRotation * pose.rotation;
        }
        _root->SetAppCulled(false);
    }

    void StandaloneRope::Hide() {
        _hasFrame = false;
        if (_root) {
            _root->SetAppCulled(true);
        }
    }

    void StandaloneRope::Reset() {
        Hide();
        Detach();
        _bones.clear();
        _nodes.clear();
        _segmentLengths.clear();
        _root.reset();
        _attachment.reset();
        _actorRoot.reset();
    }
}
