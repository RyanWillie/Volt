#include <volt/electrical/compiled_electrical_model.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/core/errors.hpp>

namespace volt {
namespace {

class IdentityEncoder final {
  public:
    void text(std::string_view value) {
        bytes_ += std::to_string(value.size());
        bytes_ += ':';
        bytes_.append(value);
        bytes_ += '\n';
    }

    template <typename Id> void id(Id value) { text(std::to_string(value.index())); }

    template <typename Enum> void enumeration(Enum value) {
        static_assert(std::is_enum_v<Enum>);
        text(std::to_string(static_cast<std::underlying_type_t<Enum>>(value)));
    }

    void number(double value) {
        auto buffer = std::array<char, 64>{};
        const auto result =
            std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                          std::chars_format::general, std::numeric_limits<double>::max_digits10);
        if (result.ec != std::errc{}) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Failed to encode compiled electrical-model number"};
        }
        text(std::string_view{buffer.data(), result.ptr});
    }

    [[nodiscard]] ContentHash digest() const { return sha256_content_hash(bytes_); }

  private:
    std::string bytes_;
};

void encode(IdentityEncoder &out, const Quantity &value) {
    out.enumeration(value.dimension());
    out.number(value.value());
}

void encode(IdentityEncoder &out, const ModelParameter &value) {
    encode(out, value.nominal());
    out.text(value.tolerance().has_value() ? "tolerance" : "no-tolerance");
    if (value.tolerance().has_value()) {
        out.enumeration(value.tolerance()->mode());
        encode(out, value.tolerance()->minus());
        encode(out, value.tolerance()->plus());
    }
    out.text(std::to_string(value.evidence().size()));
    for (const auto &evidence : value.evidence()) {
        out.text(evidence.value());
    }
}

void encode(IdentityEncoder &out, const LibraryPartRef &value) {
    out.text(value.library_namespace());
    out.text(value.library_version());
    out.text(value.part_key().value());
    out.text(value.library_digest().value());
    out.text(value.part_digest().value());
}

void encode(IdentityEncoder &out, const DcNetPair &value) {
    out.id(value.from().id());
    out.id(value.to().id());
}

void encode(IdentityEncoder &out, const ModelEndpoint &value) {
    std::visit(
        [&](const auto &endpoint) {
            using Endpoint = std::decay_t<decltype(endpoint)>;
            if constexpr (std::same_as<Endpoint, ModelTerminalKey>) {
                out.text("terminal");
            } else {
                out.text("internal");
            }
            out.text(endpoint.value());
        },
        value);
}

void encode(IdentityEncoder &out, const ResistanceElement &value) {
    out.text("resistance");
    out.text(value.key().value());
    encode(out, value.from());
    encode(out, value.to());
    encode(out, value.parameter());
}

void encode(IdentityEncoder &out, const CapacitanceElement &value) {
    out.text("capacitance");
    out.text(value.key().value());
    encode(out, value.from());
    encode(out, value.to());
    encode(out, value.parameter());
}

void encode(IdentityEncoder &out, const InductanceElement &value) {
    out.text("inductance");
    out.text(value.key().value());
    encode(out, value.from());
    encode(out, value.to());
    encode(out, value.parameter());
}

void encode(IdentityEncoder &out, const DcVoltageSource &value) {
    out.text("voltage-source");
    out.text(value.key().value());
    encode(out, value.nets());
    encode(out, value.value());
}

void encode(IdentityEncoder &out, const DcCurrentSource &value) {
    out.text("current-source");
    out.text(value.key().value());
    encode(out, value.nets());
    encode(out, value.value());
}

