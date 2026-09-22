#include "Locale.h"

#include <filesystem>
#include <glaze/glaze.hpp>
#include <system_error>
#include <unordered_map>

namespace LeashFramework::UI::Locale {
    namespace {
        constexpr auto kLocalePath = "Data/SKSE/Plugins/LeashFramework.locale.json";

        struct StringHash {
            using is_transparent = void;
            std::size_t operator()(std::string_view a_text) const noexcept { return std::hash<std::string_view>{}(a_text); }
        };

        using Dictionary = std::unordered_map<std::string, std::string, StringHash, std::equal_to<>>;
        Dictionary translations;
    }

    void Load() {
        translations.clear();
        std::error_code fileError;
        if (!std::filesystem::exists(kLocalePath, fileError)) {
            if (fileError) {
                SKSE::log::error("Could not inspect menu locale file: {}", fileError.message());
            }
            return;
        }

        Dictionary loaded;
        std::string json;
        if (const auto error = glz::read_file_json(loaded, kLocalePath, json); error) {
            SKSE::log::error("Failed to load menu locale: {}", glz::format_error(error, json));
            return;
        }
        std::erase_if(loaded, [](const auto& a_entry) {
            return a_entry.first.empty() || a_entry.second.empty() || a_entry.first.contains('\0') || a_entry.second.contains('\0');
        });
        translations = std::move(loaded);
        SKSE::log::info("Loaded menu locale ({} overrides)", translations.size());
    }

    std::string_view Lookup(std::string_view a_english) {
        const auto entry = translations.find(a_english);
        return entry != translations.end() ? std::string_view{entry->second} : a_english;
    }

    const char* Text(const char* a_english) { return a_english ? Lookup(a_english).data() : nullptr; }

    std::string Label(const char* a_english, const char* a_display) {
        const std::string_view source{a_english};
        const auto separator = source.find("###");
        const auto id = separator == std::string_view::npos ? source : source.substr(separator + 3);
        const auto visible = source.substr(0, source.find("##"));
        return std::format("{}###{}", a_display ? std::string_view{a_display} : Lookup(visible), id);
    }

    const char* FloatFormat(const char* a_english) {
        const std::string_view source{a_english};
        const auto translated = Lookup(source);
        if (translated == source) {
            return a_english;
        }

        // Keep the original conversion intact: ImGui consumes these strings as printf formats.
        const auto start = source.find('%');
        const auto end = source.find_first_of("fFeEgG", start);
        if (start == std::string_view::npos || end == std::string_view::npos) {
            return a_english;
        }
        const auto conversion = source.substr(start, end - start + 1);
        bool found{};
        for (std::size_t index = 0; index < translated.size(); ++index) {
            if (translated[index] != '%') {
                continue;
            }
            if (index + 1 < translated.size() && translated[index + 1] == '%') {
                ++index;
            } else if (!found && translated.substr(index).starts_with(conversion)) {
                found = true;
                index += conversion.size() - 1;
            } else {
                return a_english;
            }
        }
        return found ? translated.data() : a_english;
    }
}
