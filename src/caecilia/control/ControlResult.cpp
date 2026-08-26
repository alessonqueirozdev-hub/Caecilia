// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/control/ControlResult.h"

namespace caecilia::control
{

std::string_view statusName(ControlStatus status) noexcept
{
    switch (status)
    {
        case ControlStatus::Ok:            return "ok";
        case ControlStatus::Accepted:      return "accepted";
        case ControlStatus::BadRequest:    return "badRequest";
        case ControlStatus::ParseError:    return "parseError";
        case ControlStatus::UnknownOpcode: return "unknownOpcode";
        case ControlStatus::NotFound:      return "notFound";
        case ControlStatus::Unsupported:   return "unsupported";
        case ControlStatus::NotReady:      return "notReady";
        case ControlStatus::Truncated:     return "truncated";
        case ControlStatus::InternalError: return "internalError";
    }
    return "unknown";
}

} // namespace caecilia::control