[[nodiscard]] ContentHash request_identity(const DcRequest &request) {
    auto out = IdentityEncoder{};
    out.text("volt.electrical-request-content");
    out.text("1");
    out.text(request.input().identity().logical().value());
    out.text(request.input().identity().selected_parts().value());
    out.text(request.key().value());
    out.text(request.reference().has_value() ? "reference" : "no-reference");
    if (request.reference().has_value()) {
        out.id(request.reference()->id());
    }
    out.text(std::to_string(request.sources().size()));
    for (const auto &source : request.sources()) {
        std::visit([&](const auto &value) { encode(out, value); }, source);
    }
    out.text(std::to_string(request.probes().size()));
    for (const auto &probe : request.probes()) {
        std::visit(
            [&](const auto &value) {
                using Probe = std::decay_t<decltype(value)>;
                if constexpr (std::same_as<Probe, DcVoltageProbe>) {
                    out.text("voltage-probe");
                    out.text(value.key().value());
                    encode(out, value.nets());
                } else if constexpr (std::same_as<Probe, DcSourceCurrentProbe>) {
                    out.text("source-current-probe");
                    out.text(value.key().value());
                    out.text(value.source().value());
                } else {
                    out.text("model-element-current-probe");
                    out.text(value.key().value());
                    out.id(value.occurrence().id());
                    out.text(value.element().value());
                }
            },
            probe);
    }
    out.text(std::to_string(request.exclusions().size()));
    for (const auto &exclusion : request.exclusions()) {
        out.id(exclusion.occurrence().id());
        std::visit(
            [&](const auto &reason) {
                using Reason = std::decay_t<decltype(reason)>;
                if constexpr (std::same_as<Reason, DcNonElectricalExclusion>) {
                    out.text("non-electrical");
                } else if constexpr (std::same_as<Reason, DcOutsideAnalysisExclusion>) {
                    out.text("outside-analysis");
                } else {
                    out.text("replaced-by-stimulus");
                    out.text(std::to_string(reason.sources().size()));
                    for (const auto &source : reason.sources()) {
                        out.text(source.value());
                    }
                }
            },
            exclusion.reason());
    }
    return out.digest();
}

void encode(IdentityEncoder &out, const ElectricalNodeOrigin &origin) {
    std::visit(
        [&](const auto &value) {
            using Origin = std::decay_t<decltype(value)>;
            if constexpr (std::same_as<Origin, ElectricalNetOrigin>) {
                out.text("net-group");
                out.text(std::to_string(value.nets.size()));
                for (const auto net : value.nets) {
                    out.id(net);
                }
                out.text(std::to_string(value.bindings.size()));
                for (const auto binding : value.bindings) {
                    out.id(binding);
                }
            } else if constexpr (std::same_as<Origin, ElectricalOpenPinOrigin>) {
                out.text("open-pin");
                out.id(value.occurrence);
                out.id(value.pin);
                out.text(value.pin_key.value());
                out.text(value.terminal.value());
            } else {
                out.text("internal-node");
                out.id(value.occurrence);
                out.text(value.part_digest.value());
                out.text(value.node.value());
            }
        },
        origin);
}

void encode(IdentityEncoder &out, const ElectricalBranchOrigin &origin) {
    std::visit(
        [&](const auto &value) {
            using Origin = std::decay_t<decltype(value)>;
            if constexpr (std::same_as<Origin, ElectricalElementOrigin>) {
                out.text("part-element");
                out.id(value.occurrence);
                encode(out, value.part);
                out.text(value.element.value());
            } else {
                out.text("request-source");
                out.text(value.value());
            }
        },
        origin);
}

