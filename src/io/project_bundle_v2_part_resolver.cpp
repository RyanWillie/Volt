#include <volt/circuit/bom/bom.hpp>
#include <volt/io/bom/bom_writer.hpp>
#include <volt/io/electrical/dc_request_io.hpp>
#include <volt/library/part_library.hpp>

#include "project_bundle_v2_contract.hpp"
#include "project_bundle_v2_internal.hpp"

namespace volt::io::v2_open {
namespace {

class DecodedVendoredPartResolver final : public PartDefinitionResolver {
  public:
    using Parts = std::map<std::string, std::unique_ptr<PartDefinition>>;

    explicit DecodedVendoredPartResolver(const Parts &parts) noexcept : parts_{&parts} {}

    [[nodiscard]] const PartDefinition &resolve(const LibraryPartRef &reference) const & override {
        const auto id = ArtifactId{ArtifactKind::PartDefinition, reference};
        const auto match = parts_->find(detail::project_bundle_v2_artifact_key(id));
        require(match != parts_->end(), ProjectBundleOpenErrorCode::OwnershipViolation,
                "Exact reference has no verified vendored part definition");
        require(match->second->content_identity() == reference.part_digest(),
                ProjectBundleOpenErrorCode::DigestMismatch,
                "Exact reference digest differs from its verified vendored part");
        return *match->second;
    }

  private:
    const Parts *parts_;
};

} // namespace

DcInput
prepare_decoded_dc_input(const Circuit &circuit,
                         const std::map<std::string, std::unique_ptr<PartDefinition>> &parts) {
    return prepare_dc_input(circuit, DecodedVendoredPartResolver{parts});
}

[[nodiscard]] std::string write_decoded_bom(const Circuit &circuit, const LibraryDecoded &library) {
    return write_bom_json(project_bom(circuit, DecodedVendoredPartResolver{library.parts}));
}

} // namespace volt::io::v2_open
