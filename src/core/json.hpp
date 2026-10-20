#pragma once
// A small JSON reader for the few documents the game reads (the release feed).

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tp {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    // The member called `key` of an object, or nullptr.
    const JsonValue* get(std::string_view key) const;
    // A string member, or `fallback` when there is none.
    std::string str(std::string_view key, std::string_view fallback = {}) const;
    double num(std::string_view key, double fallback = 0.0) const;
};

// nullopt when the text is not valid JSON.
std::optional<JsonValue> parseJson(std::string_view text);

} // namespace tp
