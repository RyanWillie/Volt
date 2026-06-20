#include <volt/io/pcb/pcb_fabrication_writer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace volt::io {
namespace {

enum class FabricationSide {
    Top,
    Bottom,
};

struct CopperLayerMap {
    std::optional<BoardLayerId> top;
    std::optional<BoardLayerId> bottom;
};

struct PlacementExport {
    ComponentPlacementId id;
    const ComponentPlacement *placement;
    FootprintDefinition definition;
    std::vector<PadResolution> pad_resolutions;
};

struct DrillHit {
    BoardPoint position;
    double diameter_mm;
    bool plated;
};

using GlyphRows = std::array<std::string_view, 7>;

[[nodiscard]] bool is_fab_critical(const PcbFabricationLossWarning &warning) noexcept {
    return warning.fabrication_impact == PcbFabricationLossImpact::FabCritical;
}

void add_fab_critical_warning(
    PcbFabricationLossReport &report, PcbFabricationLossKind kind, std::string construct,
    std::string message,
    PcbFabricationLossSeverity severity = PcbFabricationLossSeverity::Warning) {
    report.add_warning(kind, std::move(construct), std::move(message), severity,
                       PcbFabricationLossImpact::FabCritical);
}

[[nodiscard]] std::string fabrication_diagnostic_message(const PcbFabricationLossWarning &warning) {
    return "Native PCB fabrication export has fab-critical loss for " + warning.construct + ": " +
           warning.message;
}

[[nodiscard]] double normalized_number(double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument{"Fabrication writer numeric values must be finite"};
    }
    const auto rounded = std::round(value * 1.0e12) / 1.0e12;
    if (std::abs(value - rounded) < 1.0e-12) {
        value = rounded;
    }
    if (std::abs(value) < 1.0e-12) {
        return 0.0;
    }
    return value;
}

[[nodiscard]] std::string decimal_mm(double value) {
    auto out = std::ostringstream{};
    out << std::fixed << std::setprecision(6) << normalized_number(value);
    return out.str();
}

[[nodiscard]] std::string coordinate(double value) {
    const auto scaled = static_cast<long long>(std::llround(normalized_number(value) * 1000000.0));
    auto out = std::ostringstream{};
    if (scaled < 0) {
        out << '-';
    }
    out << std::setw(10) << std::setfill('0') << (scaled < 0 ? -scaled : scaled);
    return out.str();
}

[[nodiscard]] std::string xy(BoardPoint point) {
    return "X" + coordinate(point.x_mm()) + "Y" + coordinate(point.y_mm());
}

