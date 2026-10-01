"""Native AC requests and thin source constructors. Amplitudes are peak SI values."""
from . import _volt
from .electrical import (ElectricalInput, ElectricalInputIdentity, ElectricalNetRef,
                         ElectricalOccurrenceRef, ElectricalRequestKey, ElectricalSourceKey,
                         ElectricalProbeKey, ElectricalNetPair, prepare_electrical_input)
from .dc import (DcVoltageProbe as AcVoltageProbe,
                 DcSourceCurrentProbe as AcSourceCurrentProbe,
                 DcModelElementCurrentProbe as AcModelElementCurrentProbe,
                 DcOccurrenceExclusion as AcOccurrenceExclusion,
                 DcNonElectricalExclusion as AcNonElectricalExclusion,
                 DcOutsideAnalysisExclusion as AcOutsideAnalysisExclusion,
                 DcReplacedByStimulusExclusion as AcReplacedByStimulusExclusion)

AcFrequencySweep = _volt.AcFrequencySweep
AcRequest = _volt.AcRequest
AcRequestAssessment = _volt.AcRequestAssessment
assess_ac_request = _volt.assess_ac_request


def AcVoltageSource(key, from_, to, amplitude, phase=0.0):
    """Peak voltage phasor with phase in radians and ordered branch orientation."""
    return _volt._AcVoltageSource(key, ElectricalNetPair(from_, to), amplitude, phase)


def AcCurrentSource(key, from_, to, amplitude, phase=0.0):
    """Peak current phasor positive from from_ to to; phase is in radians."""
    return _volt._AcCurrentSource(key, ElectricalNetPair(from_, to), amplitude, phase)

AcGainProbe = _volt.AcGainProbe


def AcImpedanceProbe(key, from_, to, source):
    """Measure port voltage divided by negative current-test-source current."""
    return _volt._AcImpedanceProbe(key, ElectricalNetPair(from_, to), source)
