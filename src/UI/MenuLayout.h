#pragma once

#include "../PCH.h"
#include "../../include/SKSEMenuFramework.h"
#include "Locale.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace LeashFramework::UI::MenuLayout {
    inline constexpr ImGuiMCP::ImVec4 kAccent{0.72F, 0.16F, 0.14F, 1.0F};

    inline constexpr auto kColors = std::to_array<std::pair<ImGuiMCP::ImGuiCol, ImGuiMCP::ImVec4>>({
        {ImGuiMCP::ImGuiCol_Text, {0.93F, 0.93F, 0.90F, 1.0F}},
        {ImGuiMCP::ImGuiCol_TextDisabled, {0.66F, 0.67F, 0.67F, 1.0F}},

        {ImGuiMCP::ImGuiCol_ChildBg, {0.04F, 0.04F, 0.04F, 0.65F}},
        {ImGuiMCP::ImGuiCol_Border, {0.52F, 0.16F, 0.14F, 0.28F}},

        {ImGuiMCP::ImGuiCol_FrameBg, {0.13F, 0.11F, 0.11F, 0.90F}},
        {ImGuiMCP::ImGuiCol_FrameBgHovered, {0.24F, 0.10F, 0.09F, 1.0F}},
        {ImGuiMCP::ImGuiCol_FrameBgActive, {0.32F, 0.10F, 0.09F, 1.0F}},

        {ImGuiMCP::ImGuiCol_CheckMark, kAccent},
        {ImGuiMCP::ImGuiCol_SliderGrab, kAccent},
        {ImGuiMCP::ImGuiCol_SliderGrabActive, {0.92F, 0.22F, 0.18F, 1.0F}},

        {ImGuiMCP::ImGuiCol_Button, {0.21F, 0.08F, 0.08F, 0.85F}},
        {ImGuiMCP::ImGuiCol_ButtonHovered, {0.36F, 0.10F, 0.09F, 1.0F}},
        {ImGuiMCP::ImGuiCol_ButtonActive, {0.47F, 0.11F, 0.10F, 1.0F}},

        {ImGuiMCP::ImGuiCol_Tab, {0.12F, 0.09F, 0.09F, 0.90F}},
        {ImGuiMCP::ImGuiCol_TabHovered, {0.36F, 0.10F, 0.09F, 1.0F}},
        {ImGuiMCP::ImGuiCol_TabActive, {0.29F, 0.08F, 0.07F, 1.0F}},

        {ImGuiMCP::ImGuiCol_Header, {0.27F, 0.08F, 0.07F, 0.85F}},
        {ImGuiMCP::ImGuiCol_HeaderHovered, {0.38F, 0.10F, 0.09F, 1.0F}},
        {ImGuiMCP::ImGuiCol_HeaderActive, {0.47F, 0.11F, 0.10F, 1.0F}},
    });

    struct Style {
        Style() {
            const auto em = ImGuiMCP::GetFontSize();
            ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_ItemSpacing, {em * 0.65F, em * 0.45F});
            ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_CellPadding, {em * 0.4F, em * 0.35F});
            ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FramePadding, {em * 0.45F, em * 0.25F});
            ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_WindowPadding, {em * 0.8F, em * 0.7F});
            ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameRounding, em * 0.15F);
            ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_ChildRounding, em * 0.25F);
            ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameBorderSize, 0.0F);
            for (const auto& [color, value] : kColors) {
                ImGuiMCP::PushStyleColor(color, value);
            }
        }

        ~Style() {
            ImGuiMCP::PopStyleColor(static_cast<int>(kColors.size()));
            ImGuiMCP::PopStyleVar(7);
        }
    };

    inline void NoteRaw(const char* a_text) {
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, *ImGuiMCP::GetStyleColorVec4(ImGuiMCP::ImGuiCol_TextDisabled));
        ImGuiMCP::TextWrapped("%s", a_text);
        ImGuiMCP::PopStyleColor();
    }

    inline void Note(const char* a_text) { NoteRaw(Locale::Text(a_text)); }

    inline bool Button(const char* a_label, const ImGuiMCP::ImVec2& a_size = {0.0F, 0.0F}, const char* a_display = nullptr) {
        return ImGuiMCP::Button(Locale::Label(a_label, a_display).c_str(), a_size);
    }

    inline void Help(const char* a_text, bool a_labelHovered = false) {
        if (a_text && (a_labelHovered || ImGuiMCP::IsItemHovered(ImGuiMCP::ImGuiHoveredFlags_AllowWhenDisabled)) && ImGuiMCP::BeginTooltip()) {
            ImGuiMCP::PushTextWrapPos(ImGuiMCP::GetFontSize() * 26.0F);
            ImGuiMCP::TextUnformatted(Locale::Text(a_text));
            ImGuiMCP::PopTextWrapPos();
            ImGuiMCP::EndTooltip();
        }
    }

    inline void Title(std::string_view a_title, const char* a_description = nullptr) {
        a_description = Locale::Text(a_description);
        ImGuiMCP::ImVec2 available;
        ImGuiMCP::GetContentRegionAvail(&available);
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, kAccent);
        ImGuiMCP::TextWrapped("%.*s", static_cast<int>(a_title.size()), a_title.data());
        ImGuiMCP::PopStyleColor();
        if (a_description && a_description[0] != '\0') {
            ImGuiMCP::ImVec2 titleSize;
            ImGuiMCP::ImVec2 descriptionSize;
            ImGuiMCP::CalcTextSize(&titleSize, a_title.data(), a_title.data() + a_title.size(), false, -1.0F);
            ImGuiMCP::CalcTextSize(&descriptionSize, a_description, nullptr, false, -1.0F);
            const auto em = ImGuiMCP::GetFontSize();
            const auto gap = em * 0.65F;
            if (titleSize.x + gap + std::min(descriptionSize.x, em * 12.0F) <= available.x) {
                ImGuiMCP::SameLine(0.0F, gap);
            }
            NoteRaw(a_description);
        }
    }

    inline void Title(const char* a_title, const char* a_description = nullptr) { Title(std::string_view{Locale::Text(a_title)}, a_description); }

    template <class TitleText>
    bool Heading(TitleText a_title, const char* a_button, const char* a_help = "Restores the defaults for this section.", const char* a_description = nullptr) {
        bool pressed{};
        if (ImGuiMCP::BeginTable("Heading", 2, ImGuiMCP::ImGuiTableFlags_SizingStretchProp)) {
            ImGuiMCP::TableSetupColumn("Title", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
            ImGuiMCP::TableSetupColumn("Action", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed);
            ImGuiMCP::TableNextColumn();
            ImGuiMCP::AlignTextToFramePadding();
            Title(a_title, a_description);
            ImGuiMCP::TableNextColumn();
            pressed = Button(a_button);
            Help(a_help);
            ImGuiMCP::EndTable();
        }
        return pressed;
    }

    template <class Content, class Action = std::nullptr_t>
    void Panel(const char* a_title, const char* a_description, Content a_content, Action a_action = nullptr, const char* a_actionLabel = "Reset",
        const char* a_actionHelp = "Restores the defaults for this section.") {
        if (ImGuiMCP::BeginChild(a_title, {0.0F, 0.0F}, ImGuiMCP::ImGuiChildFlags_Border | ImGuiMCP::ImGuiChildFlags_AutoResizeY)) {
            if constexpr (std::is_same_v<Action, std::nullptr_t>) {
                Title(a_title, a_description);
            } else if (Heading(a_title, a_actionLabel, a_actionHelp, a_description)) {
                a_action();
            }
            a_content();
        }
        ImGuiMCP::EndChild();
    }

    template <class Settings, class Content>
    void SettingsPanel(const char* a_title, const char* a_description, Settings& a_settings, Content a_content) {
        Panel(a_title, a_description, [&] { a_content(a_settings); }, [&] { a_settings = Settings{}; });
    }

    template <class... Panels>
    void Columns(Panels... a_panels) {
        ImGuiMCP::ImVec2 available;
        ImGuiMCP::GetContentRegionAvail(&available);
        const auto columns = std::clamp(static_cast<int>(available.x / (ImGuiMCP::GetFontSize() * 28.0F)), 1, 2);
        const ImGuiMCP::ImVec2 size{std::min(available.x, ImGuiMCP::GetFontSize() * 90.0F), 0.0F};
        if (ImGuiMCP::BeginTable("Panels", columns, ImGuiMCP::ImGuiTableFlags_SizingStretchSame, size)) {
            ((ImGuiMCP::TableNextColumn(), a_panels()), ...);
            ImGuiMCP::EndTable();
        }
    }

    inline bool Toggle(const char* a_label, bool& a_value, const char* a_help) {
        const bool changed = ImGuiMCP::Checkbox(Locale::Label(a_label).c_str(), &a_value);
        Help(a_help);
        return changed;
    }

    template <class Widget>
    bool Field(const char* a_label, Widget a_widget, const char* a_help = nullptr) {
        ImGuiMCP::PushID(a_label);
        ImGuiMCP::TextUnformatted(Locale::Text(a_label));
        const bool labelHovered = ImGuiMCP::IsItemHovered(ImGuiMCP::ImGuiHoveredFlags_AllowWhenDisabled);
        ImGuiMCP::SetNextItemWidth(-1.0F);
        const bool changed = a_widget("##Value");
        Help(a_help, labelHovered);
        ImGuiMCP::PopID();
        return changed;
    }

    inline void Slider(const char* a_label, float& a_value, float a_minimum, float a_maximum, const char* a_help, const char* a_format = "%.2f") {
        Field(a_label, [&](const char* a_id) { return ImGuiMCP::SliderFloat(a_id, &a_value, a_minimum, a_maximum, Locale::FloatFormat(a_format), ImGuiMCP::ImGuiSliderFlags_AlwaysClamp); }, a_help);
    }

    inline void Number(const char* a_label, float& a_value, const char* a_help, float a_step = 0.1F, float a_fastStep = 1.0F, const char* a_format = "%.2f") {
        Field(a_label, [&](const char* a_id) { return ImGuiMCP::InputFloat(a_id, &a_value, a_step, a_fastStep, Locale::FloatFormat(a_format)); }, a_help);
    }

    inline void Vector(const char* a_label, RE::NiPoint3& a_value, const char* a_help = nullptr) {
        Field(a_label, [&](const char* a_id) { return ImGuiMCP::InputFloat3(a_id, &a_value.x, "%.2f"); }, a_help);
    }

    template <std::size_t N>
    bool Text(const char* a_label, char (&a_value)[N], const char* a_help = nullptr, const char* a_hint = nullptr) {
        return Field(a_label, [&](const char* a_id) {
            return a_hint ? ImGuiMCP::InputTextWithHint(a_id, Locale::Text(a_hint), a_value, N) : ImGuiMCP::InputText(a_id, a_value, N);
        }, a_help);
    }

    template <class T, std::size_t N>
        requires (std::is_enum_v<T> || std::is_same_v<T, bool> || std::is_same_v<T, int>)
    bool Choice(const char* a_label, T& a_selected, const std::array<const char*, N>& a_choices) {
        static_assert(!std::is_same_v<T, bool> || N == 2);
        auto index = static_cast<int>(a_selected);
        std::array<const char*, N> choices;
        std::ranges::transform(a_choices, choices.begin(), [](const char* a_choice) { return Locale::Text(a_choice); });
        const bool changed = Field(a_label, [&](const char* a_id) { return ImGuiMCP::Combo(a_id, &index, choices.data(), static_cast<int>(N)); });
        if (changed) {
            a_selected = static_cast<T>(index);
        }
        return changed;
    }

    inline bool ChoiceItem(const char* a_label, std::uint32_t a_value, std::uint32_t& a_selected) {
        ImGuiMCP::PushID(static_cast<int>(a_value));
        const bool selected = a_value == a_selected;
        const bool changed = ImGuiMCP::Selectable(a_label, selected);
        if (changed) {
            a_selected = a_value;
        }
        if (selected) {
            ImGuiMCP::SetItemDefaultFocus();
        }
        ImGuiMCP::PopID();
        return changed;
    }

    inline void Columns(const char* a_id, float a_minimumWidthInEms, std::initializer_list<void (*)()> a_panels) {
        ImGuiMCP::ImVec2 available;
        ImGuiMCP::GetContentRegionAvail(&available);
        const auto columns = std::clamp(static_cast<int>(available.x / (ImGuiMCP::GetFontSize() * a_minimumWidthInEms)), 1, static_cast<int>(a_panels.size()));
        if (ImGuiMCP::BeginTable(a_id, columns, ImGuiMCP::ImGuiTableFlags_SizingStretchSame)) {
            for (const auto panel : a_panels) {
                ImGuiMCP::TableNextColumn();
                panel();
            }
            ImGuiMCP::EndTable();
        }
    }

    template <class Content>
    void TabItem(const char* a_label, Content a_content) {
        if (ImGuiMCP::BeginTabItem(Locale::Label(a_label).c_str())) {
            ImGuiMCP::PushID(a_label);
            a_content();
            ImGuiMCP::PopID();
            ImGuiMCP::EndTabItem();
        }
    }

    template <class Content>
    void Tab(const char* a_label, Content a_content, const char* a_display = nullptr) {
        if (ImGuiMCP::BeginTabItem(Locale::Label(a_label, a_display).c_str())) {
            ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_ChildBg, ImGuiMCP::ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
            const bool visible = ImGuiMCP::BeginChild(a_label);
            ImGuiMCP::PopStyleColor();
            if (visible) {
                a_content();
            }
            ImGuiMCP::EndChild();
            ImGuiMCP::EndTabItem();
        }
    }

    inline void Feedback(const std::string& a_message) {
        if (!a_message.empty()) {
            ImGuiMCP::Spacing();
            ImGuiMCP::TextColored(kAccent, "%s", Locale::Text("Last result"));
            ImGuiMCP::TextWrapped("%s", a_message.c_str());
        }
    }
}
