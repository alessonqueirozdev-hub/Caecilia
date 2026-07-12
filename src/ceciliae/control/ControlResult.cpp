/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/control/ControlResult.h"

namespace ceciliae::control
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

} // namespace ceciliae::control
