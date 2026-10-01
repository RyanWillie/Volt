"""Immutable native inputs and bounded requests for DC analysis consumers."""

from __future__ import annotations

from . import _volt

ANALYSIS_DIAGNOSTIC_CODES = frozenset(_volt.analysis_diagnostic_codes())

from .electrical import (ElectricalRequestKey, ElectricalSourceKey, ElectricalProbeKey,
                         ElectricalInputIdentity, ElectricalInput, ElectricalNetRef,
                         ElectricalOccurrenceRef, ElectricalNetPair, prepare_electrical_input)

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


def DcVoltageSource(key, from_, to, value):
    """Construct a native ideal voltage source over an ordered net pair."""
    return _volt._DcVoltageSource(key, ElectricalNetPair(from_, to), value)


def DcCurrentSource(key, from_, to, value):
    """Construct a native ideal current source over an ordered net pair."""
    return _volt._DcCurrentSource(key, ElectricalNetPair(from_, to), value)


def DcVoltageProbe(key, from_, to):
    """Construct a native voltage probe over an ordered net pair."""
    return _volt._DcVoltageProbe(key, ElectricalNetPair(from_, to))
