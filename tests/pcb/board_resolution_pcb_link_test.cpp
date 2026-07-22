#include <optional>

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

int main() {
    const auto circuit = volt::Circuit{};
    const auto board = volt::Board{circuit, volt::BoardName{"PCB link contract"}};
    const auto resolution = volt::BoardResolution::materialize(
        board, volt::sha256_content_hash("empty selected closure"),
        volt::BoardResolutionCapabilities{std::nullopt}, volt::FootprintLibrary{}, {});

    return resolution.parts().empty() && resolution.part(volt::ComponentId{0}) == nullptr ? 0 : 1;
}
