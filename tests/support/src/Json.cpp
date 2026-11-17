#include "m2m/test/Json.h"

#include <locale>
#include <sstream>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>

namespace m2m::test
{

int Json::asInt (int fallback) const noexcept
{
    if (t != Type::Number || ! std::isfinite (num))
        return fallback;
    if (num >= static_cast<double> (std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    if (num <= static_cast<double> (std::numeric_limits<int>::min()))
        return std::numeric_limits<int>::min();
    return static_cast<int> (std::lround (num));
}

std::size_t Json::size() const noexcept
{
    return (t == Type::Array || t == Type::Object) ? values.size() : 0;
}

void Json::push (Json v)
{
    if (t == Type::Null)
        t = Type::Array;
    if (t == Type::Array)
        values.push_back (std::move (v));
}

Json& Json::operator[] (const std::string& key)
{
    if (t == Type::Null)
        t = Type::Object;

    for (std::size_t i = 0; i < objKeys.size(); ++i)
        if (objKeys[i] == key)
            return values[i];

    objKeys.push_back (key);
    values.emplace_back();
    return values.back();
}

const Json* Json::find (const std::string& key) const noexcept
{
    if (t != Type::Object)
        return nullptr;
    for (std::size_t i = 0; i < objKeys.size(); ++i)
        if (objKeys[i] == key)
            return &values[i];
    return nullptr;
}

double Json::getNumber (const std::string& key, double fallback) const noexcept
{
    const auto* v = find (key);
    return v != nullptr && v->isNumber() ? v->num : fallback;
}

std::string Json::getString (const std::string& key, const std::string& fallback) const
{
    const auto* v = find (key);
    return v != nullptr && v->isString() ? v->str : fallback;
}

//==============================================================================
namespace
{
    void appendEscaped (std::string& out, const std::string& s)
    {
        out += '"';
        for (const char c : s)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (static_cast<unsigned char> (c) < 0x20)
                    {
                        static const char* hex = "0123456789abcdef";
                        const auto u = static_cast<unsigned char> (c);
                        out += "\\u00";
                        out += hex[u >> 4];
                        out += hex[u & 0xf];
                    }
                    else
                    {
                        out += c;
                    }
            }
        }
        out += '"';
    }

    void appendNumber (std::string& out, double v)
    {
        if (! std::isfinite (v))
        {
            out += "null";
            return;
        }
        // Shortest decimal form that reads back to exactly the same double.
        // (std::to_chars for floating point is unavailable before macOS 13.3,
        // and we target macOS 11.) Locale-independent.
        std::string text;
        for (int precision = 1; precision <= 17; ++precision)
        {
            std::ostringstream os;
            os.imbue (std::locale::classic());
            os.precision (precision);
            os << v;
            text = os.str();

            std::istringstream is (text);
            is.imbue (std::locale::classic());
            double back = 0.0;
            is >> back;
            if (back == v)
                break;
        }
        out += text;
    }

    void newline (std::string& out, int indent, int depth)
    {
        if (indent < 0)
            return;
        out += '\n';
        out.append (static_cast<std::size_t> (indent * depth), ' ');
    }
}

void Json::dumpTo (std::string& out, int indent, int depth) const
{
    switch (t)
    {
        case Type::Null:   out += "null"; break;
        case Type::Bool:   out += b ? "true" : "false"; break;
        case Type::Number: appendNumber (out, num); break;
        case Type::String: appendEscaped (out, str); break;

        case Type::Array:
        {
            if (values.empty()) { out += "[]"; break; }

            // Arrays of scalars stay on one line; arrays of containers break.
            bool allScalar = true;
            for (const auto& v : values)
                allScalar = allScalar && ! (v.isArray() || v.isObject());

            out += '[';
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0)
                    out += allScalar ? ", " : ",";
                if (! allScalar)
                    newline (out, indent, depth + 1);
                values[i].dumpTo (out, indent, depth + 1);
            }
            if (! allScalar)
                newline (out, indent, depth);
            out += ']';
            break;
        }

