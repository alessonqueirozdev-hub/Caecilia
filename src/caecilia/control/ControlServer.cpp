// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/control/ControlServer.h"

#include <algorithm>

namespace caecilia::control
{

ControlServer::ControlServer(ICommandSink& sink) noexcept
    : sink_(&sink)
{
}

ControlServer::~ControlServer()
{
    // Best-effort teardown so a destroyed server never leaves transports serving
    // into a dangling endpoint.
    stop();
}

// --- Transport management --------------------------------------------------

void ControlServer::addTransport(IControlTransport& transport)
{
    if (std::find(transports_.begin(), transports_.end(), &transport) != transports_.end())
        return; // already registered

    transport.connect(*this);
    transports_.push_back(&transport);
    addObserver(&transport); // transports also receive outbound feedback
}

void ControlServer::removeTransport(IControlTransport& transport) noexcept
{
    removeObserver(&transport);
    transports_.erase(std::remove(transports_.begin(), transports_.end(), &transport),
                      transports_.end());
}

bool ControlServer::start()
{
    bool allStarted = true;
    for (auto* transport : transports_)
        allStarted = transport->start() && allStarted;

    running_ = true;
    return allStarted;
}

void ControlServer::stop() noexcept
{
    for (auto* transport : transports_)
        transport->stop();

    running_ = false;
}

// --- IControlEndpoint ------------------------------------------------------

ControlResult ControlServer::handle(const ControlCommand& command)
{
    if (command.isNoOp())
        return ControlResult::failure(ControlStatus::UnknownOpcode,
                                      "empty or unset opcode", command.requestId);

    if (sink_ == nullptr)
        return ControlResult::failure(ControlStatus::NotReady,
                                      "no command sink bound", command.requestId);

    // The server adds no registration logic: the sink owns validation and enact.
    return sink_->submit(command);
}

// --- IStatePublisher -------------------------------------------------------

void ControlServer::addObserver(IStateObserver* observer)
{
    if (observer == nullptr)
        return;
    if (std::find(observers_.begin(), observers_.end(), observer) != observers_.end())
        return;
    observers_.push_back(observer);
}

void ControlServer::removeObserver(IStateObserver* observer) noexcept
{
    observers_.erase(std::remove(observers_.begin(), observers_.end(), observer),
                     observers_.end());
}

void ControlServer::publish(const StateEvent& event)
{
    for (auto* observer : observers_)
        observer->onStateEvent(event);
}

// --- Introspection ---------------------------------------------------------

GrammarVersion ControlServer::grammarVersion() const noexcept
{
    return sink_ != nullptr ? sink_->grammarVersion() : kGrammarVersion;
}

} // namespace caecilia::control
