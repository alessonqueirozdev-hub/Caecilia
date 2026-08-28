// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace caecilia::model::json
{

/**
 * @file Json.h
 * @brief A small, strict JSON reader and writer for organ documents.
 *
 * Written here rather than pulled in because @c caecilia_core links only the C++
 * standard library -- that constraint is what keeps the whole instrument
 * buildable with @c -DCAECILIA_CORE_ONLY and testable without a host -- and an
 * organ file is the one thing in the core that has to come off a disk.
 *
 * Deliberately small. It reads JSON and nothing more: no comments, no trailing
 * commas, no single quotes. An organ file is a document a person edits, and a
 * parser that quietly accepts what the standard forbids teaches them a dialect
 * that other tools will reject.
 *
 * Off-thread only. It allocates freely and reports errors as values rather than
 * exceptions, in the same style as @c LoadDiagnostics.
 */

/// Where in the document something went wrong. Lines and columns are 1-based,
/// because that is what an editor shows.
struct Position
{
    int line   = 1;
    int column = 1;
};

/// A parse failure: what went wrong and where.
struct Error
{
    std::string message;
    Position    where;

    /// @return "line 12, column 5: message", for a diagnostic.
    [[nodiscard]] std::string describe() const;
};

/**
 * @brief One JSON value.
 *
 * Objects keep their members in DOCUMENT ORDER rather than sorted, and are looked
 * up linearly. An organ file has tens of members per object, so a map would cost
 * more than it saves -- and document order is what makes a written file diff
 * cleanly against the one a person edited.
 */
class Value
{
public:
    enum class Kind
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Value() = default;
    explicit Value(bool b) : kind_(Kind::Bool), bool_(b) {}
    explicit Value(double d) : kind_(Kind::Number), number_(d) {}

    /**
     * @brief A number that came from a @c float, stored as the double a reader
     *        would get from the shortest text that round-trips the float.
     *
     * Without this a voicing parameter of 0.15f is written as
     * 0.15000000596046448 -- the shortest text that round-trips the DOUBLE the
     * float widened to. It is correct and it is unreadable, and an organ file is
     * a document a person edits. Storing 0.15 instead loses nothing: the field is
     * read back into a float, and 0.15 and 0.15000000596046448 are the same float.
     */
    explicit Value(float f);
    explicit Value(std::string s) : kind_(Kind::String), string_(std::move(s)) {}

    [[nodiscard]] static Value array() { Value v; v.kind_ = Kind::Array;  return v; }
    [[nodiscard]] static Value object() { Value v; v.kind_ = Kind::Object; return v; }

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool isNull() const noexcept { return kind_ == Kind::Null; }
    [[nodiscard]] bool isBool() const noexcept { return kind_ == Kind::Bool; }
    [[nodiscard]] bool isNumber() const noexcept { return kind_ == Kind::Number; }
    [[nodiscard]] bool isString() const noexcept { return kind_ == Kind::String; }
    [[nodiscard]] bool isArray() const noexcept { return kind_ == Kind::Array; }
    [[nodiscard]] bool isObject() const noexcept { return kind_ == Kind::Object; }

    [[nodiscard]] bool               asBool(bool fallback = false) const noexcept;
    [[nodiscard]] double             asNumber(double fallback = 0.0) const noexcept;
    [[nodiscard]] const std::string& asString() const noexcept;

    /// Elements of an array (empty for anything else).
    [[nodiscard]] const std::vector<Value>& items() const noexcept { return items_; }
    [[nodiscard]] std::vector<Value>&       items() noexcept { return items_; }

    /// Members of an object, in document order (empty for anything else).
    [[nodiscard]] const std::vector<std::pair<std::string, Value>>& members() const noexcept
    {
        return members_;
    }
    [[nodiscard]] std::vector<std::pair<std::string, Value>>& members() noexcept
    {
        return members_;
    }

    /**
     * @brief Look up an object member by name.
     * @return The member, or nullptr if absent or if this is not an object.
     */
    [[nodiscard]] const Value* find(std::string_view name) const noexcept;

    /// Where this value began in the source, for diagnostics that point at it.
    [[nodiscard]] Position position() const noexcept { return where_; }
    void setPosition(Position p) noexcept { where_ = p; }

private:
    friend class Parser;

    Kind        kind_ = Kind::Null;
    bool        bool_ = false;
    double      number_ = 0.0;
    std::string string_;
    std::vector<Value>                              items_;
    std::vector<std::pair<std::string, Value>>      members_;
    Position                                        where_{};
};

/**
 * @brief Parse a JSON document.
 * @param text  The document.
 * @param error Filled in when parsing fails.
 * @return The root value, or nullopt on failure.
 *
 * Rejects trailing content after the root value: a file with two documents in it
 * is a mistake, not a stream.
 */
[[nodiscard]] std::optional<Value> parse(std::string_view text, Error& error);

/**
 * @brief Write a value back out.
 * @param indent Spaces per level; 0 writes the whole document on one line.
 *
 * Members come out in the order they are held, which for a parsed document is
 * the order they were written in. That is what makes a read-modify-write of an
 * organ file diff as the change the user made rather than as a reordering.
 */
[[nodiscard]] std::string write(const Value& value, int indent = 2);

} // namespace caecilia::model::json
