#pragma once

#include <compare>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <volt/core/diagnostics.hpp>
#include <volt/core/quantities.hpp>
#include <volt/electrical/dc_input.hpp>
#include <volt/electrical/passive_model.hpp>

namespace volt {

/** Strongly typed non-empty identity local to one DC request collection. */
template <typename Tag> class DcRequestKeyValue {
  public:
    /** Construct a stable request-local key. */
    explicit DcRequestKeyValue(std::string value);

    /** Return the stable key spelling. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Compare keys in the same request-local collection. */
    [[nodiscard]] bool operator==(const DcRequestKeyValue &) const noexcept = default;
    /** Order keys for deterministic request normalization. */
    [[nodiscard]] std::strong_ordering
    operator<=>(const DcRequestKeyValue &) const noexcept = default;

  private:
    std::string value_;
};

/** Type tag for request identities. */
struct DcRequestKeyTag;
/** Type tag for independent-source identities. */
struct DcSourceKeyTag;
/** Type tag for probe identities. */
struct DcProbeKeyTag;
/** Stable identity of one DC request. */
using DcRequestKey = DcRequestKeyValue<DcRequestKeyTag>;
/** Stable identity of one independent source within a request. */
using DcSourceKey = DcRequestKeyValue<DcSourceKeyTag>;
/** Stable identity of one requested observation within a request. */
using DcProbeKey = DcRequestKeyValue<DcProbeKeyTag>;

/** One ordered pair of exact-input logical nets. */
class DcNetPair {
  public:
    /** Construct an oriented pair from references to the same exact input. */
    DcNetPair(DcNetRef from, DcNetRef to);

    /** Return the positive-voltage or outgoing-current net. */
    [[nodiscard]] const DcNetRef &from() const noexcept { return from_; }

    /** Return the negative-voltage or incoming-current net. */
    [[nodiscard]] const DcNetRef &to() const noexcept { return to_; }

    /** Compare exact input, authored endpoints, and orientation. */
    [[nodiscard]] bool operator==(const DcNetPair &) const noexcept = default;

  private:
    DcNetRef from_;
    DcNetRef to_;
};

/** Independent source imposing V(from)-V(to)=value. */
class DcVoltageSource {
  public:
    /** Construct a finite voltage source over two distinct authored nets. */
    DcVoltageSource(DcSourceKey key, DcNetPair nets, Quantity value);

    /** Return the request-local source identity. */
    [[nodiscard]] const DcSourceKey &key() const noexcept { return key_; }

    /** Return the ordered source terminals. */
    [[nodiscard]] const DcNetPair &nets() const noexcept { return nets_; }

    /** Return the finite voltage value in canonical SI form. */
    [[nodiscard]] const Quantity &value() const noexcept { return value_; }

  private:
    DcSourceKey key_;
    DcNetPair nets_;
    Quantity value_;
};

/** Independent source prescribing positive current from from-net to to-net. */
class DcCurrentSource {
  public:
    /** Construct a finite current source over two distinct authored nets. */
    DcCurrentSource(DcSourceKey key, DcNetPair nets, Quantity value);

    /** Return the request-local source identity. */
    [[nodiscard]] const DcSourceKey &key() const noexcept { return key_; }

    /** Return the ordered source terminals. */
    [[nodiscard]] const DcNetPair &nets() const noexcept { return nets_; }

    /** Return the finite current value in canonical SI form. */
    [[nodiscard]] const Quantity &value() const noexcept { return value_; }

  private:
    DcSourceKey key_;
    DcNetPair nets_;
    Quantity value_;
};

/** Closed independent DC source vocabulary. */
using DcSource = std::variant<DcVoltageSource, DcCurrentSource>;

/** Voltage observation V(from)-V(to), including a same-net zero observation. */
class DcVoltageProbe {
  public:
    /** Construct an oriented voltage probe over one exact input. */
    DcVoltageProbe(DcProbeKey key, DcNetPair nets) : key_{std::move(key)}, nets_{std::move(nets)} {}

    /** Return the request-local observation identity. */
    [[nodiscard]] const DcProbeKey &key() const noexcept { return key_; }

    /** Return the ordered observed nets. */
    [[nodiscard]] const DcNetPair &nets() const noexcept { return nets_; }

  private:
    DcProbeKey key_;
    DcNetPair nets_;
};

/** Current observation using an independent source's stored positive orientation. */
class DcSourceCurrentProbe {
  public:
    /** Construct a current probe targeting one request-local source. */
    DcSourceCurrentProbe(DcProbeKey key, DcSourceKey source)
        : key_{std::move(key)}, source_{std::move(source)} {}

    /** Return the request-local observation identity. */
    [[nodiscard]] const DcProbeKey &key() const noexcept { return key_; }

    /** Return the observed source identity. */
    [[nodiscard]] const DcSourceKey &source() const noexcept { return source_; }

