#include "StandaloneRope.h"

#include <algorithm>
#include <cmath>
#include <ranges>

#include "SceneGraph.h"

namespace LeashFramework {
    namespace {
        RE::NiNode* GetDynamicNode(RE::TESObjectCELL& a_cell) {
            using func_t = RE::NiAVObject*(RE::TESObjectCELL*);
            static REL::Relocation<func_t> func{REL::VariantID(18916, 19339, 0x28BF00)};
            auto* node = func(&a_cell);
            return node ? node->AsNode() : nullptr;
        }
    }

    StandaloneRope::StandaloneRope(const LeashDefinition& a_definition) : _definition(a_definition), _settings(std::get<StandaloneMesh>(a_definition.mesh)) {}

    StandaloneRope::~StandaloneRope() { Reset(); }

    LeashAnchor::BindResult StandaloneRope::Bind(RE::Actor& a_leashed) {
        auto* actorRoot = a_leashed.Get3D(false);
        auto* cell = a_leashed.GetParentCell();
        auto* loaded = cell && cell->IsAttached() ? cell->GetRuntimeData().loadedData : nullptr;
        auto* cellRoot = loaded ? loaded->cell3D.get() : nullptr;
        auto* dynamicNode = cellRoot ? GetDynamicNode(*cell) : nullptr;
        auto* attachment = actorRoot && dynamicNode ? actorRoot->GetObjectByName(RE::BSFixedString(_settings.leashedAttachment.boneName)) : nullptr;
        if (!attachment) {
            Reset();
            return LeashAnchor::BindResult::kFailed;
        }
        if (_root && _actorRoot.get() == actorRoot && _attachment.get() == attachment &&
            std::ranges::all_of(_nodes, [&](const auto& a_pose) { return SceneGraph::IsDescendantOf(a_pose.object.get(), _root.get()); })) {
            // Crossing a cell seam only changes the scene parent; the rope writes world transforms directly, so the
            // existing clone and its simulation state carry over.
            if (_root->parent != dynamicNode) {
                AttachTo(*cellRoot, *dynamicNode);
            }
            return LeashAnchor::BindResult::kUnchanged;
        }
        Reset();
        if (!Load()) {
            return LeashAnchor::BindResult::kFailed;
        }
        _actorRoot.reset(actorRoot);
        _attachment.reset(attachment);
        AttachTo(*cellRoot, *dynamicNode);
        return LeashAnchor::BindResult::kChanged;
    }

