/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/registration/Selector.h"

#include "ceciliae/model/Organ.h"

#include <utility>

namespace ceciliae::registration
{

Selector Selector::atom(StopQuery query)
{
    Selector s;
    s.op_    = Op::Atom;
    s.query_ = std::move(query);
    return s;
}

Selector Selector::combine(Op op, std::vector<Selector> operands)
{
    Selector s;
    s.op_       = op;
    s.operands_ = std::move(operands);
    return s;
}

Selector Selector::complementOf(Selector inner)
{
    std::vector<Selector> ops;
    ops.push_back(std::move(inner));
    return combine(Op::Complement, std::move(ops));
}

StopSet Selector::universe(const model::Organ& spec)
{
    std::vector<core::StopId> all;
    all.reserve(spec.stops().size());
    for (const auto& stop : spec.stops())
        all.push_back(stop.id());
    return StopSet{std::span<const core::StopId>{all}};
}

StopSet Selector::resolve(const model::Organ& spec, const RegistrationState& state) const
{
    switch (op_)
    {
        case Op::Atom:
            return query_.resolve(spec, state);

        case Op::Union:
        {
            StopSet acc;
            for (const auto& child : operands_)
                acc.uniteWith(child.resolve(spec, state));
            return acc;
        }

        case Op::Intersect:
        {
            if (operands_.empty())
                return StopSet{};
            StopSet acc = operands_.front().resolve(spec, state);
            for (std::size_t i = 1; i < operands_.size(); ++i)
                acc.intersectWith(operands_[i].resolve(spec, state));
            return acc;
        }

        case Op::Difference:
        {
            if (operands_.empty())
                return StopSet{};
            StopSet acc = operands_.front().resolve(spec, state);
            for (std::size_t i = 1; i < operands_.size(); ++i)
                acc.subtract(operands_[i].resolve(spec, state));
            return acc;
        }

        case Op::Complement:
        {
            StopSet u = universe(spec);
            if (!operands_.empty())
                u.subtract(operands_.front().resolve(spec, state));
            return u;
        }
    }
    return StopSet{};
}

Selector operator|(Selector a, Selector b)
{
    return Selector::combine(Selector::Op::Union, {std::move(a), std::move(b)});
}

Selector operator&(Selector a, Selector b)
{
    return Selector::combine(Selector::Op::Intersect, {std::move(a), std::move(b)});
}

Selector operator-(Selector a, Selector b)
{
    return Selector::combine(Selector::Op::Difference, {std::move(a), std::move(b)});
}

Selector operator!(Selector a)
{
    return Selector::complementOf(std::move(a));
}

} // namespace ceciliae::registration
