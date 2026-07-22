#pragma once

#include "binding_conversions.hpp"

#include <memory>
#include <string>

#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/library/part_library.hpp>

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

    /** Build the complete native bundle closure for this immutable source snapshot. */
    [[nodiscard]] py::bytes bundle_bytes() const;

  private:
    struct State;

    std::shared_ptr<const State> state_;
};

/** Owning native reopened PartLibraryBundle exposed only as typed query results. */
class PyPartLibraryBundle {
  public:
    explicit PyPartLibraryBundle(std::string bytes);

    [[nodiscard]] std::string digest() const;

    [[nodiscard]] std::string library_digest() const;

    [[nodiscard]] py::dict inspect() const;

    [[nodiscard]] py::dict part_result(const std::string &part_key) const;

    [[nodiscard]] py::list part_assets(const std::string &part_key) const;

  private:
    volt::io::PartLibraryBundle bundle_;
};

[[nodiscard]] py::dict library_part_reference_to_dict(const volt::LibraryPartRef &reference);

} // namespace volt::python
