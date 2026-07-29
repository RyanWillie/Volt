#include <volt/circuit/bom/bom.hpp>
#include <volt/io/bom/bom_writer.hpp>
#include <volt/library/part_library.hpp>

#include "project_bundle_v2_contract.hpp"
#include "project_bundle_v2_internal.hpp"

namespace volt::io::v2_open {
namespace {

class DecodedVendoredPartResolver final : public PartDefinitionResolver {
  public:
    explicit DecodedVendoredPartResolver(const LibraryDecoded &library) noexcept
        : library_{&library} {}

    [[nodiscard]] const PartDefinition &resolve(const LibraryPartRef &reference) const & override {
        const auto id = ArtifactId{ArtifactKind::PartDefinition, reference};
        const auto match = library_->parts.find(detail::project_bundle_v2_artifact_key(id));
        require(match != library_->parts.end(), ProjectBundleOpenErrorCode::OwnershipViolation,
                "BOM exact reference has no vendored part definition");
        require(match->second->content_identity() == reference.part_digest(),
                ProjectBundleOpenErrorCode::DigestMismatch,
                "BOM exact reference digest differs from its vendored part");
        return *match->second;
    }

  private:
    const LibraryDecoded *library_;
};

} // namespace

[[nodiscard]] std::string write_decoded_bom(const Circuit &circuit, const LibraryDecoded &library) {
    return write_bom_json(project_bom(circuit, DecodedVendoredPartResolver{library}));
}

} // namespace volt::io::v2_open
