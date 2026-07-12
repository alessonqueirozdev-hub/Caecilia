/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/registration/RegistrationCommand.h"
#include "caecilia/registration/RegistrationState.h"

#include <cstddef>
#include <limits>
#include <vector>

namespace caecilia::registration
{

/**
 * @brief Command-sourced, BRANCHING undo/redo for the registration.
 *
 * State is the fold of the command log, but rather than replay commands the
 * history materialises each resulting @c RegistrationState so undo/redo is O(1).
 * Undo does not discard the redo path: recording a new command after an undo
 * creates a *sibling* branch, so the history is a tree (a @c HistoryTree), not a
 * stack. This preserves every explored registration — invaluable when auditioning
 * variations of a plenum.
 *
 * Off the audio thread only; the engine diffs the state this returns into a
 * @c StateDelta for the audio seam.
 */
class RegistrationHistory
{
public:
    static constexpr std::size_t kInvalid = std::numeric_limits<std::size_t>::max();

    /// One node of the history tree.
    struct Node
    {
        RegistrationCommand      command;         ///< Command that produced this node.
        RegistrationState        state;           ///< Materialised resulting state.
        std::size_t              parent = kInvalid;
        std::vector<std::size_t> children;        ///< Explored branches from here.
        std::size_t              lastChild = kInvalid; ///< Redo default (most-recent branch).
    };

    /// Construct with an empty root registration.
    RegistrationHistory();

    /// Reset the whole tree to a single root holding @p initial.
    void reset(RegistrationState initial);

    // --- Cursor -------------------------------------------------------------
    [[nodiscard]] std::size_t              currentIndex() const noexcept { return current_; }
    [[nodiscard]] const RegistrationState& currentState() const noexcept { return nodes_[current_].state; }

    [[nodiscard]] bool canUndo() const noexcept { return nodes_[current_].parent != kInvalid; }
    [[nodiscard]] bool canRedo() const noexcept { return !nodes_[current_].children.empty(); }

    /**
     * @brief Record @p command with its @p resultingState as a new child of the
     *        current node and move the cursor onto it.
     * @return the index of the new node.
     */
    std::size_t record(RegistrationCommand command, RegistrationState resultingState);

    /// Move to the parent; @return the state now current (unchanged if at root).
    const RegistrationState& undo();

    /// Move to the most-recent child; @return the state now current (unchanged if a leaf).
    const RegistrationState& redo();

    // --- Tree inspection ----------------------------------------------------
    [[nodiscard]] std::size_t   size() const noexcept { return nodes_.size(); }
    [[nodiscard]] const Node&   node(std::size_t index) const { return nodes_[index]; }
    /// @return number of redo branches available from the current node.
    [[nodiscard]] std::size_t   branchCount() const noexcept { return nodes_[current_].children.size(); }

private:
    std::vector<Node> nodes_;
    std::size_t       current_ = 0;
};

} // namespace caecilia::registration
