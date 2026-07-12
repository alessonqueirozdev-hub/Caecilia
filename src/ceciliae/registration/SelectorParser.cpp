/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/registration/SelectorParser.h"

#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <vector>

namespace ceciliae::registration
{
namespace
{
// ---------------------------------------------------------------------------
// Tokeniser
// ---------------------------------------------------------------------------

enum class Tok
{
    LParen, RParen, Amp, Pipe, Bang, Dash, Colon, Word, End
};

struct Token
{
    Tok         kind = Tok::End;
    std::string text;      ///< Only for Word.
    std::size_t pos = 0;   ///< Byte offset in the source.
};

/// A character that may appear inside an unquoted word / value.
bool isWordChar(char c) noexcept
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0
           || c == '_' || c == '.' || c == '/' || c == '\'';
}

std::vector<Token> tokenize(std::string_view text)
{
    std::vector<Token> out;
    std::size_t        i = 0;
    while (i < text.size())
    {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c)) != 0) { ++i; continue; }

        switch (c)
        {
            case '(': out.push_back({Tok::LParen, {}, i}); ++i; continue;
            case ')': out.push_back({Tok::RParen, {}, i}); ++i; continue;
            case '&': out.push_back({Tok::Amp,    {}, i}); ++i; continue;
            case '|': out.push_back({Tok::Pipe,   {}, i}); ++i; continue;
            case '!': out.push_back({Tok::Bang,   {}, i}); ++i; continue;
            case '-': out.push_back({Tok::Dash,   {}, i}); ++i; continue;
            case ':': out.push_back({Tok::Colon,  {}, i}); ++i; continue;
            case '*': out.push_back({Tok::Word,  "*", i}); ++i; continue;
            default: break;
        }

        if (c == '"') // quoted value
        {
            const std::size_t start = i++;
            std::string       value;
            while (i < text.size() && text[i] != '"')
                value.push_back(text[i++]);
            if (i < text.size()) ++i; // consume closing quote
            out.push_back({Tok::Word, std::move(value), start});
            continue;
        }

        if (isWordChar(c))
        {
            const std::size_t start = i;
            std::string       word;
            while (i < text.size() && isWordChar(text[i]))
                word.push_back(text[i++]);
            out.push_back({Tok::Word, std::move(word), start});
            continue;
        }

        // Unknown character: emit a zero-width word so the parser can report it.
        out.push_back({Tok::Word, std::string(1, c), i});
        ++i;
    }
    out.push_back({Tok::End, {}, text.size()});
    return out;
}

// ---------------------------------------------------------------------------
// Value -> enum / footage mapping
// ---------------------------------------------------------------------------

