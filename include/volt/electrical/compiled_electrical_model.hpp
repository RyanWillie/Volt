#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <volt/electrical/dc_request.hpp>

namespace volt {

/** Type tag for node coordinates local to one compiled electrical model. */
struct ElectricalNodeIdTag;
/** Type tag for oriented branches local to one compiled electrical model. */
struct ElectricalBranchIdTag;
/** Stable node coordinate under a compiled model identity. */
using ElectricalNodeId = EntityId<ElectricalNodeIdTag>;
/** Stable branch coordinate under a compiled model identity. */
using ElectricalBranchId = EntityId<ElectricalBranchIdTag>;

/** Logical nets made continuous only by explicit Circuit PortBindings. */
struct ElectricalNetOrigin {
    /** All original NetIds in increasing document order. */
    std::vector<NetId> nets;
    /** All contributing PortBindingIds in increasing document order. */
    std::vector<PortBindingId> bindings;
};

/** Unique occurrence pin coordinate for an unconnected model terminal. */
struct ElectricalOpenPinOrigin {
    /** Owning occurrence in the immutable input. */
    ComponentId occurrence;
    /** Concrete pin; unrelated open pins never alias. */
    PinId pin;
    /** Stable component-contract pin key. */
    PinKey pin_key;
    /** Original model terminal key. */
    ModelTerminalKey terminal;
};

/** Private node that cannot be joined through Circuit authoring. */
struct ElectricalInternalNodeOrigin {
    /** Owning occurrence; repeated Parts retain separate private nodes. */
    ComponentId occurrence;
    /** Exact selected Part identity. */
    ContentHash part_digest;
    /** Original model-local node key. */
    ModelInternalNodeKey node;
};

/** Closed provenance vocabulary for compiled nodes. */
using ElectricalNodeOrigin =
    std::variant<ElectricalNetOrigin, ElectricalOpenPinOrigin, ElectricalInternalNodeOrigin>;

/** One derived KCL term, positive for current leaving this node. */
struct ElectricalIncidence {
    /** Oriented branch whose current participates. */
    ElectricalBranchId branch;
    /** Exactly +1 at from and -1 at to; coincident endpoints cancel. */
    int sign;
};

/** One observable node potential with its provenance and derived conservation equation. */
struct ElectricalNode {
    /** Coordinate equal to this node's position in the compiled node sequence. */
    ElectricalNodeId id;
    /** Stable input-local origin under the compiled input identity. */
    ElectricalNodeOrigin origin;
    /** Canonical signed terms of sum(incidence * branch current) = 0. */
    std::vector<ElectricalIncidence> incidence;
};

/** Exact occurrence/model-element provenance for one compiled branch. */
struct ElectricalElementOrigin {
    /** Logical occurrence owning this model element. */
    ComponentId occurrence;
    /** Complete exact selected Part reference. */
    LibraryPartRef part;
    /** Model-local element observation key. */
    ModelElementKey element;
};

/** A branch originates either in an exact Part element or a request-local source. */
using ElectricalBranchOrigin = std::variant<ElectricalElementOrigin, DcSourceKey>;

/** Original closed laws; parameters, orientation, uncertainty and evidence stay intact. */
using ElectricalLaw = std::variant<ResistanceElement, CapacitanceElement, InductanceElement,
                                   DcVoltageSource, DcCurrentSource>;

/** One oriented current observation and its exact primitive law. */
struct ElectricalBranch {
    /** Coordinate equal to this branch's position in the compiled branch sequence. */
    ElectricalBranchId id;
    /** Positive-voltage endpoint; positive current leaves this node. */
    ElectricalNodeId from;
    /** Negative-voltage endpoint; positive current enters this node. */
    ElectricalNodeId to;
    /** Stable authored origin, independent of backend coordinate elimination. */
    ElectricalBranchOrigin origin;
    /** Original primitive with nominal SI parameter or source value. */
    ElectricalLaw law;
};

/** Storage law q=Cv and i=dq/dt; DC consumers impose i=0 without erasing the branch. */
struct ElectricalCapacitorStorage {
    /** Capacitor branch whose voltage difference defines charge. */
    ElectricalBranchId branch;
    /** Positive nominal capacitance, retained in SI farads. */
    Quantity capacitance;
};

/** Storage law phi=Li and v=dphi/dt; DC consumers impose v=0 without merging nodes. */
struct ElectricalInductorStorage {
    /** Inductor branch whose oriented current defines flux linkage. */
    ElectricalBranchId branch;
    /** Positive nominal inductance, retained in SI henries. */
    Quantity inductance;
};

/** Closed derivative/storage vocabulary, derived from the original branch laws. */
using ElectricalStorage = std::variant<ElectricalCapacitorStorage, ElectricalInductorStorage>;

/** Resolved oriented voltage observation V(from)-V(to). */
struct ElectricalVoltageObservation {
    /** Positive potential coordinate. */
    ElectricalNodeId from;
    /** Negative potential coordinate. */
    ElectricalNodeId to;
};

/** Resolved oriented current observation using the branch's stored orientation. */
struct ElectricalCurrentObservation {
    /** Observed branch coordinate. */
    ElectricalBranchId branch;
};

/** One requested observation resolved without computing a numerical result. */
struct ElectricalProbe {
    /** Original request-local probe identity. */
    DcProbeKey key;
    /** Closed resolved observation target. */
    std::variant<ElectricalVoltageObservation, ElectricalCurrentObservation> target;
};

/** Immutable complete graph/law snapshot; completeness never proves numerical solvability. */
class CompiledElectricalModel {
  public:
    /** Native compiler owner; no author-supplied graph construction boundary exists. */
    class Compiler;

