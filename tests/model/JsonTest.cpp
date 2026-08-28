// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// The JSON reader an organ file is read with.
//
// Written in the core rather than pulled in, because caecilia_core links only the
// C++ standard library -- the constraint that keeps the whole instrument buildable
// with -DCAECILIA_CORE_ONLY -- and an organ file is the one thing in the core that
// has to come off a disk.
//
// A parser is somewhere a codebase quietly accumulates dialect: a trailing comma
// accepted here, a comment there, and a file that no other tool will read. So
// what is checked here is as much what it REFUSES as what it accepts.
//
// JSON is written below with SINGLE quotes and translated by `j`. Not a
// convenience: MSVC's traditional preprocessor does not recognise a raw string
// inside a macro argument, so CHECK(ok(R"(...)")) is read as an ordinary string
// and its backslashes become invalid escapes. Where a test is about backslashes
// they are written out explicitly.
//

#include "caecilia/model/Json.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using Catch::Approx;
namespace json = caecilia::model::json;

namespace
{
/// Single quotes to double, so a JSON document reads as one in C++ source.
std::string j(std::string s)
{
    for (char& c : s)
        if (c == '\'')
            c = '"';
    return s;
}

/// Parse, requiring success.
json::Value ok(const std::string& text)
{
    json::Error e;
    const auto  v = json::parse(text, e);
    INFO("parsing " << text << " failed: " << e.describe());
    REQUIRE(v.has_value());
    return *v;
}

/// Parse, requiring failure, and return where it complained.
json::Error bad(const std::string& text)
{
    json::Error e;
    const auto  v = json::parse(text, e);
    INFO("expected " << text << " to be rejected");
    REQUIRE_FALSE(v.has_value());
    CHECK_FALSE(e.message.empty());
    return e;
}
} // namespace

TEST_CASE("The reader reads what JSON is", "[model][json]")
{
    CHECK(ok("null").isNull());
    CHECK(ok("true").asBool());
    CHECK_FALSE(ok("false").asBool());
    CHECK(ok("0").asNumber() == Approx(0.0));
    CHECK(ok("-12").asNumber() == Approx(-12.0));
    CHECK(ok("3.25").asNumber() == Approx(3.25));
    CHECK(ok("1e3").asNumber() == Approx(1000.0));
    CHECK(ok("-2.5E-2").asNumber() == Approx(-0.025));
    CHECK(ok(j("''")).asString().empty());
    CHECK(ok(j("'Montre'")).asString() == "Montre");

    const json::Value arr = ok("[1, 2, 3]");
    REQUIRE(arr.isArray());
    REQUIRE(arr.items().size() == 3);
    CHECK(arr.items()[2].asNumber() == Approx(3.0));

    const json::Value obj = ok(j("{'a': 1, 'b': [true, null]}"));
    REQUIRE(obj.isObject());
    REQUIRE(obj.find("a") != nullptr);
    CHECK(obj.find("a")->asNumber() == Approx(1.0));
    REQUIRE(obj.find("b") != nullptr);
    CHECK(obj.find("b")->items().size() == 2);
    CHECK(obj.find("missing") == nullptr);

    CHECK(ok("[]").items().empty());
    CHECK(ok("{}").members().empty());
    CHECK(ok("  \n\t [ 1 ] \r\n ").items().size() == 1);
}

TEST_CASE("The reader refuses what JSON is not", "[model][json]")
{
    // Every one of these is something another format allows and JSON does not.
    // Accepting them would teach an organ builder a dialect that no other tool
    // reads -- and they would only find out when they handed the file to someone.
    bad("{'a': 1}");                 // single quotes, for real this time
    bad(j("{'a': 1,}"));             // trailing comma in an object
    bad("[1, 2,]");                  // trailing comma in an array
    bad(j("{'a': 1} // hi"));        // comments
    bad("{a: 1}");                   // unquoted key
    bad("01");                       // leading zero
    bad("-");                        // a sign and nothing else
    bad(".5");                       // no integer part
    bad("1.");                       // no fraction after the point
    bad("1e");                       // no exponent digits
    bad("nul");                      // truncated literal
    bad("[1 2]");                    // missing comma
    bad(j("{'a' 1}"));               // missing colon
    bad("");                         // nothing at all
    bad("   ");                      // nothing but whitespace

    // Two documents in one file is a mistake, not a stream.
    bad("{} {}");
    bad("1 2");
}

