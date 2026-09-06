"""Immutable native inputs and bounded requests for DC analysis consumers."""

from __future__ import annotations

from . import _volt

ANALYSIS_DIAGNOSTIC_CODES = frozenset(_volt.analysis_diagnostic_codes())

DcRequestKey = _volt.DcRequestKey
DcSourceKey = _volt.DcSourceKey
DcProbeKey = _volt.DcProbeKey
DcInputIdentity = _volt.DcInputIdentity
DcInput = _volt.DcInput
DcNetRef = _volt.DcNetRef
DcOccurrenceRef = _volt.DcOccurrenceRef
DcNetPair = _volt.DcNetPair
DcSourceCurrentProbe = _volt.DcSourceCurrentProbe
DcModelElementCurrentProbe = _volt.DcModelElementCurrentProbe
DcNonElectricalExclusion = _volt.DcNonElectricalExclusion
DcOutsideAnalysisExclusion = _volt.DcOutsideAnalysisExclusion
DcReplacedByStimulusExclusion = _volt.DcReplacedByStimulusExclusion
DcOccurrenceExclusion = _volt.DcOccurrenceExclusion
DcRequest = _volt.DcRequest
DcCoverageStatus = _volt.DcCoverageStatus
DcOccurrenceCoverage = _volt.DcOccurrenceCoverage
DcRequestAssessment = _volt.DcRequestAssessment
assess_dc_request = _volt.assess_dc_request


def prepare_dc_input(design):
    """Capture one exact immutable native input from an authoring Design."""
    return _volt.prepare_dc_input(design._circuit)


def DcVoltageSource(key, from_, to, value):
    """Construct a native ideal voltage source over an ordered net pair."""
    return _volt._DcVoltageSource(key, DcNetPair(from_, to), value)


def DcCurrentSource(key, from_, to, value):
    """Construct a native ideal current source over an ordered net pair."""
    return _volt._DcCurrentSource(key, DcNetPair(from_, to), value)


def DcVoltageProbe(key, from_, to):
    """Construct a native voltage probe over an ordered net pair."""
    return _volt._DcVoltageProbe(key, DcNetPair(from_, to))
