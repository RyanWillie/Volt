#pragma once

#include "binding_conversions.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>

#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/io/parts/part_library_bundle.hpp>

namespace volt::python {

/** Native P4 snapshot plus the validated P2 lowering specs used to instantiate it. */
class PyPartLibrary {
  public:
    PyPartLibrary(std::string namespace_name, std::string version, const py::list &parts,
                  bool selected_bundle);

    [[nodiscard]] const volt::PartLibrary &library() const noexcept;

    [[nodiscard]] const volt::io::PartLibraryBundle &bundle() const;

    [[nodiscard]] const volt::ExactPartResolver &resolver() const noexcept;

    [[nodiscard]] volt::LibraryPartRef require(const std::string &part_key) const;

    /** Retain the exact bundle independently of the short-lived Python snapshot handle. */
    [[nodiscard]] std::shared_ptr<const volt::io::PartLibraryBundle> bundle_owner() const;

    [[nodiscard]] const volt::ComponentSpec &component_spec(const std::string &part_key) const;

    [[nodiscard]] py::dict part_result(const std::string &part_key) const;

    [[nodiscard]] py::dict exact_reference(const std::string &part_key) const;

    [[nodiscard]] std::string digest() const;

  private:
    struct State {
        State(volt::PartLibrary selected_library,
              std::optional<volt::io::PartLibraryBundle> selected_bundle,
              std::map<std::string, volt::ComponentSpec> specs)
            : library{std::move(selected_library)}, bundle{std::move(selected_bundle)},
              component_specs{std::move(specs)} {}

        volt::PartLibrary library;
        std::optional<volt::io::PartLibraryBundle> bundle;
        std::map<std::string, volt::ComponentSpec> component_specs;
    };

    std::shared_ptr<const State> state_;
};

[[nodiscard]] py::dict library_part_reference_to_dict(const volt::LibraryPartRef &reference);

} // namespace volt::python
