#include "update_info.h"

#include <algorithm>
#include <array>
#include <map>
#include <vector>

namespace offwork {
namespace {
bool Digits(std::string_view s) {
    return !s.empty() && s.find_first_not_of("0123456789") == s.npos;
}
int CompareNumber(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return a.size() > b.size() ? 1 : -1;
    return a == b ? 0 : (a > b ? 1 : -1);
}
struct Version {
    std::array<std::string_view, 3> core;
    std::vector<std::string_view> pre;
};
bool Identifiers(std::string_view s, std::vector<std::string_view>& out, bool pre) {
    for (;;) {
        const auto dot = s.find('.'), idEnd = dot == s.npos ? s.size() : dot;
        const auto id = s.substr(0, idEnd);
        if (id.empty() || id.find_first_not_of(
                "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-") != id.npos ||
            (pre && Digits(id) && id.size() > 1 && id.front() == '0')) return false;
        out.push_back(id);
        if (dot == s.npos) return true;
        s.remove_prefix(dot + 1);
    }
}
std::optional<Version> ParseVersion(std::string_view s) {
    if (s.starts_with('v')) s.remove_prefix(1);
    const auto plus = s.find('+');
    if (plus != s.npos) {
        std::vector<std::string_view> build;
        if (!Identifiers(s.substr(plus + 1), build, false)) return {};
        s = s.substr(0, plus);
    }
    Version v;
    const auto dash = s.find('-');
    if (dash != s.npos) {
        if (!Identifiers(s.substr(dash + 1), v.pre, true)) return {};
        s = s.substr(0, dash);
    }
    for (std::size_t i = 0; i < v.core.size(); ++i) {
        const auto dot = s.find('.');
        const auto n = s.substr(0, dot);
        if (!Digits(n) || (n.size() > 1 && n.front() == '0') ||
            ((i == 2) != (dot == s.npos))) return {};
        v.core[i] = n;
        if (dot != s.npos) s.remove_prefix(dot + 1);
    }
    return v;
}

// Structural parsing avoids matching release-note text or mixing asset fields.
// Depth and input size are bounded; no code or platform APIs are invoked.
struct Json {
    enum class Kind { Null, String, Bool, Number, Object, Array } kind = Kind::Null;
    std::string text;
    std::map<std::string, Json> object;
    std::vector<Json> array;
    const Json* Get(const char* key) const {
        const auto it = object.find(key);
        return kind == Kind::Object && it != object.end() ? &it->second : nullptr;
    }
};
class JsonReader {
public:
    explicit JsonReader(std::string_view input) : input_(input) {}
    std::optional<Json> Read() {
        auto v = Value(0);
        Space();
        return at_ == input_.size() ? v : std::nullopt;
    }
private:
    void Space() {
        while (at_ < input_.size() && std::string_view(" \t\r\n").find(input_[at_]) != std::string_view::npos) ++at_;
    }
    bool Take(char c) {
        Space();
        if (at_ == input_.size() || input_[at_] != c) return false;
        ++at_; return true;
    }
    std::optional<unsigned> Hex() {
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
            if (at_ == input_.size()) return {};
            const char c = input_[at_++];
            const auto n = std::string_view("0123456789abcdef").find(
                c >= 'A' && c <= 'F' ? static_cast<char>(c + 32) : c);
            if (n == std::string_view::npos) return {};
            code = code * 16 + static_cast<unsigned>(n);
        }
        return code;
    }
    static void Utf8(std::string& out, unsigned c) {
        if (c <= 127) { out += static_cast<char>(c); return; }
        if (c > 0xffff) out += static_cast<char>(0xf0 | (c >> 18));
        else if (c > 0x7ff) out += static_cast<char>(0xe0 | (c >> 12));
        else out += static_cast<char>(0xc0 | (c >> 6));
        if (c > 0xffff) out += static_cast<char>(0x80 | ((c >> 12) & 63));
        if (c > 0x7ff) out += static_cast<char>(0x80 | ((c >> 6) & 63));
        out += static_cast<char>(0x80 | (c & 63));
    }
    std::optional<std::string> String() {
        if (!Take('"')) return {};
        std::string out;
        while (at_ < input_.size()) {
            const unsigned char c = input_[at_++];
            if (c == '"') return out;
            if (c < 32) return {};
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (at_ == input_.size()) return {};
            const char escape = input_[at_++];
            const auto pos = std::string_view("\"\\/bfnrt").find(escape);
            if (pos != std::string_view::npos) { out += std::string_view("\"\\/\b\f\n\r\t")[pos]; continue; }
            if (escape != 'u') return {};
            auto code = Hex();
            if (!code) return {};
            if (*code >= 0xd800 && *code <= 0xdbff) {
                if (input_.substr(at_, 2) != "\\u") return {};
                at_ += 2;
                const auto low = Hex();
                if (!low || *low < 0xdc00 || *low > 0xdfff) return {};
                *code = 0x10000 + ((*code - 0xd800) << 10) + *low - 0xdc00;
            } else if (*code >= 0xdc00 && *code <= 0xdfff) return {};
            Utf8(out, *code);
        }
        return {};
    }
    std::optional<Json> Value(int depth) {
        Space();
        if (depth > 32 || at_ == input_.size()) return {};
        Json r;
        const char c = input_[at_];
        if (c == '"') {
            auto s = String(); if (!s) return {};
            r.kind = Json::Kind::String; r.text = std::move(*s);
        } else if (c == '{' || c == '[') {
            ++at_;
            const bool object = c == '{';
            const char end = object ? '}' : ']';
            r.kind = object ? Json::Kind::Object : Json::Kind::Array;
            if (Take(end)) return r;
            for (;;) {
                std::optional<std::string> key;
                if (object) { key = String(); if (!key || !Take(':')) return {}; }
                auto child = Value(depth + 1); if (!child) return {};
                if (object) {
                    if (!r.object.emplace(std::move(*key), std::move(*child)).second) return {};
                } else r.array.push_back(std::move(*child));
                if (Take(end)) break;
                if (!Take(',')) return {};
            }
        } else if (c == 't' || c == 'f' || c == 'n') {
            const std::string_view word = c == 't' ? "true" : c == 'f' ? "false" : "null";
            if (input_.substr(at_, word.size()) != word) return {};
            at_ += word.size(); r.kind = c == 'n' ? Json::Kind::Null : Json::Kind::Bool; r.text = word;
        } else {
            const auto start = at_;
            if (input_[at_] == '-') ++at_;
            if (at_ == input_.size()) return {};
            if (input_[at_] == '0') ++at_;
            else {
                if (input_[at_] < '1' || input_[at_] > '9') return {};
                while (at_ < input_.size() && input_[at_] >= '0' && input_[at_] <= '9') ++at_;
            }
            for (const char marker : {'.', 'e'}) {
                if (at_ < input_.size() && (input_[at_] == marker || (marker == 'e' && input_[at_] == 'E'))) {
                    ++at_;
                    if (marker == 'e' && at_ < input_.size() && (input_[at_] == '+' || input_[at_] == '-')) ++at_;
                    const auto digits = at_;
                    while (at_ < input_.size() && input_[at_] >= '0' && input_[at_] <= '9') ++at_;
                    if (at_ == digits) return {};
                }
            }
            r.kind = Json::Kind::Number; r.text = input_.substr(start, at_ - start);
        }
        return r;
    }
    std::string_view input_;
    std::size_t at_ = 0;
};
}  // namespace

