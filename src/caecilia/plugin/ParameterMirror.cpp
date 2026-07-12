/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/plugin/ParameterMirror.h"

#include "caecilia/model/Organ.h"
#include "caecilia/model/Stop.h"

#include <algorithm>

namespace caecilia::plugin
{

const juce::Identifier CaeciliaParameterMirror::kStateRootId{"CAECILIA_STATE"};
const juce::Identifier CaeciliaParameterMirror::kRegistrationTreeId{"REGISTRATION"};

CaeciliaParameterMirror::CaeciliaParameterMirror(juce::AudioProcessor& processor)
    : apvts_(processor, &undoManager_, "PARAMETERS", ParameterLayout::create())
    , registrationTree_(kRegistrationTreeId)
{
    cacheParameterPointers();
}

void CaeciliaParameterMirror::cacheParameterPointers()
{
    for (std::size_t i = 0; i < ParameterLayout::kMaxStopParameters; ++i)
    {
        const std::string id = ParameterLayout::stopParamId(i);
        stopParams_[i]  = apvts_.getRawParameterValue(id.c_str());
        slotStopIds_[i] = core::StopId{};
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

void CaeciliaParameterMirror::bindOrgan(const model::Organ& organ)
{
    const auto& stops = organ.stops();
    const std::size_t n = std::min(stops.size(), ParameterLayout::kMaxStopParameters);

    for (std::size_t i = 0; i < ParameterLayout::kMaxStopParameters; ++i)
    {
        if (i < n)
        {
            slotStopIds_[i] = stops[i].id();
            // NOTE: JUCE parameter *display names* are fixed at construction; the
            // reserved-pool names stay generic. The UI resolves a slot's true
            // semantic label from the bound StopId via the OrganSpec instead.
            // TODO(phase0.7): mirror each bound stop into registrationTree_ so
            // provenance / explain() survives save/load.
        }
        else
        {
            slotStopIds_[i] = core::StopId{};
        }
    }

    boundStopCount_ = n;
}

core::StopId CaeciliaParameterMirror::stopIdForSlot(std::size_t index) const noexcept
{
    if (index >= slotStopIds_.size())
        return core::StopId{};
    return slotStopIds_[index];
}

void CaeciliaParameterMirror::writeState(juce::MemoryBlock& dest)
{
    // Compose a single document: the APVTS state with the semantic registration
    // tree nested underneath, so one blob round-trips both.
    juce::ValueTree root{kStateRootId};
    root.appendChild(apvts_.copyState(), nullptr);
    root.appendChild(registrationTree_.createCopy(), nullptr);

    juce::MemoryOutputStream stream{dest, false};
    root.writeToStream(stream);
}

bool CaeciliaParameterMirror::readState(const void* data, int sizeBytes)
{
    if (data == nullptr || sizeBytes <= 0)
        return false;

    juce::ValueTree root = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeBytes));
    if (! root.isValid() || root.getType() != kStateRootId)
        return false;

    // Restore APVTS (first child is the parameter state).
    if (auto params = root.getChildWithName(apvts_.state.getType()); params.isValid())
        apvts_.replaceState(params);

    // Restore the semantic registration tree if present.
    if (auto reg = root.getChildWithName(kRegistrationTreeId); reg.isValid())
        registrationTree_ = reg.createCopy();

    return true;
}

} // namespace caecilia::plugin
