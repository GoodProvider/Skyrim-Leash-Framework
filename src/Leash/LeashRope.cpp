#include "LeashRope.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "EquippedRope.h"
#include "StandaloneRope.h"

namespace LeashFramework {
    namespace {
        constexpr float kDirectionEpsilon = 0.0001F;

        [[nodiscard]] std::unique_ptr<RopeMesh> MakeRopeMesh(const LeashDefinition& a_definition) {
            if (std::holds_alternative<StandaloneMesh>(a_definition.mesh)) {
                return std::make_unique<StandaloneRope>(a_definition);
            }
            return std::make_unique<EquippedRope>(a_definition);
        }

        [[nodiscard]] RE::NiMatrix3 AlignRotation(const RE::NiMatrix3& a_neutralRotation, RE::NiPoint3 a_from, RE::NiPoint3 a_to) {
            if (a_from.Unitize() <= kDirectionEpsilon || a_to.Unitize() <= kDirectionEpsilon) {
                return a_neutralRotation;
            }

            const auto dot = std::clamp(a_from.Dot(a_to), -1.0F, 1.0F);
            if (dot > 1.0F - kDirectionEpsilon) {
                return a_neutralRotation;
            }

            auto axis = a_to.Cross(a_from);
            if (axis.Unitize() <= kDirectionEpsilon) {
                axis = a_from.Cross({1.0F, 0.0F, 0.0F});
                if (axis.Unitize() <= kDirectionEpsilon) {
                    axis = a_from.Cross({0.0F, 1.0F, 0.0F});
                    axis.Unitize();
                }
            }

            RE::NiMatrix3 alignment;
            alignment.MakeRotation(std::acos(dot), axis);
            return alignment * a_neutralRotation;
        }
    }  // namespace

    LeashRope::LeashRope(const LeashDefinition& a_definition) : _rootSide(a_definition.GetMeshSide()), _mesh(MakeRopeMesh(a_definition)) {}

    void LeashRope::ReadNeutralPose() {
        _mesh->PoseNeutral();
        const auto& bones = _mesh->GetBones();
        _neutralPositions.resize(bones.size());
        _neutralRotations.resize(bones.size());
        _segmentLengths.resize(bones.size() - 1);

        for (std::size_t index = 0; index < bones.size(); ++index) {
            _neutralPositions[index] = bones[index]->world.translate;
            _neutralRotations[index] = bones[index]->world.rotate;
            if (index > 0) {
                _segmentLengths[index - 1] = _neutralPositions[index - 1].GetDistance(_neutralPositions[index]);
            }
        }
        _length = std::accumulate(_segmentLengths.begin(), _segmentLengths.end(), 0.0F);
    }

    EndSample LeashRope::SampleRoot(RE::Actor& a_owner) const {
        return {.actor = &a_owner, .attachment = _neutralPositions.front(), .node = &_mesh->GetPoseReference(0), .cell = a_owner.GetParentCell()};
    }

    void LeashRope::Solve(const PerSide<EndSample>& a_ends, const PreparedLean& a_lean, float a_deltaTime, RE::bhkWorld* a_world, const Physics::ActorBodyCollision* a_actorCollision,
        const Physics::SimulationSettings& a_settings) {
        auto posedPositions = _neutralPositions;
        auto posedRotations = _neutralRotations;
        // The actor carrying the rope can also be leaning from its own leash, so move the bones with that pose
        const auto* owner = a_ends[_rootSide].actor;
        for (std::size_t index = 0; index < posedPositions.size(); ++index) {
            a_lean.Pose(owner, _mesh->GetPoseReference(index), posedPositions[index], posedRotations[index]);
        }
        // Same for the far end. This is what keeps a rope attached to a neck when that actor's leash makes them lean
        const auto farEnd = a_lean.Pose(a_ends[Opposite(_rootSide)]);

        const auto& positions = _solver.Solve(posedPositions, _segmentLengths, farEnd, a_deltaTime, a_world, a_actorCollision, a_settings);
        if (positions.size() == _mesh->GetBones().size()) {
            _mesh->PoseFrame(posedPositions.front(), posedRotations.front());
            StoreDeferredPose(posedPositions, posedRotations);
        }
    }

    std::optional<std::pair<RE::NiPoint3, RE::NiPoint3>> LeashRope::GetEndSegment(LeashSide a_side) const {
        const auto& positions = _solver.GetPositions();
        if (positions.size() < 2) {
            return std::nullopt;
        }
        if (a_side == _rootSide) {
            return std::pair{positions[0], positions[1]};
        }
        return std::pair{positions[positions.size() - 1], positions[positions.size() - 2]};
    }

    void LeashRope::StoreDeferredPose(const std::vector<RE::NiPoint3>& a_neutralPositions, const std::vector<RE::NiMatrix3>& a_neutralRotations) {
        const auto& positions = _solver.GetPositions();
        const auto& bones = _mesh->GetBones();
        _deferredTranslations.resize(bones.size());
        _deferredRotations.resize(bones.size());
        for (std::size_t index = 0; index < bones.size(); ++index) {
            RE::NiPoint3 neutralDirection;
            RE::NiPoint3 solvedDirection;
            if (index + 1 < bones.size()) {
                neutralDirection = a_neutralPositions[index + 1] - a_neutralPositions[index];
                solvedDirection = positions[index + 1] - positions[index];
            } else {
                neutralDirection = a_neutralPositions[index] - a_neutralPositions[index - 1];
                solvedDirection = positions[index] - positions[index - 1];
            }

            bones[index]->world.translate = positions[index];
            bones[index]->world.rotate = AlignRotation(a_neutralRotations[index], neutralDirection, solvedDirection);
            _deferredTranslations[index] = bones[index]->world.translate;
            _deferredRotations[index] = bones[index]->world.rotate;
        }
    }

    void LeashRope::ApplyDeferredPose() {
        const auto& bones = _mesh->GetBones();
        if (_deferredTranslations.empty() || _deferredTranslations.size() != bones.size() || _deferredRotations.size() != bones.size()) {
            return;
        }

        _mesh->Present();
        for (std::size_t index = 0; index < bones.size(); ++index) {
            bones[index]->world.translate = _deferredTranslations[index];
            bones[index]->world.rotate = _deferredRotations[index];
        }
        _mesh->UpdateWorldBounds();
    }

    void LeashRope::ResetSimulation() {
        _solver.Reset();
        _deferredTranslations.clear();
        _deferredRotations.clear();
        _mesh->Hide();
    }
}  // namespace LeashFramework
