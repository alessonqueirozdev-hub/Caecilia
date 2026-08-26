// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace caecilia::control
{

/**
 * @brief The outcome status of a @c ControlCommand.
 *
 * These map cleanly onto both an OSC `/reply` status code and a JSON-RPC 2.0
 * error object (see @c JsonRpcControlTransport), so one status vocabulary
 * serves every transport.
 */
enum class ControlStatus : std::uint16_t
{
    Ok            = 0,   ///< Applied (or answered) successfully.
    Accepted      = 1,   ///< Valid and queued; effect completes asynchronously.

    BadRequest    = 100, ///< Malformed command (missing/invalid arguments).
    ParseError    = 101, ///< The selector text failed to parse.
    UnknownOpcode = 102, ///< The verb is not part of this protocol version.
    NotFound      = 103, ///< A referenced stop / division / combination does not exist.
    Unsupported   = 104, ///< A valid verb this build cannot yet service.

    NotReady      = 200, ///< No organ loaded / sink not bound yet.
    Truncated     = 201, ///< Succeeded but the resulting delta overflowed capacity.
    InternalError = 202  ///< Unexpected failure inside the sink.
};

/// @return true if @p status denotes success (@c Ok or @c Accepted).
[[nodiscard]] constexpr bool isSuccess(ControlStatus status) noexcept
{
    return status == ControlStatus::Ok || status == ControlStatus::Accepted;
}

/// @return a stable, lowercase token for @p status (e.g. `"parseError"`).
[[nodiscard]] std::string_view statusName(ControlStatus status) noexcept;

/**
 * @brief The response a transport encodes back to the caller.
 *
 * Mutating commands report just a status (+ a human-readable @ref message on
 * failure). Query commands additionally fill @ref payload with a
 * transport-neutral structured body — the @c ControlServer produces it as a
 * compact, self-describing string (the JSON-RPC transport forwards it verbatim
 * as the `result`; the OSC transport flattens it into typed arguments).
 *
 * @ref requestId is copied from the originating @c ControlCommand so a client
 * that pipelines requests can correlate replies.
 */
struct ControlResult
{
    ControlStatus status    = ControlStatus::Ok; ///< Outcome.
    std::uint64_t requestId = 0;                  ///< Echoed client correlation id.
    std::string   message;                        ///< Human-readable note (usually only on error).
    std::string   payload;                        ///< Structured query answer (empty for mutations).

    /// @return true if @ref status is a success code.
    [[nodiscard]] bool ok() const noexcept { return isSuccess(status); }

    // --- Factories ----------------------------------------------------------

    [[nodiscard]] static ControlResult success(std::uint64_t reqId = 0)
    {
        return {ControlStatus::Ok, reqId, {}, {}};
    }

    [[nodiscard]] static ControlResult answered(std::string payload, std::uint64_t reqId = 0)
    {
        return {ControlStatus::Ok, reqId, {}, std::move(payload)};
    }

    [[nodiscard]] static ControlResult accepted(std::uint64_t reqId = 0)
    {
        return {ControlStatus::Accepted, reqId, {}, {}};
    }

    [[nodiscard]] static ControlResult failure(ControlStatus status, std::string message,
                                               std::uint64_t reqId = 0)
    {
        return {status, reqId, std::move(message), {}};
    }
};

} // namespace caecilia::control
