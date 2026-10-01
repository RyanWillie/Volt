#pragma once

#include <compare>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <volt/core/diagnostics.hpp>
#include <volt/core/quantities.hpp>
#include <volt/electrical/electrical_request.hpp>
#include <volt/electrical/passive_model.hpp>

namespace volt {

/** Independent source imposing V(from)-V(to)=value. */
class DcVoltageSource {
  public:
    /** Construct a finite voltage source over two distinct authored nets. */
    DcVoltageSource(ElectricalSourceKey key, ElectricalNetPair nets, Quantity value);

    /** Return the request-local source identity. */
    [[nodiscard]] const ElectricalSourceKey &key() const noexcept { return key_; }

    /** Return the ordered source terminals. */
    [[nodiscard]] const ElectricalNetPair &nets() const noexcept { return nets_; }

    /** Return the finite voltage value in canonical SI form. */
    [[nodiscard]] const Quantity &value() const noexcept { return value_; }

  private:
    ElectricalSourceKey key_;
    ElectricalNetPair nets_;
    Quantity value_;
};

/** Independent source prescribing positive current from from-net to to-net. */
class DcCurrentSource {
  public:
    /** Construct a finite current source over two distinct authored nets. */
    DcCurrentSource(ElectricalSourceKey key, ElectricalNetPair nets, Quantity value);

    /** Return the request-local source identity. */
    [[nodiscard]] const ElectricalSourceKey &key() const noexcept { return key_; }

    /** Return the ordered source terminals. */
    [[nodiscard]] const ElectricalNetPair &nets() const noexcept { return nets_; }

    /** Return the finite current value in canonical SI form. */
    [[nodiscard]] const Quantity &value() const noexcept { return value_; }

  private:
    ElectricalSourceKey key_;
    ElectricalNetPair nets_;
    Quantity value_;
};

/** Closed independent DC source vocabulary. */
using DcSource = std::variant<DcVoltageSource, DcCurrentSource>;

/** Voltage observation V(from)-V(to), including a same-net zero observation. */
class DcVoltageProbe {
  public:
    /** Construct an oriented voltage probe over one exact input. */
    DcVoltageProbe(ElectricalProbeKey key, ElectricalNetPair nets)
        : key_{std::move(key)}, nets_{std::move(nets)} {}

    /** Return the request-local observation identity. */
    [[nodiscard]] const ElectricalProbeKey &key() const noexcept { return key_; }

    /** Return the ordered observed nets. */
    [[nodiscard]] const ElectricalNetPair &nets() const noexcept { return nets_; }

  private:
    ElectricalProbeKey key_;
    ElectricalNetPair nets_;
};

/** Current observation using an independent source's stored positive orientation. */
class DcSourceCurrentProbe {
  public:
    /** Construct a current probe targeting one request-local source. */
    DcSourceCurrentProbe(ElectricalProbeKey key, ElectricalSourceKey source)
        : key_{std::move(key)}, source_{std::move(source)} {}

    /** Return the request-local observation identity. */
    [[nodiscard]] const ElectricalProbeKey &key() const noexcept { return key_; }

    /** Return the observed source identity. */
    [[nodiscard]] const ElectricalSourceKey &source() const noexcept { return source_; }

  private:
    ElectricalProbeKey key_;
    ElectricalSourceKey source_;
};

/** Current observation using one exact occurrence/model-element orientation. */
class DcModelElementCurrentProbe {
  public:
    /** Construct a current probe targeting one resolved occurrence-local model element. */
    DcModelElementCurrentProbe(ElectricalProbeKey key, ElectricalOccurrenceRef occurrence,
                               ModelElementKey element)
        : key_{std::move(key)}, occurrence_{std::move(occurrence)}, element_{std::move(element)} {}

    /** Return the request-local observation identity. */
    [[nodiscard]] const ElectricalProbeKey &key() const noexcept { return key_; }

    /** Return the exact-input-bound observed occurrence. */
    [[nodiscard]] const ElectricalOccurrenceRef &occurrence() const noexcept { return occurrence_; }

    /** Return the observed model-local element key. */
    [[nodiscard]] const ModelElementKey &element() const noexcept { return element_; }

  private:
    ElectricalProbeKey key_;
    ElectricalOccurrenceRef occurrence_;
    ModelElementKey element_;
};

/** Closed DC observation vocabulary. */
using DcProbe = std::variant<DcVoltageProbe, DcSourceCurrentProbe, DcModelElementCurrentProbe>;

/** Explicit assertion that one occurrence is non-electrical for this request. */
struct DcNonElectricalExclusion {
    /** Compare equal non-electrical assertions. */
    [[nodiscard]] bool operator==(const DcNonElectricalExclusion &) const noexcept = default;
};

/** Explicit assertion that one occurrence is outside this analysis. */
struct DcOutsideAnalysisExclusion {
    /** Compare equal outside-analysis assertions. */
    [[nodiscard]] bool operator==(const DcOutsideAnalysisExclusion &) const noexcept = default;
};

/** Explicit replacement of one whole occurrence by named request-local stimuli. */
class DcReplacedByStimulusExclusion {
  public:
    /** Construct a replacement from one or more unique source keys. */
    explicit DcReplacedByStimulusExclusion(std::vector<ElectricalSourceKey> sources);

    /** Return canonical replacement source identities. */
    [[nodiscard]] const std::vector<ElectricalSourceKey> &sources() const noexcept {
        return sources_;
    }

    /** Compare complete replacement-source sets. */
    [[nodiscard]] bool operator==(const DcReplacedByStimulusExclusion &) const noexcept = default;

