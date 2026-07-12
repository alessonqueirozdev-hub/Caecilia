/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/ui/ConsoleLayoutModel.h"

#include "caecilia/model/Division.h"
#include "caecilia/model/Organ.h"
#include "caecilia/model/Stop.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace caecilia::ui
{

std::string slugify(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    bool pendingHyphen = false;
    for (const char c : text)
    {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc))
        {
            if (pendingHyphen && !out.empty())
                out.push_back('-');
            pendingHyphen = false;
            out.push_back(static_cast<char>(std::tolower(uc)));
        }
        else
        {
            pendingHyphen = true;
        }
    }
    return out;
}

void ConsoleLayoutModel::clear() noexcept
{
    elements_.clear();
    organ_ = nullptr;
    canvas_ = {};
}

void ConsoleLayoutModel::layoutFrom(const model::Organ& organ, LayoutRect canvas)
{
    elements_.clear();
    organ_  = &organ;
    canvas_ = canvas;

    // A measured stop-jamb / terraced-manual console that fills the whole canvas:
    // full-height drawstop jambs frame both sides, the manuals are terraced large
    // in the centre with a radiating pedalboard beneath, and the combination bar,
    // expression shoe and meters sit on the console frame.
    // TODO(v0.9): curved jambs and per-builder faithful placement.

    const float margin    = canvas.width * 0.018f;
    const float jambWidth = canvas.width * 0.255f;
    const float leftJambX  = canvas.x + margin;
    const float rightJambX = canvas.right() - margin - jambWidth;

    const auto& divisions = organ.divisions();

    // Split divisions between the two jambs, keeping the pedal with the smaller
    // side so the busiest manual gets a jamb to itself.
    std::vector<const model::Division*> leftDivs, rightDivs;
    {
        std::uint16_t i = 0;
        for (const auto& d : divisions)
        {
            (((i++) % 2) == 0 ? leftDivs : rightDivs).push_back(&d);
        }
    }

    // --- Drawstop jambs: each side lays its divisions top-to-bottom, four across.
    const int   perRow   = 4;
    const float stopPad  = jambWidth * 0.045f;
    const float stopSize = (jambWidth - static_cast<float>(perRow - 1) * stopPad
                            - 2.0f * (jambWidth * 0.04f)) / static_cast<float>(perRow);
    const float jambInset = jambWidth * 0.04f;

    const auto layoutJamb = [&](const std::vector<const model::Division*>& divs, float jambX)
    {
        float cursorY = canvas.y + margin * 1.4f;
        for (const model::Division* dptr : divs)
        {
            const model::Division& division = *dptr;

            ConsoleElement header{};
            header.role   = ElementRole::Label;
            header.bounds = { jambX + jambInset, cursorY, jambWidth - 2.0f * jambInset, 26.0f };
            header.semantic.role     = ElementRole::Label;
            header.semantic.division = division.id();
            header.semantic.label    = division.name();
            elements_.push_back(header);
            cursorY += 34.0f;

            int col = 0;
            const float rowStride = stopSize + stopPad;
            for (const core::StopId sid : division.stops())
            {
                const model::Stop* stop = organ.stop(sid);
                if (stop == nullptr)
                    continue;

                const float sx = jambX + jambInset + static_cast<float>(col) * rowStride;

                ConsoleElement e{};
                e.role      = ElementRole::Drawstop;
                e.bounds    = { sx, cursorY, stopSize, stopSize };
                e.footage   = stop->footage();
                e.family    = stop->family();
                e.learnable = true;
                e.semantic.role       = ElementRole::Drawstop;
                e.semantic.stop       = sid;
                e.semantic.division   = division.id();
                e.semantic.family     = stop->family();
                e.semantic.label      = division.name() + " " + stop->displayName();
                e.semantic.oscAddress = "/caecilia/" + slugify(division.name())
                                        + "/" + slugify(stop->name());
                elements_.push_back(e);

                if (++col >= perRow) { col = 0; cursorY += rowStride; }
            }
            if (col != 0)
                cursorY += rowStride;
            cursorY += stopSize * 0.5f; // gap between divisions on the same jamb
        }
    };
    layoutJamb(leftDivs, leftJambX);
    layoutJamb(rightDivs, rightJambX);

    // --- Centre keydesk: terraced manuals over a radiating pedalboard ---------
    const float keyboardLeft  = leftJambX + jambWidth + margin * 1.5f;
    const float keyboardRight = rightJambX - margin * 1.5f;
    const float keyboardWidth = keyboardRight - keyboardLeft;

    int numManuals = 0;
    for (const auto& d : divisions)
        if (d.kind() != model::DivisionKind::Pedal)
            ++numManuals;
    numManuals = numManuals > 0 ? numManuals : 1;

    // Vertical bands: coupler rail, manuals, pedalboard, combination bar.
    const float railY        = canvas.y + canvas.height * 0.30f;
    const float manualTop    = canvas.y + canvas.height * 0.36f;
    const float manualBottom = canvas.y + canvas.height * 0.70f;
    const float manualGap    = 14.0f;
    const float manualH = (manualBottom - manualTop
                           - static_cast<float>(numManuals - 1) * manualGap)
                          / static_cast<float>(numManuals);

    // Coupler rail sits just above the top manual.
    const auto& couplers = organ.couplers();
    if (! couplers.empty())
    {
        const float couplerW = std::min(150.0f, keyboardWidth / static_cast<float>(couplers.size()) - 8.0f);
        float couplerX = keyboardLeft;
        for (const auto& coupler : couplers)
        {
            ConsoleElement e{};
            e.role      = ElementRole::Coupler;
            e.bounds    = { couplerX, railY, couplerW, 30.0f };
            e.learnable = true;
            e.semantic.role    = ElementRole::Coupler;
            e.semantic.coupler = coupler.id();
            e.semantic.label   = coupler.name();
            e.semantic.oscAddress = "/caecilia/coupler/" + slugify(coupler.name());
            elements_.push_back(e);
            couplerX += couplerW + 8.0f;
        }
    }

    // Manuals terraced from the top down; pedalboard spans the bottom.
    float manualY = manualTop;
    std::uint16_t manualIdx = 0;
    const model::Division* pedalDiv = nullptr;
    for (const auto& division : divisions)
    {
        if (division.kind() == model::DivisionKind::Pedal)
        {
            pedalDiv = &division;
            continue;
        }

        ConsoleElement kb{};
        kb.role     = ElementRole::Manual;
        kb.bounds   = { keyboardLeft, manualY, keyboardWidth, manualH };
        kb.lowNote  = division.lowNote();
        kb.highNote = division.highNote();
        kb.index    = manualIdx;
        kb.semantic.role     = ElementRole::Manual;
        kb.semantic.division = division.id();
        kb.semantic.label    = division.name() + " keyboard";
        kb.semantic.oscAddress = "/caecilia/" + slugify(division.name()) + "/keys";
        elements_.push_back(kb);

        manualY += manualH + manualGap;
        ++manualIdx;
    }

    if (pedalDiv != nullptr)
    {
        // A radiating pedalboard, centred and wider-keyed, distinct from a manual.
        const float pedalH = canvas.height * 0.16f;
        const float pedalW = keyboardWidth * 0.86f;
        const float pedalX = keyboardLeft + (keyboardWidth - pedalW) * 0.5f;
        const float pedalY = canvas.y + canvas.height * 0.735f;

        ConsoleElement pb{};
        pb.role     = ElementRole::Pedalboard;
        pb.bounds   = { pedalX, pedalY, pedalW, pedalH };
        pb.lowNote  = pedalDiv->lowNote();
        pb.highNote = pedalDiv->highNote();
        pb.index    = manualIdx;
        pb.semantic.role     = ElementRole::Pedalboard;
        pb.semantic.division = pedalDiv->id();
        pb.semantic.label    = pedalDiv->name() + " pedalboard";
        pb.semantic.oscAddress = "/caecilia/" + slugify(pedalDiv->name()) + "/keys";
        elements_.push_back(pb);
    }

    // --- Combination bar: general pistons along the base of the keydesk -------
    const float pistonY = canvas.bottom() - margin - 40.0f;
    const float pistonW = 44.0f;
    float pistonX = keyboardLeft;
    for (std::uint16_t p = 1; p <= 8; ++p)
    {
        ConsoleElement e{};
        e.role      = ElementRole::Piston;
        e.bounds    = { pistonX, pistonY, pistonW, 38.0f };
        e.index     = p;
        e.learnable = true;
        e.semantic.role  = ElementRole::Piston;
        e.semantic.label = "General " + std::to_string(p);
        e.semantic.oscAddress = "/caecilia/general/" + std::to_string(p);
        elements_.push_back(e);
        pistonX += pistonW + 8.0f;
    }

    // --- Expression shoe (right of the pistons) and meters (frame) ------------
    ConsoleElement expr{};
    expr.role   = ElementRole::ExpressionPedal;
    expr.bounds = { keyboardRight - 76.0f, pistonY - 6.0f, 76.0f, 50.0f };
    expr.semantic.role  = ElementRole::ExpressionPedal;
    expr.semantic.label = "Expression";
    expr.semantic.oscAddress = "/caecilia/expression/1";
    expr.learnable = true;
    elements_.push_back(expr);

    ConsoleElement wind{};
    wind.role   = ElementRole::WindGauge;
    wind.bounds = { leftJambX + jambInset, canvas.bottom() - margin - 88.0f, 96.0f, 88.0f };
    wind.semantic.role  = ElementRole::WindGauge;
    wind.semantic.label = "Wind pressure";
    elements_.push_back(wind);

    ConsoleElement vu{};
    vu.role   = ElementRole::VuMeter;
    vu.bounds = { rightJambX + jambWidth - jambInset - 150.0f,
                  canvas.bottom() - margin - 34.0f, 150.0f, 26.0f };
    vu.semantic.role  = ElementRole::VuMeter;
    vu.semantic.label = "Output level";
    elements_.push_back(vu);
}

const ConsoleElement* ConsoleLayoutModel::hitTest(LayoutPoint p) const noexcept
{
    // Back-to-front: the last element painted is visually on top.
    for (auto it = elements_.rbegin(); it != elements_.rend(); ++it)
    {
        if (it->role == ElementRole::Jamb || it->role == ElementRole::Label)
            continue; // static chrome is not interactive
        if (it->bounds.contains(p))
            return &(*it);
    }
    return nullptr;
}

} // namespace caecilia::ui