        case Type::Object:
        {
            if (values.empty()) { out += "{}"; break; }

            out += '{';
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0)
                    out += ',';
                newline (out, indent, depth + 1);
                appendEscaped (out, objKeys[i]);
                out += indent >= 0 ? ": " : ":";
                values[i].dumpTo (out, indent, depth + 1);
            }
            newline (out, indent, depth);
            out += '}';
            break;
        }
    }
}

std::string Json::dump (int indent) const
{
    std::string out;
    dumpTo (out, indent, 0);
    if (indent >= 0)
        out += '\n';
    return out;
}

//==============================================================================
namespace
{
    class Parser
    {
    public:
        explicit Parser (const std::string& s) : text (s) {}

        std::optional<Json> run (std::string* error)
        {
            Json v;
            if (! parseValue (v, 0))
                return fail (error);
            skipWs();
            if (pos != text.size())
            {
                err = "trailing characters";
                return fail (error);
            }
            return v;
        }

    private:
        std::optional<Json> fail (std::string* error)
        {
            if (error != nullptr)
            {
                // Report a 1-based line/column for humans.
                std::size_t line = 1, col = 1;
                for (std::size_t i = 0; i < pos && i < text.size(); ++i)
                {
                    if (text[i] == '\n') { ++line; col = 1; }
                    else ++col;
                }
                *error = "JSON parse error at line " + std::to_string (line) + ", column "
                       + std::to_string (col) + ": " + err;
            }
            return std::nullopt;
        }

