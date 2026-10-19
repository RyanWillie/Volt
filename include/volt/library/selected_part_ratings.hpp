#pragma once

#include <optional>
#include <string>
#include <vector>

#include <volt/circuit/electrical/records.hpp>
#include <volt/circuit/parts/selected_part.hpp>
#include <volt/core/diagnostics.hpp>

namespace volt {
/** Explicit coverage of one exact selected-part rating obligation. */
enum class SelectedPartRatingState { Pass, Failed, NotEvaluated, NotApplicable };

/** One finite Voltage/Current comparison; reporting tuples never become handles. */
struct SelectedPartRatingObservation {
    /** Exact ordered participants and oriented domain. */
    std::vector<EntityRef> entities;
    /** Voltage or Current, in native SI units. */
    ElectricalObservable observable;
    /** Finite compared value. */
    double value;
    /** Known inclusive lower limit. */
    std::optional<double> minimum;
    /** Known inclusive upper limit. */
    std::optional<double> maximum;
    /** Exact native comparison rule. */
    std::string rule;
    /** Outcome of this comparison. */
    SelectedPartRatingState state;
};

/** Owned audit of one component gap or canonical record semantic-key group. */
struct SelectedPartRatingSubject {
    /** Artifact-local occurrence. */
    ComponentId component;
    /** Complete exact selection, absent only for an unselected occurrence. */
    std::optional<LibraryPartRef> selection;
    /** Canonical group identity; absent for a component-level gap. */
    std::optional<ElectricalSemanticKey> key;
    /** All original records in this group, including values, conditions and evidence. */
    std::optional<ElectricalRecordSet> records;
    /** Closed native obligation rule, including explicit input/unsupported rules. */
    std::string rule;
    /** Native coverage state. */
    SelectedPartRatingState state;
    /** Deterministic missing-input or checked-result explanation. */
    std::string reason;
    /** Sorted unique reporting scope. */
    std::vector<EntityRef> entities;
    /** Actual supported comparisons in native source/domain order. */
    std::vector<SelectedPartRatingObservation> observations;
};

/** Native owned evidence, delivered only through const manufacturing-result access. */
struct SelectedPartRatings {
    /** Complete occurrence and semantic-group inventory. */
    std::vector<SelectedPartRatingSubject> subjects;
    /** Existing native diagnostic payloads for supported comparisons. */
    DiagnosticReport diagnostics;
};
} // namespace volt
