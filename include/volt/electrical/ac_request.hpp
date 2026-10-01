#pragma once

#include <complex>
#include <volt/electrical/dc_request.hpp>

namespace volt {

/** Canonical materialized positive ascending SI frequency samples. */
class AcFrequencySweep {
  public:
    /** Validate a nonempty explicit SI frequency list without reordering samples. */
    explicit AcFrequencySweep(std::vector<Quantity> frequencies);
    /** Materialize an inclusive ascending linear sweep with exact endpoints. */
    [[nodiscard]] static AcFrequencySweep linear(Quantity start, Quantity stop, std::size_t count);
    /** Materialize an inclusive ascending logarithmic sweep with exact endpoints. */
    [[nodiscard]] static AcFrequencySweep logarithmic(Quantity start, Quantity stop,
                                                      std::size_t count);

    /** Return the canonical materialized ascending SI Hz samples. */
    [[nodiscard]] const std::vector<Quantity> &frequencies() const noexcept { return frequencies_; }

  private:
    std::vector<Quantity> frequencies_;
};

/** Independent peak-amplitude phasor with time convention Re{phasor exp(j omega t)}. */
template <UnitDimension Dimension> class AcIndependentSource {
  public:
    /** Validate one distinct oriented source with a non-negative amplitude and finite radian phase.
     */
    AcIndependentSource(ElectricalSourceKey key, ElectricalNetPair nets, Quantity amplitude,
                        double phase);

    /** Return the request-local source identity. */
    [[nodiscard]] const ElectricalSourceKey &key() const noexcept { return key_; }

    /** Return the ordered positive-voltage and outgoing-current endpoints. */
    [[nodiscard]] const ElectricalNetPair &nets() const noexcept { return nets_; }

    /** Return the non-negative peak amplitude in volts or amperes. */
    [[nodiscard]] const Quantity &amplitude() const noexcept { return amplitude_; }

    /** Return finite phase radians, canonically zero for zero amplitude. */
    [[nodiscard]] double phase() const noexcept { return phase_; }

    /** Return A exp(j phase) under the exp(j omega t) time convention. */
    [[nodiscard]] std::complex<double> phasor() const noexcept {
        return std::polar(amplitude_.value(), phase_);
    }

  private:
    ElectricalSourceKey key_;
    ElectricalNetPair nets_;
    Quantity amplitude_;
    double phase_;
};

/** Independent peak-voltage source with current positive from from-net to to-net. */
using AcVoltageSource = AcIndependentSource<UnitDimension::Voltage>;
/** Independent peak-current source positive from from-net to to-net. */
using AcCurrentSource = AcIndependentSource<UnitDimension::Current>;
/** Closed independent AC stimulus vocabulary. */
using AcSource = std::variant<AcVoltageSource, AcCurrentSource>;

/** Same-dimension complex ratio of two primitive request probes. */
class AcGainProbe {
  public:
    /** Define a ratio whose primitive targets are validated by the owning request. */
    AcGainProbe(ElectricalProbeKey key, ElectricalProbeKey numerator,
                ElectricalProbeKey denominator)
        : key_{std::move(key)}, numerator_{std::move(numerator)},
          denominator_{std::move(denominator)} {}

    /** Return the request-local observation identity. */
    [[nodiscard]] const ElectricalProbeKey &key() const noexcept { return key_; }

    /** Return the primitive numerator probe identity. */
    [[nodiscard]] const ElectricalProbeKey &numerator() const noexcept { return numerator_; }

    /** Return the primitive denominator probe identity. */
    [[nodiscard]] const ElectricalProbeKey &denominator() const noexcept { return denominator_; }

  private:
    ElectricalProbeKey key_;
    ElectricalProbeKey numerator_;
    ElectricalProbeKey denominator_;
};

/** Driving-point impedance V(from)-V(to) divided by -I(test source). */
class AcImpedanceProbe {
  public:
    /** Define an oriented port and explicit current test source for request validation. */
    AcImpedanceProbe(ElectricalProbeKey key, ElectricalNetPair nets, ElectricalSourceKey source)
        : key_{std::move(key)}, nets_{std::move(nets)}, source_{std::move(source)} {}

    /** Return the request-local observation identity. */
    [[nodiscard]] const ElectricalProbeKey &key() const noexcept { return key_; }

