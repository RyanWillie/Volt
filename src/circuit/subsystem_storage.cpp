#include "subsystem_storage_impl.hpp"

#include "circuit_storage.hpp"

#include <volt/circuit/connectivity/connectivity_model.hpp>
#include <volt/circuit/constraints/net_classes.hpp>
#include <volt/circuit/electrical/electrical_model.hpp>
#include <volt/circuit/hierarchy/hierarchy_model.hpp>
#include <volt/circuit/intent/design_intent.hpp>

namespace volt::detail {

template class SubsystemStorage<ConnectivityModel, ConnectivityState>;
template class SubsystemStorage<HierarchyModel, HierarchyState>;
template class SubsystemStorage<ElectricalModel, ElectricalState>;
template class SubsystemStorage<DesignIntent, DesignIntentState>;
template class SubsystemStorage<NetClasses, NetClassesState>;

} // namespace volt::detail
