#include "core/json.hpp"

#include "core/types.hpp"

#include <cctype>
#include <charconv>
#include <cmath>

namespace tp {

namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    bool value(JsonValue& out, int depth) {
        if (depth > 64) return false;
        space();
        if (at_ >= s_.size()) return false;
        const char c = s_[at_];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            out.type = JsonValue::Type::String;
            return string(out.string);
        }
        if (literal("true")) {
            out.type = JsonValue::Type::Bool;
            out.boolean = true;
            return true;
        }
        if (literal("false")) {
            out.type = JsonValue::Type::Bool;
            return true;
        }
        if (literal("null")) return true;
        return number(out);
    }

    bool done() {
        space();
        return at_ == s_.size();
    }

private:
    void space() {
        while (at_ < s_.size() && (s_[at_] == ' ' || s_[at_] == '\t' || s_[at_] == '\n' || s_[at_] == '\r')) ++at_;
    }

    bool literal(std::string_view word) {
        if (s_.substr(at_, word.size()) != word) return false;
        at_ += word.size();
        return true;
    }

    bool number(JsonValue& out) {
        const usize start = at_;
        const auto digit = [&] { return at_ < s_.size() && s_[at_] >= '0' && s_[at_] <= '9'; };
        if (at_ < s_.size() && s_[at_] == '-') ++at_;
        if (!digit()) return false;
        if (s_[at_] == '0') ++at_;
        else while (digit()) ++at_;
        if (at_ < s_.size() && s_[at_] == '.') {
            ++at_;
            if (!digit()) return false;
            while (digit()) ++at_;
        }
        if (at_ < s_.size() && (s_[at_] == 'e' || s_[at_] == 'E')) {
            ++at_;
            if (at_ < s_.size() && (s_[at_] == '+' || s_[at_] == '-')) ++at_;
            if (!digit()) return false;
            while (digit()) ++at_;
        }
        const auto result = std::from_chars(s_.data() + start, s_.data() + at_, out.number);
        out.type = JsonValue::Type::Number;
        return result.ec == std::errc{} && result.ptr == s_.data() + at_ && std::isfinite(out.number);
    }

    static void utf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool hex4(unsigned& code) {
        if (at_ + 4 > s_.size()) return false;
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[at_++];
            code <<= 4;
            if (c >= '0' && c <= '9') code |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') code |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') code |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool string(std::string& out) {
        ++at_; // opening quote
        while (at_ < s_.size()) {
            const char c = s_[at_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= s_.size()) return false;
            switch (s_[at_++]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned code = 0;
                if (!hex4(code)) return false;
                if (code >= 0xD800 && code < 0xDC00 && s_.substr(at_, 2) == "\\u") {
                    at_ += 2;
                    unsigned low = 0;
                    if (!hex4(low) || low < 0xDC00 || low > 0xDFFF) return false;
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
                if (code >= 0xD800 && code <= 0xDFFF) return false;
                utf8(out, code);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool array(JsonValue& out, int depth) {
        out.type = JsonValue::Type::Array;
        ++at_;
        space();
        if (at_ < s_.size() && s_[at_] == ']') {
            ++at_;
            return true;
        }
        for (;;) {
            JsonValue item;
            if (!value(item, depth + 1)) return false;
            out.array.push_back(std::move(item));
            space();
            if (at_ >= s_.size()) return false;
            if (s_[at_] == ']') {
                ++at_;
                return true;
            }
            if (s_[at_++] != ',') return false;
        }
    }

    bool object(JsonValue& out, int depth) {
        out.type = JsonValue::Type::Object;
        ++at_;
        space();
        if (at_ < s_.size() && s_[at_] == '}') {
            ++at_;
            return true;
        }
        for (;;) {
            space();
            if (at_ >= s_.size() || s_[at_] != '"') return false;
            std::string key;
            if (!string(key)) return false;
            space();
            if (at_ >= s_.size() || s_[at_++] != ':') return false;
            JsonValue item;
            if (!value(item, depth + 1)) return false;
            out.object.emplace_back(std::move(key), std::move(item));
            space();
            if (at_ >= s_.size()) return false;
            if (s_[at_] == '}') {
                ++at_;
                return true;
            }
            if (s_[at_++] != ',') return false;
        }
    }

    std::string_view s_;
    usize at_ = 0;
};

} // namespace

const JsonValue* JsonValue::get(std::string_view key) const {
    for (const auto& [k, v] : object)
        if (k == key) return &v;
    return nullptr;
}

std::string JsonValue::str(std::string_view key, std::string_view fallback) const {
    const JsonValue* v = get(key);
    return v && v->type == Type::String ? v->string : std::string(fallback);
}

double JsonValue::num(std::string_view key, double fallback) const {
    const JsonValue* v = get(key);
    return v && v->type == Type::Number ? v->number : fallback;
}

std::optional<JsonValue> parseJson(std::string_view text) {
    Parser p(text);
    JsonValue v;
    if (!p.value(v, 0) || !p.done()) return std::nullopt;
    return v;
}

} // namespace tp