    /** Return the ordered positive-voltage and outgoing-current endpoints. */
    [[nodiscard]] const ElectricalNetPair &nets() const noexcept { return nets_; }

    /** Return the current test source identity. */
    [[nodiscard]] const ElectricalSourceKey &source() const noexcept { return source_; }

  private:
    ElectricalProbeKey key_;
    ElectricalNetPair nets_;
    ElectricalSourceKey source_;
};

/** Immutable AC analysis request bound to the existing exact electrical input. */
class AcRequest {
  public:
    /** Validate and retain the exact input, sweep, keyed stimuli and measurements. */
    AcRequest(ElectricalRequestKey key, const ElectricalInput &input,
              std::optional<ElectricalNetRef> reference, AcFrequencySweep sweep,
              std::vector<AcSource> sources = {}, std::vector<DcProbe> probes = {},
              std::vector<DcOccurrenceExclusion> exclusions = {},
              std::vector<AcGainProbe> gains = {}, std::vector<AcImpedanceProbe> impedances = {});

    /** Return the stable analysis request identity. */
    [[nodiscard]] const ElectricalRequestKey &key() const noexcept { return key_; }

    /** Return the immutable exact logical and selected-Part input. */
    [[nodiscard]] const ElectricalInput &input() const noexcept { return input_; }

    /** Return the explicit reference or absence for an incomplete request. */
    [[nodiscard]] const std::optional<ElectricalNetRef> &reference() const noexcept {
        return reference_;
    }

    /** Return the canonical frequency sweep. */
    [[nodiscard]] const AcFrequencySweep &sweep() const noexcept { return sweep_; }

    /** Return the canonical materialized ascending SI Hz samples. */
    [[nodiscard]] const std::vector<Quantity> &frequencies() const noexcept {
        return sweep_.frequencies();
    }

    /** Return explicit source assignments in canonical key order. */
    [[nodiscard]] const std::vector<AcSource> &sources() const noexcept { return sources_; }

    /** Return primitive measurements in canonical key order. */
    [[nodiscard]] const std::vector<DcProbe> &probes() const noexcept { return probes_; }

    /** Return typed participation exclusions in canonical occurrence order. */
    [[nodiscard]] const std::vector<DcOccurrenceExclusion> &exclusions() const noexcept {
        return exclusions_;
    }

    /** Return derived gain definitions in canonical key order. */
    [[nodiscard]] const std::vector<AcGainProbe> &gains() const noexcept { return gains_; }

    /** Return derived impedance definitions in canonical key order. */
    [[nodiscard]] const std::vector<AcImpedanceProbe> &impedances() const noexcept {
        return impedances_;
    }

  private:
    ElectricalRequestKey key_;
    ElectricalInput input_;
    std::optional<ElectricalNetRef> reference_;
    AcFrequencySweep sweep_;
    std::vector<AcSource> sources_;
    std::vector<DcProbe> probes_;
    std::vector<DcOccurrenceExclusion> exclusions_;
    std::vector<AcGainProbe> gains_;
    std::vector<AcImpedanceProbe> impedances_;
};

/** Native request coverage without topology resolution or numerical execution. */
class AcRequestAssessment {
  public:
    /** Assess native reference, assignment, exact-Part coverage and measurement readiness. */
    explicit AcRequestAssessment(const AcRequest &request);

    /** Return whether request readiness and required coverage have no errors. */
    [[nodiscard]] bool complete() const noexcept { return complete_; }

    /** Return the assessed exact input identity. */
    [[nodiscard]] const ElectricalInputIdentity &input() const noexcept { return input_; }

    /** Return one deterministic coverage record per occurrence. */
    [[nodiscard]] const std::vector<DcOccurrenceCoverage> &coverage() const noexcept {
        return coverage_;
    }

    /** Return request readiness and coverage findings without numerical execution. */
    [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept {
        return diagnostics_;
    }

  private:
    ElectricalInputIdentity input_;
    std::vector<DcOccurrenceCoverage> coverage_;
    std::vector<Diagnostic> diagnostics_;
    bool complete_;
};

/** Assess an immutable AC request without resolving topology or executing a solver. */
[[nodiscard]] AcRequestAssessment assess_ac_request(const AcRequest &request);

} // namespace volt