TEST_CASE("A string comes back as it went in", "[model][json]")
{
    // The five escapes an organ file realistically contains, spelled out because
    // this is the one test that is actually about backslashes.
    const std::string quoted    = "\"a\\\"b\"";  // JSON source: "a\"b"
    const std::string backslash = "\"a\\\\b\"";  // JSON source: "a\\b"
    const std::string slash     = "\"a\\/b\"";   // JSON source: "a\/b"
    const std::string newline   = "\"a\\nb\"";   // JSON source: "a\nb"
    const std::string tab       = "\"a\\tb\"";   // JSON source: "a\tb"

    CHECK(ok(quoted).asString()    == "a\"b");
    CHECK(ok(backslash).asString() == "a\\b");
    CHECK(ok(slash).asString()     == "a/b");
    CHECK(ok(newline).asString()   == "a\nb");
    CHECK(ok(tab).asString()       == "a\tb");

    // Accented stop names are the ordinary case here, not an edge one: this
    // instrument's own divisions are called Récit and Pédale. Raw UTF-8 passes
    // through untouched, which is what lets a name read as itself in an editor
    // rather than as a row of escapes.
    const std::string accented = "\"R\xc3\xa9\x63it\"";
    CHECK(ok(accented).asString() == "R\xc3\xa9\x63it");

    // A surrogate pair is two escapes meaning one character; emitting them
    // separately produces bytes no decoder accepts. U+1D11E, the treble clef.
    const std::string pair = "\"\\ud834\\udd1e\"";
    const std::string clef = ok(pair).asString();
    CHECK(clef.size() == 4);
    CHECK(static_cast<unsigned char>(clef[0]) == 0xF0);
    CHECK(static_cast<unsigned char>(clef[1]) == 0x9D);

    // A plain BMP escape, and the ASCII one.
    CHECK(ok("\"\\u00e9\"").asString() == "\xc3\xa9");
    CHECK(ok("\"\\u0041\"").asString() == "A");

    bad("\"\\ud834\"");        // lone high surrogate
    bad("\"\\udd1e\"");        // lone low surrogate
    bad("\"\\ud834A\"");       // high surrogate followed by something else
    bad("\"\\q\"");            // unknown escape
    bad("\"\\u00g1\"");        // bad hex digit
    bad("\"unterminated");
    bad("\"raw\nnewline\"");   // a control character in a string
}

TEST_CASE("A failure says where it happened", "[model][json]")
{
    // A diagnostic that does not point at a line is a diagnostic an organ builder
    // has to bisect their file to act on.
    const json::Error e = bad(j("{\n  'a': 1,\n  'b': nul\n}"));
    INFO(e.describe());
    CHECK(e.where.line == 3);
    CHECK(e.where.column > 1);
    CHECK(e.describe().find("line 3") != std::string::npos);

    // The FIRST failure is the one reported, not the outermost construct the
    // stack unwound through.
    const json::Error deep = bad("[[[ 01 ]]]");
    CHECK(deep.message.find("zero") != std::string::npos);
}

TEST_CASE("Nesting is bounded", "[model][json]")
{
    // A file is bytes off a disk. A few thousand open brackets would otherwise
    // recurse the parser off the stack, and a crash is a worse answer than a
    // diagnostic no real document will ever see.
    std::string deep;
    for (int i = 0; i < 5000; ++i) deep += '[';
    for (int i = 0; i < 5000; ++i) deep += ']';

    const json::Error e = bad(deep);
    CHECK(e.message.find("deep") != std::string::npos);
}

TEST_CASE("An object keeps the order it was written in", "[model][json]")
{
    // Not a nicety: it is what makes a read-modify-write of an organ file diff as
    // the change the user made rather than as a wholesale reordering.
    const json::Value v = ok(j("{'zeta': 1, 'alpha': 2, 'mu': 3}"));
    REQUIRE(v.members().size() == 3);
    CHECK(v.members()[0].first == "zeta");
    CHECK(v.members()[1].first == "alpha");
    CHECK(v.members()[2].first == "mu");

    const std::string compact = j("{'zeta':1,'alpha':2,'mu':3}");
    CHECK(json::write(v, 0) == compact);
}

TEST_CASE("What is written reads back the same", "[model][json]")
{
    const std::string source = j(
        "{\n"
        "  'name': 'Caecilia',\n"
        "  'year': 1890,\n"
        "  'ratio': 0.375,\n"
        "  'enclosed': true,\n"
        "  'nothing': null,\n"
        "  'list': [1, 'two', false, {'deep': []}],\n"
        "  'accented': 'R\xc3\xa9\x63it'\n"
        "}");

    const json::Value first  = ok(source);
    const json::Value second = ok(json::write(first));

    // Same shape, same values, same order -- checked by writing both compactly
    // and comparing the text, which is the strongest statement available without
    // an equality operator on the value type.
    CHECK(json::write(second, 0) == json::write(first, 0));

    // And indenting changes only the whitespace.
    CHECK(json::write(ok(json::write(first, 4)), 0) == json::write(first, 0));

    // A quote inside a value survives the round trip, which is the one escape a
    // writer is most likely to get wrong.
    const json::Value quoted = ok("{\"n\": \"a\\\"b\"}");
    CHECK(ok(json::write(quoted)).find("n")->asString() == "a\"b");
}

TEST_CASE("A whole number is written as one", "[model][json]")
{
    // Most numbers in an organ file are counts, note numbers and footages. A
    // writer that turns 36 into 36.0 makes a hand-edited file look mangled the
    // first time it is saved.
    CHECK(json::write(json::Value(36.0), 0) == "36");
    CHECK(json::write(json::Value(-12.0), 0) == "-12");
    CHECK(json::write(json::Value(0.0), 0) == "0");
    CHECK(json::write(json::Value(2.5), 0) == "2.5");

    // And it survives the trip.
    CHECK(ok(json::write(json::Value(812.0), 0)).asNumber() == Approx(812.0));
    CHECK(ok(json::write(json::Value(0.0625), 0)).asNumber() == Approx(0.0625));
}

TEST_CASE("Reading the wrong type gives the fallback, not a surprise",
          "[model][json]")
{
    // An organ file is written by hand, so a field WILL one day hold a string
    // where a number belongs. Every accessor answers with its fallback rather
    // than with whatever happens to be in the union, so a wrong type is a bad
    // value and never a crash.
    const json::Value s = ok(j("'text'"));
    CHECK(s.asNumber(7.0) == Approx(7.0));
    CHECK(s.asBool(true));
    CHECK(s.items().empty());
    CHECK(s.members().empty());
    CHECK(s.find("anything") == nullptr);

    const json::Value n = ok("42");
    CHECK(n.asString().empty());
}