  private:
    std::vector<ElectricalSourceKey> sources_;
};

/** Closed whole-occurrence exclusion reason vocabulary. */
using DcExclusionReason = std::variant<DcNonElectricalExclusion, DcOutsideAnalysisExclusion,
                                       DcReplacedByStimulusExclusion>;

/** One whole-occurrence participation override local to a DC request. */
class DcOccurrenceExclusion {
  public:
    /** Exclude exactly one input-bound occurrence for one typed reason. */
    DcOccurrenceExclusion(ElectricalOccurrenceRef occurrence, DcExclusionReason reason)
        : occurrence_{std::move(occurrence)}, reason_{std::move(reason)} {}

    /** Return the exact-input-bound excluded occurrence. */
    [[nodiscard]] const ElectricalOccurrenceRef &occurrence() const noexcept { return occurrence_; }

    /** Return the explicit typed exclusion reason. */
    [[nodiscard]] const DcExclusionReason &reason() const noexcept { return reason_; }

  private:
    ElectricalOccurrenceRef occurrence_;
    DcExclusionReason reason_;
};

/** Immutable DC request retaining its exact owning input snapshot. */
class DcRequest {
  public:
    /** Validate, normalize, and retain one complete request against an exact input. */
    DcRequest(ElectricalRequestKey key, const ElectricalInput &input,
              std::optional<ElectricalNetRef> reference, std::vector<DcSource> sources = {},
              std::vector<DcProbe> probes = {}, std::vector<DcOccurrenceExclusion> exclusions = {});

    /** Return the stable request identity. */
    [[nodiscard]] const ElectricalRequestKey &key() const noexcept { return key_; }

    /** Return the immutable owning exact input snapshot. */
    [[nodiscard]] const ElectricalInput &input() const noexcept { return input_; }

    /** Return the explicit reference net, or absence for an incomplete request. */
    [[nodiscard]] const std::optional<ElectricalNetRef> &reference() const noexcept {
        return reference_;
    }

    /** Return independent sources in canonical key order. */
    [[nodiscard]] const std::vector<DcSource> &sources() const noexcept { return sources_; }

    /** Return requested observations in canonical key order. */
    [[nodiscard]] const std::vector<DcProbe> &probes() const noexcept { return probes_; }

    /** Return whole-occurrence exclusions in canonical occurrence order. */
    [[nodiscard]] const std::vector<DcOccurrenceExclusion> &exclusions() const noexcept {
        return exclusions_;
    }

  private:
    ElectricalRequestKey key_;
    ElectricalInput input_;
    std::optional<ElectricalNetRef> reference_;
    std::vector<DcSource> sources_;
    std::vector<DcProbe> probes_;
    std::vector<DcOccurrenceExclusion> exclusions_;
};

/** Request-relative exact-Part/model coverage state for one Circuit occurrence. */
enum class DcCoverageStatus {
    /** The selected exact Part resolves to a supported R/C/L model. */
    Supported,
    /** The whole occurrence is explicitly excluded from this request. */
    Excluded,
    /** The occurrence has no exact Part selection. */
    Unselected,
    /** The exact selected Part is absent from the supplied resolver. */
    Unresolved,
    /** The exact selected Part resolves but has no electrical model. */
    ModelAbsent,
    /** Reserved for a valid future model that the bounded consumer cannot lower. */
    Unsupported,
};

/** Complete coverage outcome for one occurrence, retaining selection and exclusion. */
class DcOccurrenceCoverage {
  public:
    /** Construct and validate one status-shaped occurrence coverage record. */
    DcOccurrenceCoverage(ElectricalOccurrenceRef occurrence, DcCoverageStatus status,
                         std::optional<LibraryPartRef> selected_part,
                         std::optional<DcOccurrenceExclusion> exclusion);

    /** Return the exact-input-bound occurrence. */
    [[nodiscard]] const ElectricalOccurrenceRef &occurrence() const noexcept { return occurrence_; }

    /** Return this occurrence's request-relative coverage status. */
    [[nodiscard]] DcCoverageStatus status() const noexcept { return status_; }

    /** Return the exact Part selection when one exists. */
    [[nodiscard]] const std::optional<LibraryPartRef> &selected_part() const noexcept {
        return selected_part_;
    }

    /** Return the complete exclusion when this occurrence is excluded. */
    [[nodiscard]] const std::optional<DcOccurrenceExclusion> &exclusion() const noexcept {
        return exclusion_;
    }

  private:
    ElectricalOccurrenceRef occurrence_;
    DcCoverageStatus status_;
    std::optional<LibraryPartRef> selected_part_;
    std::optional<DcOccurrenceExclusion> exclusion_;
};

/** Immutable S1 request-local validation and exact Part/model coverage report. */
class DcRequestAssessment {
  public:
    /** Assess one immutable request without resolving hierarchy or numerical topology. */
    explicit DcRequestAssessment(const DcRequest &request);

    /** Return whether reference, excitation, and required occurrence coverage are complete. */
    [[nodiscard]] bool complete() const noexcept { return complete_; }

    /** Return the exact input identity assessed by this report. */
    [[nodiscard]] const ElectricalInputIdentity &input() const noexcept { return input_; }

    /** Return one deterministic coverage record for every Circuit occurrence. */
    [[nodiscard]] const std::vector<DcOccurrenceCoverage> &coverage() const noexcept {
        return coverage_;
    }

    /** Return deterministic request-local and incomplete-coverage diagnostics. */
    [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept {
        return diagnostics_;
    }

  private:
    ElectricalInputIdentity input_;
    std::vector<DcOccurrenceCoverage> coverage_;
    std::vector<Diagnostic> diagnostics_;
    bool complete_;
};

/** Assess request-local excitation and exact Part/model coverage without resolving topology. */
[[nodiscard]] DcRequestAssessment assess_dc_request(const DcRequest &request);

} // namespace volt
