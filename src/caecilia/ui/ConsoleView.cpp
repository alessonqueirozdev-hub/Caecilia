/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/ui/ConsoleView.h"

#include "caecilia/ui/JuceInterop.h"

namespace caecilia::ui
{

ConsoleView::ConsoleView()
{
    setOpaque(true);
    setWantsKeyboardFocus(true);

    // Repaint in lock-step with the display refresh; the mirror is polled there.
    vblank_ = std::make_unique<juce::VBlankAttachment>(this, [this] { onVBlank(); });
}

ConsoleView::~ConsoleView() = default;

ISkin& ConsoleView::skin() noexcept
{
    return activeMode_ == SkinId::Photoreal ? static_cast<ISkin&>(photoSkin_)
                                            : static_cast<ISkin&>(flatSkin_);
}

void ConsoleView::setLayoutModel(const ConsoleLayoutModel* model)
{
    model_ = model;
    if (model_ != nullptr)
    {
        const auto c = model_->canvasBounds();
        viewport_.setCanvas({ c.width, c.height });
        resetView();
    }
    repaint();
}

void ConsoleView::applyTheme(const Theme& theme)
{
    flatSkin_.setTheme(theme);
    photoSkin_.setTheme(theme);
    // A theme may prefer a skin; honour it but let an explicit setConsoleMode win.
    repaint();
}

void ConsoleView::setConsoleMode(SkinId mode)
{
    if (mode == activeMode_)
        return;
    activeMode_ = mode;
    repaint();
}

void ConsoleView::resetView()
{
    viewport_.setViewSize({ static_cast<float>(getWidth()), static_cast<float>(getHeight()) });
    viewport_.fitToView();
    repaint();
}

juce::AffineTransform ConsoleView::sceneTransform() const noexcept
{
    const float s = viewport_.scale();
    const auto  t = viewport_.translation();
    return juce::AffineTransform::scale(s).translated(t.x, t.y);
}

void ConsoleView::onVBlank()
{
    if (mirror_ != nullptr)
        lastFrame_ = mirror_->read();

    // TODO(v0.9): compare against the previous frame and invalidate only the
    // changed element rectangles instead of the whole component.
    repaint();
}

void ConsoleView::resized()
{
    viewport_.setViewSize({ static_cast<float>(getWidth()), static_cast<float>(getHeight()) });
    viewport_.fitToView();
}

void ConsoleView::paint(juce::Graphics& g)
{
    ISkin& sk = skin();

    // Background is painted in screen space (it fills the whole component).
    if (model_ != nullptr)
        sk.paintBackground(g, { 0.0f, 0.0f, static_cast<float>(getWidth()), static_cast<float>(getHeight()) });
    else
        g.fillAll(juce::Colours::black);

    if (model_ == nullptr)
        return;

    juce::Graphics::ScopedSaveState save(g);
    g.addTransform(sceneTransform());

    for (const auto& e : model_->elements())
    {
        switch (e.role)
        {
            case ElementRole::Jamb:
                sk.paintJamb(g, e);
                break;
            case ElementRole::Label:
                sk.paintLabel(g, e);
                break;
            case ElementRole::Drawstop:
            {
                DrawstopVisual v{};
                v.engaged = stopEngagedQuery ? stopEngagedQuery(e.semantic.stop) : false;
                v.hovered = (hovered_ == &e);
                v.pullNorm = v.engaged ? 1.0f : 0.0f; // TODO(v0.2): ease with an animator.
                sk.paintDrawstop(g, e, v);
                break;
            }
            case ElementRole::Manual:
            case ElementRole::Pedalboard:
                sk.paintKeyboard(g, e, lastFrame_.keys, e.semantic.division.value);
                break;
            case ElementRole::Coupler:
            {
                CouplerVisual v{};
                v.engaged = couplerEngagedQuery ? couplerEngagedQuery(e.semantic.coupler) : false;
                v.hovered = (hovered_ == &e);
                sk.paintCoupler(g, e, v);
                break;
            }
            case ElementRole::VuMeter:
                sk.paintMeter(g, e, lastFrame_.meters.master);
                break;
            case ElementRole::WindGauge:
                sk.paintWindGauge(g, e, lastFrame_.meters.windPressurePa, lastFrame_.meters.windSagNorm);
                break;
            case ElementRole::Piston:
            case ElementRole::SequencerButton:
            case ElementRole::ExpressionPedal:
                // TODO(v0.7): dedicated skin routines; drawn as jambs for now.
                sk.paintJamb(g, e);
                break;
            case ElementRole::Unknown:
            default:
                break;
        }
    }
}

void ConsoleView::handleElementClick(const ConsoleElement& element, bool isDown)
{
    switch (element.role)
    {
        case ElementRole::Drawstop:
            if (isDown && onDrawstopClicked)
                onDrawstopClicked(element.semantic.stop);
            break;
        case ElementRole::Coupler:
            if (isDown && onCouplerClicked)
                onCouplerClicked(element.semantic.coupler);
            break;
        case ElementRole::Piston:
            if (isDown && onPistonClicked)
                onPistonClicked(element.index);
            break;
        case ElementRole::SequencerButton:
            if (isDown && onSequencerStep)
                onSequencerStep(element.index == 0 ? -1 : +1);
            break;
        default:
            break;
    }
}

void ConsoleView::mouseDown(const juce::MouseEvent& e)
{
    if (model_ == nullptr)
        return;

    const LayoutPoint logical = viewport_.screenToLogical({ static_cast<float>(e.x), static_cast<float>(e.y) });
    const ConsoleElement* hit = model_->hitTest(logical);

    if (hit == nullptr)
    {
        // Empty canvas: begin a pan.
        panning_ = true;
        lastDragScreen_ = { static_cast<float>(e.x), static_cast<float>(e.y) };
        return;
    }

    if (hit->role == ElementRole::Manual || hit->role == ElementRole::Pedalboard)
    {
        // Map the click x across the compass to a MIDI note (proportional; the
        // true white/black hit-map lands with the skin geometry).
        const float frac = juce::jlimit(0.0f, 1.0f, (logical.x - hit->bounds.x) / juce::jmax(1.0f, hit->bounds.width));
        const int span = static_cast<int>(hit->highNote) - static_cast<int>(hit->lowNote);
        const int note = hit->lowNote + juce::roundToInt(frac * static_cast<float>(span)); // TODO(v0.2)
        activeKeyDivision_ = hit->semantic.division;
        activeKeyNote_     = static_cast<core::MidiNote>(juce::jlimit(0, 127, note));
        keyDown_ = true;
        if (onKeyEvent)
            onKeyEvent(activeKeyDivision_, activeKeyNote_, true);
        return;
    }

    handleElementClick(*hit, true);
}

void ConsoleView::mouseUp(const juce::MouseEvent&)
{
    panning_ = false;
    if (keyDown_)
    {
        keyDown_ = false;
        if (onKeyEvent)
            onKeyEvent(activeKeyDivision_, activeKeyNote_, false);
    }
}

void ConsoleView::mouseDrag(const juce::MouseEvent& e)
{
    if (!panning_)
        return;
    const float dx = static_cast<float>(e.x) - lastDragScreen_.x;
    const float dy = static_cast<float>(e.y) - lastDragScreen_.y;
    viewport_.panBy(dx, dy);
    lastDragScreen_ = { static_cast<float>(e.x), static_cast<float>(e.y) };
    repaint();
}

void ConsoleView::mouseMove(const juce::MouseEvent& e)
{
    if (model_ == nullptr)
        return;
    const LayoutPoint logical = viewport_.screenToLogical({ static_cast<float>(e.x), static_cast<float>(e.y) });
    const ConsoleElement* hit = model_->hitTest(logical);
    if (hit != hovered_)
    {
        hovered_ = hit;
        // TODO(v0.9): publish hit->semantic.label to a tooltip window and to the
        // ConsoleAccessibilityHandler as the focused element description.
        repaint();
    }
}

void ConsoleView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float factor = 1.0f + juce::jlimit(-0.5f, 0.5f, w.deltaY);
    viewport_.zoomAt({ static_cast<float>(e.x), static_cast<float>(e.y) }, factor);
    repaint();
}

} // namespace caecilia::ui
