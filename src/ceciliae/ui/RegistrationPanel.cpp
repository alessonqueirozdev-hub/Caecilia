/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/ui/RegistrationPanel.h"

#include "ceciliae/model/Organ.h"
#include "ceciliae/model/Stop.h"
#include "ceciliae/registration/RegistrationState.h"
#include "ceciliae/ui/JuceInterop.h"

#include <iterator>
#include <string>
#include <utility>

namespace ceciliae::ui
{
namespace
{
/// Selector-grammar token for a tonal family (matches SelectorParser keys).
const char* familyToken(core::TonalFamily f) noexcept
{
    switch (f)
    {
        case core::TonalFamily::Principal: return "principal";
        case core::TonalFamily::Flute:     return "flute";
        case core::TonalFamily::String:    return "string";
        case core::TonalFamily::Reed:      return "reed";
        case core::TonalFamily::Mixture:   return "mixture";
        case core::TonalFamily::Hybrid:    return "hybrid";
        case core::TonalFamily::Percussion:return "percussion";
        default:                           return "undefined";
    }
}

/// Footage as a grammar pitch value in feet (integer or exact fraction).
std::string pitchToken(core::Footage f)
{
    if (f.den == 1)
        return std::to_string(f.num);
    return std::to_string(f.num) + "/" + std::to_string(f.den);
}
} // namespace

RegistrationPanel::RegistrationPanel()
{
    families_ = {
        { core::TonalFamily::Principal, "Principal" },
        { core::TonalFamily::Flute,     "Flute" },
        { core::TonalFamily::String,    "String" },
        { core::TonalFamily::Reed,      "Reed" },
        { core::TonalFamily::Mixture,   "Mixture" },
    };
    footages_ = {
        { core::footage::kSixteen },
        { core::footage::kEight },
        { core::footage::kFour },
        { core::footage::kTwoAndTwoThird },
        { core::footage::kTwo },
        { core::footage::kOneAndThreeFifth },
        { core::footage::kOne },
    };
}

registration::Selector RegistrationPanel::selectorForCell(int row, int col) const
{
    if (row < 0 || row >= static_cast<int>(families_.size())
        || col < 0 || col >= static_cast<int>(footages_.size()))
        return registration::Selector{};

    const std::string expr = std::string("family:") + familyToken(families_[static_cast<std::size_t>(row)].family)
                           + " & pitch:" + pitchToken(footages_[static_cast<std::size_t>(col)].footage);

    const auto result = parser_.parse(expr);
    // The grammar is authored to accept exactly this shape; on the off chance of
    // a parse error we fall back to the universal atom rather than throwing.
    return result.ok ? result.selector : registration::Selector{};
}

void RegistrationPanel::previewCell(int row, int col)
{
    if (organ_ == nullptr || !onSelectionPreview)
        return;
    static const registration::RegistrationState kEmpty{};
    const auto& state = state_ != nullptr ? *state_ : kEmpty;
    const auto selector = selectorForCell(row, col);
    onSelectionPreview(selector.resolve(*organ_, state));
}

void RegistrationPanel::resized()
{
    auto r = getLocalBounds();
    opBar_     = r.removeFromTop(32);
    actionBar_ = r.removeFromBottom(36);
    grid_      = r.reduced(4);
}

void RegistrationPanel::cellAt(juce::Point<int> p, int& row, int& col) const noexcept
{
    row = col = -1;
    if (!grid_.contains(p) || families_.empty() || footages_.empty())
        return;
    const int cw = grid_.getWidth() / static_cast<int>(footages_.size());
    const int ch = grid_.getHeight() / static_cast<int>(families_.size());
    if (cw <= 0 || ch <= 0)
        return;
    col = (p.x - grid_.getX()) / cw;
    row = (p.y - grid_.getY()) / ch;
    if (row >= static_cast<int>(families_.size())) row = -1;
    if (col >= static_cast<int>(footages_.size())) col = -1;
}

void RegistrationPanel::paint(juce::Graphics& g)
{
    g.fillAll(toColour(tokens_.surface));

    // --- Op bar: the active set operation ----------------------------------
    static const std::pair<PaletteOp, const char*> ops[] = {
        { PaletteOp::Set, "Set" }, { PaletteOp::Union, "Add" },
        { PaletteOp::Difference, "Remove" }, { PaletteOp::Intersection, "Keep" },
        { PaletteOp::Solo, "Solo" },
    };
    const int opW = opBar_.getWidth() / static_cast<int>(std::size(ops));
    for (int i = 0; i < static_cast<int>(std::size(ops)); ++i)
    {
        juce::Rectangle<int> cell(opBar_.getX() + i * opW, opBar_.getY(), opW, opBar_.getHeight());
        const bool active = ops[static_cast<std::size_t>(i)].first == op_;
        g.setColour(active ? toColour(tokens_.accentActive) : toColour(tokens_.surfaceRaised));
        g.fillRoundedRectangle(cell.toFloat().reduced(2.0f), tokens_.cornerRadius);
        g.setColour(toColour(tokens_.textPrimary));
        g.setFont(juce::Font(juce::FontOptions{ 12.0f }));
        g.drawText(ops[static_cast<std::size_t>(i)].second, cell, juce::Justification::centred, false);
    }

    // --- Grid: families (rows) x footages (cols) ---------------------------
    if (!families_.empty() && !footages_.empty())
    {
        const int cw = grid_.getWidth() / static_cast<int>(footages_.size());
        const int ch = grid_.getHeight() / static_cast<int>(families_.size());
        for (int rrow = 0; rrow < static_cast<int>(families_.size()); ++rrow)
        {
            for (int ccol = 0; ccol < static_cast<int>(footages_.size()); ++ccol)
            {
                juce::Rectangle<int> cell(grid_.getX() + ccol * cw, grid_.getY() + rrow * ch, cw, ch);
                const bool hover = (rrow == hoverRow_ && ccol == hoverCol_);
                auto tint = toColour(tokens_.tintFor(families_[static_cast<std::size_t>(rrow)].family));
                g.setColour(hover ? tint.brighter(0.2f) : tint.withAlpha(0.55f));
                g.fillRoundedRectangle(cell.toFloat().reduced(2.0f), tokens_.cornerRadius);
                g.setColour(toColour(tokens_.textPrimary));
                g.setFont(juce::Font(juce::FontOptions{ 11.0f }));
                g.drawText(model::footageLabel(footages_[static_cast<std::size_t>(ccol)].footage),
                           cell, juce::Justification::centred, false);
            }
            // Row label down the left margin.
            g.setColour(toColour(tokens_.textSecondary));
            g.setFont(juce::Font(juce::FontOptions{ 10.0f }));
            g.drawText(families_[static_cast<std::size_t>(rrow)].label,
                       juce::Rectangle<int>(grid_.getX(), grid_.getY() + rrow * ch, 4, ch),
                       juce::Justification::topLeft, false);
        }
    }

    // --- Action bar: build-plenum / clear / undo ---------------------------
    static const char* actions[] = { "Build Plenum", "Clear", "Undo" };
    const int aW = actionBar_.getWidth() / 3;
    for (int i = 0; i < 3; ++i)
    {
        juce::Rectangle<int> cell(actionBar_.getX() + i * aW, actionBar_.getY(), aW, actionBar_.getHeight());
        g.setColour(toColour(tokens_.surfaceRaised));
        g.fillRoundedRectangle(cell.toFloat().reduced(2.0f), tokens_.cornerRadius);
        g.setColour(toColour(tokens_.textPrimary));
        g.setFont(juce::Font(juce::FontOptions{ 12.0f }));
        g.drawText(actions[i], cell, juce::Justification::centred, false);
    }
}

void RegistrationPanel::mouseMove(const juce::MouseEvent& e)
{
    int row = -1, col = -1;
    cellAt(e.getPosition(), row, col);
    if (row != hoverRow_ || col != hoverCol_)
    {
        hoverRow_ = row;
        hoverCol_ = col;
        if (row >= 0 && col >= 0)
            previewCell(row, col);
        repaint();
    }
}

void RegistrationPanel::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getPosition();