[[nodiscard]] std::optional<GlyphRows> glyph_for(char character) {
    switch (character) {
    case ' ':
        return GlyphRows{"00000", "00000", "00000", "00000", "00000", "00000", "00000"};
    case '-':
        return GlyphRows{"00000", "00000", "00000", "11111", "00000", "00000", "00000"};
    case '.':
        return GlyphRows{"00000", "00000", "00000", "00000", "00000", "01100", "01100"};
    case '/':
        return GlyphRows{"00001", "00010", "00100", "01000", "10000", "00000", "00000"};
    case ':':
        return GlyphRows{"00000", "01100", "01100", "00000", "01100", "01100", "00000"};
    case '_':
        return GlyphRows{"00000", "00000", "00000", "00000", "00000", "00000", "11111"};
    case '+':
        return GlyphRows{"00000", "00100", "00100", "11111", "00100", "00100", "00000"};
    case '0':
        return GlyphRows{"01110", "10001", "10011", "10101", "11001", "10001", "01110"};
    case '1':
        return GlyphRows{"00100", "01100", "00100", "00100", "00100", "00100", "01110"};
    case '2':
        return GlyphRows{"01110", "10001", "00001", "00010", "00100", "01000", "11111"};
    case '3':
        return GlyphRows{"11110", "00001", "00001", "01110", "00001", "00001", "11110"};
    case '4':
        return GlyphRows{"00010", "00110", "01010", "10010", "11111", "00010", "00010"};
    case '5':
        return GlyphRows{"11111", "10000", "10000", "11110", "00001", "00001", "11110"};
    case '6':
        return GlyphRows{"01110", "10000", "10000", "11110", "10001", "10001", "01110"};
    case '7':
        return GlyphRows{"11111", "00001", "00010", "00100", "01000", "01000", "01000"};
    case '8':
        return GlyphRows{"01110", "10001", "10001", "01110", "10001", "10001", "01110"};
    case '9':
        return GlyphRows{"01110", "10001", "10001", "01111", "00001", "00001", "01110"};
    case 'A':
        return GlyphRows{"01110", "10001", "10001", "11111", "10001", "10001", "10001"};
    case 'B':
        return GlyphRows{"11110", "10001", "10001", "11110", "10001", "10001", "11110"};
    case 'C':
        return GlyphRows{"01110", "10001", "10000", "10000", "10000", "10001", "01110"};
    case 'D':
        return GlyphRows{"11110", "10001", "10001", "10001", "10001", "10001", "11110"};
    case 'E':
        return GlyphRows{"11111", "10000", "10000", "11110", "10000", "10000", "11111"};
    case 'F':
        return GlyphRows{"11111", "10000", "10000", "11110", "10000", "10000", "10000"};
    case 'G':
        return GlyphRows{"01110", "10001", "10000", "10111", "10001", "10001", "01110"};
    case 'H':
        return GlyphRows{"10001", "10001", "10001", "11111", "10001", "10001", "10001"};
    case 'I':
        return GlyphRows{"01110", "00100", "00100", "00100", "00100", "00100", "01110"};
    case 'J':
        return GlyphRows{"00111", "00010", "00010", "00010", "00010", "10010", "01100"};
    case 'K':
        return GlyphRows{"10001", "10010", "10100", "11000", "10100", "10010", "10001"};
    case 'L':
        return GlyphRows{"10000", "10000", "10000", "10000", "10000", "10000", "11111"};
    case 'M':
        return GlyphRows{"10001", "11011", "10101", "10101", "10001", "10001", "10001"};
    case 'N':
        return GlyphRows{"10001", "11001", "10101", "10011", "10001", "10001", "10001"};
    case 'O':
        return GlyphRows{"01110", "10001", "10001", "10001", "10001", "10001", "01110"};
    case 'P':
        return GlyphRows{"11110", "10001", "10001", "11110", "10000", "10000", "10000"};
    case 'Q':
        return GlyphRows{"01110", "10001", "10001", "10001", "10101", "10010", "01101"};
    case 'R':
        return GlyphRows{"11110", "10001", "10001", "11110", "10100", "10010", "10001"};
    case 'S':
        return GlyphRows{"01111", "10000", "10000", "01110", "00001", "00001", "11110"};
    case 'T':
        return GlyphRows{"11111", "00100", "00100", "00100", "00100", "00100", "00100"};
    case 'U':
        return GlyphRows{"10001", "10001", "10001", "10001", "10001", "10001", "01110"};
    case 'V':
        return GlyphRows{"10001", "10001", "10001", "10001", "10001", "01010", "00100"};
    case 'W':
        return GlyphRows{"10001", "10001", "10001", "10101", "10101", "10101", "01010"};
    case 'X':
        return GlyphRows{"10001", "10001", "01010", "00100", "01010", "10001", "10001"};
    case 'Y':
        return GlyphRows{"10001", "10001", "01010", "00100", "00100", "00100", "00100"};
    case 'Z':
        return GlyphRows{"11111", "00001", "00010", "00100", "01000", "10000", "11111"};
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::string file_token(std::string_view value) {
    auto result = std::string{};
    auto previous_was_separator = false;
    for (const auto character : value) {
        const auto ascii_alnum = (character >= 'A' && character <= 'Z') ||
                                 (character >= 'a' && character <= 'z') ||
                                 (character >= '0' && character <= '9');
        if (ascii_alnum) {
            result += character;
            previous_was_separator = false;
            continue;
        }
        if (!result.empty() && !previous_was_separator) {
            result += '_';
            previous_was_separator = true;
        }
    }
    if (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    return result.empty() ? std::string{"board"} : result;
}

[[nodiscard]] std::string output_basename(const Board &board,
                                          const PcbFabricationExportOptions &options) {
    if (options.basename.has_value()) {
        return file_token(options.basename.value());
    }
    return file_token(board.name().value());
}

[[nodiscard]] std::string gerber_comment_text(std::string_view value) {
    auto result = std::string{};
    for (const auto character : value) {
        if (character == '*' || character == '\n' || character == '\r') {
            result += ' ';
            continue;
        }
        result += character;
    }
    return result;
}

class GerberWriter {
  public:
    explicit GerberWriter(std::string_view file_function) {
        out_ << "G04 Volt Gerber RS-274X*\n";
        out_ << "%FSLAX46Y46*%\n";
        out_ << "%MOMM*%\n";
        out_ << "%TF.GenerationSoftware,Volt,Volt Native Fabrication Exporter,0.1*%\n";
        out_ << "%TF.FileFunction," << file_function << "*%\n";
        out_ << "%TF.FilePolarity,Positive*%\n";
        out_ << "%LPD*%\n";
    }

    void comment(std::string_view value) { out_ << "G04 " << gerber_comment_text(value) << "*\n"; }

    void draw_polyline(const std::vector<BoardPoint> &points, double width_mm, bool closed) {
        if (points.size() < 2U) {
            return;
        }
        select_aperture(circle_aperture(width_mm));
        out_ << xy(points.front()) << "D02*\n";
        for (std::size_t index = 1; index < points.size(); ++index) {
            out_ << xy(points[index]) << "D01*\n";
        }
        if (closed) {
            out_ << xy(points.front()) << "D01*\n";
        }
    }

    void flash_circle(BoardPoint center, double diameter_mm) {
        select_aperture(circle_aperture(diameter_mm));
        out_ << xy(center) << "D03*\n";
    }

    void draw_region(const std::vector<BoardPoint> &points) {
        if (points.size() < 3U) {
            return;
        }
        out_ << "G36*\n";
        out_ << xy(points.front()) << "D02*\n";
        for (std::size_t index = 1; index < points.size(); ++index) {
            out_ << xy(points[index]) << "D01*\n";
        }
        out_ << xy(points.front()) << "D01*\n";
        out_ << "G37*\n";
    }

    [[nodiscard]] std::string finish() {
        out_ << "M02*\n";
        return out_.str();
    }

  private:
    struct Aperture {
        int code;
        std::string key;
        std::string declaration;
    };

    [[nodiscard]] static std::string circle_aperture(double diameter_mm) {
        return "C," + decimal_mm(diameter_mm);
    }

    void select_aperture(const std::string &key) {
        const auto match =
            std::find_if(apertures_.begin(), apertures_.end(),
                         [&](const Aperture &candidate) { return candidate.key == key; });
        auto code = 0;
        if (match == apertures_.end()) {
            code = next_aperture_code_++;
            apertures_.push_back(Aperture{code, key, "%ADD" + std::to_string(code) + key + "*%"});
            out_ << apertures_.back().declaration << "\n";
        } else {
            code = match->code;
        }
        if (current_aperture_ != code) {
            out_ << "D" << code << "*\n";
            current_aperture_ = code;
        }
    }

    std::ostringstream out_;
    std::vector<Aperture> apertures_;
    int next_aperture_code_ = 10;
    int current_aperture_ = 0;
};

[[nodiscard]] std::optional<BoardLayerId>
first_enabled_copper_layer(const Board &board, BoardLayerSide side,
                           PcbFabricationLossReport &loss_report) {
    auto result = std::optional<BoardLayerId>{};
    for (std::size_t index = 0; index < board.layer_count(); ++index) {
        const auto layer_id = BoardLayerId{index};
        const auto &layer = board.layer(layer_id);
        if (!layer.enabled() || layer.role() != BoardLayerRole::Copper || layer.side() != side) {
            continue;
        }
        if (!result.has_value()) {
            result = layer_id;
            continue;
        }
        add_fab_critical_warning(
            loss_report, PcbFabricationLossKind::UnsupportedConstruct, "board.layer.mapping",
            "Native fabrication export supports one top and one bottom copper layer in v1; "
            "duplicate copper layers on the same side are omitted");
    }
    return result;
}

void report_unsupported_copper_layers(const Board &board, PcbFabricationLossReport &loss_report) {
    for (std::size_t index = 0; index < board.layer_count(); ++index) {
        const auto layer_id = BoardLayerId{index};
        const auto &layer = board.layer(layer_id);
        if (!layer.enabled() || layer.role() != BoardLayerRole::Copper) {
            continue;
        }
        if (layer.side() != BoardLayerSide::Top && layer.side() != BoardLayerSide::Bottom) {
            add_fab_critical_warning(
                loss_report, PcbFabricationLossKind::UnsupportedConstruct,
                "board.layer.copper_side",
                "Native fabrication export v1 emits top and bottom copper only");
        }
    }
}

[[nodiscard]] CopperLayerMap build_copper_layer_map(const Board &board,
                                                    PcbFabricationLossReport &loss_report) {
    report_unsupported_copper_layers(board, loss_report);
    return CopperLayerMap{
        first_enabled_copper_layer(board, BoardLayerSide::Top, loss_report),
        first_enabled_copper_layer(board, BoardLayerSide::Bottom, loss_report),
    };
}

[[nodiscard]] const FootprintDefinition *
definition_for_placement(const FootprintLibrary &footprints, const PhysicalPart &part) {
    return footprints.find(part.footprint());
}

[[nodiscard]] const PadResolution *pad_resolution_for(const std::vector<PadResolution> &resolutions,
                                                      ComponentPlacementId placement,
                                                      FootprintPadId pad) {
    return volt::detail::find_board_pad_resolution(resolutions, placement, pad);
}

void report_invalid_pad_resolution(const Board &board, const PadResolution &resolution,
                                   PcbFabricationLossReport &loss_report) {
    if (resolution.status() == PadResolutionStatus::Connected ||
        resolution.status() == PadResolutionStatus::NonElectrical) {
        return;
    }
    const auto &component = board.circuit().component(resolution.component());
    add_fab_critical_warning(
        loss_report, PcbFabricationLossKind::IncompleteConstruct, "pad_resolution",
        "Footprint pad " + component.reference().value() + "." + resolution.pad_label() +
            " is emitted without a resolved logical net");
}

[[nodiscard]] std::vector<PlacementExport>
build_placement_exports(const Board &board, const FootprintLibrary &footprints,
                        PcbFabricationLossReport &loss_report) {
    const auto resolution_footprints = volt::detail::board_resolution_footprints(board, footprints);
    const auto resolutions = board.resolve_pads(resolution_footprints);
    auto exports = std::vector<PlacementExport>{};

    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto id = ComponentPlacementId{index};
        const auto &placement = board.placement(id);
        const auto &selected_part = board.circuit().selected_physical_part(placement.component());
        if (!selected_part.has_value()) {
            add_fab_critical_warning(
                loss_report, PcbFabricationLossKind::IncompleteConstruct, "component.part",
                "Component placement has no selected physical part for fabrication export");
            continue;
        }
        const auto *definition =
            definition_for_placement(resolution_footprints, selected_part.value());
        if (definition == nullptr) {
            add_fab_critical_warning(
                loss_report, PcbFabricationLossKind::IncompleteConstruct, "footprint",
                "Component placement has no resolved footprint definition for fabrication export");
            continue;
        }

        auto placement_export = PlacementExport{id, &placement, *definition, {}};
        placement_export.pad_resolutions.reserve(placement_export.definition.pad_count());
        for (std::size_t pad_index = 0; pad_index < placement_export.definition.pad_count();
             ++pad_index) {
            const auto pad_id = FootprintPadId{pad_index};
            const auto *resolution = pad_resolution_for(resolutions, id, pad_id);
            if (resolution == nullptr) {
                throw std::logic_error{
                    "Native fabrication export requires a pad resolution for every pad"};
            }
            report_invalid_pad_resolution(board, *resolution, loss_report);
            placement_export.pad_resolutions.push_back(*resolution);
        }
        exports.push_back(std::move(placement_export));
    }

    return exports;
}

[[nodiscard]] bool pad_has_physical_layer(const FootprintPad &pad, BoardSide placement_side,
                                          FabricationSide physical_side, FootprintLayer front_layer,
                                          FootprintLayer back_layer) {
    if (physical_side == FabricationSide::Top) {
        return pad.layers().contains(placement_side == BoardSide::Top ? front_layer : back_layer);
    }
    return pad.layers().contains(placement_side == BoardSide::Top ? back_layer : front_layer);
}

[[nodiscard]] bool pad_has_solder_mask(const FootprintPad &pad, BoardSide placement_side,
                                       FabricationSide side) {
    return pad_has_physical_layer(pad, placement_side, side, FootprintLayer::FrontSolderMask,
                                  FootprintLayer::BackSolderMask);
}

[[nodiscard]] bool pad_has_paste(const FootprintPad &pad, BoardSide placement_side,
                                 FabricationSide side) {
    return pad_has_physical_layer(pad, placement_side, side, FootprintLayer::FrontPaste,
                                  FootprintLayer::BackPaste);
}

void write_pad_shape(GerberWriter &writer, const ComponentPlacement &placement,
                     const FootprintPad &pad, const PadResolution &resolution,
                     PcbFabricationLossReport &loss_report) {
    if (pad.shape() == FootprintPadShape::Circle) {
        writer.flash_circle(resolution.position(), pad.size().width_mm());
        return;
    }
    if (pad.shape() == FootprintPadShape::Oval) {
        add_fab_critical_warning(loss_report, PcbFabricationLossKind::UnsupportedConstruct,
                                 "footprint.pad.oval",
                                 "Native fabrication export v1 does not emit oval pad geometry");
        return;
    }
    if (pad.shape() == FootprintPadShape::RoundedRectangle) {
        loss_report.add_warning(
            PcbFabricationLossKind::LossyConstruct, "footprint.pad.rounded_rectangle",
            "Native fabrication export v1 approximates rounded rectangle pads as rectangles",
            PcbFabricationLossSeverity::Warning, PcbFabricationLossImpact::Informational);
    }
    writer.draw_region(volt::detail::transformed_pad_body_corners(placement, pad));
}

[[nodiscard]] bool layer_matches_side(const BoardLayer &layer, FabricationSide side) {
    if (side == FabricationSide::Top) {
        return layer.side() == BoardLayerSide::Top;
    }
    return layer.side() == BoardLayerSide::Bottom;
}

void write_copper_layer(GerberWriter &writer, const Board &board, BoardLayerId layer_id,
                        const std::vector<PlacementExport> &placements,
                        PcbFabricationLossReport &loss_report) {
    for (std::size_t index = 0; index < board.track_count(); ++index) {
        const auto &track = board.track(BoardTrackId{index});
        if (track.layer() == layer_id) {
            writer.draw_polyline(track.points(), track.width_mm(), false);
        }
    }

    for (std::size_t index = 0; index < board.via_count(); ++index) {
        const auto &via = board.via(BoardViaId{index});
        const auto layers = volt::detail::via_copper_layers(board, via);
        if (std::find(layers.begin(), layers.end(), layer_id) != layers.end()) {
            writer.flash_circle(via.position(), via.annular_diameter_mm());
        }
    }

    for (std::size_t index = 0; index < board.zone_count(); ++index) {
        const auto &zone = board.zone(BoardZoneId{index});
        if (std::find(zone.layers().begin(), zone.layers().end(), layer_id) !=
            zone.layers().end()) {
            writer.draw_region(zone.outline());
        }
    }

    for (const auto &placement_export : placements) {
        const auto &placement = *placement_export.placement;
        const auto &definition = placement_export.definition;
        for (std::size_t pad_index = 0; pad_index < definition.pad_count(); ++pad_index) {
            const auto pad_id = FootprintPadId{pad_index};
            const auto &pad = definition.pad(pad_id);
            const auto layers = volt::detail::pad_copper_layers(board, pad, placement.side());
            if (std::find(layers.begin(), layers.end(), layer_id) == layers.end()) {
                continue;
            }
            write_pad_shape(writer, placement, pad, placement_export.pad_resolutions.at(pad_index),
                            loss_report);
        }
    }
}

void write_mask_or_paste_layer(GerberWriter &writer, const std::vector<PlacementExport> &placements,
                               FabricationSide side, bool paste,
                               PcbFabricationLossReport &loss_report) {
    for (const auto &placement_export : placements) {
        const auto &placement = *placement_export.placement;
        const auto &definition = placement_export.definition;
        for (std::size_t pad_index = 0; pad_index < definition.pad_count(); ++pad_index) {
            const auto &pad = definition.pad(FootprintPadId{pad_index});
            const auto selected = paste ? pad_has_paste(pad, placement.side(), side)
                                        : pad_has_solder_mask(pad, placement.side(), side);
            if (!selected) {
                continue;
            }
            write_pad_shape(writer, placement, pad, placement_export.pad_resolutions.at(pad_index),
                            loss_report);
        }
    }
}

[[nodiscard]] bool has_paste_content(const std::vector<PlacementExport> &placements,
                                     FabricationSide side) {
    for (const auto &placement_export : placements) {
        const auto &placement = *placement_export.placement;
        const auto &definition = placement_export.definition;
        for (std::size_t pad_index = 0; pad_index < definition.pad_count(); ++pad_index) {
            if (pad_has_paste(definition.pad(FootprintPadId{pad_index}), placement.side(), side)) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] bool has_silkscreen_content(const Board &board,
                                          const std::vector<PlacementExport> &placements,
                                          FabricationSide side) {
    for (const auto &placement_export : placements) {
        const auto &placement = *placement_export.placement;
        if ((side == FabricationSide::Top && placement.side() == BoardSide::Top) ||
            (side == FabricationSide::Bottom && placement.side() == BoardSide::Bottom)) {
            if (placement_export.definition.body().has_value()) {
                return true;
            }
        }
    }
    for (std::size_t index = 0; index < board.text_count(); ++index) {
        const auto &text = board.text(BoardTextId{index});
        const auto &layer = board.layer(text.layer());
        if (layer.role() == BoardLayerRole::Silkscreen && layer_matches_side(layer, side)) {
            return true;
        }
    }
    return false;
}

void report_unsupported_board_text_layers(const Board &board,
                                          PcbFabricationLossReport &loss_report) {
    for (std::size_t index = 0; index < board.text_count(); ++index) {
        const auto &text = board.text(BoardTextId{index});
        const auto &layer = board.layer(text.layer());
        if (layer.role() == BoardLayerRole::Silkscreen) {
            continue;
        }
        add_fab_critical_warning(
            loss_report, PcbFabricationLossKind::UnsupportedConstruct, "board.text.layer",
            "Native fabrication export v1 emits board text only on silkscreen layers");
    }
}

[[nodiscard]] BoardPoint transform_board_text_point(const BoardText &text, double local_x,
                                                    double local_y) {
    constexpr double pi = 3.14159265358979323846264338327950288;
    const auto radians = text.rotation().degrees() * pi / 180.0;
    const auto rotated_x = (std::cos(radians) * local_x) - (std::sin(radians) * local_y);
    const auto rotated_y = (std::sin(radians) * local_x) + (std::cos(radians) * local_y);
    return BoardPoint{text.position().x_mm() + rotated_x, text.position().y_mm() + rotated_y};
}

void draw_text_cell(GerberWriter &writer, const BoardText &text, double left_mm, double top_mm,
                    double right_mm, double bottom_mm) {
    writer.draw_region(std::vector{
        transform_board_text_point(text, left_mm, top_mm),
        transform_board_text_point(text, right_mm, top_mm),
        transform_board_text_point(text, right_mm, bottom_mm),
        transform_board_text_point(text, left_mm, bottom_mm),
    });
}

void report_unsupported_board_text_character(PcbFabricationLossReport &loss_report) {
    add_fab_critical_warning(
        loss_report, PcbFabricationLossKind::UnsupportedConstruct, "board.text.character",
        "Native fabrication export v1 can only render uppercase A-Z, digits, spaces, and common "
        "silkscreen punctuation in board text");
}

void write_board_text(GerberWriter &writer, const BoardText &text,
                      PcbFabricationLossReport &loss_report) {
    constexpr double text_width_factor = 0.6;
    constexpr std::size_t glyph_columns = 5U;
    constexpr std::size_t glyph_rows = 7U;
    writer.comment("TEXT " + text.text());

    const auto glyph_width_mm = text.size_mm() * text_width_factor;
    const auto cell_width_mm = glyph_width_mm / static_cast<double>(glyph_columns);
    const auto cell_height_mm = text.size_mm() / static_cast<double>(glyph_rows);
    auto cursor_x_mm = 0.0;

    for (const auto character : text.text()) {
        const auto glyph = glyph_for(character);
        if (!glyph.has_value()) {
            report_unsupported_board_text_character(loss_report);
            cursor_x_mm += glyph_width_mm;
            continue;
        }

        for (std::size_t row_index = 0; row_index < glyph_rows; ++row_index) {
            const auto row = glyph->at(row_index);
            for (std::size_t column_index = 0; column_index < glyph_columns; ++column_index) {
                if (row[column_index] != '1') {
                    continue;
                }
                const auto left_mm =
                    cursor_x_mm + (static_cast<double>(column_index) * cell_width_mm);
                const auto right_mm = left_mm + cell_width_mm;
                const auto top_mm =
                    -text.size_mm() + (static_cast<double>(row_index) * cell_height_mm);
                const auto bottom_mm = top_mm + cell_height_mm;
                draw_text_cell(writer, text, left_mm, top_mm, right_mm, bottom_mm);
            }
        }
        cursor_x_mm += glyph_width_mm;
    }
}

void write_silkscreen_layer(GerberWriter &writer, const Board &board,
                            const std::vector<PlacementExport> &placements, FabricationSide side,
                            PcbFabricationLossReport &loss_report) {
    for (const auto &placement_export : placements) {
        const auto &placement = *placement_export.placement;
        if ((side == FabricationSide::Top && placement.side() != BoardSide::Top) ||
            (side == FabricationSide::Bottom && placement.side() != BoardSide::Bottom)) {
            continue;
        }
        if (placement_export.definition.body().has_value()) {
            writer.draw_polyline(volt::detail::transformed_footprint_polygon(
                                     placement, placement_export.definition.body().value()),
                                 0.12, true);
        }
    }

    for (std::size_t index = 0; index < board.text_count(); ++index) {
        const auto &text = board.text(BoardTextId{index});
        const auto &layer = board.layer(text.layer());
        if (layer.role() != BoardLayerRole::Silkscreen) {
            continue;
        }
        if (!layer_matches_side(layer, side)) {
            continue;
        }
        write_board_text(writer, text, loss_report);
    }
}

void write_outline(GerberWriter &writer, const Board &board,
                   PcbFabricationLossReport &loss_report) {
    if (!board.outline().has_value()) {
        add_fab_critical_warning(loss_report, PcbFabricationLossKind::IncompleteConstruct,
                                 "board.outline",
                                 "Native fabrication export requires a board outline");
        return;
    }
    writer.draw_polyline(board.outline()->vertices(), 0.10, true);
}

void report_unsupported_board_features(const Board &board, PcbFabricationLossReport &loss_report) {
    for (std::size_t index = 0; index < board.feature_count(); ++index) {
        const auto &feature = board.feature(BoardFeatureId{index});
        if (feature.kind() == BoardFeatureKind::Slot) {
            add_fab_critical_warning(
                loss_report, PcbFabricationLossKind::UnsupportedConstruct, "board.feature.slot",
                "Native fabrication export v1 does not emit slotted drill features");
        } else if (feature.kind() == BoardFeatureKind::Cutout) {
            add_fab_critical_warning(
                loss_report, PcbFabricationLossKind::UnsupportedConstruct, "board.feature.cutout",
                "Native fabrication export v1 does not emit internal board cutouts");
        } else if (feature.kind() == BoardFeatureKind::Circle) {
            add_fab_critical_warning(
                loss_report, PcbFabricationLossKind::UnsupportedConstruct, "board.feature.circle",
                "Native fabrication export v1 does not emit generic board circle features");
        }
    }
}

void append_board_feature_drills(const Board &board, std::vector<DrillHit> &drills,
                                 PcbFabricationLossReport &loss_report) {
    for (std::size_t index = 0; index < board.feature_count(); ++index) {
        const auto &feature = board.feature(BoardFeatureId{index});
        if (feature.kind() != BoardFeatureKind::Hole) {
            continue;
        }
        const auto &hole = feature.hole();
        if (hole.finished_diameter_mm().has_value() &&
            std::abs(hole.finished_diameter_mm().value() - hole.drill_diameter_mm()) > 1.0e-9) {
            add_fab_critical_warning(
                loss_report, PcbFabricationLossKind::UnsupportedConstruct,
                "board.feature.hole.finished_diameter",
                "Native Excellon export v1 emits drill diameter but cannot encode distinct "
                "finished hole diameter requirements");
        }
        drills.push_back(DrillHit{hole.center(), hole.drill_diameter_mm(), hole.plated()});
    }
}

void append_via_drills(const Board &board, std::vector<DrillHit> &drills) {
    for (std::size_t index = 0; index < board.via_count(); ++index) {
        const auto &via = board.via(BoardViaId{index});
        drills.push_back(DrillHit{via.position(), via.drill_diameter_mm(), true});
    }
}

void append_pad_drills(const std::vector<PlacementExport> &placements,
                       std::vector<DrillHit> &drills) {
    for (const auto &placement_export : placements) {
        const auto &placement = *placement_export.placement;
        const auto &definition = placement_export.definition;
        for (std::size_t pad_index = 0; pad_index < definition.pad_count(); ++pad_index) {
            const auto &pad = definition.pad(FootprintPadId{pad_index});
            if (!pad.drill().has_value()) {
                continue;
            }
            drills.push_back(DrillHit{
                volt::detail::transform_footprint_point(placement, pad.position()),
                pad.drill()->diameter_mm(),
                pad.drill()->plating() == FootprintPadPlating::Plated,
            });
        }
    }
}

[[nodiscard]] std::string write_excellon(const std::vector<DrillHit> &all_drills, bool plated) {
    auto drills = std::vector<DrillHit>{};
    for (const auto &drill : all_drills) {
        if (drill.plated == plated) {
            drills.push_back(drill);
        }
    }
    std::sort(drills.begin(), drills.end(), [](const DrillHit &lhs, const DrillHit &rhs) {
        if (lhs.diameter_mm != rhs.diameter_mm) {
            return lhs.diameter_mm < rhs.diameter_mm;
        }
        if (lhs.position.x_mm() != rhs.position.x_mm()) {
            return lhs.position.x_mm() < rhs.position.x_mm();
        }
        return lhs.position.y_mm() < rhs.position.y_mm();
    });

    auto diameters = std::vector<double>{};
    for (const auto &drill : drills) {
        if (std::find(diameters.begin(), diameters.end(), drill.diameter_mm) == diameters.end()) {
            diameters.push_back(drill.diameter_mm);
        }
    }

    auto out = std::ostringstream{};
    out << "M48\n";
    out << "; Volt Excellon NC drill\n";
    out << (plated ? ";TYPE=PLATED\n" : ";TYPE=NON_PLATED\n");
    out << "METRIC,TZ\n";
    out << "FMAT,2\n";
    for (std::size_t index = 0; index < diameters.size(); ++index) {
        out << "T" << std::setw(2) << std::setfill('0') << (index + 1U) << "C"
            << decimal_mm(diameters[index]) << "\n";
    }
    out << "%\n";
    auto current_tool = std::optional<std::size_t>{};
    for (const auto &drill : drills) {
        const auto tool = static_cast<std::size_t>(
            std::distance(diameters.begin(),
                          std::find(diameters.begin(), diameters.end(), drill.diameter_mm)) +
            1);
        if (!current_tool.has_value() || current_tool.value() != tool) {
            out << "T" << std::setw(2) << std::setfill('0') << tool << "\n";
            current_tool = tool;
        }
        out << xy(drill.position) << "\n";
    }
    out << "M30\n";
    return out.str();
}

void append_file(std::vector<PcbFabricationFile> &files, std::string filename, std::string function,
                 std::string text) {
    files.push_back(PcbFabricationFile{std::move(filename), std::move(function), std::move(text)});
}

} // namespace

void PcbFabricationLossReport::add_warning(PcbFabricationLossKind kind, std::string construct,
                                           std::string message, PcbFabricationLossSeverity severity,
                                           PcbFabricationLossImpact fabrication_impact) {
    if (construct.empty()) {
        throw std::invalid_argument{"Fabrication loss warning construct must not be empty"};
    }
    if (message.empty()) {
        throw std::invalid_argument{"Fabrication loss warning message must not be empty"};
    }
    warnings_.push_back(PcbFabricationLossWarning{kind, std::move(construct), std::move(message),
                                                  severity, fabrication_impact});
}

[[nodiscard]] bool PcbFabricationLossReport::has_fab_critical_warnings() const noexcept {
    return std::any_of(warnings_.begin(), warnings_.end(), is_fab_critical);
}

[[nodiscard]] std::vector<PcbFabricationLossWarning>
PcbFabricationLossReport::fab_critical_warnings() const {
    auto result = std::vector<PcbFabricationLossWarning>{};
    for (const auto &warning : warnings_) {
        if (is_fab_critical(warning)) {
            result.push_back(warning);
        }
    }
    return result;
}

[[nodiscard]] DiagnosticReport fabrication_diagnostics(const PcbFabricationLossReport &report) {
    auto diagnostics = DiagnosticReport{};
    for (const auto &warning : report.warnings()) {
        if (!is_fab_critical(warning)) {
            continue;
        }
        diagnostics.add(Diagnostic{
            Severity::Error,
            DiagnosticCode{std::string{pcb_fabrication_diagnostic_codes::NativeFabExportLoss}},
            DiagnosticCategory{diagnostic_categories::PcbFabrication},
            fabrication_diagnostic_message(warning),
            std::vector{EntityRef::board()},
            {},
            std::nullopt,
            warning.construct,
        });
    }
    return diagnostics;
}

[[nodiscard]] PcbFabricationExportResult
write_pcb_fabrication_files(const Board &board, const FootprintLibrary &footprints,
                            PcbFabricationExportOptions options) {
    auto result = PcbFabricationExportResult{};
    const auto basename = output_basename(board, options);
    report_unsupported_board_features(board, result.loss_report);
    report_unsupported_board_text_layers(board, result.loss_report);
    const auto copper_layers = build_copper_layer_map(board, result.loss_report);
    const auto placements = build_placement_exports(board, footprints, result.loss_report);

    if (copper_layers.top.has_value()) {
        auto writer = GerberWriter{"Copper,L1,Top"};
        write_copper_layer(writer, board, copper_layers.top.value(), placements,
                           result.loss_report);
        append_file(result.files, basename + ".GTL", "copper-top", writer.finish());
    }
    if (copper_layers.bottom.has_value()) {
        auto writer = GerberWriter{"Copper,L2,Bot"};
        write_copper_layer(writer, board, copper_layers.bottom.value(), placements,
                           result.loss_report);
        append_file(result.files, basename + ".GBL", "copper-bottom", writer.finish());
    }

    {
        auto writer = GerberWriter{"Soldermask,Top"};
        write_mask_or_paste_layer(writer, placements, FabricationSide::Top, false,
                                  result.loss_report);
        append_file(result.files, basename + ".GTS", "soldermask-top", writer.finish());
    }
    {
        auto writer = GerberWriter{"Soldermask,Bot"};
        write_mask_or_paste_layer(writer, placements, FabricationSide::Bottom, false,
                                  result.loss_report);
        append_file(result.files, basename + ".GBS", "soldermask-bottom", writer.finish());
    }
    {
        auto writer = GerberWriter{"Legend,Top"};
        write_silkscreen_layer(writer, board, placements, FabricationSide::Top, result.loss_report);
        append_file(result.files, basename + ".GTO", "silkscreen-top", writer.finish());
    }
    if (has_silkscreen_content(board, placements, FabricationSide::Bottom)) {
        auto writer = GerberWriter{"Legend,Bot"};
        write_silkscreen_layer(writer, board, placements, FabricationSide::Bottom,
                               result.loss_report);
        append_file(result.files, basename + ".GBO", "silkscreen-bottom", writer.finish());
    }
    {
        auto writer = GerberWriter{"Paste,Top"};
        write_mask_or_paste_layer(writer, placements, FabricationSide::Top, true,
                                  result.loss_report);
        append_file(result.files, basename + ".GTP", "paste-top", writer.finish());
    }
    if (has_paste_content(placements, FabricationSide::Bottom)) {
        auto writer = GerberWriter{"Paste,Bot"};
        write_mask_or_paste_layer(writer, placements, FabricationSide::Bottom, true,
                                  result.loss_report);
        append_file(result.files, basename + ".GBP", "paste-bottom", writer.finish());
    }
    {
        auto writer = GerberWriter{"Profile,NP"};
        write_outline(writer, board, result.loss_report);
        append_file(result.files, basename + ".GKO", "profile", writer.finish());
    }

    auto drills = std::vector<DrillHit>{};
    append_via_drills(board, drills);
    append_pad_drills(placements, drills);
    append_board_feature_drills(board, drills, result.loss_report);
    append_file(result.files, basename + "-PTH.TXT", "drill-plated", write_excellon(drills, true));
    append_file(result.files, basename + "-NPTH.TXT", "drill-non-plated",
                write_excellon(drills, false));

    return result;
}

} // namespace volt::io
