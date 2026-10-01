#pragma once

#include <initializer_list>
#include <volt/electrical/dc_request.hpp>

namespace volt {
class DcSolution;
class DcSolveReport;

/** Materialized SI output samples beginning at zero with a positive finite horizon. */
class TransientTimeGrid {
  public:
    /** Validate at least two strictly ascending typed seconds beginning at zero. */
    explicit TransientTimeGrid(std::vector<Quantity> times);
    /** Materialize inclusive uniform samples and reject rounded duplicates. */
    [[nodiscard]] static TransientTimeGrid uniform(Quantity horizon, std::size_t count);

    /** Return the canonical requested times without interpolation. */
    [[nodiscard]] const std::vector<Quantity> &times() const noexcept { return times_; }

    /** Return the positive finite final requested time. */
    [[nodiscard]] const Quantity &horizon() const noexcept { return times_.back(); }

  private:
    std::vector<Quantity> times_;
};

/** One native continuous PWL corner, using typed SI time and source value. */
struct TransientWaveformKnot {
    /** Finite nonnegative SI seconds. */
    Quantity time;
    /** Signed finite voltage or current at this corner. */
    Quantity value;
};

/** Closed constant or continuous PWL source law; final PWL value is held. */
class TransientWaveform {
  public:
    /** Construct one finite signed voltage or current held for all nonnegative times. */
    explicit TransientWaveform(Quantity constant);
    /** Validate two or more same-dimension strictly ascending knots beginning at zero. */
    explicit TransientWaveform(std::vector<TransientWaveformKnot> knots);

    /** Return whether this law is a constant rather than a PWL. */
    [[nodiscard]] bool constant() const noexcept { return knots_.size() == 1; }

    /** Return the source value dimension. */
    [[nodiscard]] UnitDimension dimension() const noexcept {
        return knots_.front().value.dimension();
    }

    /** Return canonical corners, including one zero-time knot for a constant. */
    [[nodiscard]] const std::vector<TransientWaveformKnot> &knots() const noexcept {
        return knots_;
    }

    /** Evaluate typed nonnegative seconds using native linear interpolation and final hold. */
    [[nodiscard]] Quantity value_at(Quantity time) const;

  private:
    std::vector<TransientWaveformKnot> knots_;
};

/** Oriented independent source retaining its native waveform meaning. */
template <UnitDimension Dimension> class TransientIndependentSource {
  public:
    /** Validate distinct exact-input terminals and waveform dimension. */
    TransientIndependentSource(ElectricalSourceKey key, ElectricalNetPair nets,
                               TransientWaveform waveform);

    /** Return the request-local independent source identity. */
    [[nodiscard]] const ElectricalSourceKey &key() const noexcept { return key_; }

    /** Return positive-voltage or outgoing-current endpoints. */
    [[nodiscard]] const ElectricalNetPair &nets() const noexcept { return nets_; }

    /** Return the immutable native source law. */
    [[nodiscard]] const TransientWaveform &waveform() const noexcept { return waveform_; }

  private:
    ElectricalSourceKey key_;
    ElectricalNetPair nets_;
    TransientWaveform waveform_;
};

/** Independent voltage source imposing V(from)-V(to)=waveform(time). */
using TransientVoltageSource = TransientIndependentSource<UnitDimension::Voltage>;
/** Independent current source prescribing current from from-net to to-net. */
using TransientCurrentSource = TransientIndependentSource<UnitDimension::Current>;
/** Closed native transient stimulus vocabulary. */
using TransientSource = std::variant<TransientVoltageSource, TransientCurrentSource>;

/** Stored coordinate using the authored element orientation. */
enum class TransientStorageKind {
    /** Capacitor V(from)-V(to). */
    CapacitorVoltage,
    /** Inductor current from from-node to to-node. */
    InductorCurrent
};

/** Provenance of an explicit copy from an already successful DC result. */
struct TransientDcProvenance {
    /** Exact logical and selected-Part identity of the already successful DC result. */
    ElectricalInputIdentity input;
    /** Complete DC model/backend/numerical-policy identity. */
    ContentHash analysis_identity;
    /** Identity of the participating/excluded occurrence set under the exact input. */
    ContentHash participation_identity;
    /** Complete number of participating capacitor and inductor coordinates. */
    std::size_t storage_count;
};

/** Durable storage target; a compiled branch index is never an authored handle. */
class TransientInitialState {
  public:
    /** Validate storage kind/dimension and retain its exact durable model target. */
    TransientInitialState(ElectricalOccurrenceRef occurrence, LibraryPartRef part,
                          ModelElementKey element, TransientStorageKind kind, Quantity value);

    /** Return the exact-input-bound storage owner. */
    [[nodiscard]] const ElectricalOccurrenceRef &occurrence() const noexcept { return occurrence_; }

    /** Return the complete selected Part identity. */
    [[nodiscard]] const LibraryPartRef &part() const noexcept { return part_; }

    /** Return the model-local storage element identity. */
    [[nodiscard]] const ModelElementKey &element() const noexcept { return element_; }

    /** Return the native capacitor-voltage or inductor-current coordinate kind. */
    [[nodiscard]] TransientStorageKind kind() const noexcept { return kind_; }

    /** Return the finite initial coordinate in its authored positive orientation. */
    [[nodiscard]] const Quantity &value() const noexcept { return value_; }