    // Op bar.
    if (opBar_.contains(p))
    {
        static const PaletteOp order[] = {
            PaletteOp::Set, PaletteOp::Union, PaletteOp::Difference,
            PaletteOp::Intersection, PaletteOp::Solo
        };
        const int opW = opBar_.getWidth() / static_cast<int>(std::size(order));
        if (opW > 0)
        {
            const int idx = (p.x - opBar_.getX()) / opW;
            if (idx >= 0 && idx < static_cast<int>(std::size(order)))
            {
                op_ = order[idx];
                repaint();
            }
        }
        return;
    }

    // Action bar.
    if (actionBar_.contains(p))
    {
        const int aW = actionBar_.getWidth() / 3;
        if (aW > 0)
        {
            const int idx = (p.x - actionBar_.getX()) / aW;
            if (idx == 0 && onBuildPlenum) onBuildPlenum();
            else if (idx == 1 && onClear)  onClear();
            else if (idx == 2 && onUndo)   onUndo();
        }
        return;
    }

    // Grid: apply the current op with the cell's selector.
    int row = -1, col = -1;
    cellAt(p, row, col);
    if (row >= 0 && col >= 0 && onApplyIntent)
        onApplyIntent(op_, selectorForCell(row, col));
}

} // namespace ceciliae::ui