  private:
    DcProbeKey key_;
    DcSourceKey source_;
};

/** Current observation using one exact occurrence/model-element orientation. */
class DcModelElementCurrentProbe {
  public:
    /** Construct a current probe targeting one resolved occurrence-local model element. */
    DcModelElementCurrentProbe(DcProbeKey key, DcOccurrenceRef occurrence, ModelElementKey element)
        : key_{std::move(key)}, occurrence_{std::move(occurrence)}, element_{std::move(element)} {}

    /** Return the request-local observation identity. */
    [[nodiscard]] const DcProbeKey &key() const noexcept { return key_; }

    /** Return the exact-input-bound observed occurrence. */
    [[nodiscard]] const DcOccurrenceRef &occurrence() const noexcept { return occurrence_; }

    /** Return the observed model-local element key. */
    [[nodiscard]] const ModelElementKey &element() const noexcept { return element_; }

  private:
    DcProbeKey key_;
    DcOccurrenceRef occurrence_;
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
    explicit DcReplacedByStimulusExclusion(std::vector<DcSourceKey> sources);

    /** Return canonical replacement source identities. */
    [[nodiscard]] const std::vector<DcSourceKey> &sources() const noexcept { return sources_; }

    /** Compare complete replacement-source sets. */
    [[nodiscard]] bool operator==(const DcReplacedByStimulusExclusion &) const noexcept = default;

  private:
    std::vector<DcSourceKey> sources_;
};

/** Closed whole-occurrence exclusion reason vocabulary. */
using DcExclusionReason = std::variant<DcNonElectricalExclusion, DcOutsideAnalysisExclusion,
                                       DcReplacedByStimulusExclusion>;

/** One whole-occurrence participation override local to a DC request. */
class DcOccurrenceExclusion {
  public:
    /** Exclude exactly one input-bound occurrence for one typed reason. */
    DcOccurrenceExclusion(DcOccurrenceRef occurrence, DcExclusionReason reason)
        : occurrence_{std::move(occurrence)}, reason_{std::move(reason)} {}

    /** Return the exact-input-bound excluded occurrence. */
    [[nodiscard]] const DcOccurrenceRef &occurrence() const noexcept { return occurrence_; }

    /** Return the explicit typed exclusion reason. */
    [[nodiscard]] const DcExclusionReason &reason() const noexcept { return reason_; }

  private:
    DcOccurrenceRef occurrence_;
    DcExclusionReason reason_;
};

/** Immutable DC request retaining its exact owning input snapshot. */
class DcRequest {
  public:
    /** Validate, normalize, and retain one complete request against an exact input. */
    DcRequest(DcRequestKey key, const DcInput &input, std::optional<DcNetRef> reference,
              std::vector<DcSource> sources = {}, std::vector<DcProbe> probes = {},
              std::vector<DcOccurrenceExclusion> exclusions = {});

    /** Return the stable request identity. */
    [[nodiscard]] const DcRequestKey &key() const noexcept { return key_; }

    /** Return the immutable owning exact input snapshot. */
    [[nodiscard]] const DcInput &input() const noexcept { return input_; }

    /** Return the explicit reference net, or absence for an incomplete request. */
    [[nodiscard]] const std::optional<DcNetRef> &reference() const noexcept { return reference_; }

    /** Return independent sources in canonical key order. */
    [[nodiscard]] const std::vector<DcSource> &sources() const noexcept { return sources_; }

    /** Return requested observations in canonical key order. */
    [[nodiscard]] const std::vector<DcProbe> &probes() const noexcept { return probes_; }

    /** Return whole-occurrence exclusions in canonical occurrence order. */
    [[nodiscard]] const std::vector<DcOccurrenceExclusion> &exclusions() const noexcept {
        return exclusions_;
    }

  private:
    DcRequestKey key_;
    DcInput input_;
    std::optional<DcNetRef> reference_;
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
    DcOccurrenceCoverage(DcOccurrenceRef occurrence, DcCoverageStatus status,
                         std::optional<LibraryPartRef> selected_part,
                         std::optional<DcOccurrenceExclusion> exclusion);

    /** Return the exact-input-bound occurrence. */
    [[nodiscard]] const DcOccurrenceRef &occurrence() const noexcept { return occurrence_; }

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
    DcOccurrenceRef occurrence_;
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
    [[nodiscard]] const DcInputIdentity &input() const noexcept { return input_; }

    /** Return one deterministic coverage record for every Circuit occurrence. */
    [[nodiscard]] const std::vector<DcOccurrenceCoverage> &coverage() const noexcept {
        return coverage_;
    }

    /** Return deterministic request-local and incomplete-coverage diagnostics. */
    [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept {
        return diagnostics_;
    }

  private:
    DcInputIdentity input_;
    std::vector<DcOccurrenceCoverage> coverage_;
    std::vector<Diagnostic> diagnostics_;
    bool complete_;
};

/** Assess request-local excitation and exact Part/model coverage without resolving topology. */
[[nodiscard]] DcRequestAssessment assess_dc_request(const DcRequest &request);

} // namespace volt
