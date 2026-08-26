// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/registration/Explanation.h"

namespace caecilia::registration
{

const char* toString(Explanation::Reason reason) noexcept
{
    switch (reason)
    {
        case Explanation::Reason::NotEngaged:      return "not engaged";
        case Explanation::Reason::DirectCommand:   return "drawn directly";
        case Explanation::Reason::SelectorMatch:   return "matched a selector";
        case Explanation::Reason::PlenumPrincipal: return "plenum: principal chorus";
        case Explanation::Reason::PlenumMixture:   return "plenum: mixture crown";
        case Explanation::Reason::PlenumReed:      return "plenum: chorus reed";
        case Explanation::Reason::CouplerImplied:  return "coupled";
        case Explanation::Reason::IntentPort:      return "ported intent";
        case Explanation::Reason::SnapshotRecall:  return "recalled snapshot";
    }
    return "unknown";
}

} // namespace caecilia::registration
