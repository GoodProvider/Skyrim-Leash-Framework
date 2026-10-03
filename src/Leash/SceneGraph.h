#pragma once

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

#include "../PCH.h"

namespace LeashFramework::SceneGraph {
    // Detached nodes remain alive through NiPointer but no longer lead to the root through parent links
    [[nodiscard]] inline bool IsDescendantOf(const RE::NiAVObject* a_object, const RE::NiAVObject* a_root) {
        for (auto* object = a_object; object; object = object->parent) {
            if (object == a_root) {
                return true;
            }
        }
        return false;
    }

    // Nodes below a_parent whose names contain a_match, in traversal order
    [[nodiscard]] inline std::vector<RE::NiPointer<RE::NiAVObject>> CollectBones(RE::NiNode& a_parent, std::string_view a_match) {
        std::vector<RE::NiPointer<RE::NiAVObject>> bones;
        for (const auto& child : a_parent.GetChildren()) {
            RE::BSVisit::TraverseScenegraphObjects(child.get(), [&](RE::NiAVObject* a_object) {
                if (a_object->AsNode() && std::string_view{a_object->name}.contains(a_match)) {
                    bones.emplace_back(a_object);
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });
        }
        return bones;
    }

    struct ResolvedNode {
        RE::NiAVObject* root{};
        RE::NiAVObject* object{};
    };

    [[nodiscard]] inline ResolvedNode ResolveActorNode(RE::Actor& a_actor, std::string_view a_name) {
        std::array<RE::NiAVObject*, 3> roots{a_actor.Get3D()};
        if (a_actor.IsPlayerRef()) {
            roots[1] = a_actor.Get3D(false);
            roots[2] = a_actor.Get3D(true);
        }

        RE::NiAVObject* firstRoot{};
        for (std::size_t index = 0; index < roots.size(); ++index) {
            auto* root = roots[index];
            const auto previousEnd = roots.begin() + static_cast<std::ptrdiff_t>(index);
            if (!root || std::ranges::find(roots.begin(), previousEnd, root) != previousEnd) {
                continue;
            }
            firstRoot = firstRoot ? firstRoot : root;
            if (auto* object = root->GetObjectByName(RE::BSFixedString(a_name))) {
                return {.root = root, .object = object};
            }
        }
        return {.root = firstRoot};
    }

    inline void UpdateWorldBoundsUpward(RE::NiAVObject* a_object) {
        for (auto* object = a_object; object; object = object->parent) {
            object->UpdateWorldBound();
        }
    }

    // Moved rope geometry keeps its old bounds unless they're refreshed along with every node above it, which gets it culled
    template <class Predicate>
    void UpdateGeometryWorldBounds(RE::NiAVObject* a_root, Predicate a_predicate) {
        RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
            if (a_predicate(*a_geometry)) {
                UpdateWorldBoundsUpward(a_geometry);
            }
            return RE::BSVisit::BSVisitControl::kContinue;
        });
    }
}  // namespace LeashFramework::SceneGraph
