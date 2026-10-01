"""Immutable native transient requests; Python supplies only authoring syntax."""
from . import _volt
from .electrical import ElectricalNetPair

TransientTimeGrid = _volt.TransientTimeGrid
TransientWaveformKnot = _volt.TransientWaveformKnot
TransientWaveform = _volt.TransientWaveform
TransientStorageKind = _volt.TransientStorageKind
TransientDcProvenance = _volt.TransientDcProvenance
TransientInitialConditions = _volt.TransientInitialConditions
TransientInitialState = _volt.TransientInitialState
TransientRequest = _volt.TransientRequest
TransientRequestAssessment = _volt.TransientRequestAssessment
assess_transient_request = _volt.assess_transient_request
initial_state_from = _volt.initial_state_from

def TransientVoltageSource(key, from_, to, waveform):
    """Bind an oriented native continuous waveform to two exact input nets."""
    return _volt._TransientVoltageSource(key, ElectricalNetPair(from_, to), waveform)

def TransientCurrentSource(key, from_, to, waveform):
    """Bind an oriented native continuous waveform to two exact input nets."""
    return _volt._TransientCurrentSource(key, ElectricalNetPair(from_, to), waveform)
