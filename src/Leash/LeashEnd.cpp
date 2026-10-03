#include "LeashEnd.h"

#include <type_traits>

#include "HandGrip.h"
#include "LeashRope.h"
#include "SceneGraph.h"

namespace LeashFramework {
    namespace {
        // The actor carrying the rope's bone 0, optionally closing a hand around it
        class RopeEnd final : public LeashEnd {
        public:
            RopeEnd(LeashRope& a_rope, ClosedHand a_closedHand, RE::FormID a_actorFormID) : _rope(a_rope), _closedHand(a_closedHand), _actorFormID(a_actorFormID) {}

            [[nodiscard]] BindResult Bind(RE::Actor* a_actor) override {
                if (!a_actor) {
                    return BindResult::kFailed;
                }
                const auto result = _rope.Bind(*a_actor);
                // A missing rope must not close the hand around nothing
                if (result != BindResult::kFailed && _closedHand != ClosedHand::kNone) {
                    static_cast<void>(_grip.Bind(a_actor, _closedHand == ClosedHand::kRight, _actorFormID, "holder grip"));
                }
                return result;
            }

            [[nodiscard]] std::optional<EndSample> Sample(RE::Actor* a_actor) const override {
                if (!a_actor) {
                    return std::nullopt;
                }
                return _rope.SampleRoot(*a_actor);
            }

            void ApplyPose() override {
                if (_closedHand != ClosedHand::kNone) {
                    _grip.ApplyPose();
                }
            }

        private:
            LeashRope& _rope;
            HandGrip _grip;
            ClosedHand _closedHand;
            RE::FormID _actorFormID;
        };

        class HandEnd final : public LeashEnd {
        public:
            HandEnd(bool a_rightHand, RE::FormID a_actorFormID) : _rightHand(a_rightHand), _actorFormID(a_actorFormID) {}

            [[nodiscard]] BindResult Bind(RE::Actor* a_actor) override { return _grip.Bind(a_actor, _rightHand, _actorFormID, "attachment actor"); }

            [[nodiscard]] std::optional<EndSample> Sample(RE::Actor* a_actor) const override {
                const auto point = _grip.GetGripPoint();
                if (!a_actor || !point) {
                    return std::nullopt;
                }
                return EndSample{.actor = a_actor, .attachment = *point, .node = _grip.GetHand(), .cell = a_actor->GetParentCell()};
            }

            void ApplyPose() override { _grip.ApplyPose(); }

        private:
            HandGrip _grip;
            bool _rightHand;
            RE::FormID _actorFormID;
        };

        class BoneEnd final : public LeashEnd {
        public:
            BoneEnd(const ActorBoneAnchor& a_anchor, RE::FormID a_actorFormID) : _anchor(a_anchor), _actorFormID(a_actorFormID) {}

            [[nodiscard]] BindResult Bind(RE::Actor* a_actor) override {
                const auto resolved = a_actor ? SceneGraph::ResolveActorNode(*a_actor, _anchor.boneName) : SceneGraph::ResolvedNode{};
                if (_root.get() == resolved.root && _node.get() == resolved.object && _node && SceneGraph::IsDescendantOf(_node.get(), resolved.root)) {
                    return BindResult::kUnchanged;
                }

                _root.reset();
                _node.reset();
                if (!a_actor || !resolved.root || !resolved.object) {
                    if (!_warningLogged) {
                        SKSE::log::warn("Unable to bind leash attachment actor {:08X}: bone '{}' was not found", _actorFormID, _anchor.boneName);
                        _warningLogged = true;
                    }
                    return BindResult::kFailed;
                }

                _root.reset(resolved.root);
                _node.reset(resolved.object);
                _warningLogged = false;
                return BindResult::kChanged;
            }

            [[nodiscard]] std::optional<EndSample> Sample(RE::Actor* a_actor) const override {
                if (!a_actor || !_node) {
                    return std::nullopt;
                }
                const RE::NiPoint3 offset{_anchor.offsetX, _anchor.offsetY, _anchor.offsetZ};
                const auto position = _node->world.translate + _node->world.rotate * offset * _node->world.scale;
                return EndSample{.actor = a_actor, .attachment = position, .node = _node.get(), .cell = a_actor->GetParentCell()};
            }

        private:
            const ActorBoneAnchor& _anchor;
            RE::FormID _actorFormID;
            RE::NiPointer<RE::NiAVObject> _root;
            RE::NiPointer<RE::NiAVObject> _node;
            bool _warningLogged{};
        };

        class WorldEnd final : public LeashEnd {
        public:
            explicit WorldEnd(const WorldPositionAnchor& a_anchor) : _anchor(a_anchor) {}

            [[nodiscard]] BindResult Bind(RE::Actor*) override {
                auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(_anchor.cellFormID);
                if (_cell == cell && cell) {
                    return BindResult::kUnchanged;
                }

                _cell = nullptr;
                if (!cell) {
                    if (!_warningLogged) {
                        SKSE::log::warn("Unable to bind world leash anchor: cell {:08X} was not found", _anchor.cellFormID);
                        _warningLogged = true;
                    }
                    return BindResult::kFailed;
                }

                _cell = cell;
                _warningLogged = false;
                return BindResult::kChanged;
            }

            [[nodiscard]] std::optional<EndSample> Sample(RE::Actor*) const override { return EndSample{.attachment = {_anchor.x, _anchor.y, _anchor.z}, .cell = _cell}; }

            [[nodiscard]] RE::TESObjectCELL* GetCell(RE::Actor*) const override { return _cell; }

        private:
            const WorldPositionAnchor& _anchor;
            RE::TESObjectCELL* _cell{};
            bool _warningLogged{};
        };
    }  // namespace

    PerSide<std::unique_ptr<LeashEnd>> MakeLeashEnds(const LeashDefinition& a_definition, LeashRope& a_rope) {
        const auto ropeSide = a_rope.GetRootSide();
        const auto anchorSide = Opposite(ropeSide);
        const auto* holderMesh = std::get_if<HolderMesh>(&a_definition.mesh);
        PerSide<std::unique_ptr<LeashEnd>> ends;
        ends[ropeSide] = std::make_unique<RopeEnd>(a_rope, holderMesh ? holderMesh->closedHand : ClosedHand::kNone, a_definition.GetFormID(ropeSide));
        ends[anchorSide] = std::visit(
            [&](const auto& a_anchor) -> std::unique_ptr<LeashEnd> {
                using Anchor = std::decay_t<decltype(a_anchor)>;
                if constexpr (std::is_same_v<Anchor, HandAnchor>) {
                    return std::make_unique<HandEnd>(a_anchor.rightHand, a_definition.GetFormID(anchorSide));
                } else if constexpr (std::is_same_v<Anchor, ActorBoneAnchor>) {
                    return std::make_unique<BoneEnd>(a_anchor, a_definition.GetFormID(anchorSide));
                } else {
                    return std::make_unique<WorldEnd>(a_anchor);
                }
            },
            a_definition.anchor);
        return ends;
    }
}  // namespace LeashFramework