[[nodiscard]] ContentHash compiled_identity(const ContentHash &request,
                                            const std::vector<ElectricalNode> &nodes,
                                            const std::vector<ElectricalBranch> &branches,
                                            ElectricalNodeId reference,
                                            const std::vector<ElectricalStorage> &storage,
                                            const std::vector<ElectricalProbe> &probes) {
    auto out = IdentityEncoder{};
    out.text("volt.compiled-electrical-model");
    out.text(std::to_string(CompiledElectricalModel::compiler_version()));
    out.text(request.value());
    out.text(std::to_string(nodes.size()));
    for (const auto &node : nodes) {
        out.id(node.id);
        encode(out, node.origin);
        out.text(std::to_string(node.incidence.size()));
        for (const auto &term : node.incidence) {
            out.id(term.branch);
            out.text(std::to_string(term.sign));
        }
    }
    out.text(std::to_string(branches.size()));
    for (const auto &branch : branches) {
        out.id(branch.id);
        out.id(branch.from);
        out.id(branch.to);
        encode(out, branch.origin);
        std::visit([&](const auto &law) { encode(out, law); }, branch.law);
    }
    out.id(reference);
    out.text(std::to_string(storage.size()));
    for (const auto &entry : storage) {
        std::visit(
            [&](const auto &value) {
                using Storage = std::decay_t<decltype(value)>;
                if constexpr (std::same_as<Storage, ElectricalCapacitorStorage>) {
                    out.text("capacitor-storage");
                    out.id(value.branch);
                    encode(out, value.capacitance);
                } else {
                    out.text("inductor-storage");
                    out.id(value.branch);
                    encode(out, value.inductance);
                }
            },
            entry);
    }
    out.text(std::to_string(probes.size()));
    for (const auto &probe : probes) {
        out.text(probe.key.value());
        std::visit(
            [&](const auto &target) {
                using Target = std::decay_t<decltype(target)>;
                if constexpr (std::same_as<Target, ElectricalVoltageObservation>) {
                    out.text("voltage");
                    out.id(target.from);
                    out.id(target.to);
                } else {
                    out.text("current");
                    out.id(target.branch);
                }
            },
            probe.target);
    }
    return out.digest();
}

[[nodiscard]] Diagnostic analysis_diagnostic(Severity severity, std::string_view code,
                                             std::string message,
                                             std::vector<EntityRef> entities = {}) {
    return Diagnostic{severity, DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Analysis}, std::move(message),
                      std::move(entities)};
}

template <typename Value> [[nodiscard]] const auto &model_element_key(const Value &value) {
    return value.key();
}

[[nodiscard]] const ModelElementKey &model_element_key(const ModelElement &value) {
    return std::visit([](const auto &element) -> const auto & { return element.key(); }, value);
}

struct VoltageConstraint {
    ElectricalBranchId branch;
    ElectricalNodeId from;
    ElectricalNodeId to;
    double value;
};

} // namespace

class CompiledElectricalModel::Compiler final {
  public:
    [[nodiscard]] static std::optional<CompiledElectricalModel>
    compile(const DcRequest &request, const DcRequestAssessment &assessment,
            std::vector<Diagnostic> &diagnostics) {
        auto compiler = Compiler{request, diagnostics};
        compiler.build_net_nodes();
        compiler.check_required_ports();
        if (!assessment.complete()) {
            return std::nullopt;
        }
        compiler.build_part_nodes_and_branches(assessment);
        compiler.build_source_branches();
        compiler.derive_incidence_and_storage();
        compiler.resolve_probes();
        compiler.check_voltage_constraints();
        if (compiler.failed_) {
            return std::nullopt;
        }
        return CompiledElectricalModel{request,
                                       std::move(compiler.nodes_),
                                       std::move(compiler.branches_),
                                       *compiler.net_nodes_.at(request.reference()->id().index()),
                                       std::move(compiler.storage_),
                                       std::move(compiler.probes_)};
    }

  private:
    explicit Compiler(const DcRequest &request, std::vector<Diagnostic> &diagnostics)
        : request_{request}, continuity_{request.input().circuit()}, diagnostics_{diagnostics},
          net_nodes_(request.input().circuit().all<NetId>().size()) {}