    bool StandaloneRope::Load() {
        if (_loadFailed) {
            return false;
        }
        _loadFailed = true;
        const auto& modelPath = _settings.modelPath;
        RE::NiPointer<RE::NiNode> model;
        const RE::BSModelDB::DBTraits::ArgsType args{};
        if (RE::BSModelDB::Demand(modelPath.c_str(), model, args) != RE::BSResource::ErrorCode::kNone || !model) {
            SKSE::log::warn("Standalone rope: could not load '{}'", modelPath);
            return false;
        }
        RE::NiPointer<RE::NiObject> clone{model->Clone()};
        auto* root = clone ? clone->AsNode() : nullptr;
        if (!root || root == model.get()) {
            SKSE::log::warn("Standalone rope: '{}' did not produce an independent node clone", modelPath);
            return false;
        }
        _root.reset(root);
        const auto& parentBone = _definition.parentBone;
        auto* parent = parentBone.empty() ? root : root->GetObjectByName(RE::BSFixedString(parentBone));
        if (auto* parentNode = parent ? parent->AsNode() : nullptr) {
            _bones = SceneGraph::CollectBones(*parentNode, _definition.leashBoneMatch);
        }
        if (_bones.size() < 2) {
            SKSE::log::warn("Standalone rope: '{}' needs at least two bones matching '{}' below '{}'", modelPath, _definition.leashBoneMatch, parentBone.empty() ? "<NIF root>" : parentBone);
            Reset();
            return false;
        }

        bool validSkin = true;
        bool ropeSkinned{};
        RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geometry) {
            const auto& skin = a_geometry->GetGeometryRuntimeData().skinInstance;
            if (!skin) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }
            if (!skin->skinData || !skin->bones || !skin->boneWorldTransforms || !SceneGraph::IsDescendantOf(skin->rootParent, root)) {
                validSkin = false;
                return RE::BSVisit::BSVisitControl::kContinue;
            }
            const auto count = skin->skinData->GetBoneCount();
            for (std::uint32_t index = 0; index < count; ++index) {
                const auto* bone = skin->bones[index];
                if (!SceneGraph::IsDescendantOf(bone, root) || skin->boneWorldTransforms[index] != &bone->world) {
                    validSkin = false;
                }
                ropeSkinned |= std::ranges::contains(_bones, bone, [](const auto& a_bone) { return a_bone.get(); });
            }
            return RE::BSVisit::BSVisitControl::kContinue;
        });
        if (!validSkin || !ropeSkinned) {
            SKSE::log::warn("Standalone rope: '{}' must contain its own skeleton and rope skin bindings", modelPath);
            Reset();
            return false;
        }

        RE::NiUpdateData update{};
        update.flags.set(RE::NiUpdateData::Flag::kDirty);
        update.flags.set(RE::NiUpdateData::Flag::kDisableCollision);
        root->Update(update);
        const auto& origin = _bones.front()->world;
        const auto basis = origin.rotate.Transpose();
        bool hasCollision{};
        RE::BSVisit::TraverseScenegraphObjects(root, [&](RE::NiAVObject* a_object) {
            hasCollision |= a_object->collisionObject != nullptr;
            _nodes.push_back({RE::NiPointer<RE::NiAVObject>{a_object}, basis * (a_object->world.translate - origin.translate), basis * a_object->world.rotate});
            return RE::BSVisit::BSVisitControl::kContinue;
        });
        if (hasCollision) {
            SKSE::log::warn("Standalone rope: '{}' contains collision objects; use a visual rope NIF with collision handled by RopeSolver", modelPath);
            Reset();
            return false;
        }
        const auto validSegment = [](const auto& a_segment) {
            const auto& [from, to] = a_segment;
            const auto length = from->world.translate.GetDistance(to->world.translate);
            return std::isfinite(length) && length > 0.0001F;
        };
        if (!std::ranges::all_of(_bones | std::views::pairwise, validSegment)) {
            SKSE::log::warn("Standalone rope: '{}' has an invalid neutral segment length", modelPath);
            Reset();
            return false;
        }
        _root->SetAppCulled(true);
        _loadFailed = false;
        SKSE::log::info("Standalone rope: cloned '{}' with {} rope bones", modelPath, _bones.size());
        return true;
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
            SceneGraph::UpdateWorldBoundsUpward(parent);
        }
        _dynamicNode.reset();
        _cellRoot.reset();
    }

    void StandaloneRope::Place(const RE::NiPoint3& a_position, const RE::NiMatrix3& a_rotation) {
        for (const auto& pose : _nodes) {
            pose.object->world.translate = a_position + a_rotation * pose.translate;
            pose.object->world.rotate = a_rotation * pose.rotate;
        }
    }

    void StandaloneRope::PoseNeutral() {
        const auto& attachment = _attachment->world;
        const auto& anchor = _settings.leashedAttachment;
        const RE::NiPoint3 offset{anchor.offsetX, anchor.offsetY, anchor.offsetZ};
        Place(attachment.translate + attachment.rotate * offset * attachment.scale, attachment.rotate);
    }

    void StandaloneRope::PoseFrame(const RE::NiPoint3& a_position, const RE::NiMatrix3& a_rotation) {
        _framePosition = a_position;
        _frameRotation = a_rotation;
        Place(a_position, a_rotation);
    }

    void StandaloneRope::Present() {
        Place(_framePosition, _frameRotation);
        _root->SetAppCulled(false);
    }

    void StandaloneRope::Hide() {
        if (_root) {
            _root->SetAppCulled(true);
        }
    }

    void StandaloneRope::UpdateWorldBounds() {
        SceneGraph::UpdateGeometryWorldBounds(_root.get(), [](const RE::BSGeometry&) { return true; });
    }

    void StandaloneRope::Reset() {
        Hide();
        Detach();
        _bones.clear();
        _nodes.clear();
        _root.reset();
        _attachment.reset();
        _actorRoot.reset();
    }
}  // namespace LeashFramework