    /** Current deterministic compiler contract, independent of solver/backend versions. */
    [[nodiscard]] static constexpr std::uint32_t compiler_version() noexcept { return 1; }

    /** Return the content identity of the exact input, request, contract and derived graph. */
    [[nodiscard]] const ContentHash &identity() const noexcept { return identity_; }

    /** Return the canonical native request-content identity. */
    [[nodiscard]] const ContentHash &request_identity() const noexcept { return request_identity_; }

    /** Return the immutable request and owning exact input used for compilation. */
    [[nodiscard]] const DcRequest &request() const noexcept { return request_; }

    /** Return deterministic node-potential coordinates, origins and KCL incidence. */
    [[nodiscard]] const std::vector<ElectricalNode> &nodes() const noexcept { return nodes_; }

    /** Return deterministic oriented branches and unchanged primitive laws. */
    [[nodiscard]] const std::vector<ElectricalBranch> &branches() const noexcept {
        return branches_;
    }

    /** Return the explicit continuity-group potential constrained to zero. */
    [[nodiscard]] ElectricalNodeId reference() const noexcept { return reference_; }

    /** Return C/L derivative/storage semantics without a DC-only reduction. */
    [[nodiscard]] const std::vector<ElectricalStorage> &storage() const noexcept {
        return storage_;
    }

    /** Return all request probes in canonical request-key order. */
    [[nodiscard]] const std::vector<ElectricalProbe> &probes() const noexcept { return probes_; }

  private:
    CompiledElectricalModel(DcRequest request, std::vector<ElectricalNode> nodes,
                            std::vector<ElectricalBranch> branches, ElectricalNodeId reference,
                            std::vector<ElectricalStorage> storage,
                            std::vector<ElectricalProbe> probes);

    DcRequest request_;
    std::vector<ElectricalNode> nodes_;
    std::vector<ElectricalBranch> branches_;
    ElectricalNodeId reference_;
    std::vector<ElectricalStorage> storage_;
    std::vector<ElectricalProbe> probes_;
    ContentHash request_identity_;
    ContentHash identity_;
};

/** Compilation outcome retaining S1 coverage and S2 findings even when no model is available. */
class ElectricalCompileReport {
  public:
    /** Assess and compile one owning immutable request through the native compiler. */
    explicit ElectricalCompileReport(const DcRequest &request);

    /** Return whether a complete required graph/law model is available, not solvability. */
    [[nodiscard]] bool complete() const noexcept { return model_.has_value(); }

    /** Return the complete compiled model or null; partial models never escape. */
    [[nodiscard]] const CompiledElectricalModel *model() const & noexcept {
        return model_ ? &*model_ : nullptr;
    }

    /** Prevent borrowing a model from a temporary report owner. */
    [[nodiscard]] const CompiledElectricalModel *model() const && = delete;

    /** Return one exact-Part/model coverage record for every input occurrence. */
    [[nodiscard]] const std::vector<DcOccurrenceCoverage> &coverage() const noexcept {
        return assessment_.coverage();
    }

    /** Return the exact immutable input identity assessed and compiled. */
    [[nodiscard]] const DcInputIdentity &input() const noexcept { return assessment_.input(); }

    /** Return request-local plus topology compilation diagnostics in deterministic order. */
    [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept {
        return diagnostics_;
    }

  private:
    DcRequestAssessment assessment_;
    std::vector<Diagnostic> diagnostics_;
    std::optional<CompiledElectricalModel> model_;
};

/** Compile one immutable S1 request without a second Circuit/resolver or numerical solve. */
[[nodiscard]] ElectricalCompileReport compile_electrical(const DcRequest &request);

} // namespace volt
