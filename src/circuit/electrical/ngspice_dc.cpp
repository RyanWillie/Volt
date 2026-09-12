#include <volt/electrical/ngspice_dc.hpp>

#include "ngspice_dc_detail.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <volt/core/errors.hpp>

namespace volt {
namespace {

[[nodiscard]] std::string number_text(double value) {
    auto buffer = std::array<char, 64>{};
    const auto result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                      std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) {
        throw KernelLogicError{ErrorCode::InvalidState, "Failed to encode ngspice DC number"};
    }
    return std::string{buffer.data(), result.ptr};
}

class IdentityEncoder final {
  public:
    void text(std::string_view value) {
        bytes_ += std::to_string(value.size());
        bytes_ += ':';
        bytes_.append(value);
        bytes_ += '\n';
    }

    [[nodiscard]] ContentHash digest() const { return sha256_content_hash(bytes_); }

  private:
    std::string bytes_;
};

[[nodiscard]] std::string hash_hex(const ContentHash &hash) {
    constexpr auto prefix = std::string_view{"sha256:"};
    return hash.value().substr(prefix.size());
}

[[nodiscard]] std::string device_name(const ElectricalBranch &branch) {
    const auto prefix = std::visit(
        [](const auto &law) -> char {
            using Law = std::decay_t<decltype(law)>;
            if constexpr (std::same_as<Law, ResistanceElement>) {
                return law.parameter().nominal().value() == 0.0 ? 'v' : 'r';
            } else if constexpr (std::same_as<Law, CapacitanceElement>) {
                return 'c';
            } else if constexpr (std::same_as<Law, InductanceElement>) {
                return 'l';
            } else if constexpr (std::same_as<Law, DcVoltageSource>) {
                return 'v';
            } else {
                static_assert(std::same_as<Law, DcCurrentSource>);
                return 'i';
            }
        },
        branch.law);
    return std::string{prefix} + "b" + std::to_string(branch.id.index());
}

[[nodiscard]] detail::NgspiceCurrentProjection current_projection(const ElectricalBranch &branch) {
    return std::visit(
        [](const auto &law) {
            using Law = std::decay_t<decltype(law)>;
            if constexpr (std::same_as<Law, ResistanceElement>) {
                return law.parameter().nominal().value() == 0.0
                           ? detail::NgspiceCurrentProjection::Returned
                           : detail::NgspiceCurrentProjection::Resistance;
            } else if constexpr (std::same_as<Law, CapacitanceElement>) {
                return detail::NgspiceCurrentProjection::CapacitorOpen;
            } else if constexpr (std::same_as<Law, DcCurrentSource>) {
                return detail::NgspiceCurrentProjection::CurrentSourceNominal;
            } else {
                static_assert(std::same_as<Law, InductanceElement> ||
                              std::same_as<Law, DcVoltageSource>);
                return detail::NgspiceCurrentProjection::Returned;
            }
        },
        branch.law);
}

[[nodiscard]] std::string law_name(const ElectricalBranch &branch) {
    return std::visit(
        [](const auto &law) -> std::string {
            using Law = std::decay_t<decltype(law)>;
            if constexpr (std::same_as<Law, ResistanceElement>) {
                return law.parameter().nominal().value() == 0.0 ? "zero_ohm_constraint"
                                                                : "resistance";
            } else if constexpr (std::same_as<Law, CapacitanceElement>) {
                return "capacitance";
            } else if constexpr (std::same_as<Law, InductanceElement>) {
                return "inductance";
            } else if constexpr (std::same_as<Law, DcVoltageSource>) {
                return "dc_voltage_source";
            } else {
                static_assert(std::same_as<Law, DcCurrentSource>);
                return "dc_current_source";
            }
        },
        branch.law);
}

[[nodiscard]] ContentHash make_mapping_identity(const CompiledElectricalModel &model) {
    auto encoder = IdentityEncoder{};
    encoder.text("volt.ngspice-dc-mapping");
    encoder.text(std::to_string(NgspiceDcAnalysis::contract_version()));
    encoder.text(model.identity().value());
    encoder.text(NgspiceDcAnalysis::backend());
    encoder.text(NgspiceDcAnalysis::backend_version());
    encoder.text(NgspiceDcAnalysis::settings());
    encoder.text(std::to_string(model.reference().index()));
    for (const auto &node : model.nodes()) {
        encoder.text("node:" + std::to_string(node.id.index()));
        encoder.text(node.id == model.reference() ? "0" : "n" + std::to_string(node.id.index()));
        encoder.text(node.id == model.reference() ? "reference" : "returned");
    }
    for (const auto &branch : model.branches()) {
        encoder.text("branch:" + std::to_string(branch.id.index()));
        encoder.text(device_name(branch));
        encoder.text(law_name(branch));
        encoder.text(std::to_string(static_cast<int>(current_projection(branch))));
    }
    return encoder.digest();
}

[[nodiscard]] detail::NgspiceDcMapping make_mapping(const CompiledElectricalModel &model,
                                                    const ContentHash &identity) {
    auto result = detail::NgspiceDcMapping{};
    result.marker = "volt_mapping_" + hash_hex(identity);
    result.headers = {"volt_scale", result.marker};
    for (const auto &node : model.nodes()) {
        if (node.id != model.reference()) {
            result.headers.push_back("v(n" + std::to_string(node.id.index()) + ")");
        }
    }
    result.branches.reserve(model.branches().size());
    for (const auto &branch : model.branches()) {
        const auto projection = current_projection(branch);
        const auto device = device_name(branch);
        const auto output_vector = projection == detail::NgspiceCurrentProjection::Returned
                                       ? "i(" + device + ")"
                                       : std::string{};
        if (!output_vector.empty()) {
            result.headers.push_back(output_vector);
        }
        result.branches.push_back(detail::NgspiceBranchMapping{branch.id, device, law_name(branch),
                                                               projection, output_vector});
    }
    return result;
}

[[nodiscard]] double branch_value(const ElectricalBranch &branch) {
    return std::visit(
        [](const auto &law) {
            using Law = std::decay_t<decltype(law)>;
            if constexpr (std::same_as<Law, ResistanceElement> ||
                          std::same_as<Law, CapacitanceElement> ||
                          std::same_as<Law, InductanceElement>) {
                return law.parameter().nominal().value();
            } else {
                static_assert(std::same_as<Law, DcVoltageSource> ||
                              std::same_as<Law, DcCurrentSource>);
                return law.value().value();
            }
        },
        branch.law);
}

[[nodiscard]] std::string write_deck(const CompiledElectricalModel &model,
                                     const ContentHash &identity) {
    auto stream = std::ostringstream{};
    stream << "* Volt ngspice DC adapter contract " << NgspiceDcAnalysis::contract_version()
           << '\n';
    stream << ".options gmin=0 gminsteps=0 srcsteps=0 reltol=1e-12 abstol=1e-15 "
              "vntol=1e-12\n";
    for (const auto &branch : model.branches()) {
        stream << device_name(branch) << ' ' << detail::ngspice_node(model, branch.from) << ' '
               << detail::ngspice_node(model, branch.to) << ' ';
        std::visit(
            [&](const auto &law) {
                using Law = std::decay_t<decltype(law)>;
                if constexpr (std::same_as<Law, ResistanceElement>) {
                    if (law.parameter().nominal().value() == 0.0) {
                        stream << "DC 0";
                    } else {
                        stream << number_text(branch_value(branch));
                    }
                } else if constexpr (std::same_as<Law, DcVoltageSource> ||
                                     std::same_as<Law, DcCurrentSource>) {
                    stream << "DC " << number_text(branch_value(branch));
                } else {
                    stream << number_text(branch_value(branch));
                }
            },
            branch.law);
        stream << '\n';
    }
    const auto mapping = make_mapping(model, identity);
    stream << ".control\n"
              "set wr_singlescale\n"
              "set wr_vecnames\n"
              "set wr_onespace\n"
              "option numdgt=16\n"
              "op\n"
           << "let " << mapping.marker
           << " = 0\n"
              "let volt_scale = 0\n"
              "setscale volt_scale\n"
           << "wrdata " << NgspiceDcAnalysis::output_filename();
    for (auto index = std::size_t{1}; index < mapping.headers.size(); ++index) {
        stream << ' ' << mapping.headers[index];
    }
    stream << "\nquit\n.endc\n.end\n";
    return stream.str();
}

[[noreturn]] void parse_error(std::string message) {
    throw KernelArgumentError{ErrorCode::InvalidArgument,
                              "Invalid ngspice DC machine output: " + std::move(message)};
}

[[nodiscard]] std::vector<std::string_view> words(std::string_view line) {
    auto result = std::vector<std::string_view>{};
    auto position = std::size_t{0};
    while (position < line.size()) {
        while (position < line.size() &&
               std::isspace(static_cast<unsigned char>(line[position])) != 0) {
            ++position;
        }
        const auto begin = position;
        while (position < line.size() &&
               std::isspace(static_cast<unsigned char>(line[position])) == 0) {
            ++position;
        }
        if (begin != position) {
            result.push_back(line.substr(begin, position - begin));
        }
    }
    return result;
}

[[nodiscard]] std::vector<std::string_view> nonempty_lines(std::string_view output) {
    auto result = std::vector<std::string_view>{};
    auto begin = std::size_t{0};
    while (begin <= output.size()) {
        const auto end = output.find('\n', begin);
        auto line = output.substr(begin, end == std::string_view::npos ? output.size() - begin
                                                                       : end - begin);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (!words(line).empty()) {
            result.push_back(line);
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    return result;
}

[[nodiscard]] double parse_number(std::string_view text) {
    auto value = 0.0;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(value)) {
        parse_error("expected a finite numeric field");
    }
    return value;
}

[[nodiscard]] double finite_difference(double from, double to) {
    const auto difference = static_cast<long double>(from) - static_cast<long double>(to);
    if (!std::isfinite(difference) || std::abs(difference) > std::numeric_limits<double>::max()) {
        parse_error("branch voltage reconstruction overflowed");
    }
    return static_cast<double>(difference);
}

} // namespace

NgspiceDcAnalysis::NgspiceDcAnalysis(const CompiledElectricalModel &model)
    : model_{model}, mapping_identity_{make_mapping_identity(model_)},
      deck_{write_deck(model_, mapping_identity_)}, deck_identity_{sha256_content_hash(deck_)} {
    if (model_.branches().empty() || model_.nodes().size() == 1U) {
        diagnostics_.push_back(
            Diagnostic{Severity::Error,
                       DiagnosticCode{std::string{analysis_diagnostic_codes::DcNgspiceModelEmpty}},
                       DiagnosticCategory{diagnostic_categories::Analysis},
                       "No non-reference operating-point circuit is available for ngspice"});
    }
}

NgspiceDcAnalysis prepare_ngspice_dc(const CompiledElectricalModel &model) {
    return NgspiceDcAnalysis{model};
}

namespace detail {

std::string ngspice_node(const CompiledElectricalModel &model, ElectricalNodeId node) {
    return node == model.reference() ? "0" : "n" + std::to_string(node.index());
}

NgspiceDcMapping ngspice_dc_mapping(const NgspiceDcAnalysis &analysis) {
    return make_mapping(analysis.model(), analysis.mapping_identity());
}

std::vector<double> read_ngspice_dc_coordinates(const NgspiceDcAnalysis &analysis,
                                                std::string_view output) {
    if (!analysis.complete()) {
        parse_error("analysis preparation is incomplete");
    }
    if (output.size() > NgspiceDcAnalysis::maximum_output_bytes()) {
        parse_error("output exceeds the adapter byte limit");
    }
    const auto lines = nonempty_lines(output);
    if (lines.size() != 2U) {
        parse_error("expected exactly one header and one operating-point row");
    }
    const auto mapping = ngspice_dc_mapping(analysis);
    const auto headers = words(lines[0]);
    if (headers.size() != mapping.headers.size()) {
        parse_error("header does not match the retained model mapping");
    }
    for (auto index = std::size_t{0}; index < headers.size(); ++index) {
        if (headers[index] != mapping.headers[index]) {
            parse_error("header does not match the retained model mapping");
        }
    }
    const auto fields = words(lines[1]);
    if (fields.size() != mapping.headers.size()) {
        parse_error("operating-point row has the wrong field count");
    }
    auto values = std::vector<double>{};
    values.reserve(fields.size());
    for (const auto field : fields) {
        values.push_back(parse_number(field));
    }
    if (values[0] != 0.0 || values[1] != 0.0) {
        parse_error("scale or retained-model marker value is not zero");
    }

    const auto &model = analysis.model();
    auto cursor = std::size_t{2};
    auto node_values = std::vector<double>(model.nodes().size(), 0.0);
    auto coordinates = std::vector<double>{};
    coordinates.reserve(model.nodes().size() - 1U + model.branches().size());
    for (const auto &node : model.nodes()) {
        if (node.id != model.reference()) {
            node_values.at(node.id.index()) = values.at(cursor++);
            coordinates.push_back(node_values.at(node.id.index()));
        }
    }
    for (const auto &entry : mapping.branches) {
        const auto &branch = model.branches().at(entry.branch.index());
        const auto current = [&] {
            switch (entry.current_projection) {
            case NgspiceCurrentProjection::Returned:
                return values.at(cursor++);
            case NgspiceCurrentProjection::Resistance: {
                const auto voltage = finite_difference(node_values.at(branch.from.index()),
                                                       node_values.at(branch.to.index()));
                const auto resistance =
                    std::get<ResistanceElement>(branch.law).parameter().nominal().value();
                const auto result = voltage / resistance;
                if (!std::isfinite(result)) {
                    parse_error("resistance-current reconstruction is nonfinite");
                }
                return result;
            }
            case NgspiceCurrentProjection::CapacitorOpen:
                return 0.0;
            case NgspiceCurrentProjection::CurrentSourceNominal:
                return std::get<DcCurrentSource>(branch.law).value().value();
            }
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Unknown ngspice DC current projection"};
        }();
        coordinates.push_back(current);
    }
    if (cursor != values.size()) {
        parse_error("operating-point row contains unexpected vectors");
    }
    return coordinates;
}

} // namespace detail
} // namespace volt
