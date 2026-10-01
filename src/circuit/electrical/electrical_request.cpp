#include <volt/electrical/electrical_request.hpp>

#include <utility>

#include <volt/core/errors.hpp>

namespace volt {

template <typename Tag>
ElectricalRequestKeyValue<Tag>::ElectricalRequestKeyValue(std::string value)
    : value_{std::move(value)} {
    if (value_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Electrical request key must not be empty"};
    }
}

template class ElectricalRequestKeyValue<ElectricalRequestKeyTag>;
template class ElectricalRequestKeyValue<ElectricalSourceKeyTag>;
template class ElectricalRequestKeyValue<ElectricalProbeKeyTag>;

ElectricalNetPair::ElectricalNetPair(ElectricalNetRef from, ElectricalNetRef to)
    : from_{std::move(from)}, to_{std::move(to)} {
    if (from_.input() != to_.input()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Electrical net pair combines references from different inputs"};
    }
}

} // namespace volt