bool IsNewerVersion(std::string_view candidate, std::string_view current) {
    const auto a = ParseVersion(candidate), b = ParseVersion(current);
    if (!a || !b) return false;
    for (std::size_t i = 0; i < 3; ++i) {
        const int cmp = CompareNumber(a->core[i], b->core[i]);
        if (cmp) return cmp > 0;
    }
    if (a->pre.empty() || b->pre.empty()) return a->pre.empty() && !b->pre.empty();
    for (std::size_t i = 0; i < std::min(a->pre.size(), b->pre.size()); ++i) {
        const auto x = a->pre[i], y = b->pre[i];
        if (x == y) continue;
        if (Digits(x) != Digits(y)) return !Digits(x);
        return Digits(x) ? CompareNumber(x, y) > 0 : x > y;
    }
    return a->pre.size() > b->pre.size();
}

std::optional<ReleaseInfo> ParseReleaseInfo(std::string_view json) {
    if (json.size() > 2 * 1024 * 1024) return {};
    const auto root = JsonReader(json).Read(); if (!root) return {};
    for (const auto flag : {"draft", "prerelease"}) {
        if (const auto value = root->Get(flag); value &&
            (value->kind != Json::Kind::Bool || value->text != "false")) return {};
    }
    const auto tag = root->Get("tag_name"), assets = root->Get("assets");
    if (!tag || tag->kind != Json::Kind::String || !assets || assets->kind != Json::Kind::Array) return {};
    const auto version = ParseVersion(tag->text);
    if (!version || !version->pre.empty()) return {};
    const auto expected = "https://github.com/alertform/OffWork/releases/download/" + tag->text + "/OffWork-Setup.exe";
    for (const auto& asset : assets->array) {
        const auto name = asset.Get("name"), url = asset.Get("browser_download_url");
        if (name && name->kind == Json::Kind::String && name->text == "OffWork-Setup.exe" &&
            url && url->kind == Json::Kind::String && url->text == expected) return ReleaseInfo{tag->text, url->text};
    }
    return {};
}
}  // namespace offwork