        void skipWs()
        {
            while (pos < text.size()
                   && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r'))
                ++pos;
        }

        bool literal (const char* word)
        {
            const std::string w (word);
            if (text.compare (pos, w.size(), w) != 0)
            {
                err = "invalid literal";
                return false;
            }
            pos += w.size();
            return true;
        }

        bool parseValue (Json& out, int depth)
        {
            if (depth > 200)
            {
                err = "nesting too deep";
                return false;
            }

            skipWs();
            if (pos >= text.size())
            {
                err = "unexpected end of input";
                return false;
            }

            const char c = text[pos];
            if (c == '{') return parseObject (out, depth);
            if (c == '[') return parseArray (out, depth);
            if (c == '"')
            {
                std::string s;
                if (! parseString (s))
                    return false;
                out = Json (std::move (s));
                return true;
            }
            if (c == 't') { if (! literal ("true"))  return false; out = Json (true);  return true; }
            if (c == 'f') { if (! literal ("false")) return false; out = Json (false); return true; }
            if (c == 'n') { if (! literal ("null"))  return false; out = Json();       return true; }
            if (c == '-' || (c >= '0' && c <= '9'))
                return parseNumber (out);

            err = std::string ("unexpected character '") + c + "'";
            return false;
        }

        bool parseNumber (Json& out)
        {
            const std::size_t start = pos;
            if (text[pos] == '-')
                ++pos;
            while (pos < text.size())
            {
                const char c = text[pos];
                if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
                    ++pos;
                else
                    break;
            }

            // std::from_chars for floating point is unavailable before macOS 26,
            // so parse with a classic-locale stream and require full consumption.
            double v = 0.0;
            std::istringstream is (std::string (text.substr (start, pos - start)));
            is.imbue (std::locale::classic());
            is >> v;
            const bool consumedAll = ! is.fail() && (is.peek() == std::char_traits<char>::eof());
            if (pos == start || ! consumedAll)
            {
                pos = start;
                err = "invalid number";
                return false;
            }
            out = Json (v);
            return true;
        }

        static void appendUtf8 (std::string& s, std::uint32_t cp)
        {
            if (cp < 0x80)
            {
                s += static_cast<char> (cp);
            }
            else if (cp < 0x800)
            {
                s += static_cast<char> (0xC0 | (cp >> 6));
                s += static_cast<char> (0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                s += static_cast<char> (0xE0 | (cp >> 12));
                s += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
                s += static_cast<char> (0x80 | (cp & 0x3F));
            }
            else
            {
                s += static_cast<char> (0xF0 | (cp >> 18));
                s += static_cast<char> (0x80 | ((cp >> 12) & 0x3F));
                s += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
                s += static_cast<char> (0x80 | (cp & 0x3F));
            }
        }

        bool parseHex4 (std::uint32_t& v)
        {
            if (pos + 4 > text.size())
            {
                err = "truncated \\u escape";
                return false;
            }
            v = 0;
            for (int i = 0; i < 4; ++i)
            {
                const char c = text[pos++];
                v <<= 4;
                if (c >= '0' && c <= '9')      v |= static_cast<std::uint32_t> (c - '0');
                else if (c >= 'a' && c <= 'f') v |= static_cast<std::uint32_t> (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= static_cast<std::uint32_t> (c - 'A' + 10);
                else
                {
                    err = "invalid \\u escape";
                    return false;
                }
            }
            return true;
        }

        bool parseString (std::string& s)
        {
            ++pos; // opening quote
            while (pos < text.size())
            {
                const char c = text[pos++];
                if (c == '"')
                    return true;
                if (c != '\\')
                {
                    s += c;
                    continue;
                }
                if (pos >= text.size())
                    break;

                const char e = text[pos++];
                switch (e)
                {
                    case '"':  s += '"'; break;
                    case '\\': s += '\\'; break;
                    case '/':  s += '/'; break;
                    case 'b':  s += '\b'; break;
                    case 'f':  s += '\f'; break;
                    case 'n':  s += '\n'; break;
                    case 'r':  s += '\r'; break;
                    case 't':  s += '\t'; break;
                    case 'u':
                    {
                        std::uint32_t cp = 0;
                        if (! parseHex4 (cp))
                            return false;
                        if (cp >= 0xD800 && cp < 0xDC00 && pos + 1 < text.size()
                            && text[pos] == '\\' && text[pos + 1] == 'u')
                        {
                            pos += 2;
                            std::uint32_t lo = 0;
                            if (! parseHex4 (lo))
                                return false;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        appendUtf8 (s, cp);
                        break;
                    }
                    default:
                        err = "invalid escape";
                        return false;
                }
            }
            err = "unterminated string";
            return false;
        }

        bool parseArray (Json& out, int depth)
        {
            ++pos;
            out = Json::array();
            skipWs();
            if (pos < text.size() && text[pos] == ']')
            {
                ++pos;
                return true;
            }
            for (;;)
            {
                Json v;
                if (! parseValue (v, depth + 1))
                    return false;
                out.push (std::move (v));
                skipWs();
                if (pos < text.size() && text[pos] == ',') { ++pos; continue; }
                if (pos < text.size() && text[pos] == ']') { ++pos; return true; }
                err = "expected ',' or ']'";
                return false;
            }
        }

        bool parseObject (Json& out, int depth)
        {
            ++pos;
            out = Json::object();
            skipWs();
            if (pos < text.size() && text[pos] == '}')
            {
                ++pos;
                return true;
            }
            for (;;)
            {
                skipWs();
                if (pos >= text.size() || text[pos] != '"')
                {
                    err = "expected string key";
                    return false;
                }
                std::string key;
                if (! parseString (key))
                    return false;
                skipWs();
                if (pos >= text.size() || text[pos] != ':')
                {
                    err = "expected ':'";
                    return false;
                }
                ++pos;
                Json v;
                if (! parseValue (v, depth + 1))
                    return false;
                out[key] = std::move (v);
                skipWs();
                if (pos < text.size() && text[pos] == ',') { ++pos; continue; }
                if (pos < text.size() && text[pos] == '}') { ++pos; return true; }
                err = "expected ',' or '}'";
                return false;
            }
        }

        const std::string& text;
        std::size_t pos = 0;
        std::string err;
    };
}

std::optional<Json> Json::parse (const std::string& text, std::string* error)
{
    return Parser (text).run (error);
}

//==============================================================================
bool readTextFile (const std::string& path, std::string& out, std::string& error)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
    {
        error = "cannot open '" + path + "' for reading";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool writeTextFile (const std::string& path, const std::string& text, std::string& error)
{
    std::ofstream o (path, std::ios::binary | std::ios::trunc);
    if (! o)
    {
        error = "cannot open '" + path + "' for writing";
        return false;
    }
    o << text;
    o.flush();
    if (! o)
    {
        error = "error writing '" + path + "'";
        return false;
    }
    return true;
}

} // namespace m2m::test