std::string toLower(std::string s)
{
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

std::optional<core::TonalFamily> parseFamily(const std::string& v)
{
    const std::string k = toLower(v);
    if (k == "principal" || k == "diapason") return core::TonalFamily::Principal;
    if (k == "flute")      return core::TonalFamily::Flute;
    if (k == "string")     return core::TonalFamily::String;
    if (k == "reed")       return core::TonalFamily::Reed;
    if (k == "mixture")    return core::TonalFamily::Mixture;
    if (k == "hybrid")     return core::TonalFamily::Hybrid;
    if (k == "percussion") return core::TonalFamily::Percussion;
    return std::nullopt;
}

std::optional<core::ChorusRole> parseRole(const std::string& v)
{
    const std::string k = toLower(v);
    if (k == "foundation")                    return core::ChorusRole::Foundation;
    if (k == "chorus")                        return core::ChorusRole::Chorus;
    if (k == "crown" || k == "mixturecrown")  return core::ChorusRole::MixtureCrown;
    if (k == "solo")                          return core::ChorusRole::Solo;
    if (k == "color" || k == "colour")        return core::ChorusRole::Color;
    if (k == "effect")                        return core::ChorusRole::Effect;
    return std::nullopt;
}

std::optional<core::PitchClass> parsePitchClass(const std::string& v)
{
    const std::string k = toLower(v);
    if (k == "sub")      return core::PitchClass::Sub;
    if (k == "unison")   return core::PitchClass::Unison;
    if (k == "octave")   return core::PitchClass::Octave;
    if (k == "super")    return core::PitchClass::Super;
    if (k == "mutation") return core::PitchClass::Mutation;
    if (k == "compound") return core::PitchClass::Compound;
    return std::nullopt;
}

std::optional<std::int32_t> parseInt(std::string_view s)
{
    std::int32_t out = 0;
    const auto   res = std::from_chars(s.data(), s.data() + s.size(), out);
    if (res.ec != std::errc{} || res.ptr != s.data() + s.size())
        return std::nullopt;
    return out;
}

/// Parse a footage value in feet: "8" -> 8/1, "8/3" -> 2 2/3', "16/3" -> 5 1/3'.
std::optional<core::Footage> parseFootage(const std::string& v)
{
    const auto slash = v.find('/');
    if (slash == std::string::npos)
    {
        if (const auto n = parseInt(v); n && *n > 0)
            return core::Footage{*n, 1};
        return std::nullopt;
    }
    const auto num = parseInt(std::string_view{v}.substr(0, slash));
    const auto den = parseInt(std::string_view{v}.substr(slash + 1));
    if (num && den && *num > 0 && *den > 0)
        return core::Footage{*num, *den};
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Recursive-descent parser
// ---------------------------------------------------------------------------

class Parser
{
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    SelectorParser::Result run()
    {
        SelectorParser::Result r;
        // Empty input -> universal atom.
        if (peek().kind == Tok::End)
        {
            r.ok       = true;
            r.selector = Selector::atom(StopQuery{});
            return r;
        }

        Selector sel;
        if (!parseUnion(sel))
            return fail_;

        if (peek().kind != Tok::End)
            return error("unexpected trailing input");

        r.ok       = true;
        r.selector = std::move(sel);
        return r;
    }

private:
    const Token& peek() const { return tokens_[index_]; }
    const Token& advance() { return tokens_[index_ < tokens_.size() - 1 ? index_++ : index_]; }

    SelectorParser::Result error(std::string message)
    {
        fail_.ok       = false;
        fail_.selector = Selector::atom(StopQuery{});
        fail_.error    = std::move(message);
        fail_.errorPos = peek().pos;
        return fail_;
    }

    bool parseUnion(Selector& out)
    {
        Selector lhs;
        if (!parseIntersection(lhs)) return false;
        while (peek().kind == Tok::Pipe)
        {
            advance();
            Selector rhs;
            if (!parseIntersection(rhs)) return false;
            lhs = Selector::combine(Selector::Op::Union, {std::move(lhs), std::move(rhs)});
        }
        out = std::move(lhs);
        return true;
    }

    bool parseIntersection(Selector& out)
    {
        Selector lhs;
        if (!parseUnary(lhs)) return false;
        for (;;)
        {
            if (peek().kind == Tok::Amp)
            {
                advance();
                Selector rhs;
                if (!parseUnary(rhs)) return false;
                lhs = Selector::combine(Selector::Op::Intersect, {std::move(lhs), std::move(rhs)});
            }
            else if (peek().kind == Tok::Dash)
            {
                advance();
                Selector rhs;
                if (!parseUnary(rhs)) return false;
                lhs = Selector::combine(Selector::Op::Difference, {std::move(lhs), std::move(rhs)});
            }
            else
            {
                break;
            }
        }
        out = std::move(lhs);
        return true;
    }

    bool parseUnary(Selector& out)
    {
        if (peek().kind == Tok::Bang)
        {
            advance();
            Selector inner;
            if (!parseUnary(inner)) return false;
            out = Selector::complementOf(std::move(inner));
            return true;
        }
        return parsePrimary(out);
    }

    bool parsePrimary(Selector& out)
    {
        if (peek().kind == Tok::LParen)
        {
            advance();
            Selector inner;
            if (!parseUnion(inner)) return false;
            if (peek().kind != Tok::RParen) { error("expected ')'"); return false; }
            advance();
            out = std::move(inner);
            return true;
        }

        if (peek().kind == Tok::Word)
        {
            const Token keyTok = advance();

            // Bare keyword?
            if (peek().kind != Tok::Colon)
            {
                const std::string k = toLower(keyTok.text);
                if (k == "engaged")
                {
                    StopQuery q;
                    q.engaged = StopQuery::Engaged::Only;
                    out       = Selector::atom(std::move(q));
                    return true;
                }
                if (k == "all" || k == "*")
                {
                    out = Selector::atom(StopQuery{});
                    return true;
                }
                error("unknown keyword '" + keyTok.text + "'");
                return false;
            }

            // key ':' value
            advance(); // consume colon
            if (peek().kind != Tok::Word) { error("expected value after ':'"); return false; }
            const Token valTok = advance();
            return buildPredicate(keyTok.text, valTok.text, out);
        }

        error("expected a selector term");
        return false;
    }

    bool buildPredicate(const std::string& rawKey, const std::string& value, Selector& out)
    {
        const std::string key = toLower(rawKey);
        StopQuery         q;

        if (key == "family")
        {
            if (const auto f = parseFamily(value)) q.family = *f;
            else { error("unknown family '" + value + "'"); return false; }
        }
        else if (key == "role")
        {
            if (const auto r = parseRole(value)) q.role = *r;
            else { error("unknown role '" + value + "'"); return false; }
        }
        else if (key == "class")
        {
            if (const auto p = parsePitchClass(value)) q.pitchClass = *p;
            else { error("unknown pitch class '" + value + "'"); return false; }
        }
        else if (key == "pitch" || key == "footage")
        {
            if (const auto ft = parseFootage(value)) q.footage = *ft;
            else { error("invalid footage '" + value + "'"); return false; }
        }
        else if (key == "div" || key == "division")
        {
            q.divisionName = value;
        }
        else if (key == "name")
        {
            q.nameContains = value;
        }
        else
        {
            error("unknown key '" + rawKey + "'");
            return false;
        }

        out = Selector::atom(std::move(q));
        return true;
    }

    std::vector<Token>     tokens_;
    std::size_t            index_ = 0;
    SelectorParser::Result fail_{};
};

} // namespace

SelectorParser::Result SelectorParser::parse(std::string_view text) const
{
    Parser parser{tokenize(text)};
    return parser.run();
}

std::string grammarVersionString()
{
    const auto v = SelectorParser::version();
    return std::to_string(v.major) + "." + std::to_string(v.minor);
}

} // namespace ceciliae::registration