    void build_net_nodes() {
        const auto &circuit = request_.input().circuit();
        const auto net_count = circuit.all<NetId>().size();
        for (std::size_t candidate = 0; candidate < net_count; ++candidate) {
            const auto net = NetId{candidate};
            if (net_nodes_.at(candidate).has_value()) {
                continue;
            }
            auto members = std::vector<NetId>{};
            for (std::size_t index = candidate; index < net_count; ++index) {
                const auto member = NetId{index};
                if (continuity_.same_group(net, member)) {
                    members.push_back(member);
                }
            }
            auto bindings = std::vector<PortBindingId>{};
            for (std::size_t index = 0; index < circuit.all<PortBindingId>().size(); ++index) {
                const auto binding_id = PortBindingId{index};
                const auto &binding = circuit.get(binding_id);
                if (continuity_.same_group(net, binding.internal_net())) {
                    bindings.push_back(binding_id);
                }
            }
            const auto node = append_node(ElectricalNetOrigin{members, std::move(bindings)});
            for (const auto member : members) {
                net_nodes_.at(member.index()) = node;
            }
        }
    }

    void check_required_ports() {
        const auto &circuit = request_.input().circuit();
        for (std::size_t index = 0; index < circuit.all<ModuleInstanceId>().size(); ++index) {
            const auto instance_id = ModuleInstanceId{index};
            const auto &instance = circuit.get(instance_id);
            for (const auto port_id : circuit.get(instance.definition()).ports()) {
                if (circuit.get(port_id).required() &&
                    !queries::port_binding_for(circuit, instance_id, port_id).has_value()) {
                    fail(analysis_diagnostic_codes::ElectricalRequiredPortUnbound,
                         "Electrical compilation requires this module port to be bound",
                         {EntityRef::module_instance(instance_id),
                          EntityRef::module_def(instance.definition()),
                          EntityRef::port_def(port_id)});
                }
            }
        }
    }

    void build_part_nodes_and_branches(const DcRequestAssessment &assessment) {
        const auto &circuit = request_.input().circuit();
        for (const auto &coverage : assessment.coverage()) {
            if (coverage.status() == DcCoverageStatus::Excluded) {
                continue;
            }
            const auto occurrence = coverage.occurrence().id();
            const auto *part = request_.input().part(occurrence);
            if (part == nullptr || !part->electrical_model().has_value()) {
                throw KernelLogicError{ErrorCode::InvalidState,
                                       "Complete DC assessment lacks a resolved electrical model"};
            }
            const auto &selection = circuit.get(occurrence).selected_library_part_ref();
            if (!selection.has_value()) {
                throw KernelLogicError{ErrorCode::InvalidState,
                                       "Complete DC assessment lacks an exact Part selection"};
            }
            const auto &model = *part->electrical_model();
            auto terminal_nodes = std::map<ModelTerminalKey, ElectricalNodeId>{};
            auto internal_nodes = std::map<ModelInternalNodeKey, ElectricalNodeId>{};
            for (const auto &terminal : model.terminals()) {
                terminal_nodes.emplace(terminal.key(), terminal_node(occurrence, terminal));
            }
            for (const auto &internal : model.internal_nodes()) {
                internal_nodes.emplace(internal.key(),
                                       append_node(ElectricalInternalNodeOrigin{
                                           occurrence, selection->part_digest(), internal.key()}));
            }
            for (const auto &element : model.elements()) {
                const auto endpoints = std::visit(
                    [&](const auto &value) {
                        return std::pair{
                            endpoint_node(value.from(), terminal_nodes, internal_nodes),
                            endpoint_node(value.to(), terminal_nodes, internal_nodes)};
                    },
                    element);
                const auto branch = append_branch(
                    endpoints.first, endpoints.second,
                    ElectricalElementOrigin{occurrence, *selection, model_element_key(element)},
                    std::visit([](const auto &value) -> ElectricalLaw { return value; }, element));
                element_branches_.emplace(std::pair{occurrence.index(), model_element_key(element)},
                                          branch);
                if (const auto *resistance = std::get_if<ResistanceElement>(&element);
                    resistance != nullptr && resistance->parameter().nominal().value() == 0.0) {
                    voltage_constraints_.push_back(
                        VoltageConstraint{branch, endpoints.first, endpoints.second, 0.0});
                }
            }
        }
    }

