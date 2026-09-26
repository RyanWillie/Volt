#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <volt/electrical/compiled_electrical_model.hpp>

namespace volt {

/** Immutable exact lowering of one compiled model to the pinned ngspice DC adapter. */
class NgspiceDcAnalysis {
  public:
    /** Lower one complete compiled model without running ngspice or another native solve. */
    explicit NgspiceDcAnalysis(const CompiledElectricalModel &model);

    /** Current deterministic lowering/parser contract. */
    [[nodiscard]] static constexpr std::uint32_t contract_version() noexcept { return 1; }

    /** External backend family retained separately from the native Eigen backend. */
    [[nodiscard]] static constexpr std::string_view backend() noexcept { return "ngspice"; }

    /** Exactly supported external backend version; other versions are not interchangeable. */
    [[nodiscard]] static constexpr std::string_view backend_version() noexcept { return "46"; }

    /** Canonical effective electrical and machine-output settings included in identities. */
    [[nodiscard]] static constexpr std::string_view settings() noexcept {
        return "op;gmin=0;gminsteps=0;srcsteps=0;reltol=1e-12;abstol=1e-15;"
               "vntol=1e-12;wrdata-ascii;wr_singlescale;wr_vecnames;wr_onespace;"
               "numdgt=16";
    }

    /** Adapter-owned relative machine-output filename embedded in the generated deck. */
    [[nodiscard]] static constexpr std::string_view output_filename() noexcept {
        return "volt-dc-output.txt";
    }

    /** Maximum native parser input and process-capture size for the machine output. */
    [[nodiscard]] static constexpr std::size_t maximum_output_bytes() noexcept {
        return 1024U * 1024U;
    }

    /** Exact owning compiled model; SPICE never becomes the canonical graph. */
    [[nodiscard]] const CompiledElectricalModel &model() const noexcept { return model_; }

    /** True only when every required primitive has an exact pinned-backend projection. */
    [[nodiscard]] bool complete() const noexcept { return diagnostics_.empty(); }

    /** Deterministic self-contained adapter-owned operating-point deck. */
    [[nodiscard]] const std::string &deck() const noexcept { return deck_; }

    /** Content identity of the exact generated deck bytes. */
    [[nodiscard]] const ContentHash &deck_identity() const noexcept { return deck_identity_; }

    /** Identity of model, adapter contract, settings, names and returned-vector mapping. */
    [[nodiscard]] const ContentHash &mapping_identity() const noexcept { return mapping_identity_; }

    /** Native capability/loss findings; an incomplete analysis must not execute. */
    [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept {
        return diagnostics_;
    }

  private:
    CompiledElectricalModel model_;
    ContentHash mapping_identity_;
    std::string deck_;
    ContentHash deck_identity_;
    std::vector<Diagnostic> diagnostics_;
};

/** Prepare deterministic exact DC lowering without executing an external process. */
[[nodiscard]] NgspiceDcAnalysis prepare_ngspice_dc(const CompiledElectricalModel &model);

} // namespace volt
