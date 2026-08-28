// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/model/Json.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>

namespace caecilia::model::json
{
namespace
{
/// Nesting limit.
///
/// An organ document is four levels deep at its worst. This is not about them:
/// a file is bytes off a disk, and a few thousand open brackets in a row would
/// otherwise recurse the parser straight off the stack. Refusing at a depth no
/// real document reaches costs nothing and turns a crash into a diagnostic.
constexpr int kMaxDepth = 64;

[[nodiscard]] bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

/// Append @p cp to @p out as UTF-8.
void appendUtf8(std::string& out, std::uint32_t cp)
{
    if (cp < 0x80)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}
} // namespace

std::string Error::describe() const
{
    return "line " + std::to_string(where.line) + ", column "
         + std::to_string(where.column) + ": " + message;
}

bool Value::asBool(bool fallback) const noexcept
{
    return kind_ == Kind::Bool ? bool_ : fallback;
}

double Value::asNumber(double fallback) const noexcept
{
    return kind_ == Kind::Number ? number_ : fallback;
}

const std::string& Value::asString() const noexcept
{
    static const std::string kEmpty;
    return kind_ == Kind::String ? string_ : kEmpty;
}

const Value* Value::find(std::string_view name) const noexcept
{
    if (kind_ != Kind::Object)
        return nullptr;
    for (const auto& m : members_)
        if (m.first == name)
            return &m.second;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Reading.
// ---------------------------------------------------------------------------

class Parser
{
public:
    Parser(std::string_view text, Error& error) : text_(text), error_(error) {}

    [[nodiscard]] bool run(Value& out)
    {
        skipWhitespace();
        if (!parseValue(out, 0))
            return false;
        skipWhitespace();
        if (pos_ != text_.size())
        {
            fail("unexpected content after the document");
            return false;
        }
        return true;
    }

private:
    [[nodiscard]] Position here() const noexcept { return { line_, column_ }; }

    void fail(std::string message)
    {
        // The FIRST failure wins. A parser that overwrites its error as the stack
        // unwinds reports the outermost construct rather than the character that
        // actually went wrong, which is the least useful of the two.
        if (!failed_)
        {
            error_.message = std::move(message);
            error_.where   = here();
            failed_        = true;
        }
    }

    [[nodiscard]] bool atEnd() const noexcept { return pos_ >= text_.size(); }
    [[nodiscard]] char peek() const noexcept { return atEnd() ? '\0' : text_[pos_]; }

    char advance() noexcept
    {
        const char c = text_[pos_++];
        if (c == '\n') { ++line_; column_ = 1; }
        else           { ++column_; }
        return c;
    }

    void skipWhitespace() noexcept
    {
        while (!atEnd())
        {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                (void) advance();
            else
                break;
        }
    }

    [[nodiscard]] bool expect(char c)
    {
        if (peek() != c)
        {
            fail(std::string("expected '") + c + "'");
            return false;
        }
        (void) advance();
        return true;
    }

    [[nodiscard]] bool literal(std::string_view word)
    {
        if (text_.compare(pos_, word.size(), word) != 0)
        {
            fail("unrecognised token");
            return false;
        }
        for (std::size_t i = 0; i < word.size(); ++i)
            (void) advance();
        return true;
    }

    [[nodiscard]] bool parseValue(Value& out, int depth)
    {
        if (depth > kMaxDepth)
        {
            fail("nested too deeply");
            return false;
        }

        const Position start = here();
        bool ok = false;

        switch (peek())
        {
            case '{': ok = parseObject(out, depth); break;
            case '[': ok = parseArray(out, depth);  break;
            case '"':
            {
                std::string s;
                ok = parseString(s);
                if (ok) out = Value(std::move(s));
                break;
            }
            case 't': ok = literal("true");  if (ok) out = Value(true);  break;
            case 'f': ok = literal("false"); if (ok) out = Value(false); break;
            case 'n': ok = literal("null");  if (ok) out = Value();      break;
            default:  ok = parseNumber(out); break;
        }

        if (ok)
            out.setPosition(start);
        return ok;
    }

    [[nodiscard]] bool parseObject(Value& out, int depth)
    {
        if (!expect('{'))
            return false;
        out = Value::object();

        skipWhitespace();
        if (peek() == '}') { (void) advance(); return true; }

        for (;;)
        {
            skipWhitespace();

            std::string key;
            if (!parseString(key))
                return false;

            skipWhitespace();
            if (!expect(':'))
                return false;

            skipWhitespace();
            Value member;
            if (!parseValue(member, depth + 1))
                return false;

            out.members_.emplace_back(std::move(key), std::move(member));

            skipWhitespace();
            if (peek() == ',') { (void) advance(); continue; }
            if (peek() == '}') { (void) advance(); return true; }

            fail("expected ',' or '}'");
            return false;
        }
    }

    [[nodiscard]] bool parseArray(Value& out, int depth)
    {
        if (!expect('['))
            return false;
        out = Value::array();

        skipWhitespace();
        if (peek() == ']') { (void) advance(); return true; }

        for (;;)
        {
            skipWhitespace();
            Value element;
            if (!parseValue(element, depth + 1))
                return false;
            out.items_.push_back(std::move(element));

            skipWhitespace();
            if (peek() == ',') { (void) advance(); continue; }
            if (peek() == ']') { (void) advance(); return true; }

            fail("expected ',' or ']'");
            return false;
        }
    }

    [[nodiscard]] bool parseString(std::string& out)
    {
        if (!expect('"'))
            return false;

        out.clear();
        for (;;)
        {
            if (atEnd())
            {
                fail("unterminated string");
                return false;
            }

            const char c = advance();
            if (c == '"')
                return true;

            if (c != '\\')
            {
                // A raw control character is not allowed in a JSON string, and
                // letting one through is how a stray newline in a name becomes a
                // file that reads back differently from the one that was written.
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    fail("control character in string");
                    return false;
                }
                out.push_back(c);
                continue;
            }

            if (atEnd())
            {
                fail("unterminated escape");
                return false;
            }

            switch (advance())
            {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u':
                {
                    std::uint32_t cp = 0;
                    if (!parseHex4(cp))
                        return false;

                    // A surrogate pair is two escapes that mean one character.
                    // Emitting them separately produces bytes no decoder accepts.
                    if (cp >= 0xD800 && cp <= 0xDBFF)
                    {
                        if (peek() != '\\')
                        {
                            fail("lone high surrogate");
                            return false;
                        }
                        (void) advance();
                        if (peek() != 'u')
                        {
                            fail("lone high surrogate");
                            return false;
                        }
                        (void) advance();

                        std::uint32_t low = 0;
                        if (!parseHex4(low))
                            return false;
                        if (low < 0xDC00 || low > 0xDFFF)
                        {
                            fail("expected a low surrogate");
                            return false;
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    else if (cp >= 0xDC00 && cp <= 0xDFFF)
                    {
                        fail("lone low surrogate");
                        return false;
                    }

                    appendUtf8(out, cp);
                    break;
                }
                default:
                    fail("unknown escape");
                    return false;
            }
        }
    }

    [[nodiscard]] bool parseHex4(std::uint32_t& out)
    {
        out = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (atEnd())
            {
                fail("truncated \\u escape");
                return false;
            }
            const char c = advance();
            std::uint32_t d = 0;
            if      (c >= '0' && c <= '9') d = static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<std::uint32_t>(c - 'A' + 10);
            else
            {
                fail("bad hex digit in \\u escape");
                return false;
            }
            out = (out << 4) | d;
        }
        return true;
    }

    [[nodiscard]] bool parseNumber(Value& out)
    {
        const std::size_t start = pos_;

        if (peek() == '-')
            (void) advance();

        if (!isDigit(peek()))
        {
            fail("expected a value");
            return false;
        }

        // JSON forbids a leading zero on a multi-digit integer, and accepting one
        // is how "octal" habits from other formats quietly change a number.
        if (peek() == '0')
        {
            (void) advance();
            if (isDigit(peek()))
            {
                fail("leading zeros are not allowed");
                return false;
            }
        }
        else
        {
            while (isDigit(peek()))
                (void) advance();
        }

        if (peek() == '.')
        {
            (void) advance();
            if (!isDigit(peek()))
            {
                fail("expected a digit after '.'");
                return false;
            }
            while (isDigit(peek()))
                (void) advance();
        }

        if (peek() == 'e' || peek() == 'E')
        {
            (void) advance();
            if (peek() == '+' || peek() == '-')
                (void) advance();
            if (!isDigit(peek()))
            {
                fail("expected a digit in the exponent");
                return false;
            }
            while (isDigit(peek()))
                (void) advance();
        }

        const std::string_view token = text_.substr(start, pos_ - start);

        double       value = 0.0;
        const char*  first = token.data();
        const char*  last  = token.data() + token.size();
        const auto   res   = std::from_chars(first, last, value);
        if (res.ec != std::errc{} || res.ptr != last)
        {
            fail("number out of range");
            return false;
        }

        out = Value(value);
        return true;
    }

    std::string_view text_;
    Error&           error_;
    std::size_t      pos_    = 0;
    int              line_   = 1;
    int              column_ = 1;
    bool             failed_ = false;
};

std::optional<Value> parse(std::string_view text, Error& error)
{
    error = Error{};

    Value  root;
    Parser p(text, error);
    if (!p.run(root))
        return std::nullopt;
    return root;
}

// ---------------------------------------------------------------------------
// Writing.
// ---------------------------------------------------------------------------

namespace
{
void writeString(std::string& out, const std::string& s)
{
    out.push_back('"');
    for (const char c : s)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    // The only characters that MUST be escaped beyond the named
                    // ones. Everything above them, UTF-8 included, is written
                    // through unchanged -- a stop called "Flûte" should read as
                    // itself in a text editor, not as an escape.
                    static const char* kHex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(kHex[(static_cast<unsigned char>(c) >> 4) & 0x0F]);
                    out.push_back(kHex[static_cast<unsigned char>(c) & 0x0F]);
                }
                else
                {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void writeNumber(std::string& out, double d)
{
    // An integral value is written without a decimal point, because most numbers
    // in an organ file are counts and note numbers and "36.0" in place of "36"
    // makes a hand-edited file look machine-mangled.
    if (std::isfinite(d) && d == std::floor(d) && std::abs(d) < 1.0e15)
    {
        out += std::to_string(static_cast<long long>(d));
        return;
    }

    char       buf[40];
    const auto res = std::to_chars(buf, buf + sizeof(buf), d);
    if (res.ec == std::errc{})
        out.append(buf, res.ptr);
    else
        out += "0";
}

void writeValue(std::string& out, const Value& v, int indent, int level)
{
    const auto newline = [&](int lvl)
    {
        if (indent <= 0)
            return;
        out.push_back('\n');
        out.append(static_cast<std::size_t>(indent * lvl), ' ');
    };

    switch (v.kind())
    {
        case Value::Kind::Null:   out += "null"; break;
        case Value::Kind::Bool:   out += v.asBool() ? "true" : "false"; break;
        case Value::Kind::Number: writeNumber(out, v.asNumber()); break;
        case Value::Kind::String: writeString(out, v.asString()); break;

        case Value::Kind::Array:
            if (v.items().empty()) { out += "[]"; break; }
            out.push_back('[');
            for (std::size_t i = 0; i < v.items().size(); ++i)
            {
                if (i > 0) out.push_back(',');
                newline(level + 1);
                writeValue(out, v.items()[i], indent, level + 1);
            }
            newline(level);
            out.push_back(']');
            break;

        case Value::Kind::Object:
            if (v.members().empty()) { out += "{}"; break; }
            out.push_back('{');
            for (std::size_t i = 0; i < v.members().size(); ++i)
            {
                if (i > 0) out.push_back(',');
                newline(level + 1);
                writeString(out, v.members()[i].first);
                out.push_back(':');
                if (indent > 0) out.push_back(' ');
                writeValue(out, v.members()[i].second, indent, level + 1);
            }
            newline(level);
            out.push_back('}');
            break;
    }
}
} // namespace

std::string write(const Value& value, int indent)
{
    std::string out;
    writeValue(out, value, indent, 0);
    return out;
}

} // namespace caecilia::model::json
