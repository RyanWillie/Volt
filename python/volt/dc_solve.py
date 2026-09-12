"""Immutable native linear DC solve options and results."""

from . import _volt

DcSolveOptions = _volt.DcSolveOptions
DcSolveOutcome = _volt.DcSolveOutcome
DcSolveMetrics = _volt.DcSolveMetrics
DcNodeResult = _volt.DcNodeResult
DcBranchResult = _volt.DcBranchResult
DcProbeResult = _volt.DcProbeResult
DcSolution = _volt.DcSolution
DcSolveReport = _volt.DcSolveReport
DcSolveProvenance = _volt.DcSolveProvenance
NgspiceDcAnalysis = _volt.NgspiceDcAnalysis
solve_dc = _volt.solve_dc
prepare_ngspice_dc = _volt.prepare_ngspice_dc
solve_ngspice_dc = _volt.solve_ngspice_dc
