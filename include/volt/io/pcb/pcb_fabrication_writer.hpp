#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/core/diagnostics.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace volt::io {

/** Severity for native fabrication export loss entries. */
enum class PcbFabricationLossSeverity {
    Info,
    Warning,
    Error,
};

/** Fabrication impact for native export losses. */
enum class PcbFabricationLossImpact {
    Informational,
    FabCritical,
};

/** Category for imperfect native fabrication output. */
enum class PcbFabricationLossKind {
    MissingGeometry,
    UnsupportedGeometry,
    UnsupportedLayer,
    LossyGeometry,
};

/** One native fabrication warning for a construct that could not be emitted perfectly. */
struct PcbFabricationLossWarning {
    /** Category describing why the construct could not be emitted perfectly. */
    PcbFabricationLossKind kind;

    /** Stable construct identifier, such as board.feature.slot or footprint.pad.oval. */
    std::string construct;

    /** Human-readable explanation for diagnostics, Python, and CLI callers. */
    std::string message;

    /** Severity callers can use to decide whether to continue or fail the export. */
    PcbFabricationLossSeverity severity = PcbFabricationLossSeverity::Warning;

    /** Whether this loss means the fabrication package is incomplete for ordering. */
    PcbFabricationLossImpact fabrication_impact = PcbFabricationLossImpact::Informational;

    /** Specific model entities responsible for the loss, when the board model knows them. */
    std::vector<EntityRef> entities;
};

/** Warnings collected while writing native Gerber and Excellon fabrication files. */
class PcbFabricationLossReport {
  public:
    /** Add a warning for a construct with imperfect native fabrication output. */
    void add_warning(
        PcbFabricationLossKind kind, std::string construct, std::string message,
        PcbFabricationLossSeverity severity = PcbFabricationLossSeverity::Warning,
        PcbFabricationLossImpact fabrication_impact = PcbFabricationLossImpact::Informational,
        std::vector<EntityRef> entities = {});

    /** Return whether any native fabrication warnings were recorded. */
    [[nodiscard]] bool has_warnings() const noexcept { return !warnings_.empty(); }

    /** Return whether any warnings block fabrication handoff. */
    [[nodiscard]] bool has_fab_critical_warnings() const noexcept;

    /** Return warnings in deterministic discovery order. */
    [[nodiscard]] const std::vector<PcbFabricationLossWarning> &warnings() const noexcept {
        return warnings_;
    }

    /** Return fab-critical warnings in deterministic discovery order. */
    [[nodiscard]] std::vector<PcbFabricationLossWarning> fab_critical_warnings() const;

  private:
    std::vector<PcbFabricationLossWarning> warnings_;
};

/** One deterministic native fabrication output file. */
struct PcbFabricationFile {
    /** JLCPCB-style filename, including extension. */
    std::string filename;

    /** Stable Gerber/Excellon file function label for callers and manifests. */
    std::string function;

    /** Byte-stable file text. */
    std::string text;
};

/** Options for native Gerber and Excellon output. */
struct PcbFabricationExportOptions {
    /** Optional output basename; defaults to a deterministic token derived from the board name. */
    std::optional<std::string> basename = std::nullopt;
};

/** Stable Gerber metadata for native fabrication exports. */
struct PcbFabricationGerberExporterMetadata {
    /** Gerber dialect emitted by the native exporter. */
    std::string format = "RS-274X";

    /** Coordinate units emitted by the native exporter. */
    std::string units = "mm";

    /** Integer and decimal precision for emitted coordinates. */
    std::string coordinate_format = "4.6";

    /** Zero-suppression mode used by emitted Gerber coordinates. */
    std::string zero_suppression = "none";
};

/** Stable Excellon drill metadata for native fabrication exports. */
struct PcbFabricationDrillExporterMetadata {
    /** Drill-file dialect emitted by the native exporter. */
    std::string format = "Excellon";

    /** Coordinate units emitted by the native exporter. */
    std::string units = "mm";

    /** Integer and decimal precision for emitted drill coordinates. */
    std::string coordinate_format = "4.6";

    /** How plated and non-plated drill hits are partitioned. */
    std::string pth_npth = "separate-files";
};

/** Stable metadata describing the native fabrication exporter. */
struct PcbFabricationExporterMetadata {
    /** Stable exporter identity for package manifests and Python callers. */
    std::string name = "volt.native_fabrication";

    /** Schema version for the exporter metadata payload. */
    int schema_version = 1;

    /** Gerber-specific native exporter metadata. */
    PcbFabricationGerberExporterMetadata gerber;

    /** Excellon drill-specific native exporter metadata. */
    PcbFabricationDrillExporterMetadata drill;
};

/** Result of exporting Volt board projection data to native fabrication files. */
struct PcbFabricationExportResult {
    /** Metadata describing the native Gerber and Excellon exporter. */
    PcbFabricationExporterMetadata exporter;

    /** Files in stable package order. */
    std::vector<PcbFabricationFile> files;

    /** Structured warnings for constructs intentionally omitted or approximated. */
    PcbFabricationLossReport loss_report;
};

/** Convert native fabrication losses to stable manufacturability diagnostics. */
[[nodiscard]] DiagnosticReport fabrication_diagnostics(const PcbFabricationLossReport &report);

/** Write deterministic native Gerber RS-274X and Excellon NC drill fabrication files. */
[[nodiscard]] PcbFabricationExportResult
write_pcb_fabrication_files(const Board &board, const FootprintLibrary &footprints,
                            PcbFabricationExportOptions options = {});

} // namespace volt::io