    [[nodiscard]] ElectricalNodeId terminal_node(ComponentId occurrence,
                                                 const ModelTerminal &terminal) {
        const auto &circuit = request_.input().circuit();
        const auto &component = circuit.get(circuit.get(occurrence).definition());
        const auto &pin_keys = component.contract().pin_keys();
        const auto match = std::ranges::find(pin_keys, terminal.pin());
        if (match == pin_keys.end()) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Electrical-model terminal PinKey is absent from its component"};
        }
        const auto offset = static_cast<std::size_t>(std::distance(pin_keys.begin(), match));
        const auto pin_definition = component.pins().at(offset);
        const auto pin = queries::pin_by_definition(circuit, occurrence, pin_definition);
        if (!pin.has_value()) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Electrical-model terminal has no concrete occurrence pin"};
        }
        if (const auto net = queries::net_of(circuit, *pin); net.has_value()) {
            return *net_nodes_.at(net->index());
        }

        const auto &definition = circuit.get(pin_definition);
        const auto node =
            append_node(ElectricalOpenPinOrigin{occurrence, *pin, terminal.pin(), terminal.key()});
        const auto entities = std::vector{EntityRef::component(occurrence), EntityRef::pin(*pin),
                                          EntityRef::pin_def(pin_definition)};
        if (definition.connection_requirement() == ConnectionRequirement::Required) {
            fail(analysis_diagnostic_codes::ElectricalRequiredPinUnconnected,
                 "Electrical compilation requires this model terminal pin to be connected",
                 entities);
        } else {
            diagnostics_.push_back(analysis_diagnostic(
                Severity::Warning, analysis_diagnostic_codes::ElectricalOptionalPinOpen,
                "Electrical compilation retained an intentionally open model terminal", entities));
        }
        return node;
    }

    [[nodiscard]] static ElectricalNodeId
    endpoint_node(const ModelEndpoint &endpoint,
                  const std::map<ModelTerminalKey, ElectricalNodeId> &terminals,
                  const std::map<ModelInternalNodeKey, ElectricalNodeId> &internals) {
        return std::visit(
            [&](const auto &key) {
                using Key = std::decay_t<decltype(key)>;
                if constexpr (std::same_as<Key, ModelTerminalKey>) {
                    return terminals.at(key);
                } else {
                    return internals.at(key);
                }
            },
            endpoint);
    }

    void build_source_branches() {
        for (const auto &source : request_.sources()) {
            std::visit(
                [&](const auto &value) {
                    const auto from = *net_nodes_.at(value.nets().from().id().index());
                    const auto to = *net_nodes_.at(value.nets().to().id().index());
                    const auto branch = append_branch(from, to, value.key(), ElectricalLaw{value});
                    source_branches_.emplace(value.key(), branch);
                    using Source = std::decay_t<decltype(value)>;
                    if constexpr (std::same_as<Source, DcVoltageSource>) {
                        voltage_constraints_.push_back(
                            VoltageConstraint{branch, from, to, value.value().value()});
                    }
                },
                source);
        }
    }

    void derive_incidence_and_storage() {
        for (const auto &branch : branches_) {
            if (branch.from != branch.to) {
                nodes_.at(branch.from.index()).incidence.push_back({branch.id, 1});
                nodes_.at(branch.to.index()).incidence.push_back({branch.id, -1});
            } else {
                diagnostics_.push_back(analysis_diagnostic(
                    Severity::Warning, analysis_diagnostic_codes::ElectricalShortedBranch,
                    "Electrical compilation retained shorted " + branch_label(branch) +
                        " whose endpoints share one node",
                    branch_entities(branch)));
            }
            if (const auto *capacitor = std::get_if<CapacitanceElement>(&branch.law)) {
                storage_.emplace_back(
                    ElectricalCapacitorStorage{branch.id, capacitor->parameter().nominal()});
            } else if (const auto *inductor = std::get_if<InductanceElement>(&branch.law)) {
                storage_.emplace_back(
                    ElectricalInductorStorage{branch.id, inductor->parameter().nominal()});
            }
        }
    }

    void resolve_probes() {
        for (const auto &probe : request_.probes()) {
            std::visit(
                [&](const auto &value) {
                    using Probe = std::decay_t<decltype(value)>;
                    if constexpr (std::same_as<Probe, DcVoltageProbe>) {
                        probes_.push_back(
                            {value.key(), ElectricalVoltageObservation{
                                              *net_nodes_.at(value.nets().from().id().index()),
                                              *net_nodes_.at(value.nets().to().id().index())}});
                    } else if constexpr (std::same_as<Probe, DcSourceCurrentProbe>) {
                        probes_.push_back({value.key(), ElectricalCurrentObservation{
                                                            source_branches_.at(value.source())}});
                    } else {
                        probes_.push_back(
                            {value.key(),
                             ElectricalCurrentObservation{element_branches_.at(
                                 {value.occurrence().id().index(), value.element()})}});
                    }
                },
                probe);
        }
    }

    void check_voltage_constraints() {
        struct PriorConstraint {
            double value;
            VoltageConstraint constraint;
        };

        auto values = std::map<std::pair<std::size_t, std::size_t>, PriorConstraint>{};
        auto contradicted = std::set<std::pair<std::size_t, std::size_t>>{};
        for (const auto &constraint : voltage_constraints_) {
            if (constraint.from == constraint.to) {
                if (constraint.value != 0.0) {
                    const auto &branch = branches_.at(constraint.branch.index());
                    fail(analysis_diagnostic_codes::ElectricalContradictoryVoltageSource,
                         "Nonzero voltage constraint from " + branch_label(branch) +
                             " resolves across one electrical node",
                         branch_entities(branch));
                }
                continue;
            }
            const auto from = constraint.from.index();
            const auto to = constraint.to.index();
            const auto key = from < to ? std::pair{from, to} : std::pair{to, from};
            const auto value = from < to ? constraint.value : -constraint.value;
            const auto [match, inserted] = values.emplace(key, PriorConstraint{value, constraint});
            if (!inserted && match->second.value != value && contradicted.insert(key).second) {
                const auto &previous = branches_.at(match->second.constraint.branch.index());
                const auto &current = branches_.at(constraint.branch.index());
                auto entities = branch_entities(previous);
                append_entities(entities, branch_entities(current));
                fail(analysis_diagnostic_codes::ElectricalContradictoryVoltageSource,
                     "Unequal ideal voltage constraints from " + branch_label(previous) + " and " +
                         branch_label(current) + " resolve across one electrical node pair",
                     std::move(entities));
            }
        }
    }

    [[nodiscard]] static std::string branch_label(const ElectricalBranch &branch) {
        return std::visit(
            [](const auto &origin) {
                using Origin = std::decay_t<decltype(origin)>;
                if constexpr (std::same_as<Origin, ElectricalElementOrigin>) {
                    return "model element '" + origin.element.value() +
                           "' on component:" + std::to_string(origin.occurrence.index());
                } else {
                    return "request source '" + origin.value() + "'";
                }
            },
            branch.origin);
    }

    static void append_entity(std::vector<EntityRef> &entities, EntityRef entity) {
        if (std::ranges::find(entities, entity) == entities.end()) {
            entities.push_back(entity);
        }
    }

    static void append_entities(std::vector<EntityRef> &entities,
                                const std::vector<EntityRef> &additional) {
        for (const auto entity : additional) {
            append_entity(entities, entity);
        }
    }

    void append_node_entities(std::vector<EntityRef> &entities, ElectricalNodeId node) const {
        std::visit(
            [&](const auto &origin) {
                using Origin = std::decay_t<decltype(origin)>;
                if constexpr (std::same_as<Origin, ElectricalNetOrigin>) {
                    for (const auto net : origin.nets) {
                        append_entity(entities, EntityRef::net(net));
                    }
                } else if constexpr (std::same_as<Origin, ElectricalOpenPinOrigin>) {
                    append_entity(entities, EntityRef::component(origin.occurrence));
                    append_entity(entities, EntityRef::pin(origin.pin));
                } else {
                    append_entity(entities, EntityRef::component(origin.occurrence));
                }
            },
            nodes_.at(node.index()).origin);
    }

    [[nodiscard]] std::vector<EntityRef> branch_entities(const ElectricalBranch &branch) const {
        auto entities = std::vector<EntityRef>{};
        if (const auto *origin = std::get_if<ElectricalElementOrigin>(&branch.origin)) {
            append_entity(entities, EntityRef::component(origin->occurrence));
        }
        append_node_entities(entities, branch.from);
        append_node_entities(entities, branch.to);
        return entities;
    }

    [[nodiscard]] ElectricalNodeId append_node(ElectricalNodeOrigin origin) {
        const auto id = ElectricalNodeId{nodes_.size()};
        nodes_.push_back(ElectricalNode{id, std::move(origin), {}});
        return id;
    }

    [[nodiscard]] ElectricalBranchId append_branch(ElectricalNodeId from, ElectricalNodeId to,
                                                   ElectricalBranchOrigin origin,
                                                   ElectricalLaw law) {
        const auto id = ElectricalBranchId{branches_.size()};
        branches_.push_back(ElectricalBranch{id, from, to, std::move(origin), std::move(law)});
        return id;
    }

    void fail(std::string_view code, std::string message, std::vector<EntityRef> entities = {}) {
        diagnostics_.push_back(
            analysis_diagnostic(Severity::Error, code, std::move(message), std::move(entities)));
        failed_ = true;
    }

    const DcRequest &request_;
    detail::NetContinuityView continuity_;
    std::vector<Diagnostic> &diagnostics_;
    std::vector<ElectricalNode> nodes_;
    std::vector<ElectricalBranch> branches_;
    std::vector<ElectricalStorage> storage_;
    std::vector<ElectricalProbe> probes_;
    std::vector<std::optional<ElectricalNodeId>> net_nodes_;
    std::map<DcSourceKey, ElectricalBranchId> source_branches_;
    std::map<std::pair<std::size_t, ModelElementKey>, ElectricalBranchId> element_branches_;
    std::vector<VoltageConstraint> voltage_constraints_;
    bool failed_ = false;
};

CompiledElectricalModel::CompiledElectricalModel(DcRequest request,
                                                 std::vector<ElectricalNode> nodes,
                                                 std::vector<ElectricalBranch> branches,
                                                 ElectricalNodeId reference,
                                                 std::vector<ElectricalStorage> storage,
                                                 std::vector<ElectricalProbe> probes)
    : request_{std::move(request)}, nodes_{std::move(nodes)}, branches_{std::move(branches)},
      reference_{reference}, storage_{std::move(storage)}, probes_{std::move(probes)},
      request_identity_{::volt::request_identity(request_)},
      identity_{
          compiled_identity(request_identity_, nodes_, branches_, reference_, storage_, probes_)} {}

ElectricalCompileReport::ElectricalCompileReport(const DcRequest &request)
    : assessment_{assess_dc_request(request)}, diagnostics_{assessment_.diagnostics()},
      model_{CompiledElectricalModel::Compiler::compile(request, assessment_, diagnostics_)} {}

ElectricalCompileReport compile_electrical(const DcRequest &request) {
    return ElectricalCompileReport{request};
}

} // namespace volt
