/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/ui/ConsoleLayoutModel.h"

#include "caecilia/model/Division.h"
#include "caecilia/model/Organ.h"
#include "caecilia/model/Stop.h"

#include <cctype>

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

    // The layout is a coarse but coherent console frame; the polished, builder-
    // faithful placement (curved jambs, terraced manuals) lands with the skins.
    // TODO(v0.9): replace with a proper measured jamb/terrace layout engine.

    const float margin      = 24.0f;
    const float jambWidth   = canvas.width * 0.22f;
    const float keyboardTop = canvas.y + canvas.height * 0.52f;
    const float rowHeight   = 120.0f;

    // --- Drawstops: two jambs (left/right) filled from the divisions ---------
    const auto& divisions = organ.divisions();

    float leftY  = canvas.y + margin;
    float rightY = canvas.y + margin;
    const float stopSize = 56.0f;
    const float stopPad  = 12.0f;
    const int   perRow   = 3;

    std::uint16_t divIndex = 0;
    for (const auto& division : divisions)
    {
        const bool  leftJamb = (divIndex % 2) == 0;
        const float jambX    = leftJamb ? (canvas.x + margin)
                                        : (canvas.right() - margin - jambWidth);
        float&      cursorY  = leftJamb ? leftY : rightY;

        // Division header label.
        ConsoleElement header{};
        header.role   = ElementRole::Label;
        header.bounds = { jambX, cursorY, jambWidth, 22.0f };
        header.semantic.role       = ElementRole::Label;
        header.semantic.division   = division.id();
        header.semantic.label      = division.name();
        header.semantic.oscAddress = "/caecilia/" + slugify(division.name());
        elements_.push_back(header);
        cursorY += 30.0f;

        int col = 0;
        for (const core::StopId sid : division.stops())
        {
            const model::Stop* stop = organ.stop(sid);
            if (stop == nullptr)
                continue;

            const float sx = jambX + static_cast<float>(col) * (stopSize + stopPad);

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

            if (++col >= perRow)
            {
                col = 0;
                cursorY += stopSize + stopPad;
            }
        }
        if (col != 0)
            cursorY += stopSize + stopPad;

        ++divIndex;
    }

    // --- Keyboards: stack the manual divisions from the bottom up ------------
    float manualY = keyboardTop;
    const float keyboardLeft  = canvas.x + jambWidth + 2.0f * margin;
    const float keyboardWidth = canvas.width - 2.0f * jambWidth - 4.0f * margin;

    std::uint16_t manualIdx = 0;
    for (const auto& division : divisions)
    {
        const bool isPedal = division.kind() == model::DivisionKind::Pedal;

        ConsoleElement kb{};
        kb.role     = isPedal ? ElementRole::Pedalboard : ElementRole::Manual;
        kb.bounds   = { keyboardLeft, manualY, keyboardWidth, rowHeight - 12.0f };
        kb.lowNote  = division.lowNote();
        kb.highNote = division.highNote();
        kb.index    = manualIdx;

        kb.semantic.role       = kb.role;
        kb.semantic.division   = division.id();
        kb.semantic.label      = division.name() + " keyboard";
        kb.semantic.oscAddress = "/caecilia/" + slugify(division.name()) + "/keys";
        elements_.push_back(kb);

        manualY += rowHeight;
        ++manualIdx;
    }

    // --- Coupler rail: one tab per coupler, along the console mid-band --------
    const auto& couplers = organ.couplers();
    float couplerX = keyboardLeft;
    const float couplerY = keyboardTop - 44.0f;
    const float couplerW = 96.0f;
    for (const auto& coupler : couplers)
    {
        ConsoleElement e{};
        e.role      = ElementRole::Coupler;
        e.bounds    = { couplerX, couplerY, couplerW, 30.0f };
        e.learnable = true;
        e.semantic.role       = ElementRole::Coupler;
        e.semantic.coupler    = coupler.id();
        e.semantic.label      = coupler.name();
        e.semantic.oscAddress = "/caecilia/coupler/" + slugify(coupler.name());
        elements_.push_back(e);
        couplerX += couplerW + 10.0f;
    }

    // --- Combination bar: a fixed row of pistons plus sequencer steppers -----
    const float pistonY = canvas.bottom() - margin - 34.0f;
    const float pistonW = 40.0f;
    float pistonX = keyboardLeft;
    for (std::uint16_t p = 1; p <= 8; ++p)
    {
        ConsoleElement e{};
        e.role      = ElementRole::Piston;
        e.bounds    = { pistonX, pistonY, pistonW, 34.0f };
        e.index     = p;
        e.learnable = true;
        e.semantic.role       = ElementRole::Piston;
        e.semantic.label      = "General " + std::to_string(p);
        e.semantic.oscAddress = "/caecilia/general/" + std::to_string(p);
        elements_.push_back(e);
        pistonX += pistonW + 8.0f;
    }

    // --- Expression pedal + meters along the lower-right frame ----------------
    ConsoleElement expr{};
    expr.role   = ElementRole::ExpressionPedal;
    expr.bounds = { canvas.right() - margin - 70.0f, keyboardTop, 70.0f, canvas.bottom() - keyboardTop - margin };
    expr.semantic.role       = ElementRole::ExpressionPedal;
    expr.semantic.label      = "Expression";
    expr.semantic.oscAddress = "/caecilia/expression/1";
    expr.learnable = true;
    elements_.push_back(expr);

    ConsoleElement wind{};
    wind.role   = ElementRole::WindGauge;
    wind.bounds = { canvas.x + margin, canvas.bottom() - margin - 60.0f, 140.0f, 60.0f };
    wind.semantic.role  = ElementRole::WindGauge;
    wind.semantic.label = "Wind pressure";
    elements_.push_back(wind);

    ConsoleElement vu{};
    vu.role   = ElementRole::VuMeter;
    vu.bounds = { canvas.x + margin + 156.0f, canvas.bottom() - margin - 60.0f, 120.0f, 60.0f };
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