  private:
    ElectricalOccurrenceRef occurrence_;
    LibraryPartRef part_;
    ModelElementKey element_;
    TransientStorageKind kind_;
    Quantity value_;
};

/** Complete authored storage collection or an explicit copy of one immutable DC result. */
class TransientInitialConditions {
  public:
    /** Normalize unique durable targets and retain collection-level DC provenance, even if empty.
     */
    TransientInitialConditions(std::vector<TransientInitialState> entries = {},
                               std::optional<TransientDcProvenance> dc_provenance = std::nullopt);

    /** Ergonomic authored collection; provenance requires the explicit vector constructor. */
    TransientInitialConditions(std::initializer_list<TransientInitialState> entries)
        : TransientInitialConditions{std::vector<TransientInitialState>{entries}} {}

    /** Return immutable canonical durable storage targets. */
    [[nodiscard]] const std::vector<TransientInitialState> &entries() const noexcept {
        return entries_;
    }

    /** Return provenance of the complete copied result, including its exact input. */
    [[nodiscard]] const std::optional<TransientDcProvenance> &dc_provenance() const noexcept {
        return dc_provenance_;
    }

  private:
    std::vector<TransientInitialState> entries_;
    std::optional<TransientDcProvenance> dc_provenance_;
};

/** Immutable exact-input transient request; missing storage is incomplete assessment. */
class TransientRequest {
  public:
    /** Normalize immutable stimuli/observations/participation and validate storage origins. */
    TransientRequest(ElectricalRequestKey key, const ElectricalInput &input,
                     std::optional<ElectricalNetRef> reference, TransientTimeGrid grid,
                     std::vector<TransientSource> sources = {}, std::vector<DcProbe> probes = {},
                     std::vector<DcOccurrenceExclusion> exclusions = {},
                     TransientInitialConditions initial_conditions = {});

    /** Return the stable analysis request identity. */
    [[nodiscard]] const ElectricalRequestKey &key() const noexcept { return key_; }

    /** Return the immutable exact logical and selected-Part closure. */
    [[nodiscard]] const ElectricalInput &input() const noexcept { return input_; }

    /** Return the explicit reference or absence for incomplete assessment. */
    [[nodiscard]] const std::optional<ElectricalNetRef> &reference() const noexcept {
        return reference_;
    }

    /** Return the materialized native output grid. */
    [[nodiscard]] const TransientTimeGrid &grid() const noexcept { return grid_; }

    /** Return requested output times; waveform corners need not be output samples. */
    [[nodiscard]] const std::vector<Quantity> &times() const noexcept { return grid_.times(); }

    /** Return explicit keyed native waveforms, possibly empty for free response. */
    [[nodiscard]] const std::vector<TransientSource> &sources() const noexcept { return sources_; }

    /** Return primitive measurements with retained orientations. */
    [[nodiscard]] const std::vector<DcProbe> &probes() const noexcept { return probes_; }

    /** Return complete typed participation exclusions. */
    [[nodiscard]] const std::vector<DcOccurrenceExclusion> &exclusions() const noexcept {
        return exclusions_;
    }

    /** Return canonical exact storage coordinates; missing authored values remain incomplete. */
    [[nodiscard]] const std::vector<TransientInitialState> &initial_state() const noexcept {
        return initial_conditions_.entries();
    }

    /** Return the complete initial-state collection and explicit DC-copy provenance. */
    [[nodiscard]] const TransientInitialConditions &initial_conditions() const noexcept {
        return initial_conditions_;
    }

  private:
    ElectricalRequestKey key_;
    ElectricalInput input_;
    std::optional<ElectricalNetRef> reference_;
    TransientTimeGrid grid_;
    std::vector<TransientSource> sources_;
    std::vector<DcProbe> probes_;
    std::vector<DcOccurrenceExclusion> exclusions_;
    TransientInitialConditions initial_conditions_;
};

/** Native exact-Part coverage, reference, waveform-constraint and storage assessment. */
class TransientRequestAssessment {
  public:
    /** Assess immutable request readiness without compiling or solving. */
    explicit TransientRequestAssessment(const TransientRequest &request);

    /** Return whether required reference/model/storage coverage has no errors. */
    [[nodiscard]] bool complete() const noexcept { return complete_; }

    /** Return the exact input identity assessed. */
    [[nodiscard]] const ElectricalInputIdentity &input() const noexcept { return input_; }

    /** Return one deterministic coverage record per occurrence. */
    [[nodiscard]] const std::vector<DcOccurrenceCoverage> &coverage() const noexcept {
        return coverage_;
    }

    /** Return native request readiness findings with exact entity references. */
    [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept {
        return diagnostics_;
    }

  private:
    ElectricalInputIdentity input_;
    std::vector<DcOccurrenceCoverage> coverage_;
    std::vector<Diagnostic> diagnostics_;
    bool complete_ = false;
};

/** Assess native request completeness without numerical execution. */
[[nodiscard]] TransientRequestAssessment assess_transient_request(const TransientRequest &request);
/** Copy every oriented storage coordinate with exact identity; performs no solve. */
[[nodiscard]] TransientInitialConditions initial_state_from(const DcSolution &solution);
/** Reject unsuccessful reports rather than copying partial observations. */
[[nodiscard]] TransientInitialConditions initial_state_from(const DcSolveReport &report);
} // namespace volt
