/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/control/LoopbackTransport.h"

namespace caecilia::control
{

void LoopbackTransport::onStateEvent(const StateEvent& event)
{
    events_.push_back(event);
}

ControlResult LoopbackTransport::send(const ControlCommand& command)
{
    if (endpoint_ == nullptr)
        return ControlResult::failure(ControlStatus::NotReady,
                                      "loopback transport not connected", command.requestId);

    return endpoint_->handle(command);
}

} // namespace caecilia::control
