#include <optional>
#include <utility>
#include <vector>

#include <volt/core/content_hash.hpp>
#include <volt/pcb/resolution/board_resolution.hpp>

namespace volt::io {
class PartLibraryBundle;
}

template <typename Resolution>
concept ExposesBundleResolver =
    requires(const volt::Board &board, const volt::io::PartLibraryBundle &bundle,
             volt::BoardResolutionCapabilities capabilities) {
        Resolution::resolve(board, bundle, capabilities);
    };

static_assert(!ExposesBundleResolver<volt::BoardResolution>,
              "The P6-consuming resolver belongs to Volt::IO, not Volt::PCB");

template <typename Resolution>
concept ExposesRawMaterializer =
    requires(const volt::Board &board, volt::BoardResolutionCapabilities capabilities,
             volt::FootprintLibrary footprints, std::vector<volt::ResolvedBoardPart> parts) {
        Resolution::materialize(board, volt::sha256_content_hash("raw"), capabilities,
                                std::move(footprints), std::move(parts));
    };

static_assert(!ExposesRawMaterializer<volt::BoardResolution>,
              "BoardResolution publication must remain owned by its exact IO codec");

template <typename View>
concept ExposesRawViewFactory = requires(
    const volt::Board &board, const volt::FootprintLibrary &footprints,
    std::span<const volt::ResolvedBoardPart> parts) { View::test_only(board, footprints, parts); };

static_assert(!ExposesRawViewFactory<volt::ResolvedBoardView>,
              "Production resolved views must require a verified owner");

int main() {
    const auto capabilities = volt::BoardResolutionCapabilities{std::nullopt};
    return capabilities.additional().empty() ? 0 : 1;
}
