#pragma once

// Kleiner JSON-Leser (RFC 8259) fuer Bot-Profile und spaeter Plugin-Manifeste.

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace json {

class Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;  // Reihenfolge bleibt erhalten

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    static Value boolean(bool b);
    static Value number(double n);
    static Value string(std::string s);
    static Value array(Array a);
    static Value object(Object o);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool fallback = false) const { return isBool() ? b_ : fallback; }
    double asNumber(double fallback = 0) const { return isNumber() ? n_ : fallback; }
    const std::string& asString() const;
    const Array& asArray() const;
    const Object& asObject() const;

    // Nur fuer Objekte: Feld suchen (nullptr, wenn nicht vorhanden).
    const Value* find(std::string_view key) const;
    double getNumber(std::string_view key, double fallback) const;
    std::string getString(std::string_view key, const std::string& fallback) const;
    bool getBool(std::string_view key, bool fallback) const;

private:
    Type type_ = Type::Null;
    bool b_ = false;
    double n_ = 0;
    std::string s_;
    std::shared_ptr<Array> a_;
    std::shared_ptr<Object> o_;
};

// Gibt bei Fehlern nullopt zurueck und schreibt die Meldung (mit Zeile/Spalte) nach `error`.
std::optional<Value> parse(std::string_view text, std::string* error = nullptr);

}  // namespace json
