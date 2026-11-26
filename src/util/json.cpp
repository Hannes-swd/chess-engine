#include "util/json.hpp"

#include <cstdlib>

namespace json {

Value Value::boolean(bool b) {
    Value v;
    v.type_ = Type::Bool;
    v.b_ = b;
    return v;
}

Value Value::number(double n) {
    Value v;
    v.type_ = Type::Number;
    v.n_ = n;
    return v;
}

Value Value::string(std::string s) {
    Value v;
    v.type_ = Type::String;
    v.s_ = std::move(s);
    return v;
}

Value Value::array(Array a) {
    Value v;
    v.type_ = Type::Array;
    v.a_ = std::make_shared<Array>(std::move(a));
    return v;
}

Value Value::object(Object o) {
    Value v;
    v.type_ = Type::Object;
    v.o_ = std::make_shared<Object>(std::move(o));
    return v;
}

const std::string& Value::asString() const {
    static const std::string empty;
    return isString() ? s_ : empty;
}

const Array& Value::asArray() const {
    static const Array empty;
    return isArray() ? *a_ : empty;
}

const Object& Value::asObject() const {
    static const Object empty;
    return isObject() ? *o_ : empty;
}

const Value* Value::find(std::string_view key) const {
    if (!isObject()) return nullptr;
    for (const auto& [k, v] : *o_)
        if (k == key) return &v;
    return nullptr;
}

double Value::getNumber(std::string_view key, double fallback) const {
    const Value* v = find(key);
    return v ? v->asNumber(fallback) : fallback;
}

std::string Value::getString(std::string_view key, const std::string& fallback) const {
    const Value* v = find(key);
    return v && v->isString() ? v->asString() : fallback;
}

bool Value::getBool(std::string_view key, bool fallback) const {
    const Value* v = find(key);
    return v ? v->asBool(fallback) : fallback;
}

namespace {

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    std::optional<Value> run(std::string* error) {
        skipWs();
        auto v = parseValue(0);
        if (v) {
            skipWs();
            if (i_ != s_.size()) fail("Unerwartete Zeichen nach dem Ende");
        }
        if (!err_.empty()) {
            if (error) *error = location() + err_;
            return std::nullopt;
        }
        return v;
    }

private:
    std::string location() const {
        int line = 1, col = 1;
        for (size_t k = 0; k < i_ && k < s_.size(); ++k) {
            if (s_[k] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        return "Zeile " + std::to_string(line) + ", Spalte " + std::to_string(col) + ": ";
    }

    void fail(const std::string& msg) {
        if (err_.empty()) err_ = msg;
    }

    void skipWs() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }

    bool consume(std::string_view lit) {
        if (s_.substr(i_, lit.size()) == lit) {
            i_ += lit.size();
            return true;
        }
        return false;
    }

    std::optional<Value> parseValue(int depth) {
        if (depth > 200) {
            fail("Zu tief verschachtelt");
            return std::nullopt;
        }
        if (i_ >= s_.size()) {
            fail("Unerwartetes Ende");
            return std::nullopt;
        }
        const char c = s_[i_];
        if (c == '{') return parseObject(depth);
        if (c == '[') return parseArray(depth);
        if (c == '"') {
            auto str = parseString();
            if (!str) return std::nullopt;
            return Value::string(std::move(*str));
        }
        if (consume("true")) return Value::boolean(true);
        if (consume("false")) return Value::boolean(false);
        if (consume("null")) return Value{};
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        fail(std::string("Unerwartetes Zeichen '") + c + "'");
        return std::nullopt;
    }

    std::optional<Value> parseNumber() {
        const size_t start = i_;
        if (s_[i_] == '-') ++i_;
        while (i_ < s_.size() && ((s_[i_] >= '0' && s_[i_] <= '9') || s_[i_] == '.' || s_[i_] == 'e' ||
                                  s_[i_] == 'E' || s_[i_] == '+' || s_[i_] == '-'))
            ++i_;
        const std::string text(s_.substr(start, i_ - start));
        char* end = nullptr;
        const double d = std::strtod(text.c_str(), &end);
        if (end != text.c_str() + text.size() || text == "-") {
            i_ = start;
            fail("Ungültige Zahl");
            return std::nullopt;
        }
        return Value::number(d);
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::optional<unsigned> hex4() {
        if (i_ + 4 > s_.size()) return std::nullopt;
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = s_[i_++];
            v <<= 4;
            if (h >= '0' && h <= '9')
                v |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f')
                v |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F')
                v |= static_cast<unsigned>(h - 'A' + 10);
            else
                return std::nullopt;
        }
        return v;
    }

    std::optional<std::string> parseString() {
        ++i_;  // "
        std::string out;
        while (i_ < s_.size()) {
            const char c = s_[i_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) {
                fail("Steuerzeichen in Zeichenkette");
                return std::nullopt;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= s_.size()) break;
            const char e = s_[i_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    auto cp = hex4();
                    if (!cp) {
                        fail("Ungültige \\u-Sequenz");
                        return std::nullopt;
                    }
                    unsigned code = *cp;
                    if (code >= 0xD800 && code <= 0xDBFF && consume("\\u")) {
                        auto lo = hex4();
                        if (!lo || *lo < 0xDC00 || *lo > 0xDFFF) {
                            fail("Ungültiges Surrogatpaar");
                            return std::nullopt;
                        }
                        code = 0x10000 + ((code - 0xD800) << 10) + (*lo - 0xDC00);
                    }
                    appendUtf8(out, code);
                    break;
                }
                default: fail(std::string("Unbekannte Escape-Sequenz \\") + e); return std::nullopt;
            }
        }
        fail("Zeichenkette nicht geschlossen");
        return std::nullopt;
    }

    std::optional<Value> parseArray(int depth) {
        ++i_;  // [
        Array arr;
        skipWs();
        if (consume("]")) return Value::array(std::move(arr));
        while (true) {
            skipWs();
            auto v = parseValue(depth + 1);
            if (!v) return std::nullopt;
            arr.push_back(std::move(*v));
            skipWs();
            if (consume("]")) return Value::array(std::move(arr));
            if (!consume(",")) {
                fail("',' oder ']' erwartet");
                return std::nullopt;
            }
        }
    }

    std::optional<Value> parseObject(int depth) {
        ++i_;  // {
        Object obj;
        skipWs();
        if (consume("}")) return Value::object(std::move(obj));
        while (true) {
            skipWs();
            if (i_ >= s_.size() || s_[i_] != '"') {
                fail("Feldname in \"...\" erwartet");
                return std::nullopt;
            }
            auto key = parseString();
            if (!key) return std::nullopt;
            skipWs();
            if (!consume(":")) {
                fail("':' erwartet");
                return std::nullopt;
            }
            skipWs();
            auto v = parseValue(depth + 1);
            if (!v) return std::nullopt;
            obj.emplace_back(std::move(*key), std::move(*v));
            skipWs();
            if (consume("}")) return Value::object(std::move(obj));
            if (!consume(",")) {
                fail("',' oder '}' erwartet");
                return std::nullopt;
            }
        }
    }

    std::string_view s_;
    size_t i_ = 0;
    std::string err_;
};

}  // namespace

std::optional<Value> parse(std::string_view text, std::string* error) {
    // UTF-8-BOM (Windows-Editoren) ueberspringen
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    return Parser(text).run(error);
}

}  // namespace json
