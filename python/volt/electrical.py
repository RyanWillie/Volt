"""Native ideal passive model values; all construction and validation belongs to C++."""

from . import _volt

ContentHash = _volt.ContentHash
PinKey = _volt.PinKey
UnitDimension = _volt.UnitDimension
Quantity = _volt.Quantity
ToleranceMode = _volt.ToleranceMode
Tolerance = _volt.Tolerance
QuantityRange = _volt.QuantityRange
ModelTerminalKey = _volt.ModelTerminalKey
ModelInternalNodeKey = _volt.ModelInternalNodeKey
ModelElementKey = _volt.ModelElementKey
ModelTerminal = _volt.ModelTerminal
ModelInternalNode = _volt.ModelInternalNode
ModelTerminalHandle = _volt.ModelTerminalHandle
ModelInternalNodeHandle = _volt.ModelInternalNodeHandle
ModelParameter = _volt.ModelParameter
ResistanceElement = _volt.ResistanceElement
CapacitanceElement = _volt.CapacitanceElement
InductanceElement = _volt.InductanceElement
PartElectricalModel = _volt.PartElectricalModel
PartElectricalModelBuilder = _volt.PartElectricalModelBuilder
ohms = _volt.ohms
farads = _volt.farads
henries = _volt.henries
hertz = _volt.hertz
seconds = _volt.seconds
content_hash = _volt.content_hash


ElectricalRequestKey = _volt.ElectricalRequestKey
ElectricalSourceKey = _volt.ElectricalSourceKey
ElectricalProbeKey = _volt.ElectricalProbeKey
ElectricalInputIdentity = _volt.ElectricalInputIdentity
ElectricalInput = _volt.ElectricalInput
ElectricalNetRef = _volt.ElectricalNetRef
ElectricalOccurrenceRef = _volt.ElectricalOccurrenceRef
ElectricalNetPair = _volt.ElectricalNetPair

def prepare_electrical_input(design):
    """Capture one exact immutable native input from an authoring Design."""
    return _volt.prepare_electrical_input(design._circuit)
