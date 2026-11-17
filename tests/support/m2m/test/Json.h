#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace m2m::test
{

/** Minimal JSON value (parse + pretty print) for truth files, reports and
    baselines. Objects keep insertion order. Non-finite numbers are written as
    null. Test/tool code only: allocates freely. */
class Json
{
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json (std::nullptr_t) {}
    Json (bool v) : t (Type::Bool), b (v) {}
    Json (const char* s) : t (Type::String), str (s) {}
    Json (std::string s) : t (Type::String), str (std::move (s)) {}

    template <typename T>
        requires (std::is_arithmetic_v<T> && ! std::is_same_v<T, bool>)
    Json (T v) : t (Type::Number), num (static_cast<double> (v)) {}

    static Json array()  { Json j; j.t = Type::Array;  return j; }
    static Json object() { Json j; j.t = Type::Object; return j; }

    Type type() const noexcept { return t; }
    bool isNull() const noexcept   { return t == Type::Null; }
    bool isBool() const noexcept   { return t == Type::Bool; }
    bool isNumber() const noexcept { return t == Type::Number; }
    bool isString() const noexcept { return t == Type::String; }
    bool isArray() const noexcept  { return t == Type::Array; }
    bool isObject() const noexcept { return t == Type::Object; }

    bool asBool (bool fallback = false) const noexcept      { return t == Type::Bool ? b : fallback; }
    double asNumber (double fallback = 0.0) const noexcept  { return t == Type::Number ? num : fallback; }
    int asInt (int fallback = 0) const noexcept;
    std::string asString (const std::string& fallback = {}) const { return t == Type::String ? str : fallback; }

    /** Array/object element count (0 otherwise). */
    std::size_t size() const noexcept;

    /** Array access. push() turns a null value into an array. */
    void push (Json v);
    const Json& at (std::size_t i) const { return values.at (i); }
    const std::vector<Json>& items() const noexcept { return values; }

    /** Object access. operator[] inserts (turning a null value into an object). */
    Json& operator[] (const std::string& key);
    const Json* find (const std::string& key) const noexcept;
    const std::vector<std::string>& keys() const noexcept { return objKeys; }

    /** Object lookups with defaults (null / missing / wrong type -> fallback). */
    double getNumber (const std::string& key, double fallback = 0.0) const noexcept;
    std::string getString (const std::string& key, const std::string& fallback = {}) const;

    std::string dump (int indent = 2) const;

    static std::optional<Json> parse (const std::string& text, std::string* error = nullptr);

private:
    void dumpTo (std::string& out, int indent, int depth) const;

    Type t = Type::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Json> values;           // array elements / object values
    std::vector<std::string> objKeys;   // object keys (parallel to values)
};

bool readTextFile (const std::string& path, std::string& out, std::string& error);
bool writeTextFile (const std::string& path, const std::string& text, std::string& error);

} // namespace m2m::test
