#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <volt/circuit/circuit.hpp>
#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt::python {

/** Lower one canonical Circuit component definition into a complete typed library input. */
[[nodiscard]] volt::ComponentSpec component_spec(const volt::Circuit &circuit,
                                                 volt::ComponentDefId definition);

/** Exact immutable library closure and its atomic authored-definition transaction. */
class PyCircuitPartClosure {
  public:
    using AuthoredSymbolInput = std::pair<volt::SchematicSymbolReference, std::string>;

    explicit PyCircuitPartClosure(std::shared_ptr<const volt::io::PartLibraryBundle> bundle)
        : bundle_{std::move(bundle)} {}

    [[nodiscard]] const volt::io::PartLibraryBundle &bundle() const noexcept { return *bundle_; }

    void replace_bundle(std::shared_ptr<const volt::io::PartLibraryBundle> bundle) {
        bundle_ = std::move(bundle);
    }

    /** Return exact symbol bytes retained by the immutable closure. */
    [[nodiscard]] std::optional<std::string>
    symbol(const volt::SchematicSymbolReference &reference) const;

    /**
     * Preflight and atomically commit one definition plus its rebuilt authored closure.
     *
     * Every fallible closure operation completes before the kernel mutation. Failure preserves
     * both the current Circuit bytes and the previously selected closure.
     */
    [[nodiscard]] volt::ComponentDefId
    commit_authored_definition(volt::Circuit &circuit, volt::ComponentSpec definition,
                               std::span<const AuthoredSymbolInput> additional_symbols = {});

  private:
    std::shared_ptr<const volt::io::PartLibraryBundle> bundle_;
};

} // namespace volt::python
