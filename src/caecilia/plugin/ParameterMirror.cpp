// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/plugin/ParameterMirror.h"

#include "caecilia/model/Organ.h"
#include "caecilia/model/Stop.h"

#include <algorithm>

namespace caecilia::plugin
{

const juce::Identifier CaeciliaParameterMirror::kStateRootId{"CAECILIA_STATE"};
const juce::Identifier CaeciliaParameterMirror::kRegistrationTreeId{"REGISTRATION"};
const juce::Identifier CaeciliaParameterMirror::kConsoleTreeId{"CONSOLE"};
const juce::Identifier CaeciliaParameterMirror::kVersionProperty{"version"};

CaeciliaParameterMirror::CaeciliaParameterMirror(juce::AudioProcessor&         processor,
                                                 const model::Organ&           organ,
                                                 std::span<const core::StopId> defaultDrawn)
    : apvts_(processor, &undoManager_, "PARAMETERS",
             ParameterLayout::create(organ, defaultDrawn))
    , registrationTree_(kRegistrationTreeId)
{
    cacheParameterPointers();
}

void CaeciliaParameterMirror::cacheParameterPointers()
{
    for (std::size_t i = 0; i < ParameterLayout::kMaxStopParameters; ++i)
    {
        const std::string id = ParameterLayout::stopParamId(i);
        stopParams_[i]       = apvts_.getRawParameterValue(id.c_str());
        stopParamObjects_[i] = apvts_.getParameter(id);
    }

    for (std::size_t i = 0; i < ParameterLayout::kMaxCouplerParameters; ++i)
    {
        const std::string id    = ParameterLayout::couplerParamId(i);
        couplerParams_[i]       = apvts_.getRawParameterValue(id.c_str());
        couplerParamObjects_[i] = apvts_.getParameter(id);
    }
}

std::uint32_t CaeciliaParameterMirror::couplerBits() const noexcept
{
    std::uint32_t bits = 0;
    for (std::size_t i = 0; i < ParameterLayout::kMaxCouplerParameters; ++i)
        if (couplerParams_[i] != nullptr
            && couplerParams_[i]->load(std::memory_order_relaxed) >= 0.5f)
            bits |= (std::uint32_t{1} << i);
    return bits;
}

void CaeciliaParameterMirror::writeCouplerBits(std::uint32_t bits)
{
    for (std::size_t i = 0; i < ParameterLayout::kMaxCouplerParameters; ++i)
    {
        juce::RangedAudioParameter* p = couplerParamObjects_[i];
        if (p == nullptr)
            continue;

        const bool want = (bits & (std::uint32_t{1} << i)) != 0;
        if ((p->getValue() >= 0.5f) != want)
            p->setValueNotifyingHost(want ? 1.0f : 0.0f);
    }
}

std::uint64_t CaeciliaParameterMirror::stopBits() const noexcept
{
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < ParameterLayout::kMaxStopParameters; ++i)
        if (stopParams_[i] != nullptr
            && stopParams_[i]->load(std::memory_order_relaxed) >= 0.5f)
            bits |= (std::uint64_t{1} << i);
    return bits;
}

void CaeciliaParameterMirror::writeStopBits(std::uint64_t bits)
{
    for (std::size_t i = 0; i < ParameterLayout::kMaxStopParameters; ++i)
    {
        juce::RangedAudioParameter* p = stopParamObjects_[i];
        if (p == nullptr)
            continue;

        const bool want = (bits & (std::uint64_t{1} << i)) != 0;
        // Only where it differs. Writing every slot on every change would put
        // sixty-four points into a host's automation lanes each time one drawstop
        // moved.
        if ((p->getValue() >= 0.5f) != want)
            p->setValueNotifyingHost(want ? 1.0f : 0.0f);
    }
}

std::atomic<float>* CaeciliaParameterMirror::rawParameter(const char* paramId) const noexcept
{
    // getRawParameterValue is a lock-free lookup into a pre-built map; safe to
    // call once off-thread to cache, and the returned pointer is stable for the
    // processor's lifetime so the audio thread may dereference it directly.
    return apvts_.getRawParameterValue(paramId);
}

bool CaeciliaParameterMirror::stopEngaged(std::size_t index) const noexcept
{
    if (index >= stopParams_.size() || stopParams_[index] == nullptr)
        return false;
    return stopParams_[index]->load(std::memory_order_relaxed) >= 0.5f;
}

void CaeciliaParameterMirror::writeState(juce::MemoryBlock& dest,
                                         const juce::ValueTree& consoleState)
{
    // Compose a single document: the APVTS state, the semantic registration tree
    // and the console state, so one blob round-trips everything the user set.
    juce::ValueTree root{kStateRootId};
    root.setProperty(kVersionProperty, kDocumentVersion, nullptr);
    root.appendChild(apvts_.copyState(), nullptr);
    root.appendChild(registrationTree_.createCopy(), nullptr);
    if (consoleState.isValid())
        root.appendChild(consoleState.createCopy(), nullptr);

    juce::MemoryOutputStream stream{dest, false};
    root.writeToStream(stream);
}

bool CaeciliaParameterMirror::readState(const void* data, int sizeBytes,
                                        juce::ValueTree& consoleStateOut)
{
    consoleStateOut      = juce::ValueTree{};
    lastDocumentVersion_ = kDocumentVersion;

    if (data == nullptr || sizeBytes <= 0)
        return false;

    juce::ValueTree root = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeBytes));
    if (! root.isValid() || root.getType() != kStateRootId)
        return false;

    // A document with no version property predates the property itself, so it is
    // version 1 by definition. Version 1 documents simply have no console tree;
    // everything else about them is still readable, so they load with the console
    // at its defaults rather than being rejected.
    lastDocumentVersion_ = static_cast<int>(root.getProperty(kVersionProperty, 1));
    consoleStateOut      = root.getChildWithName(kConsoleTreeId);

    // Restore APVTS (first child is the parameter state).
    if (auto params = root.getChildWithName(apvts_.state.getType()); params.isValid())
        apvts_.replaceState(params);

    // Restore the semantic registration tree if present.
    if (auto reg = root.getChildWithName(kRegistrationTreeId); reg.isValid())
        registrationTree_ = reg.createCopy();

    return true;
}

} // namespace caecilia::plugin
