#pragma once

#include <format>

#include "../PCH.h"
#include "LeashDefinition.h"
#include "RopeMesh.h"

namespace LeashFramework {
    // Rope bones worn on the mesh owner's third-person skeleton
    class EquippedRope final : public RopeMesh {
    public:
        explicit EquippedRope(const LeashDefinition& a_definition);

        [[nodiscard]] LeashAnchor::BindResult Bind(RE::Actor& a_owner) override;
        void Reset() override;
        void UpdateWorldBounds() override;
        [[nodiscard]] const RE::NiAVObject& GetPoseReference(std::size_t a_index) const override { return *_bones[a_index]; }
        [[nodiscard]] RE::NiAVObject* GetRoot() const override { return _root.get(); }

    private:
        template <class... Args>
        [[nodiscard]] LeashAnchor::BindResult Fail(std::format_string<Args...> a_reason, Args&&... a_args);

        const LeashDefinition& _definition;
        RE::NiPointer<RE::NiAVObject> _root;
        bool _warningLogged{};
    };
}  // namespace LeashFramework
