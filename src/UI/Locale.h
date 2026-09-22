#pragma once

#include "../PCH.h"

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace LeashFramework::UI::Locale {
    void Load();

    [[nodiscard]] std::string_view Lookup(std::string_view a_english);
    [[nodiscard]] const char* Text(const char* a_english);
    [[nodiscard]] std::string Label(const char* a_english, const char* a_display = nullptr);
    [[nodiscard]] const char* FloatFormat(const char* a_english);

    template <class... Args>
    [[nodiscard]] std::string Format(std::format_string<Args...> a_english, Args&&... a_args) {
        const auto translated = Lookup(a_english.get());
        if (translated != a_english.get()) {
            try {
                return std::vformat(translated, std::make_format_args(a_args...));
            } catch (const std::format_error&) {
            }
        }
        return std::format(a_english, std::forward<Args>(a_args)...);
    }
}
