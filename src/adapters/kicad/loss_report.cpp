#include <volt/adapters/kicad/loss_report.hpp>

namespace volt::adapters::kicad {

void LossReport::add_warning(LossKind kind, std::string construct, std::string message,
                             LossSeverity severity) {
    if (construct.empty()) {
        throw std::invalid_argument{"KiCad loss warning construct must not be empty"};
    }
    if (message.empty()) {
        throw std::invalid_argument{"KiCad loss warning message must not be empty"};
    }

    warnings_.push_back(LossWarning{kind, std::move(construct), std::move(message), severity});
}

} // namespace volt::adapters::kicad
