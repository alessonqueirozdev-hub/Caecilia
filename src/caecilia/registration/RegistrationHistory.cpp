// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/registration/RegistrationHistory.h"

#include <utility>

namespace caecilia::registration
{

RegistrationHistory::RegistrationHistory()
{
    reset(RegistrationState{});
}

void RegistrationHistory::reset(RegistrationState initial)
{
    nodes_.clear();
    Node root;
    root.command = RegistrationCommand::clear(); // conventional "empty" root command
    root.state   = std::move(initial);
    root.parent  = kInvalid;
    nodes_.push_back(std::move(root));
    current_ = 0;
}

std::size_t RegistrationHistory::record(RegistrationCommand command,
                                        RegistrationState resultingState)
{
    Node child;
    child.command = std::move(command);
    child.state   = std::move(resultingState);
    child.parent  = current_;

    const std::size_t newIndex = nodes_.size();
    nodes_.push_back(std::move(child));

    // Link from parent AFTER the push (push may reallocate; use the index).
    nodes_[current_].children.push_back(newIndex);
    nodes_[current_].lastChild = newIndex;
    current_ = newIndex;
    return newIndex;
}

const RegistrationState& RegistrationHistory::undo()
{
    const std::size_t parent = nodes_[current_].parent;
    if (parent != kInvalid)
        current_ = parent;
    return nodes_[current_].state;
}

const RegistrationState& RegistrationHistory::redo()
{
    const std::size_t child = nodes_[current_].lastChild;
    if (child != kInvalid)
        current_ = child;
    return nodes_[current_].state;
}

} // namespace caecilia::registration
