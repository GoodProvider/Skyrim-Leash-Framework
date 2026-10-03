#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "../PCH.h"
#include "../Physics/RopeSolver.h"
#include "../Physics/SimulationSettings.h"
#include "BindResult.h"
#include "LeashDefinition.h"
#include "LeashEnd.h"
#include "LeashSide.h"
#include "PoseRegistry.h"
#include "RopeMesh.h"

namespace LeashFramework::Physics {
    class ActorBodyCollision;
}

namespace LeashFramework {
    // The simulated rope between both ends. Only this class knows which side carries bone 0.
    class LeashRope {
    public:
        explicit LeashRope(const LeashDefinition& a_definition);

        [[nodiscard]] LeashSide GetRootSide() const noexcept { return _rootSide; }
        [[nodiscard]] RE::NiAVObject* GetMeshRoot() const { return _mesh->GetRoot(); }
        [[nodiscard]] BindResult Bind(RE::Actor& a_owner) { return _mesh->Bind(a_owner); }
        // Releases the mesh binding, including a standalone clone parented into its cell
        void Unbind() { _mesh->Reset(); }

        // Reads bone positions from the animated pose before anything modifies them this frame
        void ReadNeutralPose();
        [[nodiscard]] float GetLength() const noexcept { return _length; }
        [[nodiscard]] EndSample SampleRoot(RE::Actor& a_owner) const;

        // Solves with both ends and the bones carried by the leaning pose this frame. Stores the result for ApplyDeferredPose.
        void Solve(const PerSide<EndSample>& a_ends, const PreparedLean& a_lean, float a_deltaTime, RE::bhkWorld* a_world, const Physics::ActorBodyCollision* a_actorCollision,
            const Physics::SimulationSettings& a_settings);
        // The solved point at a_side's end and its neighbor along the rope
        [[nodiscard]] std::optional<std::pair<RE::NiPoint3, RE::NiPoint3>> GetEndSegment(LeashSide a_side) const;
        void ApplyDeferredPose();

        void Freeze() { _solver.Freeze(); }
        void ResetSimulation();

    private:
        void StoreDeferredPose(const std::vector<RE::NiPoint3>& a_neutralPositions, const std::vector<RE::NiMatrix3>& a_neutralRotations);

        LeashSide _rootSide;
        std::unique_ptr<RopeMesh> _mesh;
        Physics::RopeSolver _solver;
        std::vector<RE::NiPoint3> _neutralPositions;
        std::vector<RE::NiMatrix3> _neutralRotations;
        std::vector<float> _segmentLengths;
        std::vector<RE::NiPoint3> _deferredTranslations;
        std::vector<RE::NiMatrix3> _deferredRotations;
        float _length{};
    };
}  // namespace LeashFramework
